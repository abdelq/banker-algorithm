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
	int *tot_avail;
	int **max;
	int **allocs;
	int nprocs, nres;
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

// Acquire a mutex
void acquire(pthread_mutex_t mutex)
{
	if (pthread_mutex_lock(&mutex) != 0) {
		perror("mutex_lock");
		abort();
	}
}

// check if a 1d array is composed of all zeros
int is_all_zeros(int *a)
{
	for (int i = 0; i < (sizeof(a) / sizeof(int)); i++) {
		if (a[i] != 0) {
			return 0;
		}
	}
	return 1;
}

// check if process p may run
int may_run(int p, int *running)
{
	for (int i = 0; i < banker.nres; i++) {
		int alloc_res_i = 0;
		for (int j = 0; j < banker.nprocs; j++) {
			// if running[j] == 0, process j has already terminated
			// in the simulation
			alloc_res_i += banker.allocs[j][i] * running[j];
		}
		if (banker.tot_avail[i] -
		    (banker.max[p][i] - banker.allocs[p][i]) - alloc_res_i <
		    0) {
			return 0;
		}
	}
	return 1;
}

// Release resource given process index in
// banker allocs/max tables
// execute on process termination
void release_by_p_index(int p)
{
	acquire(banker.mutex);
	banker.nprocs--;
	for (int i = p; i < banker.nprocs; i++) {
		banker.max[i] = banker.max[i + 1];
		banker.allocs[i] = banker.allocs[i + 1];
	}
	int **tmp_max = realloc(banker.max, banker.nprocs * sizeof(int *));
	int **tmp_allocs =
	    realloc(banker.allocs, banker.nprocs * sizeof(int *));
	if ((tmp_max == NULL || tmp_allocs == NULL) && banker.nprocs > 1) {
		perror("Error allocating memory");
		abort();
	}
	banker.max = tmp_max;
	banker.allocs = tmp_allocs;
}

// Print banker state
void show_tables()
{
	printf("\nAvailable:\n");
	for (int i = 0; i < banker.nres; i++) {
		printf("%d ", banker.tot_avail[i]);
	}
	printf("\n\n");

	printf("Allocated:\n");
	for (int i = 0; i < banker.nprocs; i++) {
		for (int j = 0; j < banker.nres; j++) {
			printf("%d ", banker.allocs[i][j]);
		}
		printf("\n");
	}
	printf("\n");

	printf("Maximums:\n");
	for (int i = 0; i < banker.nprocs; i++) {
		for (int j = 0; j < banker.nres; j++) {
			printf("%d ", banker.max[i][j]);
		}
		printf("\n");
	}
	printf("\n");
}

// WARNING: BANKER.MUTEX SHOULD BE ACQUIRED BEFORE DOING THIS
// AKA ONLY ask_gringotts SHOULD CALL THIS
// Append process to banker's queue
void append_proc(int *req)
{
	banker.nprocs++;
	int **tmp_max = realloc(banker.max, banker.nprocs * sizeof(int *));
	int **tmp_allocs =
	    realloc(banker.allocs, banker.nprocs * sizeof(int *));
	if ((tmp_max == NULL || tmp_allocs == NULL) && banker.nprocs > 1) {
		perror("Error allocating memory");
		abort();
	}
	banker.max = tmp_max;
	banker.allocs = tmp_allocs;
	// Add maxes
	banker.max[banker.nprocs - 1] = req;
	// Process is being added so no allocs = [0,0,...,0]
	int *zeros = malloc(sizeof(int) * banker.nres);
	for (int i = 0; i < banker.nres; i++)
		zeros[i] = 0;
	banker.allocs[banker.nprocs - 1] = zeros;
}

// Actual check
// return 1 if request is accepted
// return 0 if request cant be granted
int ask_gringotts(int *req)
{
	// acquire lock
	acquire(banker.mutex);

	// for sequencing tests
	int running[banker.nprocs];
	for (int i = 0; i < banker.nprocs; i++)
		running[i] = 1;

	append_proc(req);
	//show_tables();

	// loop until all process were able to run
	// or no process have been able to run for a full loop
	while (!is_all_zeros(running)) {
		int alloc_this_round = 0;
		// for all processes
		for (int i = 0; i < banker.nprocs; i++) {
			// check if process has not finished
			if (running[i]) {
				if (may_run(i, running)) {
					//process may run!
					printf("\nProcess %d may run\n", i);
					alloc_this_round++;
					running[i] = 0;
					// for debugging purpose
					//show_tables();
				}
			}
		}
		if (alloc_this_round == 0) {
			// remove last process (aka the process to be added
			// that couldn't be added to the banker queue
			release_by_p_index(banker.nprocs);
			pthread_mutex_unlock(&banker.mutex);
			return 0;
		}
	}
	// not removing last process since process was accepted
	pthread_mutex_unlock(&banker.mutex);
	return 1;
}
