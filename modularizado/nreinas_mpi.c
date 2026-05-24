#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <mpi.h>
#include "nreinas_shared.h"

#define TAG_REQUEST 1
#define TAG_WORK 2
#define TAG_RESULT 3
#define MAX_GLOBAL_TASKS 5000

// --- VARIABLES GLOBALES ---
int SIZE, SIZEE, TOPBIT, MASK, SIDEMASK;
int NUM_THREADS;

// Pool global estático del Master (Sin Mutex)
task_t global_task_pool[MAX_GLOBAL_TASKS];
int total_tasks_generated = 0;
int next_task_to_dispatch = 0;
results_t master_grand_totals = {0, 0, 0};
int temp_board[MAXSIZE];

// --- PROTOTIPOS DE FUNCIONES ---
double dwalltime(void);
void f0(int num_nodes, double tIni);
void fN(void);
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

    // Todos los ranks inicializan las máscaras base
    SIZEE  = SIZE - 1;
    TOPBIT = 1 << SIZEE;
    MASK   = (1 << SIZE) - 1;
    SIDEMASK = TOPBIT | 1;

    double tIni = dwalltime();

    if (rank == 0) {
        f0(num_nodes, tIni); // Pasamos num_nodes y tIni como parámetros
    } else {
        fN();
    }

    MPI_Finalize();
    return 0;
}

/**********************************************/
/*        Funciones para los procesos         */
/**********************************************/
void f0(int num_nodes, double tIni) {
    // 1. Generar todas las tareas
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

    int local_master_threads = (NUM_THREADS > 1) ? NUM_THREADS - 1 : 1;
    int active_remote_workers = num_nodes - 1;

    // 2. Bucle principal de computación y red intercalada (MPI_Iprobe)
    while (next_task_to_dispatch < total_tasks_generated || active_remote_workers > 0) {
        int has_message = 0;
        MPI_Status status;

        // Revisar la red sin bloquearnos (Solo si aún hay workers activos)
        if (active_remote_workers > 0) {
            if (next_task_to_dispatch < total_tasks_generated) {
                MPI_Iprobe(MPI_ANY_SOURCE, TAG_REQUEST, MPI_COMM_WORLD, &has_message, &status);
            } else {
                // Si ya no hay trabajo local, nos bloqueamos para no gastar CPU
                MPI_Probe(MPI_ANY_SOURCE, TAG_REQUEST, MPI_COMM_WORLD, &status);
                has_message = 1; 
            }
        }

        // Si alguien pidió trabajo, despachamos por la red
        if (has_message) {
            int dummy;
            MPI_Recv(&dummy, 1, MPI_INT, status.MPI_SOURCE, TAG_REQUEST, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            task_t mpi_batch[BATCH_SIZE];
            int tasks_grabbed = 0;
            while (tasks_grabbed < BATCH_SIZE && next_task_to_dispatch < total_tasks_generated) {
                mpi_batch[tasks_grabbed++] = global_task_pool[next_task_to_dispatch++];
            }
            
            MPI_Send(mpi_batch, tasks_grabbed * sizeof(task_t), MPI_BYTE, status.MPI_SOURCE, TAG_WORK, MPI_COMM_WORLD);
            if (tasks_grabbed == 0) active_remote_workers--; 
        }
        
        // Si nadie pidió trabajo, resolvemos un Batch localmente
        else if (next_task_to_dispatch < total_tasks_generated) {
            task_t local_batch[BATCH_SIZE];
            int tasks_grabbed = 0;
            while (tasks_grabbed < BATCH_SIZE && next_task_to_dispatch < total_tasks_generated) {
                local_batch[tasks_grabbed++] = global_task_pool[next_task_to_dispatch++];
            }

            // Llamamos a pthreads directamente desde f0
            results_t res = pthreads_batch(local_batch, tasks_grabbed, local_master_threads);
            master_grand_totals.count8 += res.count8;
            master_grand_totals.count4 += res.count4;
            master_grand_totals.count2 += res.count2;
        }
    }

    // 3. Recibir los resultados finales de los nodos remotos
    for (int i = 1; i < num_nodes; i++) {
        results_t remote_totals;
        MPI_Recv(&remote_totals, sizeof(results_t), MPI_BYTE, i, TAG_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        master_grand_totals.count8 += remote_totals.count8;
        master_grand_totals.count4 += remote_totals.count4;
        master_grand_totals.count2 += remote_totals.count2;
    }

    long long TOTAL = (master_grand_totals.count8 * 8) + (master_grand_totals.count4 * 4) + (master_grand_totals.count2 * 2);
    double tFin = dwalltime();
    printf("Número de resultados: %lld - Tiempo Total: %f segundos \n", TOTAL, tFin-tIni);
}

void fN(void) {
    results_t node_totals = {0, 0, 0};
    int request_msg = 1;

    while (1) {
        MPI_Send(&request_msg, 1, MPI_INT, 0, TAG_REQUEST, MPI_COMM_WORLD);

        task_t my_batch[BATCH_SIZE];
        MPI_Status status;
        MPI_Recv(my_batch, BATCH_SIZE * sizeof(task_t), MPI_BYTE, 0, TAG_WORK, MPI_COMM_WORLD, &status);

        int bytes_received;
        MPI_Get_count(&status, MPI_BYTE, &bytes_received);
        int tasks_received = bytes_received / sizeof(task_t);

        if (tasks_received == 0) break; // Poison pill

        results_t batch_res = pthreads_batch(my_batch, tasks_received, NUM_THREADS);

        node_totals.count8 += batch_res.count8;
        node_totals.count4 += batch_res.count4;
        node_totals.count2 += batch_res.count2;
    }
    
    MPI_Send(&node_totals, sizeof(results_t), MPI_BYTE, 0, TAG_RESULT, MPI_COMM_WORLD);
}

/**********************************************/
/*    Generación de tareas (las usa rank0)    */
/**********************************************/
void GenerateTasks1(int y, int left, int down, int right, int bound1) {
    if (y == 3) {
        if (total_tasks_generated >= MAX_GLOBAL_TASKS) {
            printf("Error: Límite de tareas excedido. Aumente MAX_GLOBAL_TASKS.\n");
            exit(1);
        }
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
        if (total_tasks_generated >= MAX_GLOBAL_TASKS) {
            printf("Error: Límite de tareas excedido. Aumente MAX_GLOBAL_TASKS.\n");
            exit(1);
        }
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

/**********************************************/
/*        Función para medir el tiempo        */
/**********************************************/
double dwalltime(void) {
    double sec;
    struct timeval tv;
    gettimeofday(&tv,NULL);
    sec = tv.tv_sec + tv.tv_usec/1000000.0;
    return sec;
}

