#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int *avail;
int **max, **allocs;
int nprocs,nres;

void initialize(){
    printf("Enter number of ressources:\n");
    scanf("%d", &nres);

    printf("Enter number of processes:\n");
    scanf("%d", &nprocs);

    avail = malloc(sizeof(int)*nres);
    for(int i=0;i<nres;i++){
        avail[i] = 0;
    }

    if((max = malloc(sizeof(int*) * nprocs)) == NULL){
        perror("Error allocating memory");
        abort();
    }
    if((allocs = malloc(sizeof(int*) * nprocs)) == NULL){
        perror("Error allocating memory");
        abort();
    }
    for(int i=0;i < nprocs; i++){
        max[i] = malloc(sizeof(int) * nres);
        allocs[i] = malloc(sizeof(int) * nres);
        for(int j=0;j<nres;j++){
            max[i][j] = 0;
            allocs[i][j] = 0;
        }
    }
}

void show_tables(){
    printf("\nAvailable:\n");
    for(int i=0;i<nres;i++){
        printf("%d ",avail[i]);
    }
    printf("\n\n");

    printf("Maximums:\n");
    for(int i=0;i<nprocs;i++){
        for(int j=0;j<nres;j++){
            printf("%d ",max[i][j]);
        }
        printf("\n");
    }
    printf("\n");

    printf("Allocated:\n");
    for(int i=0;i<nprocs;i++){
        for(int j=0;j<nres;j++){
            printf("%d ",allocs[i][j]);
        }
        printf("\n");
    }
    printf("\n");
}

void get_allocated_res(){
    printf("Enter allocations:\n");
    for(int i=0; i<nprocs; i++){
        for(int j=0; j<nres; j++){
            printf("proc %d, res %d: ",i,j);
            scanf("%d", &allocs[i][j]);
        }
    }
}

void get_maxs(){
    printf("Enter maximums:\n");
    for(int i=0; i<nprocs; i++){
        for(int j=0; j<nres; j++){
            printf("proc %d, res %d: ",i,j);
            scanf("%d", &max[i][j]);
        }
    }
}

void get_claim(){
    printf("Enter claim:\n");
    for(int i=0; i<nres; i++){
        printf("res %d: ",i);
        scanf("%d", &avail[i]);
    }
}

int is_all_zeros(int a[]){
    for(int i=0;i<(sizeof(a)/sizeof(int));i++){
        if(a[i] != 0){
            return 0;
        }
    }
    return 1;
}

int may_run(int p){
    for(int i=0;i<nres;i++){
        int alloc_res_i = 0;
        for(int j=0;j<nprocs;j++){
            alloc_res_i += allocs[j][i];
        }
        if(avail[i] - (max[p][i] - allocs[p][i]) - alloc_res_i < 0){
            return 0;
        }
    }
    return 1;
}

// Actual check
int ask_gringotts(){
    int is_safe = 0;
    int running[nprocs];
    for(int i=0;i<nprocs;i++) running[i]=1;

    while(!is_all_zeros(running)){
        int alloc_this_round = 0;
        for(int i=0;i<nprocs;i++){
            if(running[i]){
                if(may_run(i)){
                    //process may run!
                    alloc_this_round++;
                    running[i] = 0;
                }
            }
        }
        if(alloc_this_round == 0) return 0;
    }
    return 1;
}

int main (int argc, char** argv){
    initialize();
    get_claim();
    get_allocated_res();
    get_maxs();
    show_tables();
    printf("Gringotts answer: %d\n",ask_gringotts());
}
