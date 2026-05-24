#include <nreinas_pthreads.h> 

par_t shared_pars;

// 1. El Nodo para la lista enlazada
typedef struct Node {
    Task task;
    struct Node* next;
} Node;

// 2. La Cola protegida por un Mutex
typedef struct {
    Node* head;
    Node* tail;
    pthread_mutex_t lock;
} TaskQueue;

// Memoria aislada para cada hilo
typedef struct {
    int board[MAXSIZE];
    long long c8, c4, c2;
} ThreadState;

// El contexto que se pasa a cada hilo
typedef struct {
    TaskQueue* shared_queue;
    ResultTotals* thread_results; 
} PthreadContext;


ResultTotals solve_task_batch_with_pthreads(Task* batch, int num_tasks, int num_threads) {
    pthread_t threads[num_threads];
    PthreadContext contexts[num_threads];
    ResultTotals individual_results[num_threads];

    // 1. Inicializar y llenar la Cola
    TaskQueue my_queue;
    queue_init(&my_queue);
    
    for (int i = 0; i < num_tasks; i++) {
        queue_push(&my_queue, batch[i]); // Metemos todas las tareas del batch a la cola
    }

    // 2. Lanzar los hilos
    for (int i = 0; i < num_threads; i++) {
        contexts[i].shared_queue = &my_queue;
        contexts[i].thread_results = &individual_results[i]; 
        individual_results[i] = (ResultTotals){0, 0, 0}; 

        pthread_create(&threads[i], NULL, thread_worker_logic, &contexts[i]);
    }

    // 3. Esperar a que terminen y sumar los resultados
    ResultTotals batch_totals = {0, 0, 0};
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
        batch_totals.count8 += individual_results[i].count8;
        batch_totals.count4 += individual_results[i].count4;
        batch_totals.count2 += individual_results[i].count2;
    }

    // 4. Limpieza
    pthread_mutex_destroy(&(my_queue.lock));

    return batch_totals;
}


void* thread_worker_logic(void* arg) {
    PthreadContext* ctx = (PthreadContext*)arg;
    ThreadState my_state = { .c8 = 0, .c4 = 0, .c2 = 0 };
    Task current_task;

    // El hilo intenta extraer una tarea de la cola. 
    // Si retorna 0, significa que la cola está vacía y el ciclo termina.
    while (queue_pop(ctx->shared_queue, &current_task)) {
        
        // Copiamos el tablero inicial de la tarea
        for (int row = 0; row < current_task.y; row++) {
            my_state.board[row] = current_task.initial_board[row];
        }

        // Ejecutamos la rama correspondiente de Takaken
        if (current_task.is_backtrack1) {
            Backtrack1(current_task.y, current_task.left, current_task.down, current_task.right, &current_task, &my_state);
        } else {
            Backtrack2(current_task.y, current_task.left, current_task.down, current_task.right, &current_task, &my_state);
        }
    }

    // Guardamos los resultados finales de este hilo
    ctx->thread_results->count8 = my_state.c8;
    ctx->thread_results->count4 = my_state.c4;
    ctx->thread_results->count2 = my_state.c2;
    pthread_exit(NULL);
}


/**********************************************/
/*     Funciones de Takaken (modificadas)     */
/**********************************************/
void Backtrack1(int y, int left, int down, int right)
{
    int  bitmap, bit;

    bitmap = MASK & ~(left | down | right); // Pone 0s en MASK en las posiciones donde left down y right tienen 1s
    if (y == SIZEE) {
        if (bitmap) {
            BOARD[y] = bitmap;
            COUNT8++; // 4 rotaciones de la solucion y de la solucion espejada
            //Display();
        }
    } else {
        if (y < BOUND1) { // BOUND1 es la posicion donde esta la reina
            bitmap |= 2;
            bitmap ^= 2;
            // bitmap &= ~2
        }
       while (bitmap) {
            bitmap ^= BOARD[y] = bit = -bitmap & bitmap; // Agarra el bit menos significativo que este en 1, lo pone en BOARD[y] y lo saca de bitmap
            Backtrack1(y+1, (left | bit)<<1, down | bit, (right | bit)>>1);
        }
    }
}
void Backtrack2(int y, int left, int down, int right)
{
    int  bitmap, bit;

    bitmap = MASK & ~(left | down | right);
    if (y == SIZEE) {
        if (bitmap) {
            if (!(bitmap & LASTMASK)) {
                BOARD[y] = bitmap;
                Check();
            }
        }
    } else {
        if (y < BOUND1) {
            bitmap |= SIDEMASK;
            bitmap ^= SIDEMASK;
            // bitmap = 0xxxxxx0
        } else if (y == BOUND2) {
            if (!(down & SIDEMASK)) return;
            if ((down & SIDEMASK) != SIDEMASK) bitmap &= SIDEMASK;
        }
        while (bitmap) {
            bitmap ^= BOARD[y] = bit = -bitmap & bitmap;
            Backtrack2(y+1, (left | bit)<<1, down | bit, (right | bit)>>1);
        }
    }
}
void Check(void) {
    int  *own, *you, bit, ptn;

    /* 90-degree rotation */
    if (*BOARD2 == 1) {
        for (ptn=2,own=BOARD+1; own<=BOARDE; own++,ptn<<=1) {
            bit = 1;
            for (you=BOARDE; *you!=ptn && *own>=bit; you--)
                bit <<= 1;
            if (*own > bit) return;
            if (*own < bit) break;
        }
        if (own > BOARDE) {
            COUNT2++;
            //Display();
            return;
        }
    }

    /* 180-degree rotation */
    if (*BOARDE == ENDBIT) {
        for (you=BOARDE-1,own=BOARD+1; own<=BOARDE; own++,you--) {
            bit = 1;
            for (ptn=TOPBIT; ptn!=*you && *own>=bit; ptn>>=1)
                bit <<= 1;
            if (*own > bit) return;
            if (*own < bit) break;
        }
        if (own > BOARDE) {
            COUNT4++;
            //Display();
            return;
        }
    }

    /* 270-degree rotation */
    if (*BOARD1 == TOPBIT) {
        for (ptn=TOPBIT>>1,own=BOARD+1; own<=BOARDE; own++,ptn>>=1) {
            bit = 1;
            for (you=BOARD; *you!=ptn && *own>=bit; you++)
                bit <<= 1;
            if (*own > bit) return;
            if (*own < bit) break;
        }
    }
    COUNT8++;
    //Display();
}

/**********************************************/
/*    Operaciones de la cola (thread-safe)    */
/**********************************************/
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
        return 0; // La cola está vacía
    }

    Node* temp = q->head;
    *out_task = temp->task; // Copiamos la tarea

    q->head = q->head->next;
    if (q->head == NULL) {
        q->tail = NULL; // Si sacamos el último elemento, la cola queda vacía
    }

    pthread_mutex_unlock(&(q->lock));
    
    free(temp); // Liberamos la memoria del nodo extraído
    return 1;
}