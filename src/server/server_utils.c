#include "server.h"
#include <cjson/cJSON.h>

User users[MAX_USERS];
int user_count = 0;
pthread_mutex_t user_lock = PTHREAD_MUTEX_INITIALIZER;

// Variables globales para broadcast
char *g_broadcast_msg = NULL;
pthread_mutex_t broadcast_lock = PTHREAD_MUTEX_INITIALIZER;

// Devuelve 1 si se registró correctamente, 0 si ya existe.
int add_user(const char *username, struct lws *wsi) {
    pthread_mutex_lock(&user_lock);
    // Verificar si el usuario ya existe
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].username, username) == 0) {
            pthread_mutex_unlock(&user_lock);
            printf("Error: Usuario %s ya existe.\n", username);
            return 0;
        }
    }
    // Registrar usuario si no existe
    if (user_count < MAX_USERS) {
        strcpy(users[user_count].username, username);
        users[user_count].wsi = wsi;
        users[user_count].status = 0; // ACTIVO
        user_count++;
    }
    pthread_mutex_unlock(&user_lock);
    return 1;
}

void remove_user(struct lws *wsi) {
    pthread_mutex_lock(&user_lock);
    for (int i = 0; i < user_count; i++) {
        if (users[i].wsi == wsi) {
            users[i] = users[user_count - 1];
            user_count--;
            break;
        }
    }
    pthread_mutex_unlock(&user_lock);
}

void broadcast_message(const char *message) {
    pthread_mutex_lock(&user_lock);
    for (int i = 0; i < user_count; i++) {
        lws_callback_on_writable(users[i].wsi);
    }
    pthread_mutex_unlock(&user_lock);
}

void handle_message(const char *msg, struct lws *wsi) {
    printf("Mensaje recibido: %s\n", msg);

    cJSON *json = cJSON_Parse(msg);
    if (json == NULL) {
        printf("Error: Mensaje JSON inválido\n");
        return;
    }

    // Obtener campo "type"
    cJSON *type_item = cJSON_GetObjectItem(json, "type");
    if (!type_item) {
        printf("Error: Campo 'type' no encontrado\n");
        cJSON_Delete(json);
        return;
    }
    const char *type = type_item->valuestring;

    // Obtener campo "sender"
    cJSON *sender_item = cJSON_GetObjectItem(json, "sender");
    if (!sender_item) {
        printf("Error: Campo 'sender' no encontrado\n");
        cJSON_Delete(json);
        return;
    }
    const char *sender = sender_item->valuestring;

    // Caso: Registro de usuario
    if (strcmp(type, "register") == 0) {
        int success = add_user(sender, wsi);
        cJSON *response = cJSON_CreateObject();
        if (success) {
            cJSON_AddStringToObject(response, "type", "register_success");
            cJSON_AddStringToObject(response, "sender", "server");
            cJSON_AddStringToObject(response, "content", "Registro exitoso");

            cJSON *users_array = cJSON_CreateArray();
            pthread_mutex_lock(&user_lock);
            for (int i = 0; i < user_count; i++) {
                cJSON_AddItemToArray(users_array, cJSON_CreateString(users[i].username));
            }
            pthread_mutex_unlock(&user_lock);
            cJSON_AddItemToObject(response, "userList", users_array);
        } else {
            cJSON_AddStringToObject(response, "type", "error");
            cJSON_AddStringToObject(response, "sender", "server");
            cJSON_AddStringToObject(response, "content", "Usuario ya existe");
        }
        char *response_str = cJSON_PrintUnformatted(response);
        size_t resp_len = strlen(response_str);
        unsigned char *buf = malloc(LWS_PRE + resp_len);
        if (buf) {
            memcpy(buf + LWS_PRE, response_str, resp_len);
            lws_write(wsi, buf + LWS_PRE, resp_len, LWS_WRITE_TEXT);
            free(buf);
        }
        free(response_str);
        cJSON_Delete(response);
        cJSON_Delete(json);
        return;
    }
    // Caso: Mensaje de broadcast
    else if (strcmp(type, "broadcast") == 0) {
        pthread_mutex_lock(&broadcast_lock);
        if (g_broadcast_msg)
            free(g_broadcast_msg);
        g_broadcast_msg = strdup(msg);
        pthread_mutex_unlock(&broadcast_lock);
        broadcast_message(msg);
    }
    // Caso: Mensaje privado
    else if (strcmp(type, "private") == 0) {
        cJSON *target_item = cJSON_GetObjectItem(json, "target");
        if (target_item && target_item->valuestring) {
            const char *target = target_item->valuestring;
            pthread_mutex_lock(&user_lock);
            for (int i = 0; i < user_count; i++) {
                if (strcmp(users[i].username, target) == 0) {
                    lws_write(users[i].wsi, (unsigned char *)msg, strlen(msg), LWS_WRITE_TEXT);
                    break;
                }
            }
            pthread_mutex_unlock(&user_lock);
        }
    }
    // Caso: Listado de usuarios
    else if (strcmp(type, "list_users") == 0) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "type", "list_users_response");
        cJSON_AddStringToObject(response, "sender", "server");

        cJSON *users_array = cJSON_CreateArray();
        pthread_mutex_lock(&user_lock);
        for (int i = 0; i < user_count; i++) {
            cJSON_AddItemToArray(users_array, cJSON_CreateString(users[i].username));
        }
        pthread_mutex_unlock(&user_lock);
        cJSON_AddItemToObject(response, "content", users_array);
        char *response_str = cJSON_PrintUnformatted(response);
        size_t resp_len = strlen(response_str);
        unsigned char *buf = malloc(LWS_PRE + resp_len);
        if (buf) {
            memcpy(buf + LWS_PRE, response_str, resp_len);
            lws_write(wsi, buf + LWS_PRE, resp_len, LWS_WRITE_TEXT);
            free(buf);
        }
        free(response_str);
        cJSON_Delete(response);
    }
    // Caso: Cambio de estado
    else if (strcmp(type, "change_status") == 0) {
        cJSON *content_item = cJSON_GetObjectItem(json, "content");
        const char *new_status = content_item ? content_item->valuestring : "";
        pthread_mutex_lock(&user_lock);
        for (int i = 0; i < user_count; i++) {
            if (strcmp(users[i].username, sender) == 0) {
                if (strcmp(new_status, "OCUPADO") == 0)
                    users[i].status = 1;
                else if (strcmp(new_status, "INACTIVO") == 0)
                    users[i].status = 2;
                else
                    users[i].status = 0;
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
        char *response_str = cJSON_PrintUnformatted(response);

        // Imprimir para depuración
        printf("Broadcasting status_update: %s\n", response_str);

        // Guardar la respuesta en el buffer global para que el callback de escritura la envíe
        pthread_mutex_lock(&broadcast_lock);
        if (g_broadcast_msg) free(g_broadcast_msg);
        g_broadcast_msg = strdup(response_str);
        pthread_mutex_unlock(&broadcast_lock);

        broadcast_message(response_str);

        free(response_str);
        cJSON_Delete(response);
    }
    // Caso: Desconexión
    else if (strcmp(type, "disconnect") == 0) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "type", "user_disconnected");
        cJSON_AddStringToObject(response, "sender", "server");
        char content_msg[100];
        snprintf(content_msg, sizeof(content_msg), "%s ha salido", sender);
        cJSON_AddStringToObject(response, "content", content_msg);
        char *response_str = cJSON_PrintUnformatted(response);
        broadcast_message(response_str);
        free(response_str);
        cJSON_Delete(response);
    }
    // Caso: Tipo desconocido
    else {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "type", "error");
        cJSON_AddStringToObject(response, "sender", "server");
        cJSON_AddStringToObject(response, "content", "Tipo de mensaje no válido");
        char *response_str = cJSON_PrintUnformatted(response);
        size_t resp_len = strlen(response_str);
        unsigned char *buf = malloc(LWS_PRE + resp_len);
        if (buf) {
            memcpy(buf + LWS_PRE, response_str, resp_len);
            lws_write(wsi, buf + LWS_PRE, resp_len, LWS_WRITE_TEXT);
            free(buf);
        }
        free(response_str);
        cJSON_Delete(response);
    }

    cJSON_Delete(json);
}