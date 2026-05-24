#ifndef __PTHREADS_FUNCTIONS__
#define __PTHREADS_FUNCTIONS__

#include <pthread.h>

extern int SIZE, SIZEE;
extern int TOPBIT, MASK, SIDEMASK;
extern int T;

typedef struct {

} par_t;


extern void pthreads_function(par_t parametros);
#endif