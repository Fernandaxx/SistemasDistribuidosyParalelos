#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "nreinas_pthreads.h"

// --- ESTRUCTURAS DE LA COLA DINÁMICA ---
typedef struct Node {
    Task task;
    struct Node* next;
} Node;

typedef struct {
    Node* head;
    Node* tail;
    pthread_mutex_t lock;
} TaskQueue;

typedef struct {
    int board[MAXSIZE];
    long long c8, c4, c2;
} ThreadState;

typedef struct {
    TaskQueue* shared_queue;
    ResultTotals* thread_results; 
} PthreadContext;

// --- FUNCIONES DE LA COLA ---
void queue_init(TaskQueue* q) {
    q->head = NULL;
    q->tail = NULL;
    pthread_mutex_init(&(q->lock), NULL);
}

void queue_push(TaskQueue* q, Task t) {
    Node* new_node = (Node*)malloc(sizeof(Node));
    new_node->task = t;
    new_node->next = NULL;

    pthread_mutex_lock(&(q->lock));
    if (q->tail == NULL) {
        q->head = new_node;
        q->tail = new_node;
    } else {
        q->tail->next = new_node;
        q->tail = new_node;
    }
    pthread_mutex_unlock(&(q->lock));
}

int queue_pop(TaskQueue* q, Task* out_task) {
    pthread_mutex_lock(&(q->lock));
    if (q->head == NULL) {
        pthread_mutex_unlock(&(q->lock));
        return 0; 
    }
    Node* temp = q->head;
    *out_task = temp->task; 
    q->head = q->head->next;
    if (q->head == NULL) q->tail = NULL; 
    pthread_mutex_unlock(&(q->lock));
    
    free(temp); 
    return 1;
}

// --- MATEMÁTICAS DE TAKAKEN ---
void Check(ThreadState *state, Task *t) {
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

void Backtrack2(int y, int left, int down, int right, Task *t, ThreadState *state) {
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

void Backtrack1(int y, int left, int down, int right, Task *t, ThreadState *state) {
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

// --- LÓGICA DE LOS HILOS ---
void* thread_worker_logic(void* arg) {
    PthreadContext* ctx = (PthreadContext*)arg;
    ThreadState my_state = { {0}, 0, 0, 0 };
    Task current_task;

    while (queue_pop(ctx->shared_queue, &current_task)) {
        for (int row = 0; row < current_task.y; row++) {
            my_state.board[row] = current_task.initial_board[row];
        }
        if (current_task.is_backtrack1) {
            Backtrack1(current_task.y, current_task.left, current_task.down, current_task.right, &current_task, &my_state);
        } else {
            Backtrack2(current_task.y, current_task.left, current_task.down, current_task.right, &current_task, &my_state);
        }
    }

    ctx->thread_results->count8 = my_state.c8;
    ctx->thread_results->count4 = my_state.c4;
    ctx->thread_results->count2 = my_state.c2;
    return NULL;
}

// --- API PÚBLICA ---
ResultTotals solve_task_batch_with_pthreads(Task* batch, int num_tasks, int num_threads) {
    pthread_t threads[num_threads];
    PthreadContext contexts[num_threads];
    ResultTotals individual_results[num_threads];
    TaskQueue my_queue;
    
    queue_init(&my_queue);
    for (int i = 0; i < num_tasks; i++) {
        queue_push(&my_queue, batch[i]); 
    }

    for (int i = 0; i < num_threads; i++) {
        contexts[i].shared_queue = &my_queue;
        contexts[i].thread_results = &individual_results[i]; 
        individual_results[i] = (ResultTotals){0, 0, 0}; 
        pthread_create(&threads[i], NULL, thread_worker_logic, &contexts[i]);
    }

    ResultTotals batch_totals = {0, 0, 0};
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
        batch_totals.count8 += individual_results[i].count8;
        batch_totals.count4 += individual_results[i].count4;
        batch_totals.count2 += individual_results[i].count2;
    }

    pthread_mutex_destroy(&(my_queue.lock));
    return batch_totals;
}