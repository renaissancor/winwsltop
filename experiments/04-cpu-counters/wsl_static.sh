#!/usr/bin/env bash
# What the guest offers for CPU, in one pass. Reads only. Run in WSL.
set -uo pipefail
show() { echo "--- $1"; if [ -e "$1" ]; then cat "$1" 2>&1; else echo "absent"; fi; }
echo "kernel	$(uname -r)"
echo "nproc	$(nproc)"
echo "nproc_all	$(nproc --all)"
echo "getconf_online	$(getconf _NPROCESSORS_ONLN)"
for f in online possible present offline isolated smt/active smt/control; do show /sys/devices/system/cpu/$f; done
echo "=== /proc/cpuinfo, processors 0 and 19, flags cut"
awk -v RS= 'NR==1 || NR==20' /proc/cpuinfo | sed 's/^\(flags\|vmx flags\|bugs\)\s*:.*/\1 : (cut)/'
echo "flags_has_hypervisor	$(awk '/^flags/{print ($0 ~ / hypervisor( |$)/); exit}' /proc/cpuinfo)"
echo "=== cpufreq and cpuidle"
for d in /sys/devices/system/cpu/cpu0/cpufreq /sys/devices/system/cpu/cpufreq /sys/devices/system/cpu/cpuidle /sys/devices/system/cpu/cpu0/cpuidle; do
  if [ -d "$d" ]; then echo "$d: directory, entries: $(ls -A "$d" | wc -l)"; ls -A "$d"; else echo "$d: absent"; fi
done
show /sys/devices/system/cpu/cpuidle/current_driver
echo "=== topology: cpu core_id thread_siblings package die l1d l1i l2 l3 max_freq"
for i in $(seq 0 19); do
  c=/sys/devices/system/cpu/cpu$i
  printf 'topo\t%s\t%s\t%s\t%s\t%s' "$i" "$(cat $c/topology/core_id)" "$(cat $c/topology/thread_siblings_list)" "$(cat $c/topology/physical_package_id)" "$(cat $c/topology/die_id)"
  for x in 0 1 2 3; do printf '\t%s' "$(cat $c/cache/index$x/level 2>/dev/null)$(cat $c/cache/index$x/type 2>/dev/null | cut -c1):$(cat $c/cache/index$x/size 2>/dev/null)"; done
  printf '\n'
done
echo "=== /sys/class/thermal"
echo "thermal_zones	$(ls -d /sys/class/thermal/thermal_zone* 2>/dev/null | wc -l)"
echo "cooling_devices	$(ls -d /sys/class/thermal/cooling_device* 2>/dev/null | wc -l)"
for d in /sys/class/thermal/cooling_device*; do printf '%s\t%s\t%s\t%s\n' "$(basename "$d")" "$(cat "$d/type")" "$(cat "$d/cur_state")" "$(cat "$d/max_state")"; done | sort -V
echo "hwmon	$(ls /sys/class/hwmon 2>/dev/null | wc -l)"
echo "=== cgroup"
show /proc/self/cgroup
show /sys/fs/cgroup/cgroup.controllers
show /sys/fs/cgroup/cpu.max
show /sys/fs/cgroup/cpu.stat
show /sys/fs/cgroup/cpu.pressure
show /sys/fs/cgroup/cpuset.cpus.effective
echo "cpu.max files found below the root:"
find /sys/fs/cgroup -name cpu.max 2>/dev/null | while read -r f; do printf '%s\t%s\n' "${f#/sys/fs/cgroup/}" "$(cat "$f")"; done
echo "=== settings"
echo "wslconfig_present_in_any_windows_profile	$(ls /mnt/c/Users/*/.wslconfig 2>/dev/null | wc -l)"
show /proc/pressure/cpu
show /proc/loadavg
echo "=== GPU line for the ledger"
nvidia-smi --query-gpu=name,driver_version,memory.total,power.limit --format=csv,noheader 2>&1
