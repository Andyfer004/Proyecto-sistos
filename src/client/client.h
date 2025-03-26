#ifndef CLIENT_H
#define CLIENT_H

#include <libwebsockets.h>

// Estructura utilizada para almacenar los parámetros necesarios para enviar un mensaje privado.
typedef struct
{
    struct lws *wsi;   // WebSocket en el que se enviará el mensaje
    char sender[50];   // Nombre del remitente
    char target[50];   // Nombre del destinatario
    char message[200]; // Contenido del mensaje
} PrivateMessageArgs;

// Obtiene el timestamp actual en formato ISO 8601 y lo guarda en el buffer.
void get_timestamp(char *buffer, size_t size);

// Funcion para enviar un mensaje de registro al servidor.
int send_register_message(struct lws *wsi, const char *username);

// Funcion para enviar un mensaje de broadcast al servidor.
int send_broadcast_message(struct lws *wsi, const char *username, const char *message);

// Funcion para enviar un mensaje privado al servidor.
int send_private_message(struct lws *wsi, const char *username, const char *target, const char *message);

// Funcion para enviar un mensaje de lista de usuarios al servidor.
int send_list_users_message(struct lws *wsi, const char *username);

// Funcion para enviar un mensaje de informacion de algún usuario que este conectado en el servidor.
int send_user_info_message(struct lws *wsi, const char *username, const char *target);

// Funcion para enviar un mensaje de cambio de estado al servidor.
int send_change_status_message(struct lws *wsi, const char *username, const char *status);

// Funcion para enviar un mensaje de desconexión al servidor.
int send_disconnect_message(struct lws *wsi, const char *username);

#endif // CLIENT_H
