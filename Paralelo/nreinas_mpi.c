#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <mpi.h>
#include <pthread.h>
#include "nreinas_shared.h"

#define TAG_REQUEST 1
#define TAG_WORK 2
#define TAG_RESULT 3

// --- GLOBAL VARIABLES ---
int SIZE, SIZEE, TOPBIT, MASK, SIDEMASK;
int NUM_THREADS;

task_t global_task_pool[MAX_GLOBAL_TASKS];
int total_tasks_generated = 0;
int next_task_to_dispatch = 0;
pthread_mutex_t pool_mutex = PTHREAD_MUTEX_INITIALIZER;
int temp_board[MAXSIZE];

// --- PROTOTYPES ---
double dwalltime(void);
void GenerateTasks1(int y, int left, int down, int right, int bound1);
void GenerateTasks2(int y, int left, int down, int right, int bound1, int bound2, int lastmask, int endbit);

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
    NUM_THREADS = atoi(argv[2]);

    SIZEE  = SIZE - 1;
    TOPBIT = 1 << SIZEE;
    MASK   = (1 << SIZE) - 1;
    SIDEMASK = TOPBIT | 1;

    // We allocate thread arrays statically based on the known max configuration
    pthread_t threads[MAX_THREADS];
    results_t thread_results[MAX_THREADS];

    // ALL data structures must be initialized OUTSIDE the timer.
    MPI_Barrier(MPI_COMM_WORLD); 
    double tIni = dwalltime();

    if (rank == 0) {
        // 1. COMPUTATION: Generate the top of the tree
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

        // 2. Start local worker threads (They immediately grab tasks via mutex)
        int local_threads = (NUM_THREADS > 1) ? NUM_THREADS - 1 : 1;
        start_local_workers(local_threads, threads, thread_results);

        int active_remote_workers = num_nodes - 1;

        // 3. COMMUNICATION: Rank 0 acts strictly as a non-blocking router
        while (active_remote_workers > 0) {
            MPI_Status status;
            int dummy;
            
            // Wait until someone asks for work
            MPI_Recv(&dummy, 1, MPI_INT, MPI_ANY_SOURCE, TAG_REQUEST, MPI_COMM_WORLD, &status);

            task_t mpi_batch[BATCH_SIZE];
            int tasks_grabbed = 0;

            pthread_mutex_lock(&pool_mutex);
            while (tasks_grabbed < BATCH_SIZE && next_task_to_dispatch < total_tasks_generated) {
                mpi_batch[tasks_grabbed++] = global_task_pool[next_task_to_dispatch++];
            }
            pthread_mutex_unlock(&pool_mutex);
            
            MPI_Send(mpi_batch, tasks_grabbed * sizeof(task_t), MPI_BYTE, status.MPI_SOURCE, TAG_WORK, MPI_COMM_WORLD);
            if (tasks_grabbed == 0) active_remote_workers--; 
        }

        results_t grand_totals = {0, 0, 0};

        // 4. Wait for local threads to finish & accumulate their results
        for (int i = 0; i < local_threads; i++) {
            pthread_join(threads[i], NULL);
            grand_totals.count8 += thread_results[i].count8;
            grand_totals.count4 += thread_results[i].count4;
            grand_totals.count2 += thread_results[i].count2;
        }

        // 5. Receive remote totals
        for (int i = 1; i < num_nodes; i++) {
            results_t remote_totals;
            MPI_Recv(&remote_totals, sizeof(results_t), MPI_BYTE, i, TAG_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            grand_totals.count8 += remote_totals.count8;
            grand_totals.count4 += remote_totals.count4;
            grand_totals.count2 += remote_totals.count2;
        }

        // ================= TIMER ENDS =================
        double tFin = dwalltime();

        long long TOTAL = (grand_totals.count8 * 8) + (grand_totals.count4 * 4) + (grand_totals.count2 * 2);
        
        // Print statements strictly outside execution block
        printf("Rank 0: Tree generated. %d tasks created.\n", total_tasks_generated);
        printf("Número de resultados: %lld - Tiempo Total: %f segundos \n", TOTAL, tFin-tIni);

    } else {
        // RANK N LOGIC
        start_remote_workers(NUM_THREADS, threads, thread_results);
        int request_msg = 1;

        while (1) {
            // Request work
            MPI_Send(&request_msg, 1, MPI_INT, 0, TAG_REQUEST, MPI_COMM_WORLD);

            task_t recv_batch[BATCH_SIZE];
            MPI_Status status;
            MPI_Recv(recv_batch, BATCH_SIZE * sizeof(task_t), MPI_BYTE, 0, TAG_WORK, MPI_COMM_WORLD, &status);

            int bytes_received;
            MPI_Get_count(&status, MPI_BYTE, &bytes_received);
            int tasks_received = bytes_received / sizeof(task_t);

            if (tasks_received == 0) {
                terminate_remote_workers();
                break;
            }

            // Let threads process the batch
            feed_remote_batch(recv_batch, tasks_received);
        }
        
        results_t node_totals = {0, 0, 0};

        // Join threads and sum results
        for (int i = 0; i < NUM_THREADS; i++) {
            pthread_join(threads[i], NULL);
            node_totals.count8 += thread_results[i].count8;
            node_totals.count4 += thread_results[i].count4;
            node_totals.count2 += thread_results[i].count2;
        }

        // Send totals to Master
        MPI_Send(&node_totals, sizeof(results_t), MPI_BYTE, 0, TAG_RESULT, MPI_COMM_WORLD);
    }

    MPI_Finalize();
    return 0;
}

double dwalltime(void) {
    double sec;
    struct timeval tv;
    gettimeofday(&tv,NULL);
    sec = tv.tv_sec + tv.tv_usec/1000000.0;
    return sec;
}

// Tree generation algorithms
void GenerateTasks1(int y, int left, int down, int right, int bound1) {
    if (y == 3) {
        task_t t = {1, y, left, down, right, bound1, 0, 0, 0, {0}};
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
        task_t t = {0, y, left, down, right, bound1, bound2, lastmask, endbit, {0}};
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