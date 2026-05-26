/**************************************************************************/
/* N-Queens Solutions  ver3.1               takaken July/2003             */
/**************************************************************************/
#include <stdio.h>
#include <stdlib.h>


/* Time in seconds from some point in the past */
double dwalltime();

#define  MAXSIZE  24
#define  MINSIZE   2

int  SIZE , SIZEE;
int  BOARD[MAXSIZE] , * BOARDE , * BOARD1 , * BOARD2;
int  MASK , TOPBIT , SIDEMASK , LASTMASK , ENDBIT;
int  BOUND1 , BOUND2;

long int  COUNT8 , COUNT4 , COUNT2;
long int  TOTAL , UNIQUE;

/**********************************************/
/* Display the Board Image                    */
/**********************************************/
void Display(void){
    int  y , bit;

    printf("N= %d\n" , SIZE);
    for (y = 0; y < SIZE; y++){
        for (bit = TOPBIT; bit; bit >>= 1)
            printf("%s " , (BOARD[y] & bit) ? "Q" : "-");
        printf("\n");
    }
    printf("\n");
}
/**********************************************/
/* Check Unique Solutions                     */
/**********************************************/
void Check(void){
    int* own , * you , bit , ptn;

    /* 90-degree rotation */
    if (*BOARD2 == 1){
        for (ptn = 2 , own = BOARD + 1; own <= BOARDE; own++ , ptn <<= 1){
            bit = 1;
            for (you = BOARDE; *you != ptn && *own >= bit; you--)
                bit <<= 1;
            if (*own > bit) return;
            if (*own < bit) break;
        }
        if (own > BOARDE){
            COUNT2++;
            //Display();
            return;
        }
    }

    /* 180-degree rotation */
    if (*BOARDE == ENDBIT){
        for (you = BOARDE - 1 , own = BOARD + 1; own <= BOARDE; own++ , you--){
            bit = 1;
            for (ptn = TOPBIT; ptn != *you && *own >= bit; ptn >>= 1)
                bit <<= 1;
            if (*own > bit) return;
            if (*own < bit) break;
        }
        if (own > BOARDE){
            COUNT4++;
            //Display();
            return;
        }
    }

    /* 270-degree rotation */
    if (*BOARD1 == TOPBIT){
        for (ptn = TOPBIT >> 1 , own = BOARD + 1; own <= BOARDE; own++ , ptn >>= 1){
            bit = 1;
            for (you = BOARD; *you != ptn && *own >= bit; you++)
                bit <<= 1;
            if (*own > bit) return;
            if (*own < bit) break;
        }
    }
    COUNT8++;
    //Display();
}
/**********************************************/
/* First queen is inside                      */
/**********************************************/
void Backtrack2(int y , int left , int down , int right){
    int  bitmap , bit;

    bitmap = MASK & ~(left | down | right);
    if (y == SIZEE){
        if (bitmap){
            if (!(bitmap & LASTMASK)){ // descarta simetrias
                BOARD[y] = bitmap;
                Check();// verifica a que grupo de simetria pertenece la solucion
            }
        }
    }
    else{
        if (y < BOUND1){ //SIDEMASK =1000001 (1's en las esquinas)
            bitmap |= SIDEMASK;
            bitmap ^= SIDEMASK;// borra del bitmap las columnas laterales, para descartar sol simetricas
        }
        else if (y == BOUND2){ //poda
            if (!(down & SIDEMASK)) return;
            if ((down & SIDEMASK) != SIDEMASK) bitmap &= SIDEMASK;
        }
        while (bitmap){
            bitmap ^= BOARD[y] = bit = -bitmap & bitmap;
            Backtrack2(y + 1 , (left | bit) << 1 , down | bit , (right | bit) >> 1);
        }
    }

}
/**********************************************/
/* First queen is in the corner               */
/**********************************************/
void Backtrack1(int y , int left , int down , int right){
    int  bitmap , bit;

    bitmap = MASK & ~(left | down | right); // queda con 1 en las columnas libres
    if (y == SIZEE){ // estoy en la ultima fila
        if (bitmap){ // y encontre una posicion libre -> encontro una solucion 
            BOARD[y] = bitmap;
            COUNT8++;
            //Display();
        }
    }
    else{
        if (y < BOUND1){ // BOUND1 es la posicion donde esta la reina de la fila 1
            bitmap |= 2;
            bitmap ^= 2;// Mientras todavía no llegué a la fila BOUND1, no permito usar la columna 1. para descartar simetrias
        }
        while (bitmap){
            bitmap ^= BOARD[y] = bit = -bitmap & bitmap; //Agarra el bit menos significativo que este en 1, lo pone en BOARD[y] y lo saca de bitmap
            Backtrack1(y + 1 , (left | bit) << 1 , down | bit , (right | bit) >> 1); // llamada recursiva con columnas atacadas actualizadas
        }
    }
}
/**********************************************/
/* Search of N-Queens                         */
/**********************************************/
void NQueens(void){
    int  bit , cant;

    /* Initialize */
    COUNT8 = COUNT4 = COUNT2 = 0; // cuentan soluciones segun su simetria
    SIZEE = SIZE - 1; //ultima fila del tablero
    BOARDE = &BOARD[SIZEE];
    TOPBIT = 1 << SIZEE; //columna mas externa del tablero
    MASK = (1 << SIZE) - 1; //mascara de columnas validas del tablero N=4 -> mask = 1111



    /* 0:000000001 */
    /* 1:011111100 */
    BOARD[0] = 1;
    for (BOUND1 = 2; BOUND1 < SIZEE; BOUND1++){
        BOARD[1] = bit = 1 << BOUND1;
        Backtrack1(2 , (2 | bit) << 1 , 1 | bit , bit >> 1);
    }
    /* 0:000001110 */
    SIDEMASK = LASTMASK = TOPBIT | 1;
    ENDBIT = TOPBIT >> 1;
    for (BOUND1 = 1 , BOUND2 = SIZE - 2; BOUND1 < BOUND2; BOUND1++ , BOUND2--){
        BOARD1 = &BOARD[BOUND1];
        BOARD2 = &BOARD[BOUND2];
        BOARD[0] = bit = 1 << BOUND1;
        Backtrack2(1 , bit << 1 , bit , bit >> 1);
        LASTMASK |= LASTMASK >> 1 | LASTMASK << 1;
        ENDBIT >>= 1;
    }

    /* Unique and Total Solutions */
    UNIQUE = COUNT8 + COUNT4 + COUNT2;
    TOTAL = COUNT8 * 8 + COUNT4 * 4 + COUNT2 * 2;

}

/**********************************************/
/* N-Queens Solutions MAIN                    */
/**********************************************/
int main(int argC , char* argV[]){
    double tIni , tFin;

    SIZE = atoi(argV[1]);
    tIni = dwalltime();
    NQueens();
    tFin = dwalltime();

    printf("Número de resultados: %lu - Soluciones únicas: %lu - Tiempo Total: %f segundos \n" , TOTAL , UNIQUE, tFin - tIni);
    return 0;
}

#include <sys/time.h>

double dwalltime(){
    double sec;
    struct timeval tv;

    gettimeofday(&tv , NULL);
    sec = tv.tv_sec + tv.tv_usec / 1000000.0;
    return sec;
}
