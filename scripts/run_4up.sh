#!/bin/bash
#SBATCH -J nreinas_4up
#SBATCH -N 2
#SBATCH --ntasks=2
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=2
#SBATCH --exclusive
#SBATCH --partition=Blade
#SBATCH -o nreinas_4up_%j.out
#SBATCH -e nreinas_4up_%j.err

set -euo pipefail

THREADS=2
NS="14 15 16 17 18"

make par

for N in ${NS}; do
    mpirun -np 2 --map-by ppr:1:node:PE=${THREADS} --bind-to core ./nreinas_hibrido "${N}" "${THREADS}"
done
