#define _GNU_SOURCE
/**************************************************************************/
/* N-Queens - Etapa 5: MPI + Pthreads                                    */
/*                                                                        */
/* Estrategia implementada:                                               */
/* - MPI entre nodos/procesos: mapeo estatico ciclico de tareas.           */
/* - Pthreads dentro de cada proceso MPI: Bag of Tasks local dinamica.     */
/* - Cada hilo acumula contadores locales; el proceso reduce sus hilos.    */
/* - MPI_Reduce combina los contadores finales entre procesos.             */
/*                                                                        */
/* Politica de tiempos:                                                   */
/* - Fuera de la medicion: parseo, generacion/filtrado de tareas,          */
/*   malloc/free, inicializacion de mutex/barrera, creacion de hilos,      */
/*   impresion y calculo de metricas auxiliares.                           */
/* - Dentro de la medicion: backtracking, sincronizacion Pthreads local,   */
/*   reduccion local de resultados y comunicacion final MPI_Reduce.        */
/**************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>
#include <mpi.h>

#define MAXSIZE 24
#define MINSIZE 2
#define MAX_TASKS (2 * MAXSIZE)

typedef enum{
    TASK_BACKTRACK1 = 1 ,
    TASK_BACKTRACK2 = 2
} TaskType;

typedef struct{
    int size;
    int sizee;

    int board[MAXSIZE];

    int mask;
    int topbit;
    int sidemask;
    int lastmask;
    int endbit;

    int bound1;
    int bound2;

    unsigned long long count8;
    unsigned long long count4;
    unsigned long long count2;
    unsigned long long total;
    unsigned long long unique;
} NQueenState;

typedef struct{
    TaskType type;
    int y;
    int left;
    int down;
    int right;
    NQueenState initial_state;
} NQueenTask;

typedef struct{
    NQueenTask tasks[MAX_TASKS];
    int count;
} TaskList;

typedef struct{
    unsigned long long count8;
    unsigned long long count4;
    unsigned long long count2;
    unsigned long long unique;
    unsigned long long total;
} NQueenResult;

typedef struct{
    const TaskList* task_list;
    int next_task;
    pthread_mutex_t mutex;
    pthread_barrier_t start_barrier;
} SharedTaskPool;

typedef struct{
    int tid;
    SharedTaskPool* pool;
    NQueenResult result;
    int tasks_done;
    double elapsed;
} ThreadData;

static double dwalltime(void){
    struct timeval tv;
    gettimeofday(&tv , NULL);
    return (double) tv.tv_sec + (double) tv.tv_usec / 1000000.0;
}

static void init_state(NQueenState* st , int n){
    st->size = n;
    st->sizee = n - 1;

    st->mask = (1 << n) - 1;
    st->topbit = 1 << st->sizee;

    st->sidemask = 0;
    st->lastmask = 0;
    st->endbit = 0;
    st->bound1 = 0;
    st->bound2 = 0;

    st->count8 = 0ULL;
    st->count4 = 0ULL;
    st->count2 = 0ULL;
    st->total = 0ULL;
    st->unique = 0ULL;

    for (int i = 0; i < MAXSIZE; i++){
        st->board[i] = 0;
    }
}

static void reset_result(NQueenResult* res){
    res->count8 = 0ULL;
    res->count4 = 0ULL;
    res->count2 = 0ULL;
    res->unique = 0ULL;
    res->total = 0ULL;
}

static void finalize_result(NQueenResult* res){
    res->unique = res->count8 + res->count4 + res->count2;
    res->total = (res->count8 * 8ULL) + (res->count4 * 4ULL) + (res->count2 * 2ULL);
}

/**********************************************/
/* Check Unique Solutions                     */
/**********************************************/
static void check_solution(NQueenState* st){
    int* own;
    int* you;
    int bit;
    int ptn;

    int* board = st->board;
    int* boarde = &st->board[st->sizee];
    int* board1 = &st->board[st->bound1];
    int* board2 = &st->board[st->bound2];

    /* 90-degree rotation */
    if (*board2 == 1){
        for (ptn = 2 , own = board + 1; own <= boarde; own++ , ptn <<= 1){
            bit = 1;
            for (you = boarde; *you != ptn && *own >= bit; you--){
                bit <<= 1;
            }
            if (*own > bit) return;
            if (*own < bit) break;
        }
        if (own > boarde){
            st->count2++;
            return;
        }
    }

    /* 180-degree rotation */
    if (*boarde == st->endbit){
        for (you = boarde - 1 , own = board + 1; own <= boarde; own++ , you--){
            bit = 1;
            for (ptn = st->topbit; ptn != *you && *own >= bit; ptn >>= 1){
                bit <<= 1;
            }
            if (*own > bit) return;
            if (*own < bit) break;
        }
        if (own > boarde){
            st->count4++;
            return;
        }
    }

    /* 270-degree rotation */
    if (*board1 == st->topbit){
        for (ptn = st->topbit >> 1 , own = board + 1; own <= boarde; own++ , ptn >>= 1){
            bit = 1;
            for (you = board; *you != ptn && *own >= bit; you++){
                bit <<= 1;
            }
            if (*own > bit) return;
            if (*own < bit) break;
        }
    }

    st->count8++;
}

/**********************************************/
/* First queen is inside                      */
/**********************************************/
static void backtrack2(NQueenState* st , int y , int left , int down , int right){
    int bitmap;
    int bit;

    bitmap = st->mask & ~(left | down | right);

    if (y == st->sizee){
        if (bitmap){
            if (!(bitmap & st->lastmask)){
                st->board[y] = bitmap;
                check_solution(st);
            }
        }
    }
    else{
        if (y < st->bound1){
            bitmap |= st->sidemask;
            bitmap ^= st->sidemask;
        }
        else if (y == st->bound2){
            if (!(down & st->sidemask)) return;
            if ((down & st->sidemask) != st->sidemask) bitmap &= st->sidemask;
        }

        while (bitmap){
            bit = -bitmap & bitmap;
            st->board[y] = bit;
            bitmap ^= bit;
            backtrack2(st , y + 1 , (left | bit) << 1 , down | bit , (right | bit) >> 1);
        }
    }
}

/**********************************************/
/* First queen is in the corner               */
/**********************************************/
static void backtrack1(NQueenState* st , int y , int left , int down , int right){
    int bitmap;
    int bit;

    bitmap = st->mask & ~(left | down | right);

    if (y == st->sizee){
        if (bitmap){
            st->board[y] = bitmap;
            st->count8++;
        }
    }
    else{
        if (y < st->bound1){
            bitmap |= 2;
            bitmap ^= 2;
        }

        while (bitmap){
            bit = -bitmap & bitmap;
            st->board[y] = bit;
            bitmap ^= bit;
            backtrack1(st , y + 1 , (left | bit) << 1 , down | bit , (right | bit) >> 1);
        }
    }
}

static int add_task(TaskList* task_list ,
                    TaskType type ,
                    const NQueenState* initial_state ,
                    int y ,
                    int left ,
                    int down ,
                    int right){
    if (task_list->count >= MAX_TASKS){
        return 0;
    }

    NQueenTask* task = &task_list->tasks[task_list->count];
    task->type = type;
    task->y = y;
    task->left = left;
    task->down = down;
    task->right = right;
    task->initial_state = *initial_state;

    task_list->count++;
    return 1;
}

/**********************************************/
/* Generacion de tareas globales             */
/**********************************************/
static int generate_tasks(int n , TaskList* task_list){
    NQueenState st;
    int bit;

    task_list->count = 0;
    init_state(&st , n);

    /* Tareas equivalentes al primer ciclo de NQueens(): Backtrack1. */
    st.board[0] = 1;
    for (st.bound1 = 2; st.bound1 < st.sizee; st.bound1++){
        bit = 1 << st.bound1;
        st.board[1] = bit;

        st.count8 = st.count4 = st.count2 = 0ULL;
        st.unique = st.total = 0ULL;

        if (!add_task(task_list ,
            TASK_BACKTRACK1 ,
            &st ,
            2 ,
            (2 | bit) << 1 ,
            1 | bit ,
            bit >> 1)){
            return 0;
        }
    }

    /* Tareas equivalentes al segundo ciclo de NQueens(): Backtrack2. */
    st.sidemask = st.topbit | 1;
    st.lastmask = st.sidemask;
    st.endbit = st.topbit >> 1;

    for (st.bound1 = 1 , st.bound2 = st.size - 2;
         st.bound1 < st.bound2;
         st.bound1++ , st.bound2--){

        bit = 1 << st.bound1;
        st.board[0] = bit;
        st.board[1] = 0;

        st.count8 = st.count4 = st.count2 = 0ULL;
        st.unique = st.total = 0ULL;

        if (!add_task(task_list ,
            TASK_BACKTRACK2 ,
            &st ,
            1 ,
            bit << 1 ,
            bit ,
            bit >> 1)){
            return 0;
        }

        st.lastmask |= (st.lastmask >> 1) | (st.lastmask << 1);
        st.endbit >>= 1;
    }

    return 1;
}

/**********************************************/
/* Mapeo MPI estatico ciclico                 */
/**********************************************/
static int build_local_task_list(const TaskList* global_task_list ,
                                 int rank ,
                                 int world_size ,
                                 TaskList* local_task_list){
    local_task_list->count = 0;

    for (int i = rank; i < global_task_list->count; i += world_size){
        if (local_task_list->count >= MAX_TASKS){
            return 0;
        }
        local_task_list->tasks[local_task_list->count] = global_task_list->tasks[i];
        local_task_list->count++;
    }

    return 1;
}

/**********************************************/
/* Ejecucion secuencial de una tarea          */
/**********************************************/
static void execute_task(const NQueenTask* task , NQueenResult* result){
    NQueenState st = task->initial_state;

    if (task->type == TASK_BACKTRACK1){
        backtrack1(&st , task->y , task->left , task->down , task->right);
    }
    else{
        backtrack2(&st , task->y , task->left , task->down , task->right);
    }

    result->count8 += st.count8;
    result->count4 += st.count4;
    result->count2 += st.count2;
}

/**********************************************/
/* Worker Pthreads: Bag of Tasks local        */
/**********************************************/
static void* worker_function(void* arg){
    ThreadData* data = (ThreadData*) arg;
    SharedTaskPool* pool = data->pool;
    NQueenResult local_result;
    int local_tasks = 0;
    double t_ini;
    double t_fin;

    /* Preparacion local fuera de la region cronometrada. */
    reset_result(&local_result);

    pthread_barrier_wait(&pool->start_barrier);

    t_ini = dwalltime();

    while (1){
        int task_index;

        pthread_mutex_lock(&pool->mutex);
        task_index = pool->next_task;
        pool->next_task++;
        pthread_mutex_unlock(&pool->mutex);

        if (task_index >= pool->task_list->count){
            break;
        }

        execute_task(&pool->task_list->tasks[task_index] , &local_result);
        local_tasks++;
    }

    t_fin = dwalltime();

    data->result = local_result;
    data->tasks_done = local_tasks;
    data->elapsed = t_fin - t_ini;

    return NULL;
}

static int prepare_pool(SharedTaskPool* pool , const TaskList* task_list , int num_threads){
    pool->task_list = task_list;
    pool->next_task = 0;

    if (pthread_mutex_init(&pool->mutex , NULL) != 0){
        fprintf(stderr , "Error: no se pudo inicializar el mutex.\n");
        return 0;
    }

    if (pthread_barrier_init(&pool->start_barrier , NULL , (unsigned int) num_threads + 1U) != 0){
        fprintf(stderr , "Error: no se pudo inicializar la barrera.\n");
        pthread_mutex_destroy(&pool->mutex);
        return 0;
    }

    return 1;
}

static void destroy_pool(SharedTaskPool* pool){
    pthread_barrier_destroy(&pool->start_barrier);
    pthread_mutex_destroy(&pool->mutex);
}

static void prepare_thread_data(ThreadData* thread_data , int num_threads , SharedTaskPool* pool){
    for (int i = 0; i < num_threads; i++){
        thread_data[i].tid = i;
        thread_data[i].pool = pool;
        reset_result(&thread_data[i].result);
        thread_data[i].tasks_done = 0;
        thread_data[i].elapsed = 0.0;
    }
}

static int create_workers(pthread_t* threads , ThreadData* thread_data , int num_threads){
    for (int i = 0; i < num_threads; i++){
        if (pthread_create(&threads[i] , NULL , worker_function , &thread_data[i]) != 0){
            fprintf(stderr , "Error: no se pudo crear el hilo %d.\n" , i);
            return 0;
        }
    }

    return 1;
}

static int join_workers(pthread_t* threads , int num_threads){
    int ok = 1;

    for (int i = 0; i < num_threads; i++){
        if (pthread_join(threads[i] , NULL) != 0){
            fprintf(stderr , "Error: no se pudo esperar al hilo %d.\n" , i);
            ok = 0;
        }
    }

    return ok;
}

static void reduce_thread_results(const ThreadData* thread_data , int num_threads , NQueenResult* result){
    reset_result(result);

    for (int i = 0; i < num_threads; i++){
        result->count8 += thread_data[i].result.count8;
        result->count4 += thread_data[i].result.count4;
        result->count2 += thread_data[i].result.count2;
    }

    finalize_result(result);
}

static double max_thread_time(const ThreadData* thread_data , int num_threads){
    double max_time = 0.0;

    for (int i = 0; i < num_threads; i++){
        if (thread_data[i].elapsed > max_time){
            max_time = thread_data[i].elapsed;
        }
    }

    return max_time;
}

static double avg_thread_time(const ThreadData* thread_data , int num_threads){
    double total_time = 0.0;

    for (int i = 0; i < num_threads; i++){
        total_time += thread_data[i].elapsed;
    }

    return total_time / (double) num_threads;
}

static int total_tasks_done_threads(const ThreadData* thread_data , int num_threads){
    int total = 0;

    for (int i = 0; i < num_threads; i++){
        total += thread_data[i].tasks_done;
    }

    return total;
}

static int parse_args(int argc , char** argv , int rank , int* n , int* num_threads){
    if (argc != 3){
        if (rank == 0){
            fprintf(stderr , "Uso: %s N nroHilosPorProceso\n" , argv[0]);
        }
        return 0;
    }

    *n = atoi(argv[1]);
    *num_threads = atoi(argv[2]);

    if (*n < MINSIZE || *n > MAXSIZE){
        if (rank == 0){
            fprintf(stderr , "Error: N debe estar entre %d y %d.\n" , MINSIZE , MAXSIZE);
        }
        return 0;
    }

    if (*num_threads < 1){
        if (rank == 0){
            fprintf(stderr , "Error: nroHilosPorProceso debe ser mayor o igual a 1.\n");
        }
        return 0;
    }

    return 1;
}

int main(int argc , char** argv){
    int rank;
    int world_size;
    int provided;
    int n;
    int num_threads;

    TaskList global_task_list;
    TaskList local_task_list;
    NQueenResult local_result;
    NQueenResult global_result;

    SharedTaskPool pool;
    pthread_t* threads = NULL;
    ThreadData* thread_data = NULL;

    unsigned long long local_counts[3] = {0ULL, 0ULL, 0ULL};
    unsigned long long global_counts[3] = {0ULL, 0ULL, 0ULL};

    int local_tasks_done = 0;
    int global_tasks_done = 0;

    double t_ini;
    double t_after_local;
    double t_fin;
    double local_compute_time;
    double local_total_time;

    double local_max_thread_time;
    double local_avg_thread_time;
    double local_thread_balance;

    double max_compute_time = 0.0;
    double sum_compute_time = 0.0;
    double max_total_time = 0.0;
    double balance_mpi = 1.0;

    double sum_thread_balance = 0.0;
    double min_thread_balance = 1.0;
    double avg_thread_balance = 1.0;

    int processing_units;

    MPI_Init_thread(&argc , &argv , MPI_THREAD_FUNNELED , &provided);
    MPI_Comm_rank(MPI_COMM_WORLD , &rank);
    MPI_Comm_size(MPI_COMM_WORLD , &world_size);

    if (provided < MPI_THREAD_FUNNELED){
        if (rank == 0){
            fprintf(stderr , "Error: la implementacion MPI no soporta MPI_THREAD_FUNNELED.\n");
        }
        MPI_Abort(MPI_COMM_WORLD , EXIT_FAILURE);
    }

    if (!parse_args(argc , argv , rank , &n , &num_threads)){
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    /* Generacion global y filtrado por rank fuera de la medicion. */
    if (!generate_tasks(n , &global_task_list)){
        if (rank == 0){
            fprintf(stderr , "Error: cantidad maxima de tareas insuficiente.\n");
        }
        MPI_Abort(MPI_COMM_WORLD , EXIT_FAILURE);
    }

    if (!build_local_task_list(&global_task_list , rank , world_size , &local_task_list)){
        if (rank == 0){
            fprintf(stderr , "Error: no se pudo construir la lista local de tareas.\n");
        }
        MPI_Abort(MPI_COMM_WORLD , EXIT_FAILURE);
    }

    /* Reserva e inicializacion auxiliar fuera de la medicion. */
    threads = (pthread_t*) malloc((size_t) num_threads * sizeof(pthread_t));
    thread_data = (ThreadData*) malloc((size_t) num_threads * sizeof(ThreadData));
    if (threads == NULL || thread_data == NULL){
        fprintf(stderr , "Rank %d: error reservando memoria para Pthreads.\n" , rank);
        free(threads);
        free(thread_data);
        MPI_Abort(MPI_COMM_WORLD , EXIT_FAILURE);
    }

    if (!prepare_pool(&pool , &local_task_list , num_threads)){
        free(threads);
        free(thread_data);
        MPI_Abort(MPI_COMM_WORLD , EXIT_FAILURE);
    }

    prepare_thread_data(thread_data , num_threads , &pool);

    /* Los hilos se crean fuera de la medicion y quedan detenidos en la barrera local. */
    if (!create_workers(threads , thread_data , num_threads)){
        MPI_Abort(MPI_COMM_WORLD , EXIT_FAILURE);
    }

    reset_result(&global_result);

    /* Sincronizacion previa para iniciar la region neta en todos los procesos. */
    MPI_Barrier(MPI_COMM_WORLD);

    /* Medicion neta: Pthreads local + reduccion local + MPI_Reduce final. */
    t_ini = MPI_Wtime();

    pthread_barrier_wait(&pool.start_barrier);

    if (!join_workers(threads , num_threads)){
        MPI_Abort(MPI_COMM_WORLD , EXIT_FAILURE);
    }

    reduce_thread_results(thread_data , num_threads , &local_result);
    local_tasks_done = total_tasks_done_threads(thread_data , num_threads);

    t_after_local = MPI_Wtime();

    local_counts[0] = local_result.count8;
    local_counts[1] = local_result.count4;
    local_counts[2] = local_result.count2;

    MPI_Reduce(local_counts ,
               global_counts ,
               3 ,
               MPI_UNSIGNED_LONG_LONG ,
               MPI_SUM ,
               0 ,
               MPI_COMM_WORLD);

    t_fin = MPI_Wtime();

    local_compute_time = t_after_local - t_ini;
    local_total_time = t_fin - t_ini;

    /* Metricas auxiliares fuera del tiempo medido. */
    local_max_thread_time = max_thread_time(thread_data , num_threads);
    local_avg_thread_time = avg_thread_time(thread_data , num_threads);
    local_thread_balance = (local_max_thread_time > 0.0)
        ? (local_avg_thread_time / local_max_thread_time)
        : 1.0;

    MPI_Reduce(&local_compute_time , &max_compute_time , 1 , MPI_DOUBLE , MPI_MAX , 0 , MPI_COMM_WORLD);
    MPI_Reduce(&local_compute_time , &sum_compute_time , 1 , MPI_DOUBLE , MPI_SUM , 0 , MPI_COMM_WORLD);
    MPI_Reduce(&local_total_time , &max_total_time , 1 , MPI_DOUBLE , MPI_MAX , 0 , MPI_COMM_WORLD);
    MPI_Reduce(&local_tasks_done , &global_tasks_done , 1 , MPI_INT , MPI_SUM , 0 , MPI_COMM_WORLD);

    MPI_Reduce(&local_thread_balance , &sum_thread_balance , 1 , MPI_DOUBLE , MPI_SUM , 0 , MPI_COMM_WORLD);
    MPI_Reduce(&local_thread_balance , &min_thread_balance , 1 , MPI_DOUBLE , MPI_MIN , 0 , MPI_COMM_WORLD);

    if (rank == 0){
        global_result.count8 = global_counts[0];
        global_result.count4 = global_counts[1];
        global_result.count2 = global_counts[2];
        finalize_result(&global_result);

        if (max_compute_time > 0.0){
            balance_mpi = (sum_compute_time / (double) world_size) / max_compute_time;
        }

        avg_thread_balance = sum_thread_balance / (double) world_size;
        processing_units = world_size * num_threads;

        printf("N=%d MPI_Processes=%d ThreadsPerProcess=%d UP=%d Tasks=%d TasksDone=%d "
               "Total=%llu Unique=%llu Tiempo=%f AvgProcCompute=%f MaxProcCompute=%f "
               "BalanceMPI=%f AvgLocalThreadBalance=%f MinLocalThreadBalance=%f\n" ,
               n ,
               world_size ,
               num_threads ,
               processing_units ,
               global_task_list.count ,
               global_tasks_done ,
               global_result.total ,
               global_result.unique ,
               max_total_time ,
               sum_compute_time / (double) world_size ,
               max_compute_time ,
               balance_mpi ,
               avg_thread_balance ,
               min_thread_balance);
    }

    destroy_pool(&pool);
    free(threads);
    free(thread_data);

    MPI_Finalize();
    return EXIT_SUCCESS;
}
