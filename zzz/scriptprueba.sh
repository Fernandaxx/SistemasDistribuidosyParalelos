#!/bin/bash
#SBATCH -N 1
#SBATCH --exclusive
#SBATCH --partition=Blade
#SBATCH -o /nethome/sdyp18/prueba/outputprueba.txt
#SBATCH -e /nethome/sdyp18/prueba/erroresprueba.txt
#SBATCH --time=00:05:00
./nreinas_mpi
