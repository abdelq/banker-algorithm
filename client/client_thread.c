#include <string.h>
#include <unistd.h>

#include "client_thread.h"

//enum server_ans { ACK, WAIT, ERR };
sockaddr_in server_addr = {
	.sin_family = AF_INET
};

int num_clients;
int num_request_per_client;
int num_resources;
int *provis_resources;
int **cur_resources_per_client;

/* Variables du journal */
// Nombre de requêtes acceptées (ACK reçus en réponse à REQ)
unsigned int count_accepted = 0;
pthread_mutex_t mutex_count_accepted = PTHREAD_MUTEX_INITIALIZER;

// Nombre de requêtes en attente (WAIT reçus en réponse à REQ)
unsigned int count_on_wait = 0;
pthread_mutex_t mutex_count_on_wait = PTHREAD_MUTEX_INITIALIZER;

// Nombre de requêtes refusées (ERR reçus en réponse à REQ)
unsigned int count_invalid = 0;
pthread_mutex_t mutex_count_invalid = PTHREAD_MUTEX_INITIALIZER;

// Nombre de clients terminés correctement (ACK reçu en réponse à END)
unsigned int count_dispatched = 0;
pthread_mutex_t mutex_count_dispatched = PTHREAD_MUTEX_INITIALIZER;

// Nombre de clients qui se sont terminés incorrectement
unsigned int count_undispatched = 0;
pthread_mutex_t mutex_count_undispatched = PTHREAD_MUTEX_INITIALIZER;

// Nombre total de requêtes envoyées
unsigned int request_sent = 0;
pthread_mutex_t mutex_request_sent = PTHREAD_MUTEX_INITIALIZER;

// TODO Look for a nicer solution
int create_connected_socket()
{
	int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_fd < 0) {
		perror("ERROR creating socket");
		return -1;
	}
	if (connect
	    (socket_fd, (sockaddr *) & server_addr, sizeof(server_addr)) < 0) {
		perror("ERROR on connecting");
		close(socket_fd);
		return -1;
	}
	return socket_fd;
}

// TODO Use write instead
// Sends to socket until all the message of a certain length in buffer is sent
int sendall(int socket, char *buffer, size_t length)
{
	char *buf = buffer;
	ssize_t sent;

	while (length > 0) {
		if ((sent = send(socket, buf, length, 0)) < 0)
			return -1;

		buf += sent;
		length -= sent;
	}

	return 0;
}

// TODO Use read instead
// Receives from socket until a newline or EOF is encountered
// Source: eg.bucknell.edu/~csci335/2006-fall/code/cServer/readln.cc
int recvline(int socket, char *buffer, size_t length)
{
	char *buf = buffer, c;
	ssize_t received;

	while (buf - buffer < length) {
		if ((received = recv(socket, buf, 1, 0)) < 0)
			return -1;
		if (*buf++ == '\n')
			return buf - buffer;
	}

	// Flushing to newline or EOF
	while (recv(socket, &c, 1, 0) > 0 && c != '\n') ;	// XXX

	return buf - buffer;
}

// TODO Parse the string with sscanf for WAIT (time) and ERR (message)
int send_beg()
{
	int socket_fd = create_connected_socket();
	if (socket_fd < 0)
		return 0;

	char send_buf[64], recv_buf[64];
	int len = snprintf(send_buf, sizeof(send_buf),
			   "BEG %d\n", num_resources);

	do {
		if (sendall(socket_fd, send_buf, len) < 0) {
			perror("BEG: ERROR on sending");
			goto close;
		}
		if (recvline(socket_fd, recv_buf, sizeof(recv_buf)) < 0) {
			perror("BEG: ERROR on receiving");
			goto close;
		}
	} while (strncmp(recv_buf, "WAIT", 4) == 0);

	if (strncmp(recv_buf, "ACK", 3) == 0) {
		close(socket_fd);
		return 1;
	}

 close:
	close(socket_fd);
	return 0;
}

int send_pro()
{
	int socket_fd = create_connected_socket();
	if (socket_fd < 0)
		return 0;

	char buf[256];		// XXX
	int len = snprintf(buf, sizeof(buf), "PRO");	// XXX
	for (int i = 0; i < num_resources; i++) {
		len +=
		    snprintf(buf + len, sizeof(buf), " %d",
			     provis_resources[i]);
	}
	strncat(buf, "\n", sizeof(buf));

	if (sendall(socket_fd, buf, len) < 0) {
		perror("ERROR on sending PRO");
		close(socket_fd);
		return 0;
	}
// TODO Manage reception

	close(socket_fd);
	return 1;
}

int send_end()
{
	int socket_fd = create_connected_socket();
	if (socket_fd < 0)
		return 0;

	if (sendall(socket_fd, "END\n", 4) < 0) {	// XXX
		perror("ERROR on sending END");
		close(socket_fd);
		return 0;
	}
// TODO Manage reception

	close(socket_fd);
	return 1;
}

int send_ini(int client_id, int socket_fd)
{
	char buf[256];		// XXX
	int len = snprintf(buf, sizeof(buf), "INI %d", client_id);
	for (int i = 0; i < num_resources; i++) {
		// Source: c-faq.com/lib/randrange
		int max = rand() / (RAND_MAX / (provis_resources[i] + 1) + 1);
		len += snprintf(buf + len, sizeof(buf), " %d", max);
	}
	strncat(buf, "\n", sizeof(buf));

	if (sendall(socket_fd, buf, len) < 0) {
		perror("ERROR on sending INI");
		return 0;
	}
// TODO Manage reception

	return 1;
}

// TODO Dernière requête doit libérer toutes les ressources accumulées
int send_req(int client_id, int socket_fd, int request_id)
{
	char buf[256];		// XXX
	int len = snprintf(buf, sizeof(buf), "REQ %d", client_id);
	for (int i = 0; i < num_resources; i++) {
		int req = 42;	// TODO [-courant, max]
		len += snprintf(buf + len, sizeof(buf), " %d", req);
	}
	strncat(buf, "\n", sizeof(buf));

	if (sendall(socket_fd, buf, len) < 0) {
		perror("ERROR on sending REQ");
		return 0;
	}
// TODO Manage reception

/* XXX */
	fprintf(stdout, "Client %d is sending its %d request\n",
		client_id, request_id);

	pthread_mutex_lock(&mutex_request_sent);
	request_sent++;
	pthread_mutex_unlock(&mutex_request_sent);
/* XXX */

	return 1;
}

// FIXME Review (ACK should increment dispatched)
int send_clo(int client_id, int socket_fd)
{
	char send_buf[17], recv_buf[256];	// XXX

	int len = snprintf(send_buf, sizeof(send_buf), "CLO %d\n", client_id);

	do {
		if (sendall(socket_fd, send_buf, len) < 0) {
			perror("ERROR on sending CLO");
			return 0;
		}
		// XXX Proper receiving + bzero
		/*if (recvall(socket_fd, recv_buf, len, 0) < 0) {
		   perror("ERROR on receiving CLO");
		   return 0;
		   } */
	} while (strncmp(recv_buf, "WAIT", 4) == 0);	// XXX

	if (strncmp(recv_buf, "ERR", 3) == 0)
		return 0;

	return 1;
}

void *ct_code(void *param)
{
	int socket_fd = create_connected_socket();
	if (socket_fd < 0)
		goto undispatched;
	client_thread *ct = (client_thread *) param;

	// INI
	if (!send_ini(ct->id, socket_fd))
		goto undispatched;

	// REQ
	for (int req_id = 0; req_id < num_request_per_client; req_id++) {
		if (send_req(ct->id, socket_fd, req_id)) {
			pthread_mutex_lock(&mutex_count_accepted);
			count_accepted++;
			pthread_mutex_unlock(&mutex_count_accepted);
		} else {	// XXX On reception of ERR only?
			pthread_mutex_lock(&mutex_count_invalid);
			count_invalid++;
			pthread_mutex_unlock(&mutex_count_invalid);
		}

		// Attendre un petit peu (<0.1ms) pour simuler le calcul
		struct timespec delay = { 0, rand() % (100 * 1000) };
		nanosleep(&delay, NULL);
	}

	// CLO
	if (!send_clo(ct->id, socket_fd))
		goto undispatched;

	close(socket_fd);
	return NULL;
 undispatched:
	pthread_mutex_lock(&mutex_count_undispatched);
	count_undispatched++;
	pthread_mutex_unlock(&mutex_count_undispatched);
	close(socket_fd);
	return NULL;
}

void ct_wait_server()
{
	// XXX Possible race condition
	while (count_dispatched + count_undispatched < num_clients)
		sleep(1);

	pthread_mutex_destroy(&mutex_count_accepted);
	pthread_mutex_destroy(&mutex_count_on_wait);
	pthread_mutex_destroy(&mutex_count_invalid);
	pthread_mutex_destroy(&mutex_count_dispatched);
	pthread_mutex_destroy(&mutex_count_undispatched);
	pthread_mutex_destroy(&mutex_request_sent);

	for (int i = 0; i < num_clients; i++)
		free(cur_resources_per_client[i]);
}

void ct_create_and_start(client_thread * ct)
{
	pthread_attr_init(&(ct->pt_attr));
	pthread_attr_setdetachstate(&(ct->pt_attr), PTHREAD_CREATE_DETACHED);
	pthread_create(&(ct->pt_tid), &(ct->pt_attr), &ct_code, ct);
	pthread_attr_destroy(&(ct->pt_attr));
}

void ct_init(client_thread * ct, int id)
{
	ct->id = id;
	cur_resources_per_client[id] = calloc(num_resources, sizeof(int));
}

void st_print_results(FILE * fd, bool verbose)
{
	if (fd == NULL)
		fd = stdout;
	if (verbose) {
		fprintf(fd, "\n---- Résultat du client ----\n");
		fprintf(fd, "Requêtes acceptées : %d\n", count_accepted);
		fprintf(fd, "Requêtes en attente : %d\n", count_on_wait);
		fprintf(fd, "Requêtes invalides : %d\n", count_invalid);
		fprintf(fd, "Clients : %d\n", count_dispatched);
		fprintf(fd, "Requêtes envoyées : %d\n", request_sent);
	} else {
		fprintf(fd, "%d %d %d %d %d\n", count_accepted, count_on_wait,
			count_invalid, count_dispatched, request_sent);
	}
}
