/* This `define` tells unistd to define usleep and random. */
#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "client_thread.h"

// Socket library
//#include <netdb.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

sockaddr_in server_addr = {.sin_family = AF_INET };

int num_request_per_client = -1;
int num_resources = -1;
int *provisioned_resources = NULL;

// Variable d'initialisation des threads clients.
unsigned int count = 0;

// Variable du journal.
// Nombre de requête acceptée (ACK reçus en réponse à REQ)
unsigned int count_accepted = 0;

// Nombre de requête en attente (WAIT reçus en réponse à REQ)
unsigned int count_on_wait = 0;

// Nombre de requête refusée (ERR reçus en réponse à REQ)
unsigned int count_invalid = 0;

// Nombre de client qui se sont terminés correctement (ACK reçu en réponse à END)
unsigned int count_dispatched = 0;

// Nombre total de requêtes envoyées.
unsigned int request_sent = 0;

void send_beg_pro()
{
	int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_fd < 0)
		perror("ERROR opening socket");
	if (connect(socket_fd, (sockaddr *) & server_addr, sizeof(server_addr))
	    < 0)
		perror("ERROR on connecting");

	char buf[256];		// XXX
	int len = snprintf(buf, sizeof(buf), "BEG %d\nPRO", num_resources);
	for (int i = 0; i < num_resources; i++)
		len += snprintf(buf + len, sizeof(buf), " %d",
				provisioned_resources[i]);
	strcat(buf, "\n");

	send(socket_fd, buf, strlen(buf), 0);
	// TODO Get the ACKs
}

void send_end()
{
	int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_fd < 0)
		perror("ERROR opening socket");
	if (connect(socket_fd, (sockaddr *) & server_addr, sizeof(server_addr))
	    < 0)
		perror("ERROR on connecting");

	send(socket_fd, "END\n", 4, 0);
	// TODO Get the ACK
}

void send_ini(int client_id, int socket_fd)
{
	char buf[256];		// XXX
	int len = snprintf(buf, sizeof(buf), "INI %d", client_id);
	for (int i = 0; i < num_resources; i++)
		len += snprintf(buf + len, sizeof(buf), " %d",
				(int)(drand48() *
				      (provisioned_resources[i] + 1)));
	strcat(buf, "\n");

	send(socket_fd, buf, strlen(buf), 0);
	// TODO Get the ACK
}

// Vous devez modifier cette fonction pour faire l'envoie des requêtes
// Les ressources demandées par la requête doivent être choisies aléatoirement
// (sans dépasser le maximum pour le client). Elles peuvent être positives
// ou négatives.
// Assurez-vous que la dernière requête d'un client libère toute les ressources
// qu'il a jusqu'alors accumulées.
void send_req(int client_id, int socket_fd, int request_id)
{
	// TP2 TODO

	fprintf(stdout, "Client %d is sending its %d request\n",
		client_id, request_id);

	// TP2 TODO:END
}

void *ct_code(void *param)
{
	int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_fd < 0)
		perror("ERROR opening socket");
	if (connect(socket_fd, (sockaddr *) & server_addr, sizeof(server_addr))
	    < 0)
		perror("ERROR on connecting");

	client_thread *ct = (client_thread *) param;
	srand48(time(NULL));

	send_ini(ct->id, socket_fd);
	for (int req_id = 0; req_id < num_request_per_client; req_id++) {
		// TP2 TODO
		// Vous devez ici coder, conjointement avec le corps de send request,
		// le protocole d'envoi de requête.

		send_req(ct->id, socket_fd, req_id);

		// TP2 TODO:END

		/* Attendre un petit peu (0s-0.1s) pour simuler le calcul. */
		usleep(random() % (100 * 1000));
	}

	return NULL;
}

//
// Vous devez changer le contenu de cette fonction afin de régler le
// problème de synchronisation de la terminaison.
// Le client doit attendre que le serveur termine le traitement de chacune
// de ses requêtes avant de terminer l'exécution.
//
void ct_wait_server()
{
	// TP2 TODO: IMPORTANT code non valide.

	sleep(4);

	// TP2 TODO:END
}

void ct_init(client_thread * ct)
{
	ct->id = count++;
}

void ct_create_and_start(client_thread * ct)
{
	pthread_attr_init(&(ct->pt_attr));
	pthread_create(&(ct->pt_tid), &(ct->pt_attr), &ct_code, ct);
	pthread_detach(ct->pt_tid);
}

//
// Affiche les données recueillies lors de l'exécution du
// serveur.
// La branche else ne doit PAS être modifiée.
//
void st_print_results(FILE * fd, bool verbose)
{
	if (fd == NULL)
		fd = stdout;
	if (verbose) {
		fprintf(fd, "\n---- Résultat du client ----\n");
		fprintf(fd, "Requêtes acceptées: %d\n", count_accepted);
		fprintf(fd, "Requêtes : %d\n", count_on_wait);
		fprintf(fd, "Requêtes invalides: %d\n", count_invalid);
		fprintf(fd, "Clients : %d\n", count_dispatched);
		fprintf(fd, "Requêtes envoyées: %d\n", request_sent);
	} else {
		fprintf(fd, "%d %d %d %d %d\n", count_accepted, count_on_wait,
			count_invalid, count_dispatched, request_sent);
	}
}
