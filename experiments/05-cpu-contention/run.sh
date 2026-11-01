#!/usr/bin/env bash
# The whole run: recorders throughout, the burners in the cases of README.md, and nothing left behind.
# Usage: run.sh <dir holding contention_probe.exe, cpu_burn.exe, winwsltop.exe> <linux cpu_burn>
#               <run name> <date> <dir for full record lines> [dry]
# `dry` shortens every step to 1 s and gives every burner 0 s, which does no work: it proves the
# sequence and the file formats. Everything else here is the load the user approved on 2026-11-01.
# This script stops nothing. Each burner ends at its own deadline (cpu_burn.cpp).
set -euo pipefail
b=$1
burn_linux=$2
run=$3
date=$4
full=$5
dry=${6:-}
here=$(cd "$(dirname "$0")" && pwd)
out=$here/results
mkdir -p "$out" "$full"
cd /mnt/c

marks=$out/$run-marks-$date.txt
winburn=$out/$run-burn-windows-$date.txt
wslburn=$out/$run-burn-wsl-$date.txt
: > "$marks"; : > "$winburn"; : > "$wslburn"
mark() { printf 'mark\t%s\t%s\n' "$1" "$(date +%s.%N)" >> "$marks"; }
step() { if [ -n "$dry" ]; then sleep 1; else sleep "$1"; fi; }
secs() { if [ -n "$dry" ]; then echo 0; else echo "$1"; fi; }
win_burn() { "$b/cpu_burn.exe" "$1" "$(secs "$2")" "$3" | tr -d '\r' >> "$winburn"; }
wsl_burn() { "$burn_linux" "$1" "$(secs "$2")" "$3" >> "$wslburn"; }
clock_pings() {
  for i in 1 2 3; do
    printf 'clock\t%s\t%s\tbefore\t%s\n' "$1" "$i" "$(date +%s.%N)" >> "$marks"
    printf '%s\t%s\t%s\n' "$1" "$i" "$("$b/contention_probe.exe" clock | tr -d '\r')" >> "$marks"
    printf 'clock\t%s\t%s\tafter\t%s\n' "$1" "$i" "$(date +%s.%N)" >> "$marks"
  done
}

# Seconds of case time: 5 lead-in + 20 + 75 + 15 + 40 + 15 + 102 + 15, then a tail.
rounds=300
records=296
[ -z "$dry" ] || { rounds=20; records=16; }

"$b/contention_probe.exe" topology | tr -d '\r' > "$out/$run-topology-$date.txt"
{
  echo "kernel	$(uname -r)"
  echo "nproc	$(nproc)"
  nvidia-smi --query-gpu=name,driver_version,memory.total,power.limit --format=csv,noheader 2>&1
} > "$out/$run-static-$date.txt"
clock_pings start

python3 "$here/guest_sample.py" "$rounds" > "$out/$run-guest-$date.txt" &
guest=$!
"$b/contention_probe.exe" run "$rounds" | tr -d '\r' > "$out/$run-probe-$date.txt" &
probe=$!
"$b/winwsltop.exe" --record "$records" | tr -d '\r' > "$full/$run-record-$date.jsonl" &
record=$!

step 5
mark lead-end

mark baseline; step 20; mark baseline-end

for n in 1 2 5 10 20; do
  mark "wsl-$n"; wsl_burn "$n" 15 "wsl-$n"; [ -z "$dry" ] || sleep 1; mark "wsl-$n-end"
done

mark settle-1; step 15; mark settle-1-end

mark win-20; win_burn 20 40 win-20; [ -z "$dry" ] || sleep 1; mark win-20-end

mark settle-2; step 15; mark settle-2-end

# WSL 20 throughout, alone before and after, so each Windows step has a reference beside it.
mark both-before
wsl_burn 20 102 both &
wsl=$!
step 10
mark both-win-5; win_burn 5 12 both-win-5; [ -z "$dry" ] || sleep 1; mark both-win-5-end
mark both-win-10; win_burn 10 12 both-win-10; [ -z "$dry" ] || sleep 1; mark both-win-10-end
mark both-win-20; win_burn 20 60 both-win-20; [ -z "$dry" ] || sleep 1; mark both-win-20-end
mark both-after
wait "$wsl"
mark both-after-end

mark settle-3; step 15; mark settle-3-end

wait "$record" "$guest" "$probe"
mark recorders-end
clock_pings end
python3 "$here/slim_record.py" < "$full/$run-record-$date.jsonl" > "$out/$run-record-$date.txt"
