#ifndef CLIENTTHREAD_H
#define CLIENTTHREAD_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include <unistd.h>

/* Port TCP sur lequel le serveur attend des connections.  */
extern int port_number;

/* Nombre de requêtes que chaque client doit envoyer.  */
extern int num_request_per_client;

/* Nombre de resources différentes.  */
extern int num_resources;

/* Quantité disponible pour chaque resource.  */
extern int *provisioned_resources;


typedef struct client_thread client_thread;
struct client_thread
{
  unsigned int id;
  pthread_t pt_tid;
  pthread_attr_t pt_attr;
};


void ct_init (client_thread *);
void ct_create_and_start (client_thread *);
void ct_wait_server ();

void st_print_results (FILE *, bool);

#endif // CLIENTTHREAD_H
