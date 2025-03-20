#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <pthread.h> // Para manejar hilos
#include <unistd.h>  // Para `read`
#include <libwebsockets.h>
#include "client.h" // Incluir el header de las utilidades del cliente

static char *global_user_name = NULL;
static int interrupted = 0;

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
        printf("Mensaje recibido: %s\n", (char *)in);
        break;

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
        printf("\nComandos disponibles:\n");
        printf("1. list - Listar usuarios conectados\n");
        printf("2. broadcast <mensaje> - Enviar mensaje a todos\n");
        printf("3. private <usuario> <mensaje> - Enviar mensaje privado\n");
        printf("4. status <ACTIVO|OCUPADO|INACTIVO> - Cambiar estado\n");
        printf("5. exit - Salir\n");
        printf("Ingrese comando: ");

        fgets(input, sizeof(input), stdin);
        input[strcspn(input, "\n")] = '\0'; // Eliminar el salto de línea

        // Comando para listar usuarios
        if (strcmp(input, "list") == 0)
        {
            send_list_users_message(wsi, global_user_name);
        }
        // Comando para enviar un mensaje a todos
        else if (strncmp(input, "broadcast ", 10) == 0)
        {
            send_broadcast_message(wsi, global_user_name, input + 10);
        }
        // Comando para enviar un mensaje privado
        else if (strncmp(input, "private ", 8) == 0)
        {
            char target[50], message[200];
            if (sscanf(input + 8, "%49s %199[^\n]", target, message) == 2)
            {
                send_private_message(wsi, global_user_name, target, message);
            }
            else
            {
                printf("Formato incorrecto. Usa: private <usuario> <mensaje>\n");
            }
        }
        // Comando para cambiar estado
        else if (strncmp(input, "status ", 7) == 0)
        {
            char status[20];
            if (sscanf(input + 7, "%19s", status) == 1)
            {
                send_change_status_message(wsi, global_user_name, status);
            }
            else
            {
                printf("Formato incorrecto. Usa: status <ACTIVO|OCUPADO|INACTIVO>\n");
            }
        }
        // Comando para salir
        else if (strcmp(input, "exit") == 0)
        {
            send_disconnect_message(wsi, global_user_name);
            printf("Desconectando...\n");
            interrupted = 1; // Detiene el bucle principal en main()
            break;
        }
        // Comando desconocido
        else
        {
            printf("Comando no reconocido. Intente de nuevo.\n");
        }

        sleep(1); // Pequeña pausa para evitar que el bucle consuma CPU
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
