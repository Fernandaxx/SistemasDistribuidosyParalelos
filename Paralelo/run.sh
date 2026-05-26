#!/bin/bash

# Define the arrays for your parameters
N_VALUES=(14 15 16 17 18)
T_VALUES=(2 4 8)

# Loop through each N value
for  in "${N_VALUES[@]}"; do
    # Loop through each T value
    for T in "${T_VALUES[@]}"; do
        echo "Submitting job for N=$N, T=$T"
        sbatch ./nreinas.sh "$N" "$T"
        
        # A small delay is still good practice for cluster queues
        sleep 0.2
    done
done
