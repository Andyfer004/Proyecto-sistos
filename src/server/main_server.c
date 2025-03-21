#include "server.h"
#include <pthread.h>

static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            printf("Cliente conectado\n");
            char client_ip[48] = {0};
            lws_get_peer_simple(wsi, client_ip, sizeof(client_ip));
            printf("IP del cliente: %s\n", client_ip);
            // Envía mensaje de bienvenida, etc.
            const char *msg = "{\"type\": \"server\", \"content\": \"Conexión establecida\"}";
            size_t msg_len = strlen(msg);
            unsigned char buf[LWS_PRE + msg_len];
            unsigned char *p = &buf[LWS_PRE];
            strcpy((char *)p, msg);
            lws_write(wsi, p, msg_len, LWS_WRITE_TEXT);
            break;
        }        
        case LWS_CALLBACK_RECEIVE:
            handle_message((char *)in, wsi);
            break;
        case LWS_CALLBACK_SERVER_WRITEABLE: {
            pthread_mutex_lock(&broadcast_lock);
            if (g_broadcast_msg) {
                size_t msg_len = strlen(g_broadcast_msg);
                unsigned char buf[LWS_PRE + msg_len];
                unsigned char *p = &buf[LWS_PRE];
                strcpy((char *)p, g_broadcast_msg);
                lws_write(wsi, p, msg_len, LWS_WRITE_TEXT);
            }
            pthread_mutex_unlock(&broadcast_lock);
            break;
        }
        case LWS_CALLBACK_CLOSED:
            printf("Cliente desconectado\n");
            remove_user(wsi);
            break;
        default:
            break;
    }
    return 0;
}

static struct lws_protocols protocols[] = {
    { "chat-protocol", callback_chat, 0, 4096 },
    { NULL, NULL, 0, 0 }
};


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0) {
        fprintf(stderr, "Puerto inválido.\n");
        return 1;
    }

    struct lws_context_creation_info info = {0};
    struct lws_context *context;
    
    info.port = port;
    info.protocols = protocols;
    context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Error creando contexto WebSocket\n");
        return -1;
    }
    
    printf("Servidor WebSocket en puerto %d\n", port);
    
    while (1) {
        lws_service(context, 50); // 🔹 Ahora solo maneja nuevas conexiones
    }

    lws_context_destroy(context);
    return 0;
}
