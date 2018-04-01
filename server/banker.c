#include <string.h>

#include "server_thread.h"

bool res_more_than(int *arr1, int *arr2)
{
	for (int i = 0; i < num_resources; i++)
		if (arr1[i] > arr2[i])
			return true;
	return false;
}

void allocate_req(int *req, int *avail, int *alloc, int *need)
{
	for (int i = 0; i < num_resources; i++) {
		avail[i] -= req[i];
		alloc[i] += req[i];
		need[i] -= req[i];
	}
}

void deallocate_req(int *req, int *avail, int *alloc, int *need)
{
	for (int i = 0; i < num_resources; i++) {
		avail[i] += req[i];
		alloc[i] -= req[i];
		need[i] += req[i];
	}
}

// Ask Gringotts Wizarding Bank
// Source: geeksforgeeks.org/program-bankers-algorithm-set-1-safety-algorithm
bool is_safe(int num_clients, int *avail, client * clients)
{
	bool finish[num_clients];
	memset(finish, false, num_clients * sizeof(bool));

	int work[num_resources];
	memcpy(work, avail, num_resources * sizeof(int));

	int counter = num_clients;
	while (counter) {
		bool safe = false;

		client *c = clients;
		for (int i = 0; i < num_clients; i++) {
			if (!finish[i]) {
				int j;
				for (j = 0; j < num_resources; j++)
					if (c->need[j] > work[j])
						break;

				if (j == num_resources) {
					for (int k = 0; k < num_resources; k++)
						work[k] += c->alloc[k];

					counter--;
					finish[i] = true;
					safe = true;
				}
			}

			c = c->next;
		}

		if (!safe)
			return false;
	}

	return true;
}
