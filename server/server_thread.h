#ifndef SERVER_THREAD_H
#define SERVER_THREAD_H

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <netinet/in.h>

/* Adresse TCP sur lequel le serveur attend des connections */
typedef struct sockaddr sockaddr;
typedef struct sockaddr_in sockaddr_in;
extern sockaddr_in server_addr;

/* Indique si le serveur accepte des connexions */
extern bool accepting_connections;

/* Nombre de serveurs */
extern int num_servers;

typedef struct server_thread {
	unsigned int id;
	pthread_t pt_tid;
	pthread_attr_t pt_attr;
} server_thread;

void st_open_socket(void);
void st_init(void);
void st_process_request(server_thread *, int);
void *st_code(void *);

void st_print_results(FILE *, bool);

#endif				// SERVER_THREAD_H
