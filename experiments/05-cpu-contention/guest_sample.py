#!/usr/bin/env python3
"""One record a second of the guest's CPU files, read against contention_probe.exe and cpu_burn.

Reads only. Tab-separated; every line starts with its record kind. Experiment 04's sampler, plus
memory and io pressure, a scan of every task, and the burner's own tasks. Process names are this
PC's programs, so a row keeps its name only if it is on NAMED, and says "other" otherwise.
Usage: guest_sample.py <rounds>
"""
import os
import re
import sys
import time

NAMED = {"cpu_burn", "guest_sample.py"}   # comm is cut to 15 characters; this script is python3 anyway
TOP = 8


def read(path):
    try:
        with open(path) as f:
            return f.read()
    except OSError as e:
        return f"!{e.errno}"


def stat_row(path):
    """pid, comm, state, ticks, threads, processor from a stat file, or None if it went away."""
    text = read(path)
    if text.startswith("!"):
        return None
    close = text.rfind(")")
    comm = text[text.find("(") + 1:close]
    f = text[close + 2:].split()
    # f[0] is field 3 of proc(5). utime 14, stime 15, num_threads 20, processor 39.
    return comm, f[0], int(f[11]) + int(f[12]), int(f[17]), int(f[36])


def scan(r, last):
    """Process rows and a state count over every task, thread by thread."""
    now = {}
    states = {}
    tasks = 0
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        row = stat_row(f"/proc/{name}/stat")
        if row is None:
            continue
        comm, state, ticks, threads, cpu = row
        now[int(name)] = (comm, state, ticks, threads, cpu)
        try:
            tids = os.listdir(f"/proc/{name}/task")
        except OSError:
            continue
        for tid in tids:
            t = stat_row(f"/proc/{name}/task/{tid}/stat")
            if t is None:
                continue
            tasks += 1
            states[t[1]] = states.get(t[1], 0) + 1
            if comm == "cpu_burn" and tid != name:
                print(f"burntask\t{r}\t{tid}\t{t[1]}\t{t[4]}\t{t[2]}")
    print(f"tasks\t{r}\t{tasks}\t" + "\t".join(f"{k}={v}" for k, v in sorted(states.items())))
    rows = []
    for pid, (comm, state, ticks, threads, cpu) in now.items():
        old = last.get(pid)
        rows.append((ticks - old[2] if old else 0, pid, comm, state, threads, cpu))
    rows.sort(reverse=True)
    for d, pid, comm, state, threads, cpu in rows[:TOP]:
        print(f"top\t{r}\t{pid}\t{comm if comm in NAMED else 'other'}\t{state}\t{threads}\t{cpu}\t{d}")
    print(f"procs\t{r}\t{len(now)}")
    return now


def main():
    rounds = int(sys.argv[1])
    start = time.monotonic()
    last = {}
    for r in range(rounds):
        print(f"round\t{r}\t{time.time():.6f}\t{time.monotonic():.6f}")
        for line in read("/proc/stat").splitlines():
            key = line.split(" ", 1)[0]
            if key.startswith("cpu") or key in ("ctxt", "procs_running", "procs_blocked"):
                print("stat\t" + "\t".join([str(r)] + line.split()))
        print(f"loadavg\t{r}\t{read('/proc/loadavg').strip()}")
        for res in ("cpu", "memory", "io"):
            for line in read(f"/proc/pressure/{res}").splitlines():
                print(f"psi\t{r}\t{res}\t{line}")
        mhz = re.findall(r"^cpu MHz\s*:\s*([0-9.]+)", read("/proc/cpuinfo"), re.M)
        print("mhz\t" + "\t".join([str(r)] + mhz))
        cg = " ".join(read("/sys/fs/cgroup/cpu.stat").split())
        print(f"cgroup\t{r}\t{cg}")
        last = scan(r, last)
        print(f"spent\t{r}\t{time.monotonic() - start - r:.6f}")
        sys.stdout.flush()
        time.sleep(max(0.0, start + r + 1 - time.monotonic()))


main()
