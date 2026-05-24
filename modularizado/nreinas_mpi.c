#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <mpi.h>
#include <sys/time.h>
#include "nreinas_pthreads.h"

#define TAG_REQUEST 1
#define TAG_WORK 2
#define TAG_RESULT 3

// Variables globales instanciadas en este archivo
int SIZE, SIZEE, TOPBIT, MASK, SIDEMASK;
int NUM_THREADS;

// Pool global dinámico del Master
Task* global_task_pool = NULL;
int pool_capacity = 5000;
int total_tasks_generated = 0;
int next_task_to_dispatch = 0;
pthread_mutex_t master_pool_lock; 
ResultTotals master_grand_totals = {0,0,0};
int temp_board[MAXSIZE];

double dwalltime() {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return tv.tv_sec + tv.tv_usec/1000000.0;
}

// Verifica y expande la memoria si es necesario
void check_and_realloc() {
    if (total_tasks_generated >= pool_capacity) {
        pool_capacity *= 2;
        global_task_pool = (Task*)realloc(global_task_pool, pool_capacity * sizeof(Task));
        if (global_task_pool == NULL) {
            printf("Error: Out of memory!\n");
            exit(1);
        }
    }
}

// --- GENERADORES DE TAREAS (Rank 0) ---
void GenerateTasks1(int y, int left, int down, int right, int bound1) {
    if (y == 3) {
        check_and_realloc();
        Task t = {1, y, left, down, right, bound1, 0, 0, 0, {0}};
        for(int i=0; i<y; i++) t.initial_board[i] = temp_board[i];
        global_task_pool[total_tasks_generated++] = t;
        return;
    }
    int bitmap = MASK & ~(left | down | right);
    if (y < bound1) { bitmap |= 2; bitmap ^= 2; }
    int bit;
    while (bitmap) {
        bitmap ^= temp_board[y] = bit = -bitmap & bitmap;
        GenerateTasks1(y+1, (left | bit)<<1, down | bit, (right | bit)>>1, bound1);
    }
}

void GenerateTasks2(int y, int left, int down, int right, int bound1, int bound2, int lastmask, int endbit) {
    if (y == 3) {
        check_and_realloc();
        Task t = {0, y, left, down, right, bound1, bound2, lastmask, endbit, {0}};
        for(int i=0; i<y; i++) t.initial_board[i] = temp_board[i];
        global_task_pool[total_tasks_generated++] = t;
        return;
    }
    int bitmap = MASK & ~(left | down | right);
    if (y < bound1) { bitmap |= SIDEMASK; bitmap ^= SIDEMASK; }
    else if (y == bound2) {
        if (!(down & SIDEMASK)) return;
        if ((down & SIDEMASK) != SIDEMASK) bitmap &= SIDEMASK;
    }
    int bit;
    while (bitmap) {
        bitmap ^= temp_board[y] = bit = -bitmap & bitmap;
        GenerateTasks2(y+1, (left | bit)<<1, down | bit, (right | bit)>>1, bound1, bound2, lastmask, endbit);
    }
}

// Hilo local de Rank 0 para computar mientras gestiona MPI
void* rank0_local_worker_manager(void* arg) {
    int local_threads = *((int*)arg);
    while(1) {
        Task local_batch[BATCH_SIZE];
        int tasks_grabbed = 0;

        pthread_mutex_lock(&master_pool_lock);
        while (tasks_grabbed < BATCH_SIZE && next_task_to_dispatch < total_tasks_generated) {
            local_batch[tasks_grabbed++] = global_task_pool[next_task_to_dispatch++];
        }
        pthread_mutex_unlock(&master_pool_lock);

        if (tasks_grabbed == 0) break;

        ResultTotals res = solve_task_batch_with_pthreads(local_batch, tasks_grabbed, local_threads);

        pthread_mutex_lock(&master_pool_lock);
        master_grand_totals.count8 += res.count8;
        master_grand_totals.count4 += res.count4;
        master_grand_totals.count2 += res.count2;
        pthread_mutex_unlock(&master_pool_lock);
    }
    return NULL;
}

// --- MAIN ---
int main(int argc, char *argv[]) {
    int rank, num_nodes;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_nodes);

    if (argc < 3) {
        if (rank == 0) printf("Usage: %s <N> <NUM_THREADS>\n", argv[0]);
        MPI_Finalize();
        return 0;
    }

    SIZE = atoi(argv[1]);
    NUM_THREADS = atoi(argv[2]); // Leemos los hilos como parámetro

    // Todos los rangos inicializan las máscaras base
    SIZEE  = SIZE - 1;
    TOPBIT = 1 << SIZEE;
    MASK   = (1 << SIZE) - 1;
    SIDEMASK = TOPBIT | 1;

    double tIni = dwalltime();

    if (rank == 0) {
        // --- MASTER LOGIC ---
        global_task_pool = (Task*)malloc(pool_capacity * sizeof(Task));

        temp_board[0] = 1; 
        for (int b1=2; b1<SIZEE; b1++) {
            temp_board[1] = 1 << b1;
            GenerateTasks1(2, (2 | (1<<b1))<<1, 1 | (1<<b1), (1<<b1)>>1, b1);
        }

        int lastm = TOPBIT | 1;
        int endb = TOPBIT >> 1;
        for (int b1=1, b2=SIZE-2; b1<b2; b1++, b2--) {
            temp_board[0] = 1 << b1;
            GenerateTasks2(1, (1<<b1)<<1, (1<<b1), (1<<b1)>>1, b1, b2, lastm, endb);
            lastm |= lastm>>1 | lastm<<1;
            endb >>= 1;
        }

        printf("Rank 0: Tree generated. %d tasks created.\n", total_tasks_generated);

        // Rank 0 reserva 1 núcleo para MPI si hay varios hilos
        int local_master_threads = (NUM_THREADS > 1) ? NUM_THREADS - 1 : 1;
        pthread_mutex_init(&master_pool_lock, NULL);
        
        pthread_t my_local_worker;
        pthread_create(&my_local_worker, NULL, rank0_local_worker_manager, &local_master_threads);

        int active_remote_workers = num_nodes - 1;
        while (active_remote_workers > 0) {
            MPI_Status status;
            int request_dummy;
            MPI_Recv(&request_dummy, 1, MPI_INT, MPI_ANY_SOURCE, TAG_REQUEST, MPI_COMM_WORLD, &status);
            int remote_node = status.MPI_SOURCE;

            Task mpi_batch[BATCH_SIZE];
            int tasks_grabbed = 0;
            
            pthread_mutex_lock(&master_pool_lock);
            while (tasks_grabbed < BATCH_SIZE && next_task_to_dispatch < total_tasks_generated) {
                mpi_batch[tasks_grabbed++] = global_task_pool[next_task_to_dispatch++];
            }
            pthread_mutex_unlock(&master_pool_lock);

            MPI_Send(mpi_batch, tasks_grabbed * sizeof(Task), MPI_BYTE, remote_node, TAG_WORK, MPI_COMM_WORLD);
            if (tasks_grabbed == 0) active_remote_workers--; 
        }

        pthread_join(my_local_worker, NULL);

        for (int i = 1; i < num_nodes; i++) {
            ResultTotals remote_totals;
            MPI_Recv(&remote_totals, sizeof(ResultTotals), MPI_BYTE, i, TAG_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            master_grand_totals.count8 += remote_totals.count8;
            master_grand_totals.count4 += remote_totals.count4;
            master_grand_totals.count2 += remote_totals.count2;
        }

        long long TOTAL = (master_grand_totals.count8 * 8) + (master_grand_totals.count4 * 4) + (master_grand_totals.count2 * 2);
        double tFin = dwalltime();
        printf("Número de resultados: %lld - Tiempo Total: %f segundos \n", TOTAL, tFin-tIni);

        free(global_task_pool); 

    } else {
        // --- WORKER LOGIC ---
        ResultTotals node_totals = {0,0,0};
        int request_msg = 1;

        while (1) {
            MPI_Send(&request_msg, 1, MPI_INT, 0, TAG_REQUEST, MPI_COMM_WORLD);

            Task my_batch[BATCH_SIZE];
            MPI_Status status;
            MPI_Recv(my_batch, BATCH_SIZE * sizeof(Task), MPI_BYTE, 0, TAG_WORK, MPI_COMM_WORLD, &status);

            int bytes_received;
            MPI_Get_count(&status, MPI_BYTE, &bytes_received);
            int tasks_received = bytes_received / sizeof(Task);

            if (tasks_received == 0) break; // Poison pill (0 bytes) recibida

            ResultTotals batch_res = solve_task_batch_with_pthreads(my_batch, tasks_received, NUM_THREADS);

            node_totals.count8 += batch_res.count8;
            node_totals.count4 += batch_res.count4;
            node_totals.count2 += batch_res.count2;
        }
        MPI_Send(&node_totals, sizeof(ResultTotals), MPI_BYTE, 0, TAG_RESULT, MPI_COMM_WORLD);
    }

    MPI_Finalize();
    return 0;
}