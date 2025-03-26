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
    time_t now = time(NULL);                        // Obtener el tiempo actual (en segundos desde Epoch)
    struct tm *t = localtime(&now);                 // Convertir a tiempo local (estructura tm)
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%S", t); // Formatear la fecha y hora en formato ISO 8601
}

// Función interna que se encarga de enviar un string JSON al servidor vía WebSocket (wsi)
// Recibe el WebSocket (`wsi`) y el mensaje JSON a enviar (`json_msg`).
static int send_message(struct lws *wsi, const char *json_msg)
{
    // Buffer que debe incluir un offset especial (LWS_PRE) requerido por libwebsockets
    unsigned char buf[LWS_PRE + MSG_BUFFER_SIZE];
    size_t msg_len = strlen(json_msg);

    // Verifica que el mensaje no exceda el tamaño máximo permitido
    if (msg_len > MSG_BUFFER_SIZE)
    {
        lwsl_err("Mensaje demasiado largo para enviar\n");
        return -1;
    }

    // Inicializa el buffer y copia el mensaje a partir del offset LWS_PRE
    memset(buf, 0, sizeof(buf));
    memcpy(&buf[LWS_PRE], json_msg, msg_len);

    // Envía el mensaje a través del WebSocket especificando que es texto
    int n = lws_write(wsi, &buf[LWS_PRE], msg_len, LWS_WRITE_TEXT);
    // Si no se pudo enviar el mensaje, se retorna un error
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

    // Construye el mensaje JSON
    char msg[MSG_BUFFER_SIZE];

    // Construye un JSON con el tipo "register" y el nombre de usuario.
    snprintf(msg, sizeof(msg),
             "{\"type\": \"register\", \"sender\": \"%s\"}",
             username);

    // Envia el mensaje al servidor
    return send_message(wsi, msg);
}

// Envía un mensaje de difusión (broadcast) a todos los usuarios.
int send_broadcast_message(struct lws *wsi, const char *username, const char *message)
{
    // Obtiene el timestamp actual
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    // Se construye el mensaje JSON
    char msg[MSG_BUFFER_SIZE];

    // Incluye el remitente, el contenido del mensaje y el timestamp actual.
    snprintf(msg, sizeof(msg),
             "{\"type\": \"broadcast\", \"sender\": \"%s\", \"content\": \"%s\", \"timestamp\": \"%s\"}",
             username, message, timestamp);

    return send_message(wsi, msg);
}

// Envia un mensaje privado a un destinatario en específico
int send_private_message(struct lws *wsi, const char *username, const char *target, const char *message)
{
    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    char msg[MSG_BUFFER_SIZE];
    // Construye un JSON con el tipo "private", incluyendo remitente, destinatario,
    // contenido del mensaje y timestamp.
    snprintf(msg, sizeof(msg),
             "{\"type\": \"private\", \"sender\": \"%s\", \"target\": \"%s\", \"content\": \"%s\", \"timestamp\": \"%s\"}",
             username, target, message, timestamp);
    return send_message(wsi, msg);
}

// Solicita al servidor la lista de usuarios conectados
int send_list_users_message(struct lws *wsi, const char *username)
{
    char msg[MSG_BUFFER_SIZE];
    // Envía un JSON con el tipo "list_users" y el nombre del usuario que realiza la consulta.
    snprintf(msg, sizeof(msg),
             "{\"type\": \"list_users\", \"sender\": \"%s\"}",
             username);
    return send_message(wsi, msg);
}

// Solicita información (estado/IP) sobre un usuario específico
int send_user_info_message(struct lws *wsi, const char *username, const char *target)
{
    char msg[MSG_BUFFER_SIZE];
    // Envía un JSON con el tipo "user_info", indicando quién solicita la información
    // y cuál es el usuario objetivo.
    snprintf(msg, sizeof(msg),
             "{\"type\": \"user_info\", \"sender\": \"%s\", \"target\": \"%s\"}",
             username, target);
    return send_message(wsi, msg);
}

// Envia un cambio de estado del usuario (ej: ACTIVO, OCUPADO, INACTIVO)
int send_change_status_message(struct lws *wsi, const char *username, const char *status)
{
    char msg[MSG_BUFFER_SIZE];
    // Construye un JSON con el tipo "change_status" que incluye el nombre del usuario
    // y el nuevo estado.
    snprintf(msg, sizeof(msg),
             "{\"type\": \"change_status\", \"sender\": \"%s\", \"content\": \"%s\"}",
             username, status);
    return send_message(wsi, msg);
}

// Envia al servidor una notificación de que el usuario se está desconectando
int send_disconnect_message(struct lws *wsi, const char *username)
{
    char msg[MSG_BUFFER_SIZE];
    // Construye un JSON con el tipo "disconnect" para indicar que el usuario se está
    // desconectando.
    snprintf(msg, sizeof(msg),
             "{\"type\": \"disconnect\", \"sender\": \"%s\", \"content\": \"Cierre de sesión\"}",
             username);
    return send_message(wsi, msg);
}
