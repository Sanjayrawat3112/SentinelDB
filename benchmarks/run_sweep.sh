#!/bin/bash
# usage: benchmarks/run_sweep.sh   (run from anywhere; uses build/bench, writes build/results.csv)
BIN=~/SentinelDB/build/bench
OUT=~/SentinelDB/build/results.csv
echo "engine,workload,capacity,seed,seconds,ops_per_sec,p50_us,p99_us,hits,misses,evictions" > "$OUT"
cd ~/SentinelDB/build || exit 1
$BIN naive random 0 99 >/dev/null 2>&1      # throwaway warm-up run
for seed in 1 2 3 4 5; do
  for wl in random seq mixed; do
    $BIN naive $wl 0 $seed 2>/dev/null >> "$OUT"
    for cap in 64 256 1024; do
      $BIN sentinel $wl $cap $seed 2>/dev/null >> "$OUT"
    done
  done
done
echo "done: $(wc -l < "$OUT") lines (expect 61)"
