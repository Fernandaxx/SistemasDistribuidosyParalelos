/**************************************************************************/
/* N-Queens refactor secuencial para preparar paralelizacion MPI+Pthreads  */
/* Basado en nreinas.c de referencia: se eliminan variables globales        */
/* compartidas y se encapsula el estado de busqueda en una estructura.      */
/**************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/time.h>

#define MAXSIZE 24
#define MINSIZE 2

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

/**********************************************/
/* Search of N-Queens                         */
/**********************************************/
static void solve_nqueens(NQueenState *st) {
    int bit;

    st->count8 = 0;
    st->count4 = 0;
    st->count2 = 0;
    st->total = 0;
    st->unique = 0;

    /* First queen is in the corner */
    st->board[0] = 1;
    for (st->bound1 = 2; st->bound1 < st->sizee; st->bound1++) {
        bit = 1 << st->bound1;
        st->board[1] = bit;
        backtrack1(st, 2, (2 | bit) << 1, 1 | bit, bit >> 1);
    }

    /* First queen is inside */
    st->sidemask = st->topbit | 1;
    st->lastmask = st->sidemask;
    st->endbit = st->topbit >> 1;

    for (st->bound1 = 1, st->bound2 = st->size - 2;
         st->bound1 < st->bound2;
         st->bound1++, st->bound2--) {

        bit = 1 << st->bound1;
        st->board[0] = bit;
        backtrack2(st, 1, bit << 1, bit, bit >> 1);

        st->lastmask |= (st->lastmask >> 1) | (st->lastmask << 1);
        st->endbit >>= 1;
    }

    st->unique = st->count8 + st->count4 + st->count2;
    st->total = (st->count8 * 8) + (st->count4 * 4) + (st->count2 * 2);
}

static int parse_args(int argc, char **argv, int *n) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s N\n", argv[0]);
        return 0;
    }

    *n = atoi(argv[1]);
    if (*n < MINSIZE || *n > MAXSIZE) {
        fprintf(stderr, "Error: N debe estar entre %d y %d.\n", MINSIZE, MAXSIZE);
        return 0;
    }

    return 1;
}

int main(int argc, char **argv) {
    int n;
    NQueenState st;
    double t_ini;
    double t_fin;

    if (!parse_args(argc, argv, &n)) {
        return EXIT_FAILURE;
    }

    /* Inicializacion fuera de la medicion. */
    init_state(&st, n);

    /* Medicion neta de la busqueda secuencial refactorizada. */
    t_ini = dwalltime();
    solve_nqueens(&st);
    t_fin = dwalltime();

    printf("N=%d Total=%" PRIu64 " Unique=%" PRIu64 " Tiempo=%f\n",
           n, st.total, st.unique, t_fin - t_ini);

    return EXIT_SUCCESS;
}
