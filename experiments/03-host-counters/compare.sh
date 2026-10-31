#!/usr/bin/env bash
# Runs every reading in this experiment and saves it under results/. Reads only, starts no load.
# Usage: compare.sh <dir holding host_probe.exe> <date>, e.g. compare.sh /mnt/c/.../build 2026-11-01
set -uo pipefail
b=$1
date=$2
here=$(cd "$(dirname "$0")" && pwd)
out=$here/results
windows=3     # concurrent windows
rounds=60     # collector rounds in each, a second apart
samples=75    # Get-Counter samples in each, enough to start before and end after the collector
mkdir -p "$out"
cp "$here"/counters.ps1 "$here"/netstats.ps1 "$here"/identity.ps1 "$b/"
cd /mnt/c
ps() { powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w "$b/$1")" "${@:2}" 2>&1 | tr -d '\r'; }
probe() { "$b/host_probe.exe" "$@" | tr -d '\r'; }

{
  printf 'kernel\t%s\n' "$(uname -r)"
  ps identity.ps1
  probe rows
} > "$out/identity-$date.txt"

for i in 1 2 3 4 5 6 7 8 9 10; do
  echo "== bracket $i"
  probe raw | sed 's/^raw/before/'
  ps netstats.ps1
  probe raw | sed 's/^raw/after/'
done > "$out/net-bracket-$date.txt"

# One side of the Windows-against-guest window: guest counters between two raw Windows reads.
edge() {
  echo "== $1"
  probe raw | sed 's/^raw/before/'
  sed -n 's/^ *eth0: */wsl\t/p' /proc/net/dev | tr -s ' ' '\t'
  probe raw | sed 's/^raw/after/'
}

for w in $(seq 1 "$windows"); do
  ps counters.ps1 -Samples "$samples" > "$out/window$w-counters-$date.txt" &
  sleep 6       # powershell.exe takes seconds to start; the collector must start inside its samples
  edge start > "$out/window$w-edges-$date.txt"
  probe run "$rounds" > "$out/window$w-probe-$date.txt"
  edge end >> "$out/window$w-edges-$date.txt"
  wait
done

python3 "$here/analyze.py" "$out" "$date" > "$out/analysis-$date.txt"
