#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "server_thread.h"

sockaddr_in server_addr = {
	.sin_family = AF_INET
};

int server_socket_fd = -1;
int max_wait_time = 30;
int server_backlog_size = 5;

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

// Nombre total de requêtes (REQ) traités
unsigned int request_processed = 0;
pthread_mutex_t mutex_request_processed = PTHREAD_MUTEX_INITIALIZER;

// Nombre de clients terminés correctement (ACK envoyé en réponse à CLO)
unsigned int count_dispatched = 0;
pthread_mutex_t mutex_count_dispatched = PTHREAD_MUTEX_INITIALIZER;

// Nombre de clients ayant envoyé le message CLO
unsigned int clients_ended = 0;
pthread_mutex_t mutex_clients_ended = PTHREAD_MUTEX_INITIALIZER;

// Structure du banquier
struct {
	int *avail;
	client *clients;
	pthread_mutex_t mutex;
} banker;

static void sigint_handler(int signum)
{
	accepting_connections = false;
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

	/* Parse received data */
	if (sscanf(args, " %d\n", &num_resources) != 1)
		return "ERR invalid arguments\n";
	if (num_resources <= 0)
		return "ERR invalid values\n";

	pthread_mutex_lock(&banker.mutex);
	banker.avail = calloc(num_resources, sizeof(int));
	pthread_mutex_unlock(&banker.mutex);

	return "ACK\n";
}

bool is_empty(int *res)
{
	for (int i = 0; i < num_resources; i++)
		if (res[i] != 0)
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
	pthread_mutex_unlock(&banker.mutex);

	/* Parse received data */
	char *next = args;
	int j;
	int pro[num_resources];
	for (int i = 0; i < num_resources; i++) {
		if (sscanf(next, " %d%n", &pro[i], &j) != 1)
			return "ERR invalid argument length\n";
		if (pro[i] < 0)
			return "ERR invalid values\n";
		next += j;
	}

	if (sscanf(next, " %d%n", &j, &j) == 1)
		return "ERR invalid argument length\n";
	if (is_empty(pro))	// XXX
		return "ERR invalid values\n";

	pthread_mutex_lock(&banker.mutex);
	memcpy(banker.avail, pro, num_resources * sizeof(int));
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
	if (count_dispatched < nb_registered_clients) {	// XXX
		pthread_mutex_unlock(&mutex_nb_registered_clients);
		pthread_mutex_unlock(&mutex_count_dispatched);
		return "ERR clients not dispatched yet\n";
	}
	pthread_mutex_unlock(&mutex_nb_registered_clients);
	pthread_mutex_unlock(&mutex_count_dispatched);

	return "ACK\n";
}

client *find_client(int id)
{
	client *c = banker.clients;
	while (c != NULL) {
		if (c->id == id)
			return c;
		c = c->next;
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

	int num_client, j;
	if (sscanf(args, " %d%n", &num_client, &j) != 1)
		return "ERR invalid argument length\n";
	if (num_client < 0)
		return "ERR invalid client number\n";

	/* Verify that client doesn't already exist */
	pthread_mutex_lock(&banker.mutex);
	if (find_client(num_client)) {
		pthread_mutex_unlock(&banker.mutex);
		return "ERR already exists\n";
	}
	pthread_mutex_unlock(&banker.mutex);

	/* Parse received data */
	char *next = args + j;
	int *max = malloc(num_resources * sizeof(int));
	for (int i = 0; i < num_resources; i++) {
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
	if (is_empty(max)) {	// XXX
		free(max);
		return "ERR invalid values\n";
	}

	/* Client */
	client *c = malloc(sizeof(client));
	c->id = num_client;
	c->closed = false;
	c->waiting = false;
	c->max = max;
	c->alloc = calloc(num_resources, sizeof(int));
	c->need = malloc(num_resources * sizeof(int));
	memcpy(c->need, max, num_resources * sizeof(int));

	// Insert into list
	pthread_mutex_lock(&banker.mutex);
	c->next = banker.clients;
	banker.clients = c;
	pthread_mutex_unlock(&banker.mutex);

	pthread_mutex_lock(&mutex_nb_registered_clients);
	nb_registered_clients++;
	pthread_mutex_unlock(&mutex_nb_registered_clients);

	return "ACK\n";
}

char *recv_req(char *args)
{
	pthread_mutex_lock(&mutex_request_processed);
	request_processed++;
	pthread_mutex_unlock(&mutex_request_processed);

	int num_client, j;
	if (sscanf(args, " %d%n", &num_client, &j) != 1) {
		pthread_mutex_lock(&mutex_count_invalid);
		count_invalid++;
		pthread_mutex_unlock(&mutex_count_invalid);
		return "ERR invalid argument length\n";
	}
	if (num_client < 0) {
		pthread_mutex_lock(&mutex_count_invalid);
		count_invalid++;
		pthread_mutex_unlock(&mutex_count_invalid);
		return "ERR invalid client number\n";
	}

	client *c;
	pthread_mutex_lock(&banker.mutex);
	if (!(c = find_client(num_client))) {
		pthread_mutex_unlock(&banker.mutex);
		pthread_mutex_lock(&mutex_count_invalid);
		count_invalid++;
		pthread_mutex_unlock(&mutex_count_invalid);
		return "ERR REQ before INI\n";
	}
	if (c->closed) {
		/*c->waiting = false; */
		pthread_mutex_unlock(&banker.mutex);
		pthread_mutex_lock(&mutex_count_invalid);
		count_invalid++;
		pthread_mutex_unlock(&mutex_count_invalid);
		return "ERR REQ after CLO\n";
	}
	pthread_mutex_unlock(&banker.mutex);

	/* Parse received data */
	char *next = args + j;
	int req[num_resources];
	for (int i = 0; i < num_resources; i++) {
		if (sscanf(next, " %d%n", &req[i], &j) != 1) {
			/*pthread_mutex_lock(&banker.mutex);
			   c->waiting = false;
			   pthread_mutex_unlock(&banker.mutex); */
			pthread_mutex_lock(&mutex_count_invalid);
			count_invalid++;
			pthread_mutex_unlock(&mutex_count_invalid);
			return "ERR invalid argument length\n";
		}
		next += j;
	}

	if (sscanf(next, " %d%n", &j, &j) == 1) {
		/*pthread_mutex_lock(&banker.mutex);
		   c->waiting = false;
		   pthread_mutex_unlock(&banker.mutex); */
		pthread_mutex_lock(&mutex_count_invalid);
		count_invalid++;
		pthread_mutex_unlock(&mutex_count_invalid);
		return "ERR invalid argument length\n";
	}

	/* Requesting more than needed */
	pthread_mutex_lock(&banker.mutex);
	if (res_more_than(req, c->need)) {
		/*c->waiting = false; */
		pthread_mutex_unlock(&banker.mutex);
		pthread_mutex_lock(&mutex_count_invalid);
		count_invalid++;
		pthread_mutex_unlock(&mutex_count_invalid);
		return "ERR requesting more than needed\n";
	}

	/* Freeing more than allocated */
	for (int i = 0; i < num_resources; i++) {
		if (req[i] + c->alloc[i] < 0) {
			/*c->waiting = false; */
			pthread_mutex_unlock(&banker.mutex);
			pthread_mutex_lock(&mutex_count_invalid);
			count_invalid++;
			pthread_mutex_unlock(&mutex_count_invalid);
			return "ERR freeing more than allocated\n";
		}
	}

	/* Requesting more than available */
	if (res_more_than(req, banker.avail)) {
		c->waiting = true;
		pthread_mutex_unlock(&banker.mutex);
		return "WAIT 1\n";
	}

	/* Algorithme du banquier */
	allocate_req(req, banker.avail, c->alloc, c->need);
	pthread_mutex_lock(&mutex_nb_registered_clients);
	if (!is_safe(nb_registered_clients, banker.avail, banker.clients)) {
		pthread_mutex_unlock(&mutex_nb_registered_clients);
		deallocate_req(req, banker.avail, c->alloc, c->need);
		c->waiting = true;
		pthread_mutex_unlock(&banker.mutex);
		return "WAIT 1\n";
	}
	pthread_mutex_unlock(&mutex_nb_registered_clients);

	if (c->waiting) {
		c->waiting = false;
		pthread_mutex_unlock(&banker.mutex);
		pthread_mutex_lock(&mutex_count_wait);
		count_wait++;
		pthread_mutex_unlock(&mutex_count_wait);
	} else {
		pthread_mutex_unlock(&banker.mutex);
		pthread_mutex_lock(&mutex_count_accepted);
		count_accepted++;
		pthread_mutex_unlock(&mutex_count_accepted);
	}

	return "ACK\n";
}

char *recv_clo(char *args)
{
	pthread_mutex_lock(&mutex_clients_ended);
	clients_ended++;	// XXX
	pthread_mutex_unlock(&mutex_clients_ended);

	int num_client;
	if (sscanf(args, " %d\n", &num_client) != 1)
		return "ERR invalid arguments\n";
	if (num_client < 0)
		return "ERR invalid client number\n";

	client *c;
	pthread_mutex_lock(&banker.mutex);
	if (!(c = find_client(num_client))) {
		pthread_mutex_unlock(&banker.mutex);
		return "ERR CLO before INI\n";
	}

	if (!is_empty(c->alloc)) {
		pthread_mutex_unlock(&banker.mutex);
		return "ERR resources not freed\n";
	}
	c->closed = true;
	pthread_mutex_unlock(&banker.mutex);

	pthread_mutex_lock(&mutex_count_dispatched);
	count_dispatched++;
	pthread_mutex_unlock(&mutex_count_dispatched);

	return "ACK\n";
}

void st_process_requests(server_thread * st, int socket_fd)
{
	FILE *socket_rw = fdopen(socket_fd, "r+");

	while (accepting_connections) {
		char cmd[4] = "";
		if (!fread(cmd, 3, 1, socket_rw))
			break;

		char *args = NULL;
		size_t args_len = 0;
		ssize_t count = getline(&args, &args_len, socket_rw);
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
			if (strncmp(answer, "WAIT", 4) == 0) {
				char new_answer[8];
				snprintf(new_answer, 8, "WAIT %d\n", 1 + rand() % 5);	// XXX
				answer = new_answer;
			}
		} else if (strcmp(cmd, "CLO") == 0) {
			answer = recv_clo(args);
		}

		fprintf(socket_rw, answer);
		fflush(socket_rw);
		free(args);
	}

	fclose(socket_rw);
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
		perror("ERROR on setting socket option");
		exit(1);
	}

	if (bind
	    (server_socket_fd, (sockaddr *) & server_addr,
	     sizeof(server_addr)) < 0)
		perror("ERROR on binding");

	if (listen(server_socket_fd, server_backlog_size) < 0)
		perror("ERROR on listening");
}

void st_init()
{
	// Ouvre un socket pour le serveur
	st_open_socket();

	// Gestion d'interruption
	signal(SIGINT, &sigint_handler);

	// Structure du banquier
	banker.avail = NULL;
	banker.clients = NULL;
	pthread_mutex_init(&banker.mutex, NULL);
}

void free_clients(client * head)
{
	client *tmp;
	while (head != NULL) {
		tmp = head;
		head = head->next;
		free(tmp->max);
		free(tmp->alloc);
		free(tmp->need);
		free(tmp);
	}
}

void st_uninit()
{
	// Structure du banquier
	free(banker.avail);
	free_clients(banker.clients);
	pthread_mutex_destroy(&banker.mutex);

	// Mutexes
	pthread_mutex_destroy(&mutex_nb_registered_clients);
	pthread_mutex_destroy(&mutex_count_accepted);
	pthread_mutex_destroy(&mutex_count_wait);
	pthread_mutex_destroy(&mutex_count_invalid);
	pthread_mutex_destroy(&mutex_count_dispatched);
	pthread_mutex_destroy(&mutex_request_processed);
	pthread_mutex_destroy(&mutex_clients_ended);

	// Socket
	if (server_socket_fd > -1)
		close(server_socket_fd);
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
