#!/bin/bash
#SBATCH -N 2
#SBATCH --exclusive
#SBATCH --partition=Blade
#SBATCH -o /nethome/sdyp18/output1404.txt
#SBATCH -e /nethome/sdyp18/errores1404.txt
#SBATCH --time=00:05:00
mpirun --bind-to none ./hibrido 14 2