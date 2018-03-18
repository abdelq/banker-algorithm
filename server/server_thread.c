#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "server_thread.h"

sockaddr_in server_addr = {
	.sin_family = AF_INET
};

int server_socket_fd;
const int max_wait_time = 30;
const int server_backlog_size = 5;

int num_servers;

// Nombre de clients enregistrés
unsigned int nb_registered_clients = 0;
//pthread_mutex_t mutex_nb_registered_clients = PTHREAD_MUTEX_INITIALIZER;

/* Variables du journal */
// Nombre de requêtes acceptées immédiatement (ACK envoyé en réponse à REQ)
unsigned int count_accepted = 0;
pthread_mutex_t mutex_count_accepted = PTHREAD_MUTEX_INITIALIZER;

// Nombre de requêtes acceptées après un délai (ACK après REQ, mais retardé)
unsigned int count_wait = 0;
pthread_mutex_t mutex_count_wait = PTHREAD_MUTEX_INITIALIZER;

// Nombre de requêtes erronées (ERR envoyé en réponse à REQ)
unsigned int count_invalid = 0;
pthread_mutex_t mutex_count_invalid = PTHREAD_MUTEX_INITIALIZER;

// Nombre de clients terminés correctement (ACK envoyé en réponse à CLO)
unsigned int count_dispatched = 0;
pthread_mutex_t mutex_count_dispatched = PTHREAD_MUTEX_INITIALIZER;

// Nombre total de requêtes (REQ) traités
unsigned int request_processed = 0;
pthread_mutex_t mutex_request_processed = PTHREAD_MUTEX_INITIALIZER;

// Nombre de clients ayant envoyé le message CLO
unsigned int clients_ended = 0;
pthread_mutex_t mutex_clients_ended = PTHREAD_MUTEX_INITIALIZER;

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

	// TODO Attend la connection d'un client et initialise les structures pour
	// l'algorithme du banquier
	//pthread_mutex_init(&banker.mutex, NULL); // TODO Destroy
}

// FIXME
void st_process_requests(server_thread * st, int socket_fd)
{
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
		fflush(socket_w);
		free(args);
	}

	fclose(socket_r);
	fclose(socket_w);
}

int st_wait()
{
	sockaddr_in socket_addr;
	socklen_t socket_len = sizeof(socket_addr);

	int thread_socket_fd = -1, start_time = time(NULL);
	while (thread_socket_fd < 0 && accepting_connections) {
		thread_socket_fd = accept(server_socket_fd,
					  (sockaddr *) & socket_addr,
					  &socket_len);
		if (difftime(start_time, time(NULL)) > max_wait_time)
			break;
	}

	return thread_socket_fd;
}

void *st_code(void *param)
{
	server_thread *st = (server_thread *) param;

	int thread_socket_fd;
	while (accepting_connections) {
		// Attente d'un socket
		if ((thread_socket_fd = st_wait()) < 0) {
			fprintf(stderr, "Time out on thread %d\n", st->id);
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
void st_open_socket()
{
	server_socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (server_socket_fd < 0)
		perror("ERROR creating socket");

	int reuse = 1;
	if (setsockopt
	    (server_socket_fd, SOL_SOCKET, SO_REUSEPORT,
	     &reuse, sizeof(reuse)) < 0) {
		perror("setsockopt");
		exit(1);
	}

	if (bind
	    (server_socket_fd, (sockaddr *) & server_addr,
	     sizeof(server_addr)) < 0)
		perror("ERROR on binding");

	if (listen(server_socket_fd, server_backlog_size) < 0)
		perror("ERROR on listening");
}

void st_print_results(FILE * fd, bool verbose)
{
	if (fd == NULL)
		fd = stdout;
	if (verbose) {
		fprintf(fd, "\n---- Résultat du serveur ----\n");
		fprintf(fd, "Requêtes acceptées : %d\n", count_accepted);
		fprintf(fd, "Requêtes en attente : %d\n", count_wait);
		fprintf(fd, "Requêtes invalides : %d\n", count_invalid);
		fprintf(fd, "Clients : %d\n", count_dispatched);
		fprintf(fd, "Requêtes traitées : %d\n", request_processed);
	} else {
		fprintf(fd, "%d %d %d %d %d\n", count_accepted, count_wait,
			count_invalid, count_dispatched, request_processed);
	}
}
