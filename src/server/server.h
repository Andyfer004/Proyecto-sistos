#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <libwebsockets.h>

#define MAX_USERS 100

typedef struct {
    char username[50];
    struct lws *wsi;
    int status; // 0 = ACTIVO, 1 = OCUPADO, 2 = INACTIVO
} User;

extern User users[MAX_USERS];
extern int user_count;
extern pthread_mutex_t user_lock;

// Variables globales para broadcast
extern char *g_broadcast_msg;
extern pthread_mutex_t broadcast_lock;

// Funciones
// Ahora add_user devuelve 1 si se registró correctamente, 0 si ya existe.
int add_user(const char *username, struct lws *wsi);
void remove_user(struct lws *wsi);
void broadcast_message(const char *message);
void handle_message(const char *msg, struct lws *wsi);

#endif