#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "nreinas_shared.h"

// --- Shared State for Remote Workers (Rank N) ---
static task_t shared_batch[BATCH_SIZE];
static int shared_batch_size = 0;
static int shared_batch_idx = 0;
static int tasks_completed = 0;
static int time_to_die = 0;

static pthread_mutex_t batch_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t work_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t done_cond = PTHREAD_COND_INITIALIZER;

typedef struct {
    int board[MAXSIZE];
    long long c8, c4, c2;
} ThreadState;

// Forward declarations for Takaken algorithms
void Check(ThreadState *state, task_t *t);
void Backtrack2(int y, int left, int down, int right, task_t *t, ThreadState *state);
void Backtrack1(int y, int left, int down, int right, task_t *t, ThreadState *state);

// Wrapper to process a single task
void process_task(task_t* task, ThreadState* state) {
    for (int row = 0; row < task->y; row++) {
        state->board[row] = task->initial_board[row];
    }
    if (task->is_backtrack1) {
        Backtrack1(task->y, task->left, task->down, task->right, task, state);
    } else {
        Backtrack2(task->y, task->left, task->down, task->right, task, state);
    }
}

// =========================================================================
// RANK 0: LOCAL WORKERS
// =========================================================================
void* local_worker_func(void* arg) {
    results_t* my_results = (results_t*)arg;
    ThreadState my_state = { {0}, 0, 0, 0 };

    while (1) {
        pthread_mutex_lock(&pool_mutex);
        if (next_task_to_dispatch >= total_tasks_generated) {
            pthread_mutex_unlock(&pool_mutex);
            break; // No more work in the global pool
        }
        task_t t = global_task_pool[next_task_to_dispatch++];
        pthread_mutex_unlock(&pool_mutex);

        process_task(&t, &my_state);
    }

    my_results->count8 = my_state.c8;
    my_results->count4 = my_state.c4;
    my_results->count2 = my_state.c2;
    return NULL;
}

void start_local_workers(int num_threads, pthread_t* threads, results_t* thread_results) {
    for (int i = 0; i < num_threads; i++) {
        thread_results[i] = (results_t){0, 0, 0};
        pthread_create(&threads[i], NULL, local_worker_func, &thread_results[i]);
    }
}

// =========================================================================
// RANK N: REMOTE WORKERS (Thread Pool with Condition Variables)
// =========================================================================
void* remote_worker_func(void* arg) {
    results_t* my_results = (results_t*)arg;
    ThreadState my_state = { {0}, 0, 0, 0 };

    while (1) {
        pthread_mutex_lock(&batch_mutex);
        while (shared_batch_idx >= shared_batch_size && !time_to_die) {
            pthread_cond_wait(&work_cond, &batch_mutex);
        }
        if (time_to_die) {
            pthread_mutex_unlock(&batch_mutex);
            break;
        }
        
        task_t t = shared_batch[shared_batch_idx++];
        pthread_mutex_unlock(&batch_mutex);

        process_task(&t, &my_state);

        pthread_mutex_lock(&batch_mutex);
        tasks_completed++;
        if (tasks_completed == shared_batch_size) {
            pthread_cond_signal(&done_cond); // Signal main thread that batch is done
        }
        pthread_mutex_unlock(&batch_mutex);
    }

    my_results->count8 = my_state.c8;
    my_results->count4 = my_state.c4;
    my_results->count2 = my_state.c2;
    return NULL;
}

void start_remote_workers(int num_threads, pthread_t* threads, results_t* thread_results) {
    for (int i = 0; i < num_threads; i++) {
        thread_results[i] = (results_t){0, 0, 0};
        pthread_create(&threads[i], NULL, remote_worker_func, &thread_results[i]);
    }
}

void feed_remote_batch(task_t* batch, int num_tasks) {
    pthread_mutex_lock(&batch_mutex);
    for (int i = 0; i < num_tasks; i++) {
        shared_batch[i] = batch[i];
    }
    shared_batch_size = num_tasks;
    shared_batch_idx = 0;
    tasks_completed = 0;
    
    pthread_cond_broadcast(&work_cond); // Wake up workers
    
    while (tasks_completed < shared_batch_size) {
        pthread_cond_wait(&done_cond, &batch_mutex); // Main thread waits for batch completion
    }
    pthread_mutex_unlock(&batch_mutex);
}

void terminate_remote_workers(void) {
    pthread_mutex_lock(&batch_mutex);
    time_to_die = 1;
    pthread_cond_broadcast(&work_cond);
    pthread_mutex_unlock(&batch_mutex);
}

// =========================================================================
// TAKAKEN BACKTRACKING ALGORITHMS
// =========================================================================
void Check(ThreadState *state, task_t *t) {
    int *BOARD = state->board;
    int *BOARDE = &BOARD[SIZEE];
    int *BOARD1 = &BOARD[t->bound1];
    int *BOARD2 = &BOARD[t->bound2];
    int *own, *you, bit, ptn;

    if (*BOARD2 == 1) {
        for (ptn=2,own=BOARD+1; own<=BOARDE; own++,ptn<<=1) {
            bit = 1;
            for (you=BOARDE; *you!=ptn && *own>=bit; you--) bit <<= 1;
            if (*own > bit) return;
            if (*own < bit) break;
        }
        if (own > BOARDE) { state->c2++; return; }
    }
    if (*BOARDE == t->endbit) {
        for (you=BOARDE-1,own=BOARD+1; own<=BOARDE; own++,you--) {
            bit = 1;
            for (ptn=TOPBIT; ptn!=*you && *own>=bit; ptn>>=1) bit <<= 1;
            if (*own > bit) return;
            if (*own < bit) break;
        }
        if (own > BOARDE) { state->c4++; return; }
    }
    if (*BOARD1 == TOPBIT) {
        for (ptn=TOPBIT>>1,own=BOARD+1; own<=BOARDE; own++,ptn>>=1) {
            bit = 1;
            for (you=BOARD; *you!=ptn && *own>=bit; you++) bit <<= 1;
            if (*own > bit) return;
            if (*own < bit) break;
        }
    }
    state->c8++;
}

void Backtrack2(int y, int left, int down, int right, task_t *t, ThreadState *state) {
    int bitmap = MASK & ~(left | down | right);
    int bit;
    if (y == SIZEE) {
        if (bitmap && !(bitmap & t->lastmask)) {
            state->board[y] = bitmap;
            Check(state, t);
        }
    } else {
        if (y < t->bound1) {
            bitmap |= SIDEMASK;
            bitmap ^= SIDEMASK;
        } else if (y == t->bound2) {
            if (!(down & SIDEMASK)) return;
            if ((down & SIDEMASK) != SIDEMASK) bitmap &= SIDEMASK;
        }
        while (bitmap) {
            bitmap ^= state->board[y] = bit = -bitmap & bitmap;
            Backtrack2(y+1, (left | bit)<<1, down | bit, (right | bit)>>1, t, state);
        }
    }
}

void Backtrack1(int y, int left, int down, int right, task_t *t, ThreadState *state) {
    int bitmap = MASK & ~(left | down | right);
    int bit;
    if (y == SIZEE) {
        if (bitmap) {
            state->board[y] = bitmap;
            state->c8++; 
        }
    } else {
        if (y < t->bound1) {
            bitmap |= 2;
            bitmap ^= 2;
        }
       while (bitmap) {
            bitmap ^= state->board[y] = bit = -bitmap & bitmap;
            Backtrack1(y+1, (left | bit)<<1, down | bit, (right | bit)>>1, t, state);
        }
    }
}