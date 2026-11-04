#!/usr/bin/env bash
# Experiment 07, one run: static facts, then walk_probe.py for 120 s with churn.py from 60 s to 80 s.
# Read only toward the system. The only processes started are the probe and churn.py's own children,
# which it waits for. Usage, from the repo root: experiments/07-guest-walk/run.sh <run> <date>
set -euo pipefail
run=$1; date=$2
here=experiments/07-guest-walk
out=$here/results
mkdir -p "$out"
{
  echo "utc $(date -u +%FT%TZ)"
  echo "windows $(/mnt/c/Windows/System32/cmd.exe /c ver 2>/dev/null | tr -d '\r' | grep -v '^$')"
  echo "kernel $(uname -r)"
  echo "distro $WSL_DISTRO_NAME"
  echo "registered distros (HKCU Lxss DistributionName):"
  /mnt/c/Windows/System32/reg.exe query 'HKCU\Software\Microsoft\Windows\CurrentVersion\Lxss' /s /v DistributionName \
    2>/dev/null | tr -d '\r' | grep DistributionName
  echo "pid namespace $(readlink /proc/self/ns/pid), pid 1 $(cat /proc/1/comm)"
} > "$out/$run-static-$date.txt"
python3 -I $here/walk_probe.py 120 > "$out/$run-walk-$date.txt" &
probe=$!
sleep 60
python3 -I $here/churn.py 20 > "$out/$run-churn-$date.txt"
wait $probe
