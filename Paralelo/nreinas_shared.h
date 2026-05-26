#ifndef NREINAS_SHARED_H
#define NREINAS_SHARED_H

#include <pthread.h>

#define MAXSIZE 24
#define BATCH_SIZE 40
#define MAX_GLOBAL_TASKS 5000
#define MAX_THREADS 16

extern int SIZE , SIZEE;
extern int TOPBIT , MASK , SIDEMASK;

typedef struct{
    int is_backtrack1;
    int y , left , down , right;
    int bound1 , bound2;
    int lastmask , endbit;
    int initial_board[MAXSIZE];
} task_t;

typedef struct{
    long long count8;
    long long count4;
    long long count2;
} results_t;

/* Pool global de tareas usado por el rank 0. */
extern task_t global_task_pool[MAX_GLOBAL_TASKS];
extern int total_tasks_generated;
extern int next_task_to_dispatch;
extern pthread_mutex_t pool_mutex;

/* Funciones implementadas con Pthreads. */
void CrearHilosLocales(int num_threads , pthread_t* threads , results_t* thread_results);
void CrearHilosRemotos(int num_threads , pthread_t* threads , results_t* thread_results);
void LiberarHilosLocales(void);
void RepartirLoteRemoto(task_t* batch , int num_tasks);
void DetenerHilosRemotos(void);

#endif