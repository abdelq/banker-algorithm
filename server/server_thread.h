#ifndef SERVER_THREAD_H
#define SERVER_THREAD_H

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern bool accepting_connections;

typedef struct server_thread {
	unsigned int id;
	pthread_t pt_tid;
	pthread_attr_t pt_attr;
} server_thread;

void st_open_socket(int port_number);
void st_init(void);
void st_process_request(server_thread *, int);
void st_signal(void);
void *st_code(void *);
void st_print_results(FILE *, bool);

#endif				// SERVER_THREAD_H
