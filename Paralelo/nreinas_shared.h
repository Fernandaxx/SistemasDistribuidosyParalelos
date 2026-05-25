#ifndef NREINAS_SHARED_H
#define NREINAS_SHARED_H

#include <pthread.h>

#define MAXSIZE 24
#define BATCH_SIZE 40
#define MAX_GLOBAL_TASKS 5000
#define MAX_THREADS 16 // Covers the 8-core requirement

extern int SIZE, SIZEE;
extern int TOPBIT, MASK, SIDEMASK;

typedef struct {
    int is_backtrack1;
    int y, left, down, right;
    int bound1, bound2;
    int lastmask, endbit;
    int initial_board[MAXSIZE];
} task_t;

typedef struct {
    long long count8;
    long long count4;
    long long count2;
} results_t;

// --- Global Task Pool (Used by Rank 0) ---
extern task_t global_task_pool[MAX_GLOBAL_TASKS];
extern int total_tasks_generated;
extern int next_task_to_dispatch;
extern pthread_mutex_t pool_mutex;

// --- Thread Prototypes ---
void start_local_workers(int num_threads, pthread_t* threads, results_t* thread_results);
void start_remote_workers(int num_threads, pthread_t* threads, results_t* thread_results);
void feed_remote_batch(task_t* batch, int num_tasks);
void terminate_remote_workers(void);

#endif