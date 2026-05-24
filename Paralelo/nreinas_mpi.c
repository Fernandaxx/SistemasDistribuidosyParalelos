/**************************************************************************/
/* N-Queens - Etapa 4: MPI con mapeo estatico ciclico                    */
/*                                                                          */
/* Objetivo de esta etapa:                                                  */
/* - Mantener el algoritmo secuencial de referencia.                         */
/* - Mantener la descomposicion en tareas de la Etapa 2.                     */
/* - Distribuir las tareas entre procesos MPI con mapeo estatico ciclico.    */
/* - Reducir los contadores parciales con comunicacion colectiva MPI.         */
/*                                                                          */
/* Esta version todavia NO usa Pthreads.                                     */
/**************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <mpi.h>

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

    unsigned long long count8;
    unsigned long long count4;
    unsigned long long count2;
    unsigned long long total;
    unsigned long long unique;
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
    unsigned long long count8;
    unsigned long long count4;
    unsigned long long count2;
    unsigned long long unique;
    unsigned long long total;
} NQueenResult;

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

    st->count8 = 0ULL;
    st->count4 = 0ULL;
    st->count2 = 0ULL;
    st->total = 0ULL;
    st->unique = 0ULL;

    for (int i = 0; i < MAXSIZE; i++) {
        st->board[i] = 0;
    }
}

static void reset_result(NQueenResult *res) {
    res->count8 = 0ULL;
    res->count4 = 0ULL;
    res->count2 = 0ULL;
    res->unique = 0ULL;
    res->total = 0ULL;
}

static void finalize_result(NQueenResult *res) {
    res->unique = res->count8 + res->count4 + res->count2;
    res->total = (res->count8 * 8ULL) + (res->count4 * 4ULL) + (res->count2 * 2ULL);
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

        st.count8 = st.count4 = st.count2 = 0ULL;
        st.unique = st.total = 0ULL;

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

        st.count8 = st.count4 = st.count2 = 0ULL;
        st.unique = st.total = 0ULL;

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
static void execute_task(const NQueenTask *task, NQueenResult *result) {
    NQueenState st = task->initial_state;

    if (task->type == TASK_BACKTRACK1) {
        backtrack1(&st, task->y, task->left, task->down, task->right);
    } else {
        backtrack2(&st, task->y, task->left, task->down, task->right);
    }

    result->count8 += st.count8;
    result->count4 += st.count4;
    result->count2 += st.count2;
}

/**********************************************/
/* Mapeo MPI estatico ciclico                 */
/**********************************************/
static void execute_tasks_static_cyclic(const TaskList *task_list,
                                        int rank,
                                        int world_size,
                                        NQueenResult *local_result,
                                        int *local_tasks_done) {
    reset_result(local_result);
    *local_tasks_done = 0;

    for (int i = rank; i < task_list->count; i += world_size) {
        execute_task(&task_list->tasks[i], local_result);
        (*local_tasks_done)++;
    }

    finalize_result(local_result);
}

static int parse_args(int argc, char **argv, int rank, int *n) {
    if (argc != 2) {
        if (rank == 0) {
            fprintf(stderr, "Uso: %s N\n", argv[0]);
        }
        return 0;
    }

    *n = atoi(argv[1]);
    if (*n < MINSIZE || *n > MAXSIZE) {
        if (rank == 0) {
            fprintf(stderr, "Error: N debe estar entre %d y %d.\n", MINSIZE, MAXSIZE);
        }
        return 0;
    }

    return 1;
}

int main(int argc, char **argv) {
    int rank;
    int world_size;
    int n;
    int local_tasks_done = 0;
    int total_tasks_done = 0;
    TaskList task_list;
    NQueenResult local_result;
    NQueenResult global_result;
    unsigned long long local_counts[3];
    unsigned long long global_counts[3];
    double t_ini;
    double t_after_compute;
    double t_fin;
    double local_compute_time;
    double local_total_time;
    double max_compute_time = 0.0;
    double sum_compute_time = 0.0;
    double max_total_time = 0.0;
    double balance_mpi = 1.0;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (!parse_args(argc, argv, rank, &n)) {
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    /* Generacion de tareas fuera de la medicion. Cada proceso genera la misma lista. */
    if (!generate_tasks(n, &task_list)) {
        if (rank == 0) {
            fprintf(stderr, "Error: cantidad maxima de tareas insuficiente.\n");
        }
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    reset_result(&global_result);

    /* Sincronizacion previa para alinear el inicio de la medicion. */
    MPI_Barrier(MPI_COMM_WORLD);

    /* Medicion neta: computo local + comunicacion/sincronizacion de MPI_Reduce. */
    t_ini = MPI_Wtime();

    execute_tasks_static_cyclic(&task_list, rank, world_size, &local_result, &local_tasks_done);
    t_after_compute = MPI_Wtime();

    local_counts[0] = local_result.count8;
    local_counts[1] = local_result.count4;
    local_counts[2] = local_result.count2;

    MPI_Reduce(local_counts,
               global_counts,
               3,
               MPI_UNSIGNED_LONG_LONG,
               MPI_SUM,
               0,
               MPI_COMM_WORLD);

    t_fin = MPI_Wtime();

    local_compute_time = t_after_compute - t_ini;
    local_total_time = t_fin - t_ini;

    /* Reducciones auxiliares para metricas: quedan fuera del tiempo medido. */
    MPI_Reduce(&local_compute_time, &max_compute_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_compute_time, &sum_compute_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_total_time, &max_total_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_tasks_done, &total_tasks_done, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        global_result.count8 = global_counts[0];
        global_result.count4 = global_counts[1];
        global_result.count2 = global_counts[2];
        finalize_result(&global_result);

        if (max_compute_time > 0.0) {
            balance_mpi = (sum_compute_time / (double)world_size) / max_compute_time;
        }

        printf("N=%d MPI_Processes=%d Tasks=%d TasksDone=%d Total=%llu Unique=%llu "
               "Tiempo=%f AvgProcCompute=%f MaxProcCompute=%f BalanceMPI=%f\n",
               n,
               world_size,
               task_list.count,
               total_tasks_done,
               global_result.total,
               global_result.unique,
               max_total_time,
               sum_compute_time / (double)world_size,
               max_compute_time,
               balance_mpi);
    }

    MPI_Finalize();
    return EXIT_SUCCESS;
}
