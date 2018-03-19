#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

struct {
	int *tot_avail;
	int **max;
	int **allocs;
	int nprocs, nres;
	pthread_mutex_t mutex;
} banker;

void initialize()
{
	printf("Enter number of ressources:\n");
	scanf("%d", &banker.nres);

	printf("Enter number of processes:\n");
	scanf("%d", &banker.nprocs);

	banker.tot_avail = malloc(sizeof(int) * banker.nres);
	for (int i = 0; i < banker.nres; i++) {
		banker.tot_avail[i] = 0;
	}

	if ((banker.max = malloc(sizeof(int *) * banker.nprocs)) == NULL) {
		perror("Error allocating memory");
		abort();
	}
	if ((banker.allocs = malloc(sizeof(int *) * banker.nprocs)) == NULL) {
		perror("Error allocating memory");
		abort();
	}
	for (int i = 0; i < banker.nprocs; i++) {
		banker.max[i] = malloc(sizeof(int) * banker.nres);
		banker.allocs[i] = malloc(sizeof(int) * banker.nres);
		for (int j = 0; j < banker.nres; j++) {
			banker.max[i][j] = 0;
			banker.allocs[i][j] = 0;
		}
	}
	pthread_mutex_init(&banker.mutex, NULL);
}

void get_allocated_res()
{
	printf("Enter allocations:\n");
	for (int i = 0; i < banker.nprocs; i++) {
		for (int j = 0; j < banker.nres; j++) {
			printf("proc %d, res %d: ", i, j);
			scanf("%d", &banker.allocs[i][j]);
		}
	}
}

void get_maxs()
{
	printf("Enter maximums:\n");
	for (int i = 0; i < banker.nprocs; i++) {
		for (int j = 0; j < banker.nres; j++) {
			printf("proc %d, res %d: ", i, j);
			scanf("%d", &banker.max[i][j]);
		}
	}
}

void get_claim()
{
	printf("Enter claim:\n");
	for (int i = 0; i < banker.nres; i++) {
		printf("res %d: ", i);
		scanf("%d", &banker.tot_avail[i]);
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

int main(int argc, char **argv)
{
	initialize();
	get_claim();
	get_allocated_res();
	get_maxs();
	//show_tables();
	int req[4] = { 0, 0, 0, 0 };
	printf("\nGringotts answer: %d\n", ask_gringotts(req));
	//show_tables();
}
