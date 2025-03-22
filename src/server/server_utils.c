#include "server.h"
#include <cjson/cJSON.h>
#include <unistd.h>

// Definiciones de variables globales y mutex (igual que antes)
User users[MAX_USERS];
int user_count = 0;
pthread_mutex_t user_lock = PTHREAD_MUTEX_INITIALIZER;
char *g_broadcast_msg = NULL;
pthread_mutex_t broadcast_lock = PTHREAD_MUTEX_INITIALIZER;

void *user_thread(void *arg)
{
    User *user = (User *)arg;
    printf("Hilo creado para %s (ID: %p)\n", user->username, (void *)pthread_self());

    while (1)
    {
        sleep(1);

        time_t now = time(NULL);
        int debe_cambiar = 0;

        pthread_mutex_lock(&user_lock);
        if (user->status != 2 && difftime(now, user->last_activity) >= 10)
        {
            user->status = 2;
            debe_cambiar = 1;
        }
        pthread_mutex_unlock(&user_lock);

        if (debe_cambiar)
        {
            printf("Usuario %s pasó a INACTIVO\n", user->username);

            cJSON *response = cJSON_CreateObject();
            cJSON_AddStringToObject(response, "type", "status_update");
            cJSON_AddStringToObject(response, "sender", "server");

            cJSON *status_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(status_obj, "user", user->username);
            cJSON_AddStringToObject(status_obj, "status", "INACTIVO");
            cJSON_AddItemToObject(response, "content", status_obj);

            char timestamp[32];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", localtime(&now));
            cJSON_AddStringToObject(response, "timestamp", timestamp);

            char *msg = cJSON_PrintUnformatted(response);
            pthread_mutex_lock(&broadcast_lock);
            if (g_broadcast_msg)
                free(g_broadcast_msg);
            g_broadcast_msg = strdup(msg);
            pthread_mutex_unlock(&broadcast_lock);
            broadcast_message(msg);

            free(msg);
            cJSON_Delete(response);
        }
    }

    return NULL;
}

int add_user(const char *username, struct lws *wsi)
{
    char client_ip[48] = {0};
    lws_get_peer_simple(wsi, client_ip, sizeof(client_ip));
    users[user_count].last_activity = time(NULL);

    pthread_mutex_lock(&user_lock);
    for (int i = 0; i < user_count; i++)
    {
        if (strcmp(users[i].username, username) == 0)
        {
            pthread_mutex_unlock(&user_lock);
            printf("Error: Usuario %s ya existe.\n", username);
            return 0;
        }
    }

    if (user_count < MAX_USERS)
    {
        strcpy(users[user_count].username, username);
        users[user_count].wsi = wsi;
        users[user_count].status = 0;
        strcpy(users[user_count].ip, client_ip);

        // 🔹 Lanzar hilo para este usuario
        if (pthread_create(&users[user_count].thread_id, NULL, user_thread, &users[user_count]) != 0)
        {
            printf("No se pudo crear hilo para %s\n", username);
            pthread_mutex_unlock(&user_lock);
            return 0;
        }
        printf("Hilo creado para %s con ID %p\n", username, (void *)users[user_count].thread_id);

        user_count++;
    }
    pthread_mutex_unlock(&user_lock);
    return 1;
}

void remove_user(struct lws *wsi)
{
    pthread_mutex_lock(&user_lock);

    for (int i = 0; i < user_count; i++)
    {
        if (users[i].wsi == wsi)
        {
            printf("Eliminando usuario: %s (hilo: %p)\n", users[i].username, (void *)users[i].thread_id);

            // 🔹 Cancelar y unir el hilo del usuario
            pthread_cancel(users[i].thread_id);
            pthread_join(users[i].thread_id, NULL);

            // 🔹 Liberar posición moviendo el último usuario al actual
            users[i] = users[user_count - 1];
            user_count--;

            break;
        }
    }

    pthread_mutex_unlock(&user_lock);
}

void broadcast_message(const char *message)
{
    pthread_mutex_lock(&user_lock);
    for (int i = 0; i < user_count; i++)
    {
        lws_callback_on_writable(users[i].wsi);
    }
    pthread_mutex_unlock(&user_lock);
}

// Función auxiliar para enviar mensajes de error en el formato estándar
void send_error(struct lws *wsi, const char *error_desc)
{
    cJSON *error_response = cJSON_CreateObject();
    cJSON_AddStringToObject(error_response, "type", "error");
    cJSON_AddStringToObject(error_response, "sender", "server");
    cJSON_AddStringToObject(error_response, "content", error_desc);
    // Agregar timestamp
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", t);
    cJSON_AddStringToObject(error_response, "timestamp", timestamp);

    char *err_str = cJSON_PrintUnformatted(error_response);
    size_t err_len = strlen(err_str);
    unsigned char *buf = malloc(LWS_PRE + err_len);
    if (buf)
    {
        memcpy(buf + LWS_PRE, err_str, err_len);
        lws_write(wsi, buf + LWS_PRE, err_len, LWS_WRITE_TEXT);
        free(buf);
    }
    free(err_str);
    cJSON_Delete(error_response);
}

void handle_message(const char *msg, struct lws *wsi)
{
    // Imprime el mensaje crudo para depuración
    printf("Mensaje recibido (crudo, len=%zu): [%s]\n", strlen(msg), msg);

    cJSON *json = cJSON_Parse(msg);
    if (json == NULL)
    {
        send_error(wsi, "Mensaje JSON inválido");
        return;
    }

    pthread_mutex_lock(&user_lock);
    for (int i = 0; i < user_count; i++)
    {
        if (users[i].wsi == wsi)
        {
            users[i].last_activity = time(NULL); // ⏱️ Marca la actividad
            break;
        }
    }
    pthread_mutex_unlock(&user_lock);

    // Validar campo "type"
    cJSON *type_item = cJSON_GetObjectItem(json, "type");
    if (!type_item || !cJSON_IsString(type_item))
    {
        send_error(wsi, "Campo 'type' no encontrado o inválido");
        cJSON_Delete(json);
        return;
    }
    const char *type = type_item->valuestring;

    // Validar campo "sender"
    cJSON *sender_item = cJSON_GetObjectItem(json, "sender");
    if (!sender_item || !cJSON_IsString(sender_item))
    {
        send_error(wsi, "Campo 'sender' no encontrado o inválido");
        cJSON_Delete(json);
        return;
    }
    const char *sender = sender_item->valuestring;

    // --- CASO: Registro de usuario ---
    if (strcmp(type, "register") == 0)
    {

        // Opcional: validar que no falte algún campo (por ejemplo, "content" se ignora en register)
        int success = add_user(sender, wsi);
        cJSON *response = cJSON_CreateObject();
        if (success)
        {
            cJSON_AddStringToObject(response, "type", "register_success");
            cJSON_AddStringToObject(response, "sender", "server");
            cJSON_AddStringToObject(response, "content", "Registro exitoso");
            // Crear lista de usuarios
            cJSON *userList = cJSON_CreateArray();
            pthread_mutex_lock(&user_lock);
            for (int i = 0; i < user_count; i++)
            {
                cJSON_AddItemToArray(userList, cJSON_CreateString(users[i].username));
            }
            pthread_mutex_unlock(&user_lock);
            cJSON_AddItemToObject(response, "userList", userList);
            // Agregar timestamp
            time_t now = time(NULL);
            struct tm *t = localtime(&now);
            char timestamp[32];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", t);
            cJSON_AddStringToObject(response, "timestamp", timestamp);
        }
        else
        {
            send_error(wsi, "Usuario ya existe");
            cJSON_Delete(json);
            return;
        }
        char *response_str = cJSON_PrintUnformatted(response);
        size_t resp_len = strlen(response_str);
        unsigned char *buf = malloc(LWS_PRE + resp_len);
        if (buf)
        {
            memcpy(buf + LWS_PRE, response_str, resp_len);
            lws_write(wsi, buf + LWS_PRE, resp_len, LWS_WRITE_TEXT);
            free(buf);
        }
        free(response_str);
        cJSON_Delete(response);
    }
    // --- CASO: Broadcast ---
    else if (strcmp(type, "broadcast") == 0)
    {
        int sender_found = 0;
        pthread_mutex_lock(&user_lock);
        for (int i = 0; i < user_count; i++)
        {
            if (strcmp(users[i].username, sender) == 0)
            {
                sender_found = 1;
                break;
            }
        }
        pthread_mutex_unlock(&user_lock);
        if (!sender_found)
        {
            send_error(wsi, "Usuario no registrado. Por favor, regístrese primero.");
            cJSON_Delete(json);
            return;
        }
        cJSON *content_item = cJSON_GetObjectItem(json, "content");
        if (!content_item || !cJSON_IsString(content_item))
        {
            send_error(wsi, "Campo 'content' inválido para broadcast");
            cJSON_Delete(json);
            return;
        }
        // Obtener timestamp actual
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", t);

        // Construir respuesta
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "type", "broadcast");
        cJSON_AddStringToObject(response, "sender", sender);
        cJSON_AddStringToObject(response, "content", content_item->valuestring);
        cJSON_AddStringToObject(response, "timestamp", timestamp);

        char *response_str = cJSON_PrintUnformatted(response);
        pthread_mutex_lock(&broadcast_lock);
        if (g_broadcast_msg)
            free(g_broadcast_msg);
        g_broadcast_msg = strdup(response_str);
        pthread_mutex_unlock(&broadcast_lock);
        broadcast_message(response_str);
        free(response_str);
        cJSON_Delete(response);
    }
    // --- CASO: Mensaje privado ---
    else if (strcmp(type, "private") == 0)
    {
        int sender_found = 0;
        pthread_mutex_lock(&user_lock);
        for (int i = 0; i < user_count; i++)
        {
            if (strcmp(users[i].username, sender) == 0)
            {
                sender_found = 1;
                break;
            }
        }
        pthread_mutex_unlock(&user_lock);
        if (!sender_found)
        {
            send_error(wsi, "Usuario no registrado. Por favor, regístrese primero.");
            cJSON_Delete(json);
            return;
        }
        cJSON *target_item = cJSON_GetObjectItem(json, "target");
        cJSON *content_item = cJSON_GetObjectItem(json, "content");
        if (!target_item || !cJSON_IsString(target_item) ||
            !content_item || !cJSON_IsString(content_item))
        {
            send_error(wsi, "Campos 'target' o 'content' inválidos para mensaje privado");
            cJSON_Delete(json);
            return;
        }
        const char *target = target_item->valuestring;
        const char *message_content = content_item->valuestring;
        int found = 0;
        pthread_mutex_lock(&user_lock);
        for (int i = 0; i < user_count; i++)
        {
            if (strcmp(users[i].username, target) == 0)
            {
                found = 1;
                // Obtener timestamp actual
                time_t now = time(NULL);
                struct tm *t = localtime(&now);
                char timestamp[32];
                strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", t);

                cJSON *response = cJSON_CreateObject();
                cJSON_AddStringToObject(response, "type", "private");
                cJSON_AddStringToObject(response, "sender", sender);
                cJSON_AddStringToObject(response, "target", target);
                cJSON_AddStringToObject(response, "content", message_content);
                cJSON_AddStringToObject(response, "timestamp", timestamp);

                char *response_str = cJSON_PrintUnformatted(response);
                size_t msg_len = strlen(response_str);
                unsigned char *buf = malloc(LWS_PRE + msg_len);
                if (buf)
                {
                    memcpy(buf + LWS_PRE, response_str, msg_len);
                    lws_write(users[i].wsi, buf + LWS_PRE, msg_len, LWS_WRITE_TEXT);
                    free(buf);
                }
                free(response_str);
                cJSON_Delete(response);
                break;
            }
        }
        pthread_mutex_unlock(&user_lock);
        if (!found)
        {
            send_error(wsi, "Usuario no encontrado para mensaje privado");
        }
    }
    // --- CASO: Listado de usuarios ---
    else if (strcmp(type, "list_users") == 0)
    {
        int sender_found = 0;
        pthread_mutex_lock(&user_lock);
        for (int i = 0; i < user_count; i++)
        {
            if (strcmp(users[i].username, sender) == 0)
            {
                sender_found = 1;
                break;
            }
        }
        pthread_mutex_unlock(&user_lock);
        if (!sender_found)
        {
            send_error(wsi, "Usuario no registrado. Por favor, regístrese primero.");
            cJSON_Delete(json);
            return;
        }
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "type", "list_users_response");
        cJSON_AddStringToObject(response, "sender", "server");

        cJSON *userList = cJSON_CreateArray();
        pthread_mutex_lock(&user_lock);
        for (int i = 0; i < user_count; i++)
        {
            cJSON_AddItemToArray(userList, cJSON_CreateString(users[i].username));
        }
        pthread_mutex_unlock(&user_lock);
        cJSON_AddItemToObject(response, "content", userList);

        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", t);
        cJSON_AddStringToObject(response, "timestamp", timestamp);

        char *response_str = cJSON_PrintUnformatted(response);
        size_t resp_len = strlen(response_str);
        unsigned char *buf = malloc(LWS_PRE + resp_len);
        if (buf)
        {
            memcpy(buf + LWS_PRE, response_str, resp_len);
            lws_write(wsi, buf + LWS_PRE, resp_len, LWS_WRITE_TEXT);
            free(buf);
        }
        free(response_str);
        cJSON_Delete(response);
    }
    // --- CASO: Cambio de estado ---
    else if (strcmp(type, "change_status") == 0)
    {
        int sender_found = 0;
        pthread_mutex_lock(&user_lock);
        for (int i = 0; i < user_count; i++)
        {
            if (strcmp(users[i].username, sender) == 0)
            {
                sender_found = 1;
                break;
            }
        }
        pthread_mutex_unlock(&user_lock);
        if (!sender_found)
        {
            send_error(wsi, "Usuario no registrado. Por favor, regístrese primero.");
            cJSON_Delete(json);
            return;
        }
        cJSON *content_item = cJSON_GetObjectItem(json, "content");
        if (!content_item || !cJSON_IsString(content_item))
        {
            send_error(wsi, "Campo 'content' inválido para cambio de estado");
            cJSON_Delete(json);
            return;
        }
        const char *new_status = content_item->valuestring;

        // Validar que el estado sea uno de los permitidos
        if (strcmp(new_status, "ACTIVO") != 0 &&
            strcmp(new_status, "OCUPADO") != 0 &&
            strcmp(new_status, "INACTIVO") != 0)
        {
            send_error(wsi, "Estado inválido. Los estados permitidos son: ACTIVO, OCUPADO, INACTIVO");
            cJSON_Delete(json);
            return;
        }

        // Actualizar el estado en el registro del usuario
        pthread_mutex_lock(&user_lock);
        for (int i = 0; i < user_count; i++)
        {
            if (strcmp(users[i].username, sender) == 0)
            {
                if (strcmp(new_status, "OCUPADO") == 0)
                    users[i].status = 1;
                else if (strcmp(new_status, "INACTIVO") == 0)
                    users[i].status = 2;
                else
                    users[i].status = 0; // ACTIVO
                break;
            }
        }
        pthread_mutex_unlock(&user_lock);

        // Construir respuesta de actualización de estado con cJSON
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "type", "status_update");
        cJSON_AddStringToObject(response, "sender", "server");

        cJSON *status_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(status_obj, "user", sender);
        cJSON_AddStringToObject(status_obj, "status", new_status);
        cJSON_AddItemToObject(response, "content", status_obj);

        // Agregar timestamp al mismo nivel que "content"
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", t);
        cJSON_AddStringToObject(response, "timestamp", timestamp);

        char *response_str = cJSON_PrintUnformatted(response);
        pthread_mutex_lock(&broadcast_lock);
        if (g_broadcast_msg)
            free(g_broadcast_msg);
        g_broadcast_msg = strdup(response_str);
        pthread_mutex_unlock(&broadcast_lock);
        broadcast_message(response_str);

        free(response_str);
        cJSON_Delete(response);
    }
    // --- CASO: Desconexión ---
    else if (strcmp(type, "disconnect") == 0)
    {
        int sender_found = 0;
        pthread_mutex_lock(&user_lock);
        for (int i = 0; i < user_count; i++)
        {
            if (strcmp(users[i].username, sender) == 0)
            {
                sender_found = 1;
                break;
            }
        }
        pthread_mutex_unlock(&user_lock);
        if (!sender_found)
        {
            send_error(wsi, "Usuario no registrado. Por favor, regístrese primero.");
            cJSON_Delete(json);
            return;
        }
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "type", "user_disconnected");
        cJSON_AddStringToObject(response, "sender", "server");

        char content_msg[100];
        snprintf(content_msg, sizeof(content_msg), "%s ha salido", sender);
        cJSON_AddStringToObject(response, "content", content_msg);

        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", t);
        cJSON_AddStringToObject(response, "timestamp", timestamp);

        char *response_str = cJSON_PrintUnformatted(response);
        pthread_mutex_lock(&broadcast_lock);
        if (g_broadcast_msg)
            free(g_broadcast_msg);
        g_broadcast_msg = strdup(response_str);
        pthread_mutex_unlock(&broadcast_lock);
        broadcast_message(response_str);
        remove_user(wsi);
        free(response_str);
        cJSON_Delete(response);
    }
    // --- CASO: Solicitud de información de usuario ---
    else if (strcmp(type, "user_info") == 0)
    {
        int sender_found = 0;
        pthread_mutex_lock(&user_lock);
        for (int i = 0; i < user_count; i++)
        {
            if (strcmp(users[i].username, sender) == 0)
            {
                sender_found = 1;
                break;
            }
        }
        pthread_mutex_unlock(&user_lock);
        if (!sender_found)
        {
            send_error(wsi, "Usuario no registrado. Por favor, regístrese primero.");
            cJSON_Delete(json);
            return;
        }
        cJSON *target_item = cJSON_GetObjectItem(json, "target");
        if (target_item && cJSON_IsString(target_item))
        {
            const char *target = target_item->valuestring;
            cJSON *response = cJSON_CreateObject();
            cJSON_AddStringToObject(response, "type", "user_info_response");
            cJSON_AddStringToObject(response, "sender", "server");
            cJSON_AddStringToObject(response, "target", target);

            cJSON *content = cJSON_CreateObject();
            int found = 0;
            pthread_mutex_lock(&user_lock);
            for (int i = 0; i < user_count; i++)
            {
                if (strcmp(users[i].username, target) == 0)
                {
                    found = 1;
                    cJSON_AddStringToObject(content, "ip", users[i].ip);
                    const char *status_str = (users[i].status == 0) ? "ACTIVO" : (users[i].status == 1) ? "OCUPADO"
                                                                                                        : "INACTIVO";
                    cJSON_AddStringToObject(content, "status", status_str);
                    break;
                }
            }
            pthread_mutex_unlock(&user_lock);

            if (!found)
            {
                cJSON_AddStringToObject(content, "error", "Usuario no encontrado");
            }

            cJSON_AddItemToObject(response, "content", content);

            time_t now = time(NULL);
            struct tm *t = localtime(&now);
            char timestamp[32];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", t);
            cJSON_AddStringToObject(response, "timestamp", timestamp);

            char *response_str = cJSON_PrintUnformatted(response);
            size_t resp_len = strlen(response_str);
            unsigned char *buf = malloc(LWS_PRE + resp_len);
            if (buf)
            {
                memcpy(buf + LWS_PRE, response_str, resp_len);
                lws_write(wsi, buf + LWS_PRE, resp_len, LWS_WRITE_TEXT);
                free(buf);
            }
            free(response_str);
            cJSON_Delete(response);
        }
    }
    // --- CASO: Tipo desconocido ---
    else
    {
        send_error(wsi, "Tipo de mensaje no válido");
    }

    cJSON_Delete(json);
}