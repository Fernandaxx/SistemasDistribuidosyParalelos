#!/bin/bash
#SBATCH -J nreinas_16up
#SBATCH -N 2
#SBATCH --ntasks=2
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=8
#SBATCH --exclusive
#SBATCH --partition=Blade
#SBATCH -o nreinas_16up_%j.out
#SBATCH -e nreinas_16up_%j.err

set -euo pipefail

THREADS=8
NS="14 15 16 17 18"

make par

for N in ${NS}; do
    mpirun -np 2 --map-by ppr:1:node:PE=${THREADS} --bind-to core ./nreinas_hibrido "${N}" "${THREADS}"
done
