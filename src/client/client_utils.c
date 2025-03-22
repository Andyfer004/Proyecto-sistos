#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libwebsockets.h>

// Definición de un tamaño máximo para los mensajes (puedes ajustarlo si es necesario)
#define MSG_BUFFER_SIZE 256

// Función para obtener el timestamp actual en formato ISO 8601.
void get_timestamp(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%S", t);
}

// Función interna para enviar un mensaje JSON a través del WebSocket.
static int send_message(struct lws *wsi, const char *json_msg)
{
    unsigned char buf[LWS_PRE + MSG_BUFFER_SIZE];
    size_t msg_len = strlen(json_msg);

    if (msg_len > MSG_BUFFER_SIZE)
    {
        lwsl_err("Mensaje demasiado largo para enviar\n");
        return -1;
    }

    // Limpiar el buffer y copiar el mensaje con el offset requerido.
    memset(buf, 0, sizeof(buf));
    memcpy(&buf[LWS_PRE], json_msg, msg_len);

    int n = lws_write(wsi, &buf[LWS_PRE], msg_len, LWS_WRITE_TEXT);
    if (n < (int)msg_len)
    {
        lwsl_err("Error enviando mensaje\n");
        return -1;
    }
    return 0;
}

// Envía un mensaje de registro al servidor.
int send_register_message(struct lws *wsi, const char *username)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    char msg[MSG_BUFFER_SIZE];
    snprintf(msg, sizeof(msg),
             "{\"type\": \"register\", \"sender\": \"%s\"}",
             username);

    return send_message(wsi, msg);
}

// Envía un mensaje de chat general (broadcast) al servidor.
int send_broadcast_message(struct lws *wsi, const char *username, const char *message)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    char msg[MSG_BUFFER_SIZE];
    snprintf(msg, sizeof(msg),
             "{\"type\": \"broadcast\", \"sender\": \"%s\", \"content\": \"%s\", \"timestamp\": \"%s\"}",
             username, message, timestamp);
    return send_message(wsi, msg);
}

// Envía un mensaje privado a un destinatario específico.
int send_private_message(struct lws *wsi, const char *username, const char *target, const char *message)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    char msg[MSG_BUFFER_SIZE];
    snprintf(msg, sizeof(msg),
             "{\"type\": \"private\", \"sender\": \"%s\", \"target\": \"%s\", \"content\": \"%s\", \"timestamp\": \"%s\"}",
             username, target, message, timestamp);
    return send_message(wsi, msg);
}

// Solicita la lista de usuarios conectados.
int send_list_users_message(struct lws *wsi, const char *username)
{
    char msg[MSG_BUFFER_SIZE];
    snprintf(msg, sizeof(msg),
             "{\"type\": \"list_users\", \"sender\": \"%s\"}",
             username);
    return send_message(wsi, msg);
}

// Solicita información de un usuario en específico.
int send_user_info_message(struct lws *wsi, const char *username, const char *target)
{
    char msg[MSG_BUFFER_SIZE];
    snprintf(msg, sizeof(msg),
             "{\"type\": \"user_info\", \"sender\": \"%s\", \"target\": \"%s\"}",
             username, target);
    return send_message(wsi, msg);
}

// Envía un mensaje para cambiar el estado del usuario.
int send_change_status_message(struct lws *wsi, const char *username, const char *status)
{
    char msg[MSG_BUFFER_SIZE];
    snprintf(msg, sizeof(msg),
             "{\"type\": \"change_status\", \"sender\": \"%s\", \"content\": \"%s\"}",
             username, status);
    return send_message(wsi, msg);
}

// Envía un mensaje de desconexión al servidor.
int send_disconnect_message(struct lws *wsi, const char *username)
{
    char msg[MSG_BUFFER_SIZE];
    snprintf(msg, sizeof(msg),
             "{\"type\": \"disconnect\", \"sender\": \"%s\", \"content\": \"Cierre de sesión\"}",
             username);
    return send_message(wsi, msg);
}
