#!/usr/bin/env bash
# Experiment 07: is WSLg's system distro running, and are its processes in this distro's /proc? Read only.
# Usage: experiments/07-guest-walk/wslg.sh > experiments/07-guest-walk/results/<run>-wslg-<date>.txt
echo "utc $(date -u +%FT%TZ)"
echo "/mnt/wslg exists: $(test -d /mnt/wslg && echo yes || echo no)"
echo "/mnt/wslg entries: $(ls /mnt/wslg 2>/dev/null | tr '\n' ' ')"
echo "WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-unset} DISPLAY=${DISPLAY:-unset}"
echo "/tmp/.X11-unix: $(ls -l /tmp/.X11-unix 2>/dev/null | tail -n +2 | awk '{print $1, $NF}' | tr '\n' ' ')"
n=0
for p in /proc/[0-9]*; do
  case "$(cat "$p/comm" 2>/dev/null)" in weston|Xwayland|pulseaudio) n=$((n + 1));; esac
done
echo "pids in this distro's /proc whose comm is weston, Xwayland or pulseaudio: $n"
