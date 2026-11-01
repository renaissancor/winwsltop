#!/usr/bin/env bash
# Runs every reading in this experiment and saves it under results/. Reads only, starts no load.
# Usage: collect.sh <dir holding cpu_probe.exe and winwsltop.exe> <date> <dir for full record lines>
# e.g.   collect.sh /mnt/c/.../build 2026-11-01 /tmp/full
set -euo pipefail
b=$1
date=$2
full=$3
here=$(cd "$(dirname "$0")" && pwd)
out=$here/results
windows=3     # concurrent windows
records=60    # winwsltop --record lines in each
rounds=70     # probe and guest rounds in each, enough to start before and end after the records
costs=60      # rounds of cost mode
mkdir -p "$out" "$full"
cp "$here/windows.ps1" "$b/"
cd /mnt/c
probe() { "$b/cpu_probe.exe" "$@" | tr -d '\r'; }

bash "$here/wsl_static.sh" > "$out/wsl-static-$date.txt" 2>&1
probe topology > "$out/topology-$date.txt"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w "$b/windows.ps1")" 2>&1 | tr -d '\r' > "$out/windows-identity-$date.txt"

# -q prints no machine name. The file keeps the counter lines only: typeperf's status lines are in
# the console's language and code page.
for o in "Processor Information" "System" "Thermal Zone Information" "Hyper-V Hypervisor Virtual Processor" \
         "Hyper-V Hypervisor Logical Processor" "Hyper-V Hypervisor Root Virtual Processor" "Hyper-V Worker Virtual Processor"; do
  echo "=== $o: counters"
  typeperf.exe -q "\\$o" 2>&1 | tr -d '\r' | LC_ALL=C grep -a '^\\' | sed 's/^\\[^(\\]*\((\*)\)\{0,1\}\\//'
  echo "=== $o: instances"
  typeperf.exe -qx "\\$o" 2>&1 | tr -d '\r' | LC_ALL=C grep -a '^\\' | sed -n 's/^\\[^(]*(\(.*\))\\.*/\1/p' | sort -u
done > "$out/pdh-names-$date.txt"

for w in $(seq 1 "$windows"); do
  python3 "$here/wsl_sample.py" "$rounds" > "$out/window$w-guest-$date.txt" &
  guest=$!
  probe run "$rounds" > "$out/window$w-probe-$date.txt" &
  win=$!
  sleep 3
  "$b/winwsltop.exe" --record "$records" | tr -d '\r' > "$full/window$w-record-$date.jsonl"
  python3 "$here/slim_record.py" < "$full/window$w-record-$date.jsonl" > "$out/window$w-record-$date.txt"
  wait "$guest" "$win"
done

probe cost "$costs" > "$out/cost-$date.txt"
