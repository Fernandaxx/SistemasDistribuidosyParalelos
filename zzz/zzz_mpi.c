#include <mpi.h>
#include <nreinas_pthreads.h>
#include <stdio.h>

#define MAXSIZE 24
#define MINSIZE 2
#define MAX_GLOBAL_TASKS 5000

int SIZE, SIZEE;
int TOPBIT, MASK, SIDEMASK;
int T;

// Rank 0 Master Pool
Task global_task_pool[MAX_GLOBAL_TASKS];
int total_tasks_generated = 0;
int next_task_to_dispatch = 0;
pthread_mutex_t master_pool_lock; 
ResultTotals master_grand_totals = {0,0,0};
int temp_board[MAXSIZE];

// prototipos
double dwalltime();
static void mpi_function(int rank);

int main(int argc, char* argv[]) {
    int rank;
    int provided;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (argc < 3) {
        if (rank == 0) printf("Se deben pasar los parámetros N y T.");
        MPI_Finalize();
        return 1;
    }

    // Cada proceso inicializa sus constantes
    SIZE = atoi(argv[1]);
    SIZEE  = SIZE - 1;
    TOPBIT = 1 << SIZEE;
    MASK   = (1 << SIZE) - 1;
    SIDEMASK = TOPBIT | 1;
    T = atoi(argv[2]);

    if (rank==0) {
        double tInicio = dwalltime();
        f0();
        double tFin = dwalltime();
        printf("Número de resultados: %lu -  Tiempo Total: %f segundos \n", TOTAL, tFin-tIni);
    
    } else {
        f1();
    }

    MPI_Finalize();
    return 0;
}

double dwalltime() {
	double sec;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	sec = tv.tv_sec + tv.tv_usec/1000000.0;
	return sec;
}

/**********************************************/
/*        Funciones para los procesos         */
/**********************************************/
static void f0() {
    temp_board[0] = 1; 
        for (int b1=2; b1<SIZEE; b1++) {
            temp_board[1] = 1 << b1;
            GenerateTasks1(2, (2 | (1<<b1))<<1, 1 | (1<<b1), (1<<b1)>>1, b1);
        }

        int lastm = TOPBIT | 1;
        int endb = TOPBIT >> 1;
        for (int b1=1, b2=SIZE-2; b1<b2; b1++, b2--) {
            temp_board[0] = 1 << b1;
            GenerateTasks2(1, (1<<b1)<<1, (1<<b1), (1<<b1)>>1, b1, b2, lastm, endb);
            lastm |= lastm>>1 | lastm<<1;
            endb >>= 1;
        }

        printf("Rank 0: Total Tasks Generated: %d\n", total_tasks_generated);

        pthread_mutex_init(&master_pool_lock, NULL);
        pthread_t my_local_worker;
        pthread_create(&my_local_worker, NULL, rank0_local_worker_manager, NULL);

        int active_remote_workers = num_nodes - 1;
        
        while (active_remote_workers > 0) {
            MPI_Status status;
            int request_dummy;
            MPI_Recv(&request_dummy, 1, MPI_INT, MPI_ANY_SOURCE, TAG_REQUEST, MPI_COMM_WORLD, &status);
            int remote_node = status.MPI_SOURCE;

            Task mpi_batch[BATCH_SIZE];
            int tasks_grabbed = 0;
            
            pthread_mutex_lock(&master_pool_lock);
            while (tasks_grabbed < BATCH_SIZE && next_task_to_dispatch < total_tasks_generated) {
                mpi_batch[tasks_grabbed++] = global_task_pool[next_task_to_dispatch++];
            }
            pthread_mutex_unlock(&master_pool_lock);

            MPI_Send(mpi_batch, tasks_grabbed * sizeof(Task), MPI_BYTE, remote_node, TAG_WORK, MPI_COMM_WORLD);
            if (tasks_grabbed == 0) active_remote_workers--; 
        }

        pthread_join(my_local_worker, NULL);

        // Wait for all other nodes to send back their final totals
        for (int i = 1; i < num_nodes; i++) {
            ResultTotals remote_totals;
            MPI_Recv(&remote_totals, sizeof(ResultTotals), MPI_BYTE, i, TAG_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            master_grand_totals.count8 += remote_totals.count8;
            master_grand_totals.count4 += remote_totals.count4;
            master_grand_totals.count2 += remote_totals.count2;
        }

        long long TOTAL = (master_grand_totals.count8 * 8) + 
                          (master_grand_totals.count4 * 4) + 
                          (master_grand_totals.count2 * 2);
}
static void fN(){
    ResultTotals node_totals = {0,0,0};
    int request_msg = 1;

    while (1) {
        MPI_Send(&request_msg, 1, MPI_INT, 0, TAG_REQUEST, MPI_COMM_WORLD);

        Task my_batch[BATCH_SIZE];
        MPI_Status status;
        MPI_Recv(my_batch, BATCH_SIZE * sizeof(Task), MPI_BYTE, 0, TAG_WORK, MPI_COMM_WORLD, &status);

        int bytes_received;
        MPI_Get_count(&status, MPI_BYTE, &bytes_received);
        int tasks_received = bytes_received / sizeof(Task);

        if (tasks_received == 0) break; // Poison Pill

        // Send to modular Pthreads engine (Using all 8 cores)
        ResultTotals batch_res = solve_task_batch_with_pthreads(my_batch, tasks_received, 8);

        node_totals.count8 += batch_res.count8;
        node_totals.count4 += batch_res.count4;
        node_totals.count2 += batch_res.count2;
    }

    MPI_Send(&node_totals, sizeof(ResultTotals), MPI_BYTE, 0, TAG_RESULT, MPI_COMM_WORLD);
}
// Background thread so Rank 0 computes its own share dynamically
void* rank0_local_worker_manager(void* arg) {
    while(1) {
        Task local_batch[BATCH_SIZE];
        int tasks_grabbed = 0;

        pthread_mutex_lock(&master_pool_lock);
        while (tasks_grabbed < BATCH_SIZE && next_task_to_dispatch < total_tasks_generated) {
            local_batch[tasks_grabbed++] = global_task_pool[next_task_to_dispatch++];
        }
        pthread_mutex_unlock(&master_pool_lock);

        if (tasks_grabbed == 0) break; // Finished!

        // Pass to the modular Pthreads engine! (Using 7 threads locally)
        ResultTotals res = solve_task_batch_with_pthreads(local_batch, tasks_grabbed, 7);

        pthread_mutex_lock(&master_pool_lock);
        master_grand_totals.count8 += res.count8;
        master_grand_totals.count4 += res.count4;
        master_grand_totals.count2 += res.count2;
        pthread_mutex_unlock(&master_pool_lock);
    }
    return NULL;
}

/**********************************************/
/*    Generación de tareas (las usa rank0)    */
/**********************************************/
void GenerateTasks1(int y, int left, int down, int right, int bound1) {
    if (y == 3) {
        Task t = {1, y, left, down, right, bound1, 0, 0, 0};
        for(int i=0; i<y; i++) t.initial_board[i] = temp_board[i];
        global_task_pool[total_tasks_generated++] = t;
        return;
    }
    int bitmap = MASK & ~(left | down | right);
    if (y < bound1) { bitmap |= 2; bitmap ^= 2; }
    int bit;
    while (bitmap) {
        bitmap ^= temp_board[y] = bit = -bitmap & bitmap;
        GenerateTasks1(y+1, (left | bit)<<1, down | bit, (right | bit)>>1, bound1);
    }
}
void GenerateTasks2(int y, int left, int down, int right, int bound1, int bound2, int lastmask, int endbit) {
    if (y == 3) {
        Task t = {0, y, left, down, right, bound1, bound2, lastmask, endbit};
        for(int i=0; i<y; i++) t.initial_board[i] = temp_board[i];
        global_task_pool[total_tasks_generated++] = t;
        return;
    }
    int bitmap = MASK & ~(left | down | right);
    if (y < bound1) { bitmap |= SIDEMASK; bitmap ^= SIDEMASK; }
    else if (y == bound2) {
        if (!(down & SIDEMASK)) return;
        if ((down & SIDEMASK) != SIDEMASK) bitmap &= SIDEMASK;
    }
    int bit;
    while (bitmap) {
        bitmap ^= temp_board[y] = bit = -bitmap & bitmap;
        GenerateTasks2(y+1, (left | bit)<<1, down | bit, (right | bit)>>1, bound1, bound2, lastmask, endbit);
    }
}
