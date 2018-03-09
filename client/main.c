#include <stdlib.h>
#include <netinet/in.h>

#include "client_thread.h"

int main(int argc, char *argv[])
{
	if (argc < 5) {
		fprintf(stderr,
			"Usage: %s <port-nb> <nb-clients> <nb-requests> <resources>...\n",
			argv[0]);
		exit(1);
	}

	server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	server_addr.sin_port = htons(atoi(argv[1]));

	int num_clients = atoi(argv[2]);
	num_request_per_client = atoi(argv[3]);
	num_resources = argc - 4;

	cur_resources_per_client = malloc(num_clients * sizeof(int *));
	provisioned_resources = malloc(num_resources * sizeof(int));
	for (int i = 0; i < num_resources; i++)
		provisioned_resources[i] = atoi(argv[i + 4]);

	if (send_beg_pro()) {
		srand48(time(NULL));

		client_thread client_threads[num_clients];
		for (int i = 0; i < num_clients; i++)
			ct_init(&(client_threads[i]));
		for (int i = 0; i < num_clients; i++)
			ct_create_and_start(&(client_threads[i]));
		ct_wait_server();

		send_end();
	}

	free(cur_resources_per_client);
	free(provisioned_resources);

	// Affiche le journal
	st_print_results(stdout, true);
	FILE *fp = fopen("client.log", "w");
	if (fp == NULL) {
		fprintf(stderr, "Could not print log");
		return EXIT_FAILURE;
	}
	st_print_results(fp, false);
	fclose(fp);

	return EXIT_SUCCESS;
}
