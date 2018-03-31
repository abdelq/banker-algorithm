#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "server_thread.h"

sockaddr_in server_addr = {
	.sin_family = AF_INET
};

int server_socket_fd = -1;
int max_wait_time = 30;
int server_backlog_size = 5;

int num_servers;
int num_resources;

// Nombre de clients enregistrés
unsigned int nb_registered_clients = 0;
pthread_mutex_t mutex_nb_registered_clients = PTHREAD_MUTEX_INITIALIZER;

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

// Structures du banquier
typedef struct matrix {
	int client;
	int *resources;
	struct matrix *next;
} matrix;

struct {
	int *avail;
	matrix *max;
	matrix *alloc;
	matrix *need;
	pthread_mutex_t mutex;
} banker;

static void sigint_handler(int signum)
{
	accepting_connections = false;
}

void st_init()
{
	// Ouvre un socket pour le serveur
	st_open_socket();

	// Gestion d'interruption
	signal(SIGINT, &sigint_handler);

	// Structure du banquier
	banker.avail = NULL;
	banker.max = NULL;
	banker.alloc = NULL;
	banker.need = NULL;
	pthread_mutex_init(&banker.mutex, NULL);
}

void freemat(matrix * head)
{
	matrix *tmp;
	while (head != NULL) {
		tmp = head;
		head = head->next;
		free(tmp->resources);
		free(tmp);
	}
}

void st_uninit()
{
	// Structure du banquier
	free(banker.avail);
	freemat(banker.max);
	freemat(banker.alloc);
	freemat(banker.need);
	pthread_mutex_destroy(&banker.mutex);

	pthread_mutex_destroy(&mutex_nb_registered_clients);
	pthread_mutex_destroy(&mutex_count_accepted);
	pthread_mutex_destroy(&mutex_count_wait);
	pthread_mutex_destroy(&mutex_count_invalid);
	pthread_mutex_destroy(&mutex_count_dispatched);
	pthread_mutex_destroy(&mutex_request_processed);
	pthread_mutex_destroy(&mutex_clients_ended);

	if (server_socket_fd > -1)
		close(server_socket_fd);
}

char *recv_beg(char *args)
{
	pthread_mutex_lock(&banker.mutex);
	// Verify that BEG has not been called already
	if (banker.avail != NULL) {
		pthread_mutex_unlock(&banker.mutex);
		return "ERR already sent\n";
	}
	pthread_mutex_unlock(&banker.mutex);

	if (sscanf(args, " %d\n", &num_resources) != 1)
		return "ERR invalid arguments\n";
	if (num_resources <= 0)
		return "ERR invalid values\n";

	pthread_mutex_lock(&banker.mutex);
	banker.avail = calloc(num_resources, sizeof(int));
	pthread_mutex_unlock(&banker.mutex);

	return "ACK\n";
}

bool is_empty(int *arr)
{
	for (int i = 0; i < num_resources; i++)
		if (arr[i] != 0)
			return false;
	return true;
}

char *recv_pro(char *args)
{
	pthread_mutex_lock(&banker.mutex);
	// Verify that PRO has not been called before BEG
	if (banker.avail == NULL) {
		pthread_mutex_unlock(&banker.mutex);
		return "ERR PRO before BEG\n";
	}
	// Verify that PRO has not been called already
	if (!is_empty(banker.avail)) {
		pthread_mutex_unlock(&banker.mutex);
		return "ERR already sent\n";
	}

	/* Parse received data */
	char *next = args;
	int i, j;
	for (i = 0; i < num_resources; i++) {
		if (sscanf(next, " %d%n", &banker.avail[i], &j) != 1) {
			memset(banker.avail, 0, ++i * sizeof(int));
			pthread_mutex_unlock(&banker.mutex);
			return "ERR invalid argument length\n";
		}
		if (banker.avail[i] < 0) {
			memset(banker.avail, 0, ++i * sizeof(int));
			pthread_mutex_unlock(&banker.mutex);
			return "ERR invalid values\n";
		}
		next += j;
	}
	if (sscanf(next, " %d%n", &j, &j) == 1) {
		memset(banker.avail, 0, ++i * sizeof(int));
		pthread_mutex_unlock(&banker.mutex);
		return "ERR invalid argument length\n";
	}

	if (is_empty(banker.avail)) {
		pthread_mutex_unlock(&banker.mutex);
		return "ERR invalid values\n";
	}
	pthread_mutex_unlock(&banker.mutex);

	return "ACK\n";
}

char *recv_end(char *args)
{
	// No arguments allowed
	if (strncmp(args, "\n", 1) != 0)
		return "ERR invalid argument length\n";

	pthread_mutex_lock(&mutex_nb_registered_clients);
	pthread_mutex_lock(&mutex_count_dispatched);
	if (count_dispatched < nb_registered_clients) {
		pthread_mutex_unlock(&mutex_nb_registered_clients);
		pthread_mutex_unlock(&mutex_count_dispatched);
		return "ERR clients not dispatched yet\n";
	}
	pthread_mutex_unlock(&mutex_nb_registered_clients);
	pthread_mutex_unlock(&mutex_count_dispatched);

	return "ACK\n";
}

matrix *client_exists(int client)
{
	matrix *mat = banker.alloc;
	while (mat != NULL) {
		if (mat->client == client)
			return mat;
		mat = mat->next;
	}
	return NULL;
}

char *recv_ini(char *args)
{
	pthread_mutex_lock(&banker.mutex);
	// Verify that INI has not been called before PRO
	if (is_empty(banker.avail)) {
		pthread_mutex_unlock(&banker.mutex);
		return "ERR INI before PRO\n";
	}
	pthread_mutex_unlock(&banker.mutex);

	int num_client, i, j;
	if (sscanf(args, " %d%n", &num_client, &j) != 1)
		return "ERR invalid argument length\n";
	if (num_client < 0)
		return "ERR invalid client number";
	// Verify that client doesn't already exist
	pthread_mutex_lock(&banker.mutex);
	if (client_exists(num_client)) {
		pthread_mutex_unlock(&banker.mutex);
		return "ERR already exists\n";
	}
	pthread_mutex_unlock(&banker.mutex);

	/* Parse received data */
	char *next = args + j;
	int *max = malloc(num_resources * sizeof(int));
	for (i = 0; i < num_resources; i++) {
		if (sscanf(next, " %d%n", &max[i], &j) != 1) {
			free(max);
			return "ERR invalid argument length\n";
		}
		if (max[i] < 0) {
			free(max);
			return "ERR invalid values\n";
		}
		next += j;
	}
	if (sscanf(next, " %d%n", &j, &j) == 1) {
		free(max);
		return "ERR invalid argument length\n";
	}

	if (is_empty(max)) {
		free(max);
		return "ERR invalid values\n";
	}

	matrix *mat;
	pthread_mutex_lock(&banker.mutex);
	// Maximum
	mat = malloc(sizeof(matrix));
	mat->client = num_client;
	mat->resources = max;
	mat->next = banker.max;
	banker.max = mat;
	// Needed
	mat = malloc(sizeof(matrix));
	mat->client = num_client;
	mat->resources = malloc(num_resources * sizeof(int));
	memcpy(mat->resources, max, num_resources * sizeof(int));
	mat->next = banker.need;
	banker.need = mat;
	// Allocated
	mat = malloc(sizeof(matrix));
	mat->client = num_client;
	mat->resources = malloc(num_resources * sizeof(int));
	memset(mat->resources, 0, num_resources * sizeof(int));
	mat->next = banker.alloc;
	banker.alloc = mat;
	pthread_mutex_unlock(&banker.mutex);

	pthread_mutex_lock(&mutex_nb_registered_clients);
	nb_registered_clients++;
	pthread_mutex_unlock(&mutex_nb_registered_clients);
	return "ACK\n";
}

// FIXME Wait w/ random value for time being
// Look for item in matrix
char *recv_req(char *args)
{
	return "ACK\n";
}

char *recv_clo(char *args)
{
	int num_client;
	if (sscanf(args, " %d\n", &num_client) != 1)
		return "ERR invalid arguments\n";
	if (num_client < 0)
		return "ERR invalid client number";

	matrix *mat;
	pthread_mutex_lock(&banker.mutex);
	if ((mat = client_exists(num_client))) {
		pthread_mutex_lock(&mutex_clients_ended);
		clients_ended++;	// XXX
		pthread_mutex_unlock(&mutex_clients_ended);
		if (!is_empty(mat->resources)) {
			pthread_mutex_unlock(&banker.mutex);
			return "ERR resources not freed";
		}
		mat->client = -1;
	} else {
		pthread_mutex_unlock(&banker.mutex);
		return "ERR invalid client number";
	}
	pthread_mutex_unlock(&banker.mutex);

	pthread_mutex_lock(&mutex_count_dispatched);
	count_dispatched++;
	pthread_mutex_unlock(&mutex_count_dispatched);
	return "ACK\n";
}

void st_process_requests(server_thread * st, int socket_fd)
{
	// TODO Use a read/write file instead of two separate
	FILE *socket_r = fdopen(socket_fd, "r");
	FILE *socket_w = fdopen(socket_fd, "w");

	while (accepting_connections) {
		char cmd[4] = "";
		if (!fread(cmd, 3, 1, socket_r))
			break;

		char *args = NULL;
		size_t args_len = 0;
		ssize_t count = getline(&args, &args_len, socket_r);
		if (!args || count < 1 || args[count - 1] != '\n') {
			fprintf(stderr,
				"Thread %d received incomplete command: %s\n",
				st->id, cmd);
			break;
		} else {
			fprintf(stdout,
				"Thread %d received command: %s%s",
				st->id, cmd, args);
		}

		char *answer = "ERR unknown command\n";
		if (strcmp(cmd, "BEG") == 0) {
			answer = recv_beg(args);
		} else if (strcmp(cmd, "PRO") == 0) {
			answer = recv_pro(args);
		} else if (strcmp(cmd, "END") == 0) {
			answer = recv_end(args);
			if (strncmp(answer, "ACK", 3) == 0)
				accepting_connections = false;	// XXX
		} else if (strcmp(cmd, "INI") == 0) {
			answer = recv_ini(args);
		} else if (strcmp(cmd, "REQ") == 0) {
			answer = recv_req(args);
		} else if (strcmp(cmd, "CLO") == 0) {
			answer = recv_clo(args);
		}

		fprintf(socket_w, answer);
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

void st_open_socket()
{
	server_socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (server_socket_fd < 0) {
		perror("ERROR creating socket");
		exit(1);
	}

	int reuse = 1;
	if (setsockopt
	    (server_socket_fd, SOL_SOCKET, SO_REUSEADDR,
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
