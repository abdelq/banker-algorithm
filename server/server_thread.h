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

/* Nombre de resources */
extern int num_resources;

typedef struct server_thread {
	unsigned int id;
	pthread_t pt_id;
	pthread_attr_t pt_attr;
} server_thread;

void st_init(void);
void st_uninit(void);
void *st_code(void *);

typedef struct client {
	int id;
	bool closed;
	int *max;
	int *alloc;
	int *need;
	struct client *next;
} client;

bool res_more_than(int *, int *);
void allocate_req(int *, int *, int *, int *);
void deallocate_req(int *, int *, int *, int *);
bool is_safe(int, int *, client *);

void st_print_results(FILE *, bool);

#endif				// SERVER_THREAD_H
