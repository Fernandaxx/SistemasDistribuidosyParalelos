#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <mpi.h>
#include <pthread.h>
#include "nreinas_shared.h"

#define TAG_REQUEST 1
#define TAG_WORK    2
#define TAG_RESULT  3

/* Variables globales compartidas por los módulos. */
int SIZE , SIZEE , TOPBIT , MASK , SIDEMASK , NUM_THREADS;

task_t global_task_pool[MAX_GLOBAL_TASKS];
int total_tasks_generated = 0;
int next_task_to_dispatch = 0;
pthread_mutex_t pool_mutex = PTHREAD_MUTEX_INITIALIZER;

int temp_board[MAXSIZE];

pthread_t threads[MAX_THREADS];
results_t thread_results[MAX_THREADS];

/* Prototipos locales. */
double dwalltime(void);

void f0(int num_nodes , double tIni);
void fN(void);

void GenerateTasks1(int y , int left , int down , int right , int bound1);
void GenerateTasks2(int y , int left , int down , int right , int bound1 , int bound2 , int lastmask , int endbit);


/* Inicializa MPI, configura parámetros y deriva la ejecución al rol master o worker. */
int main(int argc , char* argv[]){
    int rank , num_nodes;
    int provided;

    MPI_Init_thread(&argc , &argv , MPI_THREAD_FUNNELED , &provided);

    MPI_Comm_rank(MPI_COMM_WORLD , &rank);
    MPI_Comm_size(MPI_COMM_WORLD , &num_nodes);

    if (provided < MPI_THREAD_FUNNELED){
        if (rank == 0){
            fprintf(stderr , "Error: la implementación MPI no provee soporte MPI_THREAD_FUNNELED.\n");
        }

        MPI_Abort(MPI_COMM_WORLD , 1);
    }

    if (argc < 3){
        if (rank == 0){
            fprintf(stderr , "Uso: %s <N> <NUM_THREADS>\n" , argv[0]);
        }

        MPI_Finalize();
        return 1;
    }

    SIZE = atoi(argv[1]);
    NUM_THREADS = atoi(argv[2]);

    if (SIZE < 2 || SIZE > MAXSIZE){
        if (rank == 0){
            fprintf(stderr , "Error: N debe estar entre 2 y %d.\n" , MAXSIZE);
        }

        MPI_Finalize();
        return 1;
    }

    if (NUM_THREADS < 1 || NUM_THREADS > MAX_THREADS){
        if (rank == 0){
            fprintf(stderr , "Error: NUM_THREADS debe estar entre 1 y %d.\n" , MAX_THREADS);
        }

        MPI_Finalize();
        return 1;
    }

    SIZEE = SIZE - 1;
    TOPBIT = 1 << SIZEE;
    MASK = (1 << SIZE) - 1;
    SIDEMASK = TOPBIT | 1;


    double tIni = dwalltime();
    if (rank == 0){
        int local_threads = (NUM_THREADS > 1) ? NUM_THREADS - 1 : 1;
        CrearHilosLocales(local_threads , threads , thread_results);
    }
    else{
        CrearHilosRemotos(NUM_THREADS , threads , thread_results);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    if (rank == 0){
        f0(num_nodes , tIni);
    }
    else{
        fN();
    }

    MPI_Finalize();
    return 0;
}

/* Proceso master. Genera tareas, las distribuye y consolida resultados. */
void f0(int num_nodes , double tIni){
    /*
     * Generación de tareas.
     * Cada tarea representa un subárbol independiente del backtracking.
     */
    temp_board[0] = 1;

    for (int b1 = 2; b1 < SIZEE; b1++){
        temp_board[1] = 1 << b1;
        GenerateTasks1(2 , (2 | (1 << b1)) << 1 , 1 | (1 << b1) , (1 << b1) >> 1 , b1);

    }

    int lastm = TOPBIT | 1;
    int endb = TOPBIT >> 1;

    for (int b1 = 1 , b2 = SIZE - 2; b1 < b2; b1++ , b2--){
        temp_board[0] = 1 << b1;
        GenerateTasks2(1 , (1 << b1) << 1 , (1 << b1) , (1 << b1) >> 1 , b1 , b2 , lastm , endb);
        lastm |= lastm >> 1 | lastm << 1;
        endb >>= 1;
    }

    /*
     * A partir de este punto los hilos locales del rank 0 ya pueden
     * consumir tareas del pool.
     */
    LiberarHilosLocales();

    int local_threads = (NUM_THREADS > 1) ? NUM_THREADS - 1 : 1;
    int active_remote_workers = num_nodes - 1;

    /*
     * Distribución dinámica de lotes.
     * Los ranks remotos piden trabajo y el master entrega hasta BATCH_SIZE tareas.
     */
    while (active_remote_workers > 0){
        MPI_Status status;
        int request_msg;
        MPI_Recv(&request_msg , 1 , MPI_INT , MPI_ANY_SOURCE , TAG_REQUEST , MPI_COMM_WORLD , &status);


        task_t mpi_batch[BATCH_SIZE];
        int tasks_grabbed = 0;

        pthread_mutex_lock(&pool_mutex);

        while (tasks_grabbed < BATCH_SIZE && next_task_to_dispatch < total_tasks_generated){
            mpi_batch[tasks_grabbed++] = global_task_pool[next_task_to_dispatch++];
        }

        pthread_mutex_unlock(&pool_mutex);

        MPI_Send(mpi_batch , tasks_grabbed * sizeof(task_t) , MPI_BYTE , status.MPI_SOURCE , TAG_WORK , MPI_COMM_WORLD);

        // Un lote vacío indica que no quedan tareas para ese rank remoto.

        if (tasks_grabbed == 0){
            active_remote_workers--;
        }
    }

    results_t grand_totals = {0, 0, 0};

    /* Resultados de los hilos locales del rank 0. */
    for (int i = 0; i < local_threads; i++){
        pthread_join(threads[i] , NULL);

        grand_totals.count8 += thread_results[i].count8;
        grand_totals.count4 += thread_results[i].count4;
        grand_totals.count2 += thread_results[i].count2;
    }

    /* Resultados enviados por los ranks remotos. */
    for (int i = 1; i < num_nodes; i++){
        results_t remote_totals = {0, 0, 0};

        MPI_Recv(&remote_totals , sizeof(results_t) , MPI_BYTE , i , TAG_RESULT , MPI_COMM_WORLD , MPI_STATUS_IGNORE);


        grand_totals.count8 += remote_totals.count8;
        grand_totals.count4 += remote_totals.count4;
        grand_totals.count2 += remote_totals.count2;
    }

    long long UNIQUE = grand_totals.count8 + grand_totals.count4 + grand_totals.count2;
    long long TOTAL = (grand_totals.count8 * 8) + (grand_totals.count4 * 4) + (grand_totals.count2 * 2);
    
    double tFin = dwalltime();

    printf("Tareas generadas: %d\n" , total_tasks_generated);
    printf("N=%d, Hilos por proceso=%d, Soluciones Totales=%lld, Soluciones Unicas=%lld, Tiempo=%f segundos\n" ,
        SIZE , NUM_THREADS , TOTAL , UNIQUE , tFin - tIni
    );
}

/* Proceso worker remoto. Pide lotes, los entrega a sus hilos y envía resultados. */
void fN(void){
    int request_msg = 1;

    while (1){
        MPI_Send(&request_msg , 1 , MPI_INT , 0 , TAG_REQUEST , MPI_COMM_WORLD);


        task_t recv_batch[BATCH_SIZE];
        MPI_Status status;

        MPI_Recv(recv_batch , BATCH_SIZE * sizeof(task_t) , MPI_BYTE , 0 , TAG_WORK , MPI_COMM_WORLD , &status);


        int bytes_received;

        MPI_Get_count(&status , MPI_BYTE , &bytes_received);
        int tasks_received = bytes_received / (int) sizeof(task_t);

        if (tasks_received == 0){
            DetenerHilosRemotos();
            break;
        }

        RepartirLoteRemoto(recv_batch , tasks_received);
    }

    results_t node_totals = {0, 0, 0};

    for (int i = 0; i < NUM_THREADS; i++){
        pthread_join(threads[i] , NULL);

        node_totals.count8 += thread_results[i].count8;
        node_totals.count4 += thread_results[i].count4;
        node_totals.count2 += thread_results[i].count2;
    }

    MPI_Send(&node_totals , sizeof(results_t) , MPI_BYTE , 0 , TAG_RESULT , MPI_COMM_WORLD);

}

/* Genera tareas derivadas de Backtrack1. */
void GenerateTasks1(int y , int left , int down , int right , int bound1){
    if (y == 3){
        task_t t = {1, y, left, down, right, bound1, 0, 0, 0, {0}};
        for (int i = 0; i < y; i++) t.initial_board[i] = temp_board[i];
        global_task_pool[total_tasks_generated++] = t;
        return;
    }
    int bitmap = MASK & ~(left | down | right);
    if (y < bound1){ bitmap |= 2; bitmap ^= 2; }
    int bit;
    while (bitmap){
        bitmap ^= temp_board[y] = bit = -bitmap & bitmap;
        GenerateTasks1(y + 1 , (left | bit) << 1 , down | bit , (right | bit) >> 1 , bound1);
    }
}

/* Genera tareas derivadas de Backtrack2. */
void GenerateTasks2(int y , int left , int down , int right , int bound1 , int bound2 , int lastmask , int endbit){
    if (y == 3){
        task_t t = {0, y, left, down, right, bound1, bound2, lastmask, endbit, {0}};
        for (int i = 0; i < y; i++) t.initial_board[i] = temp_board[i];
        global_task_pool[total_tasks_generated++] = t;
        return;
    }
    int bitmap = MASK & ~(left | down | right);
    if (y < bound1){ bitmap |= SIDEMASK; bitmap ^= SIDEMASK; }
    else if (y == bound2){
        if (!(down & SIDEMASK)) return;
        if ((down & SIDEMASK) != SIDEMASK) bitmap &= SIDEMASK;
    }
    int bit;
    while (bitmap){
        bitmap ^= temp_board[y] = bit = -bitmap & bitmap;
        GenerateTasks2(y + 1 , (left | bit) << 1 , down | bit , (right | bit) >> 1 , bound1 , bound2 , lastmask , endbit);
    }
}

double dwalltime(void){
    double sec;
    struct timeval tv;
    gettimeofday(&tv , NULL);
    sec = tv.tv_sec + tv.tv_usec / 1000000.0;
    return sec;
}