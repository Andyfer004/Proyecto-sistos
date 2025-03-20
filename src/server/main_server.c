#include "server.h"

static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            printf("Cliente conectado\n");
            // Envía mensaje de bienvenida
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

int main() {
    struct lws_context_creation_info info = {0};
    struct lws_context *context;
    
    info.port = 8080;
    info.protocols = protocols;
    context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "Error creando contexto WebSocket\n");
        return -1;
    }
    
    printf("Servidor WebSocket en puerto 8080\n");
    while (1) {
        lws_service(context, 50);
    }
    
    lws_context_destroy(context);
    return 0;
}