#!/usr/bin/env python3
"""One record a second of the guest's CPU files, for cpu_probe.exe run to be read against.

Reads only. Tab-separated; every line starts with its record kind. Usage: wsl_sample.py <rounds>
"""
import re
import sys
import time


def read(path):
    try:
        with open(path) as f:
            return f.read()
    except OSError as e:
        return f"!{e.errno}"


def main():
    rounds = int(sys.argv[1])
    start = time.monotonic()
    for r in range(rounds):
        print(f"round\t{r}\t{time.time():.6f}\t{time.monotonic():.6f}")
        for line in read("/proc/stat").splitlines():
            key = line.split(" ", 1)[0]
            if key.startswith("cpu") or key in ("ctxt", "procs_running", "procs_blocked"):
                print("stat\t" + "\t".join([str(r)] + line.split()))
        print(f"loadavg\t{r}\t{read('/proc/loadavg').strip()}")
        for line in read("/proc/pressure/cpu").splitlines():
            print(f"psi\t{r}\t{line}")
        mhz = re.findall(r"^cpu MHz\s*:\s*([0-9.]+)", read("/proc/cpuinfo"), re.M)
        print("mhz\t" + "\t".join([str(r)] + mhz))
        cg = " ".join(read("/sys/fs/cgroup/cpu.stat").split())
        print(f"cgroup\t{r}\t{cg}")
        sys.stdout.flush()
        time.sleep(max(0.0, start + r + 1 - time.monotonic()))


main()
