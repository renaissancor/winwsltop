#!/usr/bin/env bash
# Runs every reading in this experiment and saves it under results/. Reads only, starts no load.
# It starts three idle processes of its own: `sleep` in the guest and two kinds of ping.exe to 127.0.0.1
# (windows.ps1), and ends each one. Nothing else is started, stopped or signalled.
# Usage: run.sh <dir holding mem_probe.exe and winwsltop.exe, as a /mnt/c path> <run> <date> <dir for full record lines>
set -euo pipefail
b=$1
run=$2
date=$3
full=$4
here=$(cd "$(dirname "$0")" && pwd)
out=$here/results
rounds=25        # windows.ps1 rounds, about 3.5 s each
records=60       # winwsltop --record lines
guest_s=110      # guest_sample.py span, enough to start before and end after the records
mkdir -p "$out" "$full"
cp "$here/windows.ps1" "$b/"
cd /mnt/c

{
  printf 'date\t%s\n' "$(date -u +%FT%TZ)"
  printf 'kernel\t%s\n' "$(uname -r)"
  printf 'windows\t%s\n' "$(cmd.exe /c ver | tr -d '\r' | sed -n 's/.*\[[^0-9]*\([0-9.]*\)\].*/\1/p')"
  printf 'cpu\t%s\n' "$(sed -n 's/^model name\t: //p' /proc/cpuinfo | head -1)"
  printf 'guest_threads\t%s\n' "$(nproc)"
  printf 'gpu\t%s\n' "$(nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader 2>&1)"
  printf 'pid_max\t%s\n' "$(cat /proc/sys/kernel/pid_max)"
  printf 'ns_last_pid_start\t%s\n' "$(cat /proc/sys/kernel/ns_last_pid)"
} > "$out/$run-static-$date.txt"

sleep 900 &          # the guest's known process: idle, ended below
idle=$!
printf 'sleep_pid\t%s\n' "$idle" >> "$out/$run-static-$date.txt"

python3 "$here/guest_sample.py" "$guest_s" 0.5 > "$out/$run-guest-$date.txt" &
guest=$!
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w "$b/windows.ps1")" \
  -Probe "$(wslpath -w "$b/mem_probe.exe")" -Rounds "$rounds" 2>&1 | tr -d '\r' > "$out/$run-windows-$date.txt" &
win=$!
sleep 5
"$b/winwsltop.exe" --record "$records" | tr -d '\r' > "$full/$run-record-$date.jsonl"
python3 "$here/slim_record.py" < "$full/$run-record-$date.jsonl" > "$out/$run-record-$date.txt"
wait "$win" "$guest"

printf 'ns_last_pid_end\t%s\n' "$(cat /proc/sys/kernel/ns_last_pid)" >> "$out/$run-static-$date.txt"
kill "$idle"
wait "$idle" 2>/dev/null || true
printf 'sleep_ended\t%s\n' "$(date -u +%FT%TZ)" >> "$out/$run-static-$date.txt"
