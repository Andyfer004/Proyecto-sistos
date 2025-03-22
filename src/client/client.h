#ifndef CLIENT_H
#define CLIENT_H

#include <libwebsockets.h>

typedef struct
{
    struct lws *wsi;
    char sender[50];
    char target[50];
    char message[200];
} PrivateMessageArgs;

// Obtiene el timestamp actual en formato ISO 8601 y lo guarda en el buffer.
void get_timestamp(char *buffer, size_t size);

// Funciones para enviar mensajes en formato JSON a través del WebSocket.
int send_register_message(struct lws *wsi, const char *username);
int send_broadcast_message(struct lws *wsi, const char *username, const char *message);
int send_private_message(struct lws *wsi, const char *username, const char *target, const char *message);
int send_list_users_message(struct lws *wsi, const char *username);
int send_user_info_message(struct lws *wsi, const char *username, const char *target);
int send_change_status_message(struct lws *wsi, const char *username, const char *status);
int send_disconnect_message(struct lws *wsi, const char *username);

#endif // CLIENT_H
