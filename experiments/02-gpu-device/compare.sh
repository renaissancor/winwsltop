#!/usr/bin/env bash
# Alternates the probe with nvidia-smi, then brackets Get-Counter between two probe reads.
# Reads only. Usage: compare.sh <dir holding gpu_probe.exe and counters.ps1>
set -uo pipefail
b=$1
cd /mnt/c
for i in 1 2 3 4 5; do
  "$b/gpu_probe.exe" 1 0 | grep -E '^[0-9]|^  (util|mem used)'
  nvidia-smi.exe --query-gpu=timestamp,memory.used,memory.total,power.draw,power.limit,temperature.gpu,clocks.sm,utilization.gpu --format=csv,noheader | tr -d '\r'
  sleep 1
done
for i in 1 2 3; do
  echo "== bracket $i"
  "$b/gpu_probe.exe" 1 500 | grep -E '^  pid' | sed 's/^/before/'
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w "$b/counters.ps1")" 2>&1 | tr -d '\r'
  "$b/gpu_probe.exe" 1 500 | grep -E '^  pid' | sed 's/^/after/'
done
