#!/bin/bash
#SBATCH -N 1
#SBATCH --exclusive
#SBATCH --partition=Blade
#SBATCH -o salida/output.txt
#SBATCH -e salida/errors.txt
#SBATCH --time=00:05:00

./prueba