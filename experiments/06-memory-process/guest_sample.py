#!/usr/bin/env python3
"""Reads the guest's memory and every process, every <period> seconds for <seconds>, beside the collector.
Reads only. Each round, in this order, timed on CLOCK_MONOTONIC, the clock the collector's `mono_s` uses:
  meminfo   /proc/meminfo, the fields below
  stat      every /proc/pid/stat: pid, utime + stime ticks, starttime, rss pages, and /proc/pid/status VmRSS
  ps        `ps -eo pid=,rss=,cputimes=`
  free      `free -b`, the Mem and Swap lines
Comm names outside KEEP print as `other`: this repo is public. Tab-separated, one record per line.
Usage: guest_sample.py <seconds> <period>"""
import os
import subprocess
import sys
import time

KEEP = {"python3", "sleep", "bash", "sh", "ps", "free", "init", "systemd", "wsl-collector"}
FIELDS = ("MemTotal", "MemFree", "MemAvailable", "Buffers", "Cached", "SwapCached", "Shmem", "SReclaimable",
          "SwapTotal", "SwapFree", "AnonPages", "Mapped")


def mono():
    return "%.6f" % time.monotonic()


def main():
    seconds, period = float(sys.argv[1]), float(sys.argv[2])
    print("const\tHZ\t%d\tPAGE\t%d" % (os.sysconf("SC_CLK_TCK"), os.sysconf("SC_PAGE_SIZE")))
    print("pid_max\t%s" % open("/proc/sys/kernel/pid_max").read().strip())
    end = time.monotonic() + seconds
    r = 0
    while time.monotonic() < end:
        r += 1
        t0 = time.monotonic()
        print("== round %d\t%s\t%.6f" % (r, mono(), time.time()))
        m = {}
        for line in open("/proc/meminfo"):
            k, _, rest = line.partition(":")
            if k in FIELDS:
                m[k] = int(rest.split()[0]) * 1024
        print("meminfo\t%s\t%s" % (mono(), "\t".join("%s=%d" % (k, m[k]) for k in FIELDS)))
        print("t\tstat_start\t%s" % mono())
        for name in os.listdir("/proc"):
            if not name.isdigit():
                continue
            try:
                text = open("/proc/%s/stat" % name, errors="replace").read()
                vmrss = -1
                for line in open("/proc/%s/status" % name):
                    if line.startswith("VmRSS:"):
                        vmrss = int(line.split()[1]) * 1024
                        break
            except OSError:
                continue
            end_c = text.rfind(")")
            comm = text[text.find("(") + 1:end_c]
            f = text[end_c + 2:].split()
            print("stat\t%s\t%s\t%d\t%s\t%s\t%d" % (name, comm if comm in KEEP else "other",
                                                   int(f[11]) + int(f[12]), f[19], f[21], vmrss))
        print("t\tstat_end\t%s" % mono())
        print("t\tps_start\t%s" % mono())
        out = subprocess.run(["ps", "-eo", "pid=,rss=,cputimes="], capture_output=True, text=True).stdout
        for line in out.splitlines():
            print("ps\t" + "\t".join(line.split()))
        print("t\tps_end\t%s" % mono())
        out = subprocess.run(["free", "-b"], capture_output=True, text=True).stdout
        for line in out.splitlines()[1:]:
            print("free\t" + "\t".join(line.split()))
        print("t\tfree_end\t%s" % mono())
        sys.stdout.flush()
        time.sleep(max(0.0, period - (time.monotonic() - t0)))


if __name__ == "__main__":
    main()
