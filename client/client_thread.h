#ifndef CLIENT_THREAD_H
#define CLIENT_THREAD_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <netinet/in.h>

/* Adresse TCP sur lequel le serveur attend des connections */
typedef struct sockaddr sockaddr;
typedef struct sockaddr_in sockaddr_in;
extern sockaddr_in server_addr;

/* Nombre de clients */
extern int num_clients;

/* Nombre de requêtes que chaque client doit envoyer */
extern int num_request_per_client;

/* Nombre de resources différentes */
extern int num_resources;

/* Quantité disponible pour chaque resource */
extern int *provis_resources;

/* Quantité de resources courante/maximale de chaque client */
extern int **cur_resources_per_client;
extern int **max_resources_per_client;

typedef struct client_thread {
	unsigned int id;
	pthread_t pt_id;
	pthread_attr_t pt_attr;
} client_thread;

void ct_init(client_thread *, int);
void ct_create_and_start(client_thread *);
void ct_wait_server(void);

int send_beg(void);
int send_pro(void);
int send_end(void);

void st_print_results(FILE *, bool);

#endif				// CLIENT_THREAD_H
