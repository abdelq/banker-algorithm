#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "client_thread.h"

sockaddr_in server_addr = {
	.sin_family = AF_INET
};

int num_request_per_client;
int num_resources;
int *provis_resources;
int **cur_resources_per_client;

// Variable d'initialisation des threads clients
unsigned int count = 0;

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

// Nombre de clients qui se sont terminés correctement (ACK reçu en réponse à END)
unsigned int count_dispatched = 0;
pthread_mutex_t mutex_count_dispatched = PTHREAD_MUTEX_INITIALIZER;

// Nombre de clients qui se sont terminés incorrectement
unsigned int count_undispatched = 0;
pthread_mutex_t mutex_count_undispatched = PTHREAD_MUTEX_INITIALIZER;

// Nombre total de requêtes envoyées
unsigned int request_sent = 0;
pthread_mutex_t mutex_request_sent = PTHREAD_MUTEX_INITIALIZER;

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

int sendall(int socket, const void *buffer, size_t length, int flags)
{
	const char *buf = (char *)buffer;
	ssize_t sent;

	while (length > 0) {
		if ((sent = send(socket, buf, length, flags)) < 0)
			return -1;

		buf += sent;
		length -= sent;
	}

	return 0;
}

// FIXME Use getline instead
int recvall(int socket, void *buffer, size_t length, int flags)
{
	char *buf = (char *)buffer;
	ssize_t received;

	while (length > 0) {
		if ((received = recv(socket, buf, length, flags)) < 0)
			return -1;

		buf += received;
		length -= received;
	}

	return 0;
}

int send_beg()
{
	int socket_fd = create_connected_socket();
	if (socket_fd < 0)
		return 0;

	char buf[128];		// XXX
	int len = snprintf(buf, sizeof(buf), "BEG %d\n", num_resources);

	if (sendall(socket_fd, buf, len, 0) < 0) {
		perror("ERROR on sending BEG");
		close(socket_fd);
		return 0;
	}
	// TODO Manage reception

	close(socket_fd);
	return 1;
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

	if (sendall(socket_fd, buf, len, 0) < 0) {
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

	if (sendall(socket_fd, "END\n", 4, 0) < 0) {	// XXX
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

	if (sendall(socket_fd, buf, len, 0) < 0) {
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

	if (sendall(socket_fd, buf, len, 0) < 0) {
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

int send_clo(int client_id, int socket_fd)
{
	char buf[128];		// XXX
	int len = snprintf(buf, sizeof(buf), "CLO %d\n", client_id);

	if (sendall(socket_fd, buf, len, 0) < 0) {
		perror("ERROR on sending CLO");
		return 0;
	}
	// TODO Manage reception

	return 1;
}

void *ct_code(void *param)
{
	int socket_fd = create_connected_socket();
	if (socket_fd < 0)
		goto undispatched;

	client_thread *ct = (client_thread *) param;
	if (!send_ini(ct->id, socket_fd))
		goto undispatched;

	for (int req_id = 0; req_id < num_request_per_client; req_id++) {
		if (send_req(ct->id, socket_fd, req_id)) {
			pthread_mutex_lock(&mutex_count_accepted);
			count_accepted++;
			pthread_mutex_unlock(&mutex_count_accepted);
		} else {
			pthread_mutex_lock(&mutex_count_invalid);
			count_invalid++;
			pthread_mutex_unlock(&mutex_count_invalid);
		}

		// Attendre un petit peu pour simuler le calcul
		usleep(rand() % (100 * 1000));	// XXX Not uniform (usage of modulo)
	}

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
	while (count_dispatched + count_undispatched < count)
		sleep(1);

	pthread_mutex_destroy(&mutex_count_accepted);
	pthread_mutex_destroy(&mutex_count_on_wait);
	pthread_mutex_destroy(&mutex_count_invalid);
	pthread_mutex_destroy(&mutex_count_dispatched);
	pthread_mutex_destroy(&mutex_count_undispatched);
	pthread_mutex_destroy(&mutex_request_sent);

	for (int i = 0; i < count; i++)
		free(cur_resources_per_client[i]);
}

void ct_create_and_start(client_thread * ct)
{
	pthread_attr_init(&(ct->pt_attr));
	pthread_attr_setdetachstate(&(ct->pt_attr), PTHREAD_CREATE_DETACHED);
	pthread_create(&(ct->pt_tid), &(ct->pt_attr), &ct_code, ct);
	pthread_attr_destroy(&(ct->pt_attr));
}

void ct_init(client_thread * ct)
{
	ct->id = count++;
	cur_resources_per_client[ct->id] = calloc(num_resources, sizeof(int));
}

void st_print_results(FILE * fd, bool verbose)
{
	if (fd == NULL)
		fd = stdout;
	if (verbose) {
		fprintf(fd, "\n---- Résultat du client ----\n");
		fprintf(fd, "Requêtes acceptées : %d\n", count_accepted);
		fprintf(fd, "Requêtes : %d\n", count_on_wait);
		fprintf(fd, "Requêtes invalides : %d\n", count_invalid);
		fprintf(fd, "Clients : %d\n", count_dispatched);
		fprintf(fd, "Requêtes envoyées : %d\n", request_sent);
	} else {
		fprintf(fd, "%d %d %d %d %d\n", count_accepted, count_on_wait,
			count_invalid, count_dispatched, request_sent);
	}
}
