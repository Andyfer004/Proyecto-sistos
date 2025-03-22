#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <pthread.h> // Para manejar hilos
#include <unistd.h>  // Para `read`
#include <libwebsockets.h>
#include "client.h" // Incluir el header de las utilidades del cliente
#include <cjson/cJSON.h>

static char *global_user_name = NULL;
static int interrupted = 0;
static int connection_failed = 0;

static void sigint_handler(int sig)
{
    interrupted = 1;
}

static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len)
{
    switch (reason)
    {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        lwsl_user("Conexión establecida con el servidor WebSocket\n");
        // Aquí, en lugar de construir el JSON manualmente,
        // llamamos a la función de utilidad para enviar el registro.
        send_register_message(wsi, global_user_name);
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
    {
        printf("Mensaje recibido: %s\n", (char *)in);

        cJSON *json = cJSON_Parse((char *)in);
        if (!json)
            break;

        // Esto sirve para identificar el tipo de mensaje recibido
        cJSON *type = cJSON_GetObjectItemCaseSensitive(json, "type");

        if (cJSON_IsString(type))
        {
            if (strcmp(type->valuestring, "user_info_response") == 0)
            {
                cJSON *target = cJSON_GetObjectItem(json, "target");
                cJSON *content = cJSON_GetObjectItem(json, "content");
                cJSON *ip = cJSON_GetObjectItem(content, "ip");
                cJSON *status = cJSON_GetObjectItem(content, "status");
                cJSON *timestamp = cJSON_GetObjectItem(json, "timestamp");

                if (cJSON_IsString(target) && cJSON_IsString(ip) && cJSON_IsString(status) && cJSON_IsString(timestamp))
                {
                    printf("\nInformación del usuario: %s\n", target->valuestring);
                    printf("   Estado: %s\n", status->valuestring);
                    printf("   IP: %s\n", ip->valuestring);
                    printf("   Timestamp: %s\n\n", timestamp->valuestring);
                }
            }
            else if (strcmp(type->valuestring, "register_success") == 0)
            {
                cJSON *content = cJSON_GetObjectItem(json, "content");
                cJSON *userList = cJSON_GetObjectItem(json, "userList");
                cJSON *timestamp = cJSON_GetObjectItem(json, "timestamp");

                if (cJSON_IsString(content) && cJSON_IsArray(userList) && cJSON_IsString(timestamp))
                {
                    printf("\nRegistro exitoso: %s\n", content->valuestring);
                    printf("Usuarios conectados:\n");
                    int size = cJSON_GetArraySize(userList);
                    for (int i = 0; i < size; i++)
                    {
                        cJSON *user = cJSON_GetArrayItem(userList, i);
                        if (cJSON_IsString(user))
                        {
                            printf("   - %s\n", user->valuestring);
                        }
                    }
                    printf("Timestamp: %s\n\n", timestamp->valuestring);
                }
            }
            else if (strcmp(type->valuestring, "list_users_response") == 0)
            {
                cJSON *users = cJSON_GetObjectItem(json, "content");
                cJSON *timestamp = cJSON_GetObjectItem(json, "timestamp");

                if (cJSON_IsArray(users) && cJSON_IsString(timestamp))
                {
                    printf("\nLista de usuarios conectados:\n");
                    int size = cJSON_GetArraySize(users);
                    for (int i = 0; i < size; i++)
                    {
                        cJSON *user = cJSON_GetArrayItem(users, i);
                        if (cJSON_IsString(user))
                        {
                            printf("   - %s\n", user->valuestring);
                        }
                    }
                    printf("Timestamp: %s\n\n", timestamp->valuestring);
                }
            }
            else if (strcmp(type->valuestring, "status_update") == 0)
            {
                cJSON *content = cJSON_GetObjectItem(json, "content");
                cJSON *user = cJSON_GetObjectItem(content, "user");
                cJSON *status = cJSON_GetObjectItem(content, "status");
                cJSON *timestamp = cJSON_GetObjectItem(json, "timestamp");

                if (cJSON_IsString(user) && cJSON_IsString(status) && cJSON_IsString(timestamp))
                {
                    printf("\nEstado actualizado:\n");
                    printf("   Usuario: %s\n", user->valuestring);
                    printf("   Nuevo estado: %s\n", status->valuestring);
                    printf("   Timestamp: %s\n\n", timestamp->valuestring);
                }
            }
            else if (strcmp(type->valuestring, "user_disconnected") == 0)
            {
                cJSON *content = cJSON_GetObjectItem(json, "content");
                cJSON *timestamp = cJSON_GetObjectItem(json, "timestamp");

                if (cJSON_IsString(content) && cJSON_IsString(timestamp))
                {
                    printf("\nUsuario desconectado: %s\n", content->valuestring);
                    printf("Timestamp: %s\n\n", timestamp->valuestring);
                }
            }
            else if (strcmp(type->valuestring, "error") == 0)
            {
                cJSON *content = cJSON_GetObjectItem(json, "content");
                cJSON *timestamp = cJSON_GetObjectItem(json, "timestamp");

                if (cJSON_IsString(content) && cJSON_IsString(timestamp))
                {
                    printf("\nError del servidor: %s\n", content->valuestring);
                    printf("Timestamp: %s\n\n", timestamp->valuestring);
                    connection_failed = 1;
                    interrupted = 1;
                }
            }
            else
            {
                printf("Mensaje recibido sin formato especial: %s\n", (char *)in);
            }
        }

        cJSON_Delete(json);
        break;
    }

    case LWS_CALLBACK_CLIENT_WRITEABLE:
        // En este ejemplo, la escritura se realiza cuando se recibe la notificación.
        // Podrías ampliar este bloque para enviar otros tipos de mensajes basados en entrada del usuario.
        break;

    case LWS_CALLBACK_CLOSED:
        lwsl_user("Conexión cerrada\n");
        break;

    default:
        break;
    }
    return 0;
}

static struct lws_protocols protocols[] = {
    {
        "chat-protocol",
        callback_chat,
        0,
        256,
    },
    {NULL, NULL, 0, 0}};

void *user_input_thread(void *arg)
{
    struct lws *wsi = (struct lws *)arg;
    char input[256];

    while (1)
    {
        printf("\n========= MENÚ DE CHAT =========\n");
        printf("1. Chatear con todos (broadcast)\n");
        printf("2. Enviar mensaje privado\n");
        printf("3. Cambiar de estado\n");
        printf("4. Listar usuarios conectados\n");
        printf("5. Ver información de un usuario\n");
        printf("6. Ayuda\n");
        printf("7. Salir\n");
        printf("Seleccione una opción (1-7): ");

        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = '\0'; // Eliminar el salto de línea

        if (strcmp(input, "1") == 0)
        {
            printf("Escribe el mensaje para enviar a todos: ");
            fgets(input, sizeof(input), stdin);
            input[strcspn(input, "\n")] = '\0';
            send_broadcast_message(wsi, global_user_name, input);
        }
        else if (strcmp(input, "2") == 0)
        {
            char target[50], message[200];
            printf("Ingrese el usuario destinatario: ");
            fgets(target, sizeof(target), stdin);
            target[strcspn(target, "\n")] = '\0';

            printf("Ingrese el mensaje: ");
            fgets(message, sizeof(message), stdin);
            message[strcspn(message, "\n")] = '\0';

            send_private_message(wsi, global_user_name, target, message);
        }
        else if (strcmp(input, "3") == 0)
        {
            char status[20];
            printf("Ingrese su nuevo estado (ACTIVO, OCUPADO, INACTIVO): ");
            fgets(status, sizeof(status), stdin);
            status[strcspn(status, "\n")] = '\0';

            send_change_status_message(wsi, global_user_name, status);
        }
        else if (strcmp(input, "4") == 0)
        {
            send_list_users_message(wsi, global_user_name);
        }
        else if (strcmp(input, "5") == 0)
        {
            char target[50];
            printf("Ingrese el nombre del usuario: ");
            fgets(target, sizeof(target), stdin);
            target[strcspn(target, "\n")] = '\0';

            send_user_info_message(wsi, global_user_name, target);
        }
        else if (strcmp(input, "6") == 0)
        {
            printf("\n=== AYUDA ===\n");
            printf("1. Chatear con todos: Envía un mensaje público a todos los usuarios.\n");
            printf("2. Enviar mensaje privado: Especifique un usuario y envíele un mensaje directo.\n");
            printf("3. Cambiar de estado: Puede cambiar su estado a ACTIVO, OCUPADO o INACTIVO.\n");
            printf("4. Listar usuarios: Muestra los usuarios conectados al chat.\n");
            printf("5. Información de un usuario: Muestra detalles sobre un usuario específico.\n");
            printf("6. Ayuda: Muestra esta información.\n");
            printf("7. Salir: Desconectarse del servidor y cerrar el programa.\n");
        }
        else if (strcmp(input, "7") == 0)
        {
            send_disconnect_message(wsi, global_user_name);
            printf("Desconectando...\n");
            interrupted = 1; // Para salir del bucle en main()
            break;
        }
        else
        {
            printf("⚠ Comando no reconocido. Intente nuevamente.\n");
        }

        sleep(1);
    }

    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "Uso: %s <nombre_usuario> <direccion_servidor> <puerto>\n", argv[0]);
        return -1;
    }
    global_user_name = argv[1];
    char *server_addr = argv[2];
    int port = atoi(argv[3]);

    signal(SIGINT, sigint_handler);

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    struct lws_context *context = lws_create_context(&info);
    if (!context)
    {
        fprintf(stderr, "Error al crear el contexto de libwebsockets\n");
        return -1;
    }

    struct lws_client_connect_info ccinfo = {0};
    ccinfo.context = context;
    ccinfo.address = server_addr; // Aquí se asigna la IP del servidor
    ccinfo.port = port;           // Y se asigna el puerto
    ccinfo.path = "/";            // Ruta del endpoint en el servidor
    ccinfo.host = lws_canonical_hostname(context);
    ccinfo.origin = "origin";
    ccinfo.protocol = protocols[0].name;
    ccinfo.ietf_version_or_minus_one = -1;

    struct lws *wsi = lws_client_connect_via_info(&ccinfo);
    if (!wsi)
    {
        fprintf(stderr, "Error en la conexión con el servidor\n");
        lws_context_destroy(context);
        return -1;
    }

    pthread_t input_thread;

    // Esperamos un pequeño tiempo para que llegue la respuesta del servidor
    int wait_ms = 0;
    while (!connection_failed && wait_ms < 500)
    {
        lws_service(context, 50); // Esperar respuesta del servidor
        wait_ms += 50;
    }

    // Si hubo error, salir
    if (connection_failed)
    {
        printf("No se pudo conectar correctamente. Cerrando cliente.\n");
        lws_context_destroy(context);
        return -1;
    }

    // Si no hubo error, lanzar el hilo de entrada
    pthread_create(&input_thread, NULL, user_input_thread, wsi);

    while (!interrupted)
    {
        lws_service(context, 50);
        // Aquí podrías incluir lógica para leer entrada del usuario y
        // llamar a otras funciones de client_utils según el comando (broadcast, privado, etc.)
    }

    pthread_join(input_thread, NULL);

    // Destruir el contexto una sola vez
    if (context)
    {
        lws_context_destroy(context);
        context = NULL;
    }

    printf("Cliente desconectado. Saliendo...\n");

    return 0;
}
