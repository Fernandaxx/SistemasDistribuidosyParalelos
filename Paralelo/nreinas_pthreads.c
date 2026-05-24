/**************************************************************************/
/* N-Queens - Etapa 3: Pthreads + Bag of Tasks local                     */
/*                                                                          */
/* Objetivo de esta etapa:                                                  */
/* - Mantener el algoritmo secuencial de referencia.                         */
/* - Mantener la descomposicion en tareas de la Etapa 2.                     */
/* - Ejecutar las tareas con Pthreads usando un pool local dinamico.          */
/* - Cada hilo acumula resultados locales y se reduce al finalizar.           */
/*                                                                          */
/* Esta version todavia NO usa MPI.                                          */
/**************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/time.h>

#define MAXSIZE 24
#define MINSIZE 2
#define MAX_TASKS (2 * MAXSIZE)

typedef enum {
    TASK_BACKTRACK1 = 1,
    TASK_BACKTRACK2 = 2
} TaskType;

typedef struct {
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

    uint64_t count8;
    uint64_t count4;
    uint64_t count2;
    uint64_t total;
    uint64_t unique;
} NQueenState;

typedef struct {
    TaskType type;
    int y;
    int left;
    int down;
    int right;
    NQueenState initial_state;
} NQueenTask;

typedef struct {
    NQueenTask tasks[MAX_TASKS];
    int count;
} TaskList;

typedef struct {
    uint64_t count8;
    uint64_t count4;
    uint64_t count2;
    uint64_t unique;
    uint64_t total;
} NQueenResult;

typedef struct {
    const TaskList *task_list;
    int next_task;
    pthread_mutex_t mutex;
    pthread_barrier_t start_barrier;
} SharedTaskPool;

typedef struct {
    int tid;
    SharedTaskPool *pool;
    NQueenResult result;
    int tasks_done;
    double elapsed;
} ThreadData;

static double dwalltime(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

static void init_state(NQueenState *st, int n) {
    st->size = n;
    st->sizee = n - 1;

    st->mask = (1 << n) - 1;
    st->topbit = 1 << st->sizee;

    st->sidemask = 0;
    st->lastmask = 0;
    st->endbit = 0;
    st->bound1 = 0;
    st->bound2 = 0;

    st->count8 = 0;
    st->count4 = 0;
    st->count2 = 0;
    st->total = 0;
    st->unique = 0;

    for (int i = 0; i < MAXSIZE; i++) {
        st->board[i] = 0;
    }
}

static void reset_result(NQueenResult *res) {
    res->count8 = 0;
    res->count4 = 0;
    res->count2 = 0;
    res->unique = 0;
    res->total = 0;
}

static void finalize_result(NQueenResult *res) {
    res->unique = res->count8 + res->count4 + res->count2;
    res->total = (res->count8 * 8) + (res->count4 * 4) + (res->count2 * 2);
}

/**********************************************/
/* Check Unique Solutions                     */
/**********************************************/
static void check_solution(NQueenState *st) {
    int *own;
    int *you;
    int bit;
    int ptn;

    int *board = st->board;
    int *boarde = &st->board[st->sizee];
    int *board1 = &st->board[st->bound1];
    int *board2 = &st->board[st->bound2];

    /* 90-degree rotation */
    if (*board2 == 1) {
        for (ptn = 2, own = board + 1; own <= boarde; own++, ptn <<= 1) {
            bit = 1;
            for (you = boarde; *you != ptn && *own >= bit; you--) {
                bit <<= 1;
            }
            if (*own > bit) return;
            if (*own < bit) break;
        }
        if (own > boarde) {
            st->count2++;
            return;
        }
    }

    /* 180-degree rotation */
    if (*boarde == st->endbit) {
        for (you = boarde - 1, own = board + 1; own <= boarde; own++, you--) {
            bit = 1;
            for (ptn = st->topbit; ptn != *you && *own >= bit; ptn >>= 1) {
                bit <<= 1;
            }
            if (*own > bit) return;
            if (*own < bit) break;
        }
        if (own > boarde) {
            st->count4++;
            return;
        }
    }

    /* 270-degree rotation */
    if (*board1 == st->topbit) {
        for (ptn = st->topbit >> 1, own = board + 1; own <= boarde; own++, ptn >>= 1) {
            bit = 1;
            for (you = board; *you != ptn && *own >= bit; you++) {
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
static void backtrack2(NQueenState *st, int y, int left, int down, int right) {
    int bitmap;
    int bit;

    bitmap = st->mask & ~(left | down | right);

    if (y == st->sizee) {
        if (bitmap) {
            if (!(bitmap & st->lastmask)) {
                st->board[y] = bitmap;
                check_solution(st);
            }
        }
    } else {
        if (y < st->bound1) {
            bitmap |= st->sidemask;
            bitmap ^= st->sidemask;
        } else if (y == st->bound2) {
            if (!(down & st->sidemask)) return;
            if ((down & st->sidemask) != st->sidemask) bitmap &= st->sidemask;
        }

        while (bitmap) {
            bit = -bitmap & bitmap;
            st->board[y] = bit;
            bitmap ^= bit;
            backtrack2(st, y + 1, (left | bit) << 1, down | bit, (right | bit) >> 1);
        }
    }
}

/**********************************************/
/* First queen is in the corner               */
/**********************************************/
static void backtrack1(NQueenState *st, int y, int left, int down, int right) {
    int bitmap;
    int bit;

    bitmap = st->mask & ~(left | down | right);

    if (y == st->sizee) {
        if (bitmap) {
            st->board[y] = bitmap;
            st->count8++;
        }
    } else {
        if (y < st->bound1) {
            bitmap |= 2;
            bitmap ^= 2;
        }

        while (bitmap) {
            bit = -bitmap & bitmap;
            st->board[y] = bit;
            bitmap ^= bit;
            backtrack1(st, y + 1, (left | bit) << 1, down | bit, (right | bit) >> 1);
        }
    }
}

static int add_task(TaskList *task_list,
                    TaskType type,
                    const NQueenState *initial_state,
                    int y,
                    int left,
                    int down,
                    int right) {
    if (task_list->count >= MAX_TASKS) {
        return 0;
    }

    NQueenTask *task = &task_list->tasks[task_list->count];
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
/* Generacion de tareas                       */
/**********************************************/
static int generate_tasks(int n, TaskList *task_list) {
    NQueenState st;
    int bit;

    task_list->count = 0;
    init_state(&st, n);

    /* Tareas equivalentes al primer ciclo de NQueens(): Backtrack1. */
    st.board[0] = 1;
    for (st.bound1 = 2; st.bound1 < st.sizee; st.bound1++) {
        bit = 1 << st.bound1;
        st.board[1] = bit;

        st.count8 = st.count4 = st.count2 = 0;
        st.unique = st.total = 0;

        if (!add_task(task_list,
                      TASK_BACKTRACK1,
                      &st,
                      2,
                      (2 | bit) << 1,
                      1 | bit,
                      bit >> 1)) {
            return 0;
        }
    }

    /* Tareas equivalentes al segundo ciclo de NQueens(): Backtrack2. */
    st.sidemask = st.topbit | 1;
    st.lastmask = st.sidemask;
    st.endbit = st.topbit >> 1;

    for (st.bound1 = 1, st.bound2 = st.size - 2;
         st.bound1 < st.bound2;
         st.bound1++, st.bound2--) {

        bit = 1 << st.bound1;
        st.board[0] = bit;
        st.board[1] = 0;

        st.count8 = st.count4 = st.count2 = 0;
        st.unique = st.total = 0;

        if (!add_task(task_list,
                      TASK_BACKTRACK2,
                      &st,
                      1,
                      bit << 1,
                      bit,
                      bit >> 1)) {
            return 0;
        }

        st.lastmask |= (st.lastmask >> 1) | (st.lastmask << 1);
        st.endbit >>= 1;
    }

    return 1;
}

/**********************************************/
/* Ejecucion secuencial de una tarea          */
/**********************************************/
static void execute_task(const NQueenTask *task, NQueenResult *global_result) {
    NQueenState st = task->initial_state;

    if (task->type == TASK_BACKTRACK1) {
        backtrack1(&st, task->y, task->left, task->down, task->right);
    } else {
        backtrack2(&st, task->y, task->left, task->down, task->right);
    }

    global_result->count8 += st.count8;
    global_result->count4 += st.count4;
    global_result->count2 += st.count2;
}

/**********************************************/
/* Worker Pthreads: toma dinamica de tareas   */
/**********************************************/
static void *worker_function(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    SharedTaskPool *pool = data->pool;
    NQueenResult local_result;
    int local_tasks = 0;
    double t_ini;
    double t_fin;

    /* Preparacion local fuera de la region cronometrada. */
    reset_result(&local_result);

    pthread_barrier_wait(&pool->start_barrier);

    t_ini = dwalltime();

    while (1) {
        int task_index;

        pthread_mutex_lock(&pool->mutex);
        task_index = pool->next_task;
        pool->next_task++;
        pthread_mutex_unlock(&pool->mutex);

        if (task_index >= pool->task_list->count) {
            break;
        }

        execute_task(&pool->task_list->tasks[task_index], &local_result);
        local_tasks++;
    }

    t_fin = dwalltime();

    data->result = local_result;
    data->tasks_done = local_tasks;
    data->elapsed = t_fin - t_ini;

    return NULL;
}

static int prepare_pool(SharedTaskPool *pool, const TaskList *task_list, int num_threads) {
    pool->task_list = task_list;
    pool->next_task = 0;

    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        fprintf(stderr, "Error: no se pudo inicializar el mutex.\n");
        return 0;
    }

    if (pthread_barrier_init(&pool->start_barrier, NULL, (unsigned int)num_threads + 1U) != 0) {
        fprintf(stderr, "Error: no se pudo inicializar la barrera.\n");
        pthread_mutex_destroy(&pool->mutex);
        return 0;
    }

    return 1;
}

static void destroy_pool(SharedTaskPool *pool) {
    pthread_barrier_destroy(&pool->start_barrier);
    pthread_mutex_destroy(&pool->mutex);
}

static void prepare_thread_data(ThreadData *thread_data, int num_threads, SharedTaskPool *pool) {
    for (int i = 0; i < num_threads; i++) {
        thread_data[i].tid = i;
        thread_data[i].pool = pool;
        reset_result(&thread_data[i].result);
        thread_data[i].tasks_done = 0;
        thread_data[i].elapsed = 0.0;
    }
}

static int create_workers(pthread_t *threads, ThreadData *thread_data, int num_threads) {
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, worker_function, &thread_data[i]) != 0) {
            fprintf(stderr, "Error: no se pudo crear el hilo %d.\n", i);
            return i;
        }
    }

    return num_threads;
}

static int join_workers(pthread_t *threads, int num_threads) {
    int ok = 1;

    for (int i = 0; i < num_threads; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            fprintf(stderr, "Error: no se pudo esperar al hilo %d.\n", i);
            ok = 0;
        }
    }

    return ok;
}

static void reduce_thread_results(const ThreadData *thread_data, int num_threads, NQueenResult *result) {
    reset_result(result);

    for (int i = 0; i < num_threads; i++) {
        result->count8 += thread_data[i].result.count8;
        result->count4 += thread_data[i].result.count4;
        result->count2 += thread_data[i].result.count2;
    }

    finalize_result(result);
}

static double max_thread_time(const ThreadData *thread_data, int num_threads) {
    double max_time = 0.0;

    for (int i = 0; i < num_threads; i++) {
        if (thread_data[i].elapsed > max_time) {
            max_time = thread_data[i].elapsed;
        }
    }

    return max_time;
}

static double avg_thread_time(const ThreadData *thread_data, int num_threads) {
    double total_time = 0.0;

    for (int i = 0; i < num_threads; i++) {
        total_time += thread_data[i].elapsed;
    }

    return total_time / (double)num_threads;
}

static int total_tasks_done(const ThreadData *thread_data, int num_threads) {
    int total = 0;

    for (int i = 0; i < num_threads; i++) {
        total += thread_data[i].tasks_done;
    }

    return total;
}

static int parse_args(int argc, char **argv, int *n, int *num_threads) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s N nroHilos\n", argv[0]);
        return 0;
    }

    *n = atoi(argv[1]);
    *num_threads = atoi(argv[2]);

    if (*n < MINSIZE || *n > MAXSIZE) {
        fprintf(stderr, "Error: N debe estar entre %d y %d.\n", MINSIZE, MAXSIZE);
        return 0;
    }

    if (*num_threads < 1) {
        fprintf(stderr, "Error: nroHilos debe ser mayor o igual a 1.\n");
        return 0;
    }

    return 1;
}

int main(int argc, char **argv) {
    int n;
    int num_threads;
    int created_threads;
    TaskList task_list;
    NQueenResult result;
    SharedTaskPool pool;
    pthread_t *threads = NULL;
    ThreadData *thread_data = NULL;
    double t_ini;
    double t_fin;
    double elapsed;
    double max_t;
    double avg_t;
    double balance;
    int tasks_done;

    if (!parse_args(argc, argv, &n, &num_threads)) {
        return EXIT_FAILURE;
    }

    /* Generacion de tareas fuera de la medicion. */
    if (!generate_tasks(n, &task_list)) {
        fprintf(stderr, "Error: cantidad maxima de tareas insuficiente.\n");
        return EXIT_FAILURE;
    }

    /* Reserva e inicializacion auxiliar fuera de la medicion. */
    threads = (pthread_t *)malloc((size_t)num_threads * sizeof(pthread_t));
    thread_data = (ThreadData *)malloc((size_t)num_threads * sizeof(ThreadData));
    if (threads == NULL || thread_data == NULL) {
        fprintf(stderr, "Error: no se pudo reservar memoria para Pthreads.\n");
        free(threads);
        free(thread_data);
        return EXIT_FAILURE;
    }

    if (!prepare_pool(&pool, &task_list, num_threads)) {
        free(threads);
        free(thread_data);
        return EXIT_FAILURE;
    }
    prepare_thread_data(thread_data, num_threads, &pool);

    /* Los hilos se crean fuera de la medicion y quedan bloqueados en la barrera. */
    created_threads = create_workers(threads, thread_data, num_threads);
    if (created_threads != num_threads) {
        pthread_barrier_wait(&pool.start_barrier);
        join_workers(threads, created_threads);
        destroy_pool(&pool);
        free(threads);
        free(thread_data);
        return EXIT_FAILURE;
    }

    /* Medicion neta: computo de backtracking + sincronizacion mutex/barrera/join + reduccion local. */
    t_ini = dwalltime();
    pthread_barrier_wait(&pool.start_barrier);

    if (!join_workers(threads, num_threads)) {
        destroy_pool(&pool);
        free(threads);
        free(thread_data);
        return EXIT_FAILURE;
    }

    reduce_thread_results(thread_data, num_threads, &result);
    t_fin = dwalltime();

    elapsed = t_fin - t_ini;
    max_t = max_thread_time(thread_data, num_threads);
    avg_t = avg_thread_time(thread_data, num_threads);
    balance = (max_t > 0.0) ? (avg_t / max_t) : 1.0;
    tasks_done = total_tasks_done(thread_data, num_threads);

    printf("N=%d Threads=%d Tasks=%d TasksDone=%d Total=%" PRIu64 " Unique=%" PRIu64
           " Tiempo=%f AvgThreadTime=%f MaxThreadTime=%f BalanceLocal=%f\n",
           n, num_threads, task_list.count, tasks_done, result.total, result.unique,
           elapsed, avg_t, max_t, balance);

    destroy_pool(&pool);
    free(threads);
    free(thread_data);

    return EXIT_SUCCESS;
}
