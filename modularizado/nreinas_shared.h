#ifndef NREINAS_SHARED_H
#define NREINAS_SHARED_H

#define MAXSIZE 24
#define BATCH_SIZE 40 // Tareas enviadas por cada mensaje MPI

// Variables globales de matemáticas (Inicializadas por cada proceso MPI)
extern int SIZE, SIZEE;
extern int TOPBIT, MASK, SIDEMASK;

// La "Caja" de variables que se envía por red y a los hilos
typedef struct {
    int is_backtrack1;
    int y, left, down, right;
    int bound1, bound2;
    int lastmask, endbit;
    int initial_board[MAXSIZE];
} task_t;

// Contenedor de resultados
typedef struct {
    long long count8;
    long long count4;
    long long count2;
} results_t;

// La única función pública del módulo de hilos
results_t pthreads_batch(task_t* batch, int num_tasks, int num_threads);

#endif