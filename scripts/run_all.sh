#!/bin/bash
set -euo pipefail

sbatch scripts/run_4up.sh
sbatch scripts/run_8up.sh
sbatch scripts/run_16up.sh
