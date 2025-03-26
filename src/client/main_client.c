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
#include <pthread.h>

// Variables globales para el nombre de usuario y flags para controlar la ejecución del cliente.
static char *global_user_name = NULL;
static int interrupted = 0;
static int connection_failed = 0;

// Esta función maneja la señal de interrupción (Ctrl+C) para salir del bucle principal.
static void sigint_handler(int sig)
{
    interrupted = 1;
}

// Función que se ejecuta en un hilo separado para enviar un mensaje privado sin bloquear el hilo principal.
void *send_private_message_thread(void *arg)
{
    // Obtener los argumentos del hilo
    PrivateMessageArgs *args = (PrivateMessageArgs *)arg;

    send_private_message(args->wsi, args->sender, args->target, args->message);

    free(args); // liberar memoria asignada al struct
    return NULL;
}

// Callback principal para el protocolo de chat. Se invoca en diferentes eventos del ciclo de vida del WebSocket.
static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len)
{
    // Manejar los diferentes eventos del ciclo de vida del WebSocket
    switch (reason)
    {
    // Cuando se establece la conexión con el servidor
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        lwsl_user("Conexión establecida con el servidor WebSocket\n");

        // Envía el mensaje de registro para identificar al usuario
        send_register_message(wsi, global_user_name);
        break;

    // Cuando se recibe un mensaje del servidor
    case LWS_CALLBACK_CLIENT_RECEIVE:
    {
        printf("\nMensaje recibido: %s\n", (char *)in);

        // Parsea el mensaje recibido como JSON
        cJSON *json = cJSON_Parse((char *)in);
        if (!json)
            break;

        // Obtiene el campo "type" del JSON para determinar el tipo de mensaje
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

                    // Muestra cada usuario conectado
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
            else if (strcmp(type->valuestring, "broadcast") == 0)
            {
                cJSON *sender = cJSON_GetObjectItem(json, "sender");
                cJSON *content = cJSON_GetObjectItem(json, "content");
                cJSON *timestamp = cJSON_GetObjectItem(json, "timestamp");

                if (cJSON_IsString(sender) && cJSON_IsString(content) && cJSON_IsString(timestamp))
                {
                    printf("\nMensaje para todos %s: %s\n", sender->valuestring, content->valuestring);
                    printf("Timestamp: %s\n\n", timestamp->valuestring);
                }
            }
            else if (strcmp(type->valuestring, "private") == 0)
            {
                cJSON *sender = cJSON_GetObjectItem(json, "sender");
                cJSON *content = cJSON_GetObjectItem(json, "content");
                cJSON *timestamp = cJSON_GetObjectItem(json, "timestamp");

                if (cJSON_IsString(sender) && cJSON_IsString(content) && cJSON_IsString(timestamp))
                {
                    printf("\nMensaje privado de %s: %s\n", sender->valuestring, content->valuestring);
                    printf("Timestamp: %s\n\n", timestamp->valuestring);
                }
            }
            else if (strcmp(type->valuestring, "server") == 0)
            {
                cJSON *content = cJSON_GetObjectItem(json, "content");

                if (cJSON_IsString(content))
                {
                    printf("\nMensaje del servidor: %s\n\n", content->valuestring);
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
        break;

    // Cuando se cierra la conexión
    case LWS_CALLBACK_CLOSED:
        lwsl_user("Conexión cerrada\n");
        break;

    default:
        break;
    }
    return 0;
}

// Definición de los protocolos que utilizará libwebsockets.
static struct lws_protocols protocols[] = {
    {
        "chat-protocol", // Nombre del protocolo
        callback_chat,   // Callback para manejar los eventos del WebSocket
        0,               // Tamaño de la estructura de usuario
        256,             // Tamaño del buffer de recepción
    },
    {NULL, NULL, 0, 0} // Terminador de la lista de protocolos, debe ser NULL porque libwebsockets espera un array de structs con un último elemento nulo.
};

// Hilo que se encarga de gestionar la entrada del usuario.
// Muestra un menú de opciones y, según la selección, llama a la función
// correspondiente para enviar el mensaje o realizar la acción solicitada.
void *user_input_thread(void *arg)
{
    struct lws *wsi = (struct lws *)arg;
    char input[256];

    while (1)
    {
        // Menú de opciones
        printf("\n========= MENÚ DE CHAT =========\n");
        printf("1. Chatear con todos (broadcast)\n");
        printf("2. Enviar mensaje privado\n");
        printf("3. Cambiar de estado\n");
        printf("4. Listar usuarios conectados\n");
        printf("5. Ver información de un usuario\n");
        printf("6. Ayuda\n");
        printf("7. Salir\n");
        printf("Seleccione una opción (1-7): ");

        // Leer la opción ingresada por el usuario
        if (!fgets(input, sizeof(input), stdin))
        {
            perror("Error leyendo entrada");
            continue;
        }
        input[strcspn(input, "\n")] = '\0'; // Eliminar el salto de línea

        if (strcmp(input, "1") == 0)
        {
            // Option 1: Broadcast message
            printf("Escribe el mensaje para enviar a todos: ");
            if (!fgets(input, sizeof(input), stdin))
            {
                perror("Error leyendo entrada");
                continue;
            }
            input[strcspn(input, "\n")] = '\0';
            send_broadcast_message(wsi, global_user_name, input);
        }
        else if (strcmp(input, "2") == 0)
        {
            // Option 2: Private message
            char target[50], message[200];
            printf("Ingrese el usuario destinatario: ");
            if (!fgets(target, sizeof(target), stdin))
            {
                perror("Error leyendo el usuario destinatario");
                continue;
            }
            target[strcspn(target, "\n")] = '\0';

            printf("Ingrese el mensaje: ");
            if (!fgets(message, sizeof(message), stdin))
            {
                perror("Error leyendo el mensaje");
                continue;
            }
            message[strcspn(message, "\n")] = '\0';

            // Crear un struct para pasar los parámetros al hilo que envía el mensaje privado
            PrivateMessageArgs *args = malloc(sizeof(PrivateMessageArgs));
            if (!args)
            {
                perror("No se pudo asignar memoria para mensaje privado");
                continue;
            }

            args->wsi = wsi;
            strncpy(args->sender, global_user_name, sizeof(args->sender));
            strncpy(args->target, target, sizeof(args->target));
            strncpy(args->message, message, sizeof(args->message));

            pthread_t pm_thread;
            // Crear el hilo para enviar el mensaje privado
            if (pthread_create(&pm_thread, NULL, send_private_message_thread, args) != 0)
            {
                perror("No se pudo crear el hilo para mensaje privado");
                free(args);
            }
            else
            {
                // Se usa pthread_detach para no tener que esperar la finalización del hilo
                pthread_detach(pm_thread);
            }
        }

        else if (strcmp(input, "3") == 0)
        {
            char status[20];
            int valid = 0;
            while (!valid)
            {
                printf("Ingrese su nuevo estado (ACTIVO, OCUPADO, INACTIVO): ");
                if (!fgets(status, sizeof(status), stdin))
                {
                    perror("Error leyendo el estado");
                    continue;
                }
                status[strcspn(status, "\n")] = '\0'; // Elimina el salto de línea

                // Verifica si el estado ingresado es uno de los permitidos
                if (strcmp(status, "ACTIVO") == 0 ||
                    strcmp(status, "OCUPADO") == 0 ||
                    strcmp(status, "INACTIVO") == 0)
                {
                    valid = 1;
                }
                else
                {
                    printf("Estado no válido. Por favor, ingrese ACTIVO, OCUPADO o INACTIVO.\n");
                }
            }
            send_change_status_message(wsi, global_user_name, status);
        }

        else if (strcmp(input, "4") == 0)
        {
            // Opción 4: Listar usuarios conectados
            send_list_users_message(wsi, global_user_name);
        }
        else if (strcmp(input, "5") == 0)
        {
            // Opción 5: Información de un usuario
            char target[50];
            printf("Ingrese el nombre del usuario: ");
            if (!fgets(target, sizeof(target), stdin))
            {
                perror("Error leyendo el nombre del usuario");
                continue;
            };
            target[strcspn(target, "\n")] = '\0';

            send_user_info_message(wsi, global_user_name, target);
        }
        else if (strcmp(input, "6") == 0)
        {
            // Opción 6: Mostrar ayuda con la descripción de las opciones
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
            // Opción 7: Desconectarse y salir del programa
            send_disconnect_message(wsi, global_user_name);
            printf("Desconectando...\n");
            interrupted = 1; // Indicar que se debe salir del bucle principal
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

// Función principal que configura la conexión con el servidor WebSocket,
// crea el contexto de libwebsockets, lanza el hilo de entrada del usuario y
// procesa los eventos del WebSocket hasta que se interrumpe el programa.
int main(int argc, char **argv)
{
    // Verifica que se hayan pasado los parámetros necesarios
    if (argc < 4)
    {
        fprintf(stderr, "Uso: %s <nombre_usuario> <direccion_servidor> <puerto>\n", argv[0]);
        return -1;
    }

    global_user_name = argv[1];  // Asigna el nombre de usuario global
    char *server_addr = argv[2]; // Dirección IP o nombre del servidor
    int port = atoi(argv[3]);    // Puerto del servidor (convertido a entero)

    // Configura el manejador para la señal SIGINT (Ctrl+C)
    signal(SIGINT, sigint_handler);

    // Inicializa la estructura de configuración para el contexto de libwebsockets
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;                   // El cliente no escucha en un puerto específico
    info.protocols = protocols;                           // Asigna los protocolos definidos
    info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT; // Inicializa SSL si es necesario

    // Crea el contexto de libwebsockets, que maneja la conexión con el servidor
    struct lws_context *context = lws_create_context(&info);
    if (!context)
    {
        fprintf(stderr, "Error al crear el contexto de libwebsockets\n");
        return -1;
    }

    // Configura la información para la conexión del cliente
    struct lws_client_connect_info ccinfo = {0};
    ccinfo.context = context;
    ccinfo.address = server_addr;                  // Dirección del servidor
    ccinfo.port = port;                            // Puerto del servidor
    ccinfo.path = "/";                             // Ruta del endpoint en el servidor
    ccinfo.host = lws_canonical_hostname(context); // Nombre canónico del host
    ccinfo.origin = "origin";                      // Origen de la conexión
    ccinfo.protocol = protocols[0].name;           // Usa el primer protocolo definido ("chat-protocol")
    ccinfo.ietf_version_or_minus_one = -1;         // Versión del protocolo IETF o -1 para la versión predeterminada

    // Establece la conexión con el servidor WebSocket
    struct lws *wsi = lws_client_connect_via_info(&ccinfo);
    if (!wsi)
    {
        fprintf(stderr, "Error en la conexión con el servidor\n");
        lws_context_destroy(context);
        return -1;
    }

    pthread_t input_thread;

    // Espera brevemente para permitir que el servidor envíe una respuesta inicial
    int wait_ms = 0;
    while (!connection_failed && wait_ms < 500)
    {
        lws_service(context, 50); // Procesa eventos del WebSocket
        wait_ms += 50;
    }

    // Si hubo error en la conexión, se finaliza el programa
    if (connection_failed)
    {
        printf("No se pudo conectar correctamente. Cerrando cliente.\n");
        lws_context_destroy(context);
        return -1;
    }

    // Si no hubo error, lanzar el hilo de entrada
    pthread_create(&input_thread, NULL, user_input_thread, wsi);

    // Bucle principal que procesa los eventos del WebSocket hasta que se interrumpe
    while (!interrupted)
    {
        lws_service(context, 50);
    }

    pthread_join(input_thread, NULL);

    // Destruye el contexto de libwebsockets antes de salir
    if (context)
    {
        lws_context_destroy(context);
        context = NULL;
    }

    printf("Cliente desconectado. Saliendo...\n");

    return 0;
}
