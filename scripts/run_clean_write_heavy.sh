#!/bin/bash
# Clean run: write-heavy YCSB Cheetah vs Caracal (same setup as archive_build/build_cluster_write_only).
# Run from repo root: ./scripts/run_clean_write_heavy.sh

set -e
cd "$(dirname "$0")/.."
echo "Removing old build/bin/res ..."
rm -rf build/bin/res
echo "Running ycsb.py (build + run + plot) ..."
python3 -u scripts/ycsb.py
echo "Done. Results: build/bin/res/result.csv, plots: build/bin/res/plots/"
