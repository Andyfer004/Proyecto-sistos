#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <libwebsockets.h>

#define MSG_BUFFER_SIZE 256 // Tamaño máximo de los mensajes a enviar

// Obtiene el timestamp actual en formato ISO 8601 (ej: 2025-03-25T14:30:00)
void get_timestamp(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%S", t);
}

// Función interna que se encarga de enviar un string JSON al servidor vía WebSocket (wsi)
static int send_message(struct lws *wsi, const char *json_msg)
{
    unsigned char buf[LWS_PRE + MSG_BUFFER_SIZE];
    size_t msg_len = strlen(json_msg);

    // Verifica si el mensaje excede el tamaño permitido
    if (msg_len > MSG_BUFFER_SIZE)
    {
        lwsl_err("Mensaje demasiado largo para enviar\n");
        return -1;
    }

    // Copia el mensaje al buffer, respetando el espacio reservado por libwebsockets (LWS_PRE)
    memset(buf, 0, sizeof(buf));
    memcpy(&buf[LWS_PRE], json_msg, msg_len);

    // Envia el mensaje al servidor a través del WebSocket
    int n = lws_write(wsi, &buf[LWS_PRE], msg_len, LWS_WRITE_TEXT);

    // Si no se pudo enviar el mensaje, se muestra un error
    if (n < (int)msg_len)
    {
        lwsl_err("Error enviando mensaje\n");
        return -1;
    }
    return 0;
}

// Envia mensaje de tipo "register" para registrar al usuario en el servidor
int send_register_message(struct lws *wsi, const char *username)
{
    // Obtiene el timestamp actual
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    // Construye el mensaje JSON
    char msg[MSG_BUFFER_SIZE];

    // El mensaje debe tener el formato {"type": "register", "sender": "nombre_usuario"}
    snprintf(msg, sizeof(msg),
             "{\"type\": \"register\", \"sender\": \"%s\"}",
             username);

    // Envia el mensaje al servidor
    return send_message(wsi, msg);
}

// Envia mensaje público tipo "broadcast" a todos los usuarios
int send_broadcast_message(struct lws *wsi, const char *username, const char *message)
{
    // Obtiene el timestamp actual
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    // Se construye el mensaje JSON
    char msg[MSG_BUFFER_SIZE];

    // El mensaje debe tener el formato {"type": "broadcast", "sender": "nombre_usuario", "content": "mensaje", "timestamp": "2025-03-25T14:30:00"}
    snprintf(msg, sizeof(msg),
             "{\"type\": \"broadcast\", \"sender\": \"%s\", \"content\": \"%s\", \"timestamp\": \"%s\"}",
             username, message, timestamp);

    return send_message(wsi, msg);
}

// Envia un mensaje privado a un usuario específico
int send_private_message(struct lws *wsi, const char *username, const char *target, const char *message)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    char msg[MSG_BUFFER_SIZE];
    // El mensaje debe tener el formato {"type": "private", "sender": "nombre_usuario", "target": "usuario_destino", "content": "mensaje", "timestamp": "2025-03-25T14:30:00"}
    snprintf(msg, sizeof(msg),
             "{\"type\": \"private\", \"sender\": \"%s\", \"target\": \"%s\", \"content\": \"%s\", \"timestamp\": \"%s\"}",
             username, target, message, timestamp);
    return send_message(wsi, msg);
}

// Solicita al servidor la lista de usuarios conectados
int send_list_users_message(struct lws *wsi, const char *username)
{
    char msg[MSG_BUFFER_SIZE];
    // El mensaje debe tener el formato {"type": "list_users", "sender": "nombre_usuario"}
    snprintf(msg, sizeof(msg),
             "{\"type\": \"list_users\", \"sender\": \"%s\"}",
             username);
    return send_message(wsi, msg);
}

// Solicita información (estado/IP) sobre un usuario específico
int send_user_info_message(struct lws *wsi, const char *username, const char *target)
{
    char msg[MSG_BUFFER_SIZE];
    // El mensaje debe tener el formato {"type": "user_info", "sender": "nombre_usuario", "target": "usuario_destino"}
    snprintf(msg, sizeof(msg),
             "{\"type\": \"user_info\", \"sender\": \"%s\", \"target\": \"%s\"}",
             username, target);
    return send_message(wsi, msg);
}

// Envia un cambio de estado del usuario (ej: ACTIVO, OCUPADO, INACTIVO)
int send_change_status_message(struct lws *wsi, const char *username, const char *status)
{
    char msg[MSG_BUFFER_SIZE];
    // El mensaje debe tener el formato {"type": "change_status", "sender": "nombre_usuario", "content": "nuevo_estado"}
    snprintf(msg, sizeof(msg),
             "{\"type\": \"change_status\", \"sender\": \"%s\", \"content\": \"%s\"}",
             username, status);
    return send_message(wsi, msg);
}

// Envia al servidor una notificación de que el usuario se está desconectando
int send_disconnect_message(struct lws *wsi, const char *username)
{
    char msg[MSG_BUFFER_SIZE];
    // El mensaje debe tener el formato {"type": "disconnect", "sender": "nombre_usuario", "content": "Cierre de sesión"}
    snprintf(msg, sizeof(msg),
             "{\"type\": \"disconnect\", \"sender\": \"%s\", \"content\": \"Cierre de sesión\"}",
             username);
    return send_message(wsi, msg);
}
