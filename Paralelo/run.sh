#!/bin/bash
#SBATCH -N 2
#SBATCH --exclusive
#SBATCH --partition=Blade
#SBATCH -o /nethome/sdyp18/salidas/output_%j.txt
#SBATCH -e /nethome/sdyp18/errores/errores_%j.txt
#SBATCH --time=00:05:00

mpirun --bind-to none ./hibrido $1 $2