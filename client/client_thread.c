#define _XOPEN_SOURCE 500

#include <string.h>
#include <time.h>
#include <unistd.h>

#include "client_thread.h"

sockaddr_in server_addr = {
	.sin_family = AF_INET
};

int num_clients;
int num_request_per_client;
int num_resources;
int *provis_resources;
int **cur_resources_per_client;
int **max_resources_per_client;

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

// Returns number of WAIT commands received on success or -1 on error
int send_cmd(int socket, char *send_buf, char *recv_buf)
{
	int wait_num = 0, wait_dur = 0;

	do {
		// Send
		sleep(wait_dur);
		if (sendall(socket, send_buf, strlen(send_buf)) < 0)
			return -1;
		// Receive
		memset(recv_buf, '\0', strlen(recv_buf));	// XXX
		if (recvline(socket, recv_buf, sizeof(recv_buf)) < 0)
			return -1;
	} while (sscanf(recv_buf, "WAIT %d\n", &wait_dur) == 1 && ++wait_num);

	return wait_num;
}

int send_beg()
{
	int socket_fd = create_connected_socket();
	if (socket_fd < 0)
		return 0;

	char send_buf[128] = "", recv_buf[128] = "";	// XXX
	snprintf(send_buf, sizeof(send_buf), "BEG %d\n", num_resources);

	if (send_cmd(socket_fd, send_buf, recv_buf) < 0) {
		perror("BEG");
		close(socket_fd);
		return 0;
	}

	if (strncmp(recv_buf, "ACK", 3) == 0) {
		close(socket_fd);
		return 1;
	}

	fprintf(stderr, "BEG: %s\n", recv_buf);
	close(socket_fd);
	return 0;
}

int send_pro()
{
	int socket_fd = create_connected_socket();
	if (socket_fd < 0)
		return 0;

	char send_buf[128] = "PRO", recv_buf[128] = "";	// XXX
	int len = 3;
	for (int i = 0; i < num_resources; i++)
		len += snprintf(send_buf + len, sizeof(send_buf) - len,
				" %d", provis_resources[i]);
	strncat(send_buf, "\n", 1);

	if (send_cmd(socket_fd, send_buf, recv_buf) < 0) {
		perror("PRO");
		close(socket_fd);
		return 0;
	}

	if (strncmp(recv_buf, "ACK", 3) == 0) {
		close(socket_fd);
		return 1;
	}

	fprintf(stderr, "PRO: %s\n", recv_buf);
	close(socket_fd);
	return 0;
}

int send_end()
{
	int socket_fd = create_connected_socket();
	if (socket_fd < 0)
		return 0;

	char send_buf[128] = "END\n", recv_buf[128] = "";	// XXX

	if (send_cmd(socket_fd, send_buf, recv_buf) < 0) {
		perror("END");
		close(socket_fd);
		return 0;
	}

	if (strncmp(recv_buf, "ACK", 3) == 0) {
		close(socket_fd);
		return 1;
	}

	fprintf(stderr, "END: %s\n", recv_buf);
	close(socket_fd);
	return 0;
}

int send_ini(int client_id, int socket_fd)
{
	char send_buf[128] = "", recv_buf[128] = "";	// XXX
	int len = snprintf(send_buf, sizeof(send_buf), "INI %d", client_id);
	int ini_resources[num_resources];
	for (int i = 0; i < num_resources; i++) {
		// Source: c-faq.com/lib/randrange
		ini_resources[i] =
		    rand() / (RAND_MAX / (provis_resources[i] + 1) + 1);
		len += snprintf(send_buf + len, sizeof(send_buf) - len,
				" %d", ini_resources[i]);
	}
	strncat(send_buf, "\n", 1);

	if (send_cmd(socket_fd, send_buf, recv_buf) < 0) {
		perror("INI");
		return 0;
	}

	if (strncmp(recv_buf, "ACK", 3) == 0) {
		for (int i = 0; i < num_resources; i++)
			max_resources_per_client[client_id][i] =
			    ini_resources[i];
		return 1;
	}

	fprintf(stderr, "INI: %s\n", recv_buf);
	return 0;
}

int send_req(int client_id, int socket_fd, int request_id, int free)
{
	char send_buf[128] = "", recv_buf[128] = "";	// XXX
	int len = snprintf(send_buf, sizeof(send_buf), "REQ %d", client_id);
	int req_resources[num_resources];
	for (int i = 0; i < num_resources; i++) {
		int cur = cur_resources_per_client[client_id][i];
		int max = max_resources_per_client[client_id][i];

		req_resources[i] = -cur;
		if (!free)
			req_resources[i] +=
			    rand() / (RAND_MAX / (max + cur + 1) + 1);

		len += snprintf(send_buf + len, sizeof(send_buf) - len,
				" %d", req_resources[i]);
	}
	strncat(send_buf, "\n", 1);

	int waits;
	if ((waits = send_cmd(socket_fd, send_buf, recv_buf)) < 0) {
		perror("REQ");
		return 0;
	} else if (waits > 0) {
		pthread_mutex_lock(&mutex_count_on_wait);
		count_on_wait += waits;
		pthread_mutex_unlock(&mutex_count_on_wait);
	}

	if (strncmp(recv_buf, "ACK", 3) == 0) {
		pthread_mutex_lock(&mutex_count_accepted);
		count_accepted++;
		pthread_mutex_unlock(&mutex_count_accepted);
		for (int i = 0; i < num_resources; i++)
			cur_resources_per_client[client_id][i] +=
			    req_resources[i];
		return 1;
	}

	if (strncmp(recv_buf, "ERR", 3) == 0) {
		pthread_mutex_lock(&mutex_count_invalid);
		count_invalid++;
		pthread_mutex_unlock(&mutex_count_invalid);
	}

	fprintf(stderr, "REQ: %s\n", recv_buf);
	return 0;
}

int send_clo(int client_id, int socket_fd)
{
	char send_buf[128] = "", recv_buf[128] = "";	// XXX
	snprintf(send_buf, sizeof(send_buf), "CLO %d\n", client_id);

	if (send_cmd(socket_fd, send_buf, recv_buf) < 0) {
		perror("CLO");
		return 0;
	}

	if (strncmp(recv_buf, "ACK", 3) == 0)
		return 1;

	fprintf(stderr, "CLO: %s\n", recv_buf);
	return 0;
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
		printf("Client %d is sending its %d request\n", ct->id, req_id);
		send_req(ct->id, socket_fd, req_id,
			 num_request_per_client - req_id == 1);

		pthread_mutex_lock(&mutex_request_sent);
		request_sent++;	// XXX
		pthread_mutex_unlock(&mutex_request_sent);

		// Attendre un petit peu (<0.1ms) pour simuler le calcul
		struct timespec delay = { 0, rand() % (100 * 1000) };
		nanosleep(&delay, NULL);
	}

	// CLO
	if (!send_clo(ct->id, socket_fd))
		goto undispatched;

	pthread_mutex_lock(&mutex_count_dispatched);
	count_dispatched++;
	pthread_mutex_unlock(&mutex_count_dispatched);
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

	for (int i = 0; i < num_clients; i++) {
		free(cur_resources_per_client[i]);
		free(max_resources_per_client[i]);
	}
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
	max_resources_per_client[id] = calloc(num_resources, sizeof(int));
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
