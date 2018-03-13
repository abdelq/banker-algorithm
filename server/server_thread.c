#define _XOPEN_SOURCE 700

#include <netdb.h>
#include <signal.h>
#include <strings.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "server_thread.h"

/* Configuration constants */
enum {
	max_wait_time = 30,
	server_backlog_size = 5
};

int server_socket_fd;

// Nombre de clients enregistrés
unsigned int nb_registered_clients = 0;

/* Variables du journal */
// Nombre de requêtes acceptées immédiatement (ACK envoyé en réponse à REQ)
unsigned int count_accepted = 0;

// Nombre de requêtes acceptées après un délai (ACK après REQ, mais retardé)
unsigned int count_wait = 0;

// Nombre de requêtes erronées (ERR envoyé en réponse à REQ)
unsigned int count_invalid = 0;

// Nombre de clients qui se sont terminés correctement (ACK envoyé en réponse à CLO)
unsigned int count_dispatched = 0;

// Nombre total de requêtes (REQ) traités
unsigned int request_processed = 0;

// Nombre de clients ayant envoyé le message CLO
unsigned int clients_ended = 0;

struct {
	int *available;
	int **max;
	int **allocation;
	int **need;
    pthread_mutex_t mutex;
} banker;

static void sigint_handler(int signum)
{
	accepting_connections = false;
}

void st_init()
{
	// Handle interrupt
	signal(SIGINT, &sigint_handler);

    pthread_mutex_init(&banker.mutex, NULL);
	// TODO

	// Attend la connection d'un client et initialise les structures pour
	// l'algorithme du banquier.

	// END TODO
}

void st_process_requests(server_thread * st, int socket_fd)
{
	// TODO: Remplacer le contenu de cette fonction
	FILE *socket_r = fdopen(socket_fd, "r");
	FILE *socket_w = fdopen(socket_fd, "w");

	while (true) {
		char cmd[4] = { '\0', '\0', '\0', '\0' };
		if (!fread(cmd, 3, 1, socket_r))
			break;
		char *args = NULL;
		size_t args_len = 0;
		ssize_t cnt = getline(&args, &args_len, socket_r);
		if (!args || cnt < 1 || args[cnt - 1] != '\n') {
			printf("Thread %d received incomplete cmd=%s!\n",
			       st->id, cmd);
			break;
		}

		printf("Thread %d received the command: %s%s", st->id, cmd,
		       args);

		fprintf(socket_w, "ERR Unknown command\n");
		free(args);
	}

	fclose(socket_r);
	fclose(socket_w);
	// TODO end
}

void st_signal()
{
    pthread_mutex_destroy(&banker.mutex); // XXX
	// TODO Remplacer le contenu de cette fonction
}

int st_wait()
{
	struct sockaddr_in thread_addr;
	socklen_t socket_len = sizeof(thread_addr);
	int thread_socket_fd = -1;
	int end_time = time(NULL) + max_wait_time;

	while (thread_socket_fd < 0 && accepting_connections) {
		thread_socket_fd = accept(server_socket_fd,
					  (struct sockaddr *)&thread_addr,
					  &socket_len);
		if (time(NULL) >= end_time) {
			break;
		}
	}
	return thread_socket_fd;
}

void *st_code(void *param)
{
	server_thread *st = (server_thread *) param;

	int thread_socket_fd = -1;

	// Boucle de traitement des requêtes
	while (accepting_connections) {
		// Wait for a I/O socket
		thread_socket_fd = st_wait();
		if (thread_socket_fd < 0) {
			fprintf(stderr, "Time out on thread %d.\n", st->id);
			continue;
		}

		if (thread_socket_fd > 0) {
			st_process_requests(st, thread_socket_fd);
			close(thread_socket_fd);
		}
	}
	return NULL;
}

// Ouvre un socket pour le serveur
void st_open_socket(int port_number)
{
	server_socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (server_socket_fd < 0)
		perror("ERROR creating socket");

	if (setsockopt(server_socket_fd, SOL_SOCKET, SO_REUSEPORT, &(int) {
		       1}, sizeof(int)) < 0) {
		perror("setsockopt()");
		exit(1);
	}

	struct sockaddr_in serv_addr = {
		.sin_addr = {INADDR_ANY},
		.sin_port = htons(port_number),
		.sin_family = AF_INET,
	};

	if (bind
	    (server_socket_fd, (struct sockaddr *)&serv_addr,
	     sizeof(serv_addr)) < 0)
		perror("ERROR on binding");

	listen(server_socket_fd, server_backlog_size);
}

// Affiche les données recueillies lors de l'exécution du serveur
void st_print_results(FILE * fd, bool verbose)
{
	if (fd == NULL)
		fd = stdout;
	if (verbose) {
		fprintf(fd, "\n---- Résultat du serveur ----\n");
		fprintf(fd, "Requêtes acceptées : %d\n", count_accepted);
		fprintf(fd, "Requêtes : %d\n", count_wait);
		fprintf(fd, "Requêtes invalides : %d\n", count_invalid);
		fprintf(fd, "Clients : %d\n", count_dispatched);
		fprintf(fd, "Requêtes traitées : %d\n", request_processed);
	} else {
		fprintf(fd, "%d %d %d %d %d\n", count_accepted, count_wait,
			count_invalid, count_dispatched, request_processed);
	}
}
