#include "server_thread.h"

bool accepting_connections = true;

int main(int argc, char *argv[])
{
	if (argc < 3) {
		fprintf(stderr, "Usage: %s PORT THREADS\n", argv[0]);
		exit(1);
	}

	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(atoi(argv[1]));

	num_servers = atoi(argv[2]);

	st_open_socket();
	st_init();

	// Lance les fils d'exécution
	server_thread st[num_servers];
	for (int i = 0; i < num_servers; i++) {
		st[i].id = i;
		pthread_attr_init(&(st[i].pt_attr));
		pthread_create(&(st[i].pt_tid), &(st[i].pt_attr),
			       &st_code, &(st[i]));
		pthread_attr_destroy(&(st[i].pt_attr));
	}
	for (int i = 0; i < num_servers; i++)
		pthread_join(st[i].pt_tid, NULL);

	// Affiche le journal
	st_print_results(stdout, true);
	FILE *fp = fopen("server.log", "w");
	if (fp == NULL) {
		fprintf(stderr, "Could not print log");
		return EXIT_FAILURE;
	}
	st_print_results(fp, false);
	fclose(fp);

	return EXIT_SUCCESS;
}
