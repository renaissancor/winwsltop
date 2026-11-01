#!/usr/bin/env python3
"""Cuts `winwsltop --record` lines down to what this experiment reads.

A full line lists every Windows and guest process, which is about 100 KB a line and names the
programs this machine runs, in a public repository. Kept whole: cpu, vm, cost_ms, the timing keys
and the numeric wsl fields. Kept from processes: the rows named in KEEP, and the sum of cores over
all rows but Idle. Usage: slim_record.py < full > slim
"""
import json
import sys

KEEP = {"vmmemWSL", "vmwp.exe", "vmcompute.exe", "wslhost.exe", "wslservice.exe", "wslrelay.exe", "wsl.exe", "winwsltop.exe", "Idle", "System"}
TOP = ("schema", "unix_ms", "interval_s", "cpu", "vm", "cost_ms")
WSL_DATA = ("kernel", "unix_s", "mono_s", "busy_pct", "core_pct", "steal_pct", "pressure", "process_count", "cost_ms")

for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    r = json.loads(line)
    out = {k: r.get(k) for k in TOP}
    procs = r.get("processes", [])
    out["windows_cores_sum"] = round(sum(p.get("cores", 0) for p in procs if p.get("name") != "Idle"), 4)
    out["windows_rows"] = len(procs)
    out["processes"] = [{k: p.get(k) for k in ("pid", "ppid", "name", "cores", "threads")} for p in procs if p.get("name") in KEEP]
    w = dict(r.get("wsl") or {})
    data = w.pop("data", None)
    out["wsl"] = w
    if data:
        out["wsl"]["data"] = {k: data.get(k) for k in WSL_DATA}
        out["wsl"]["data"]["guest_cores_sum"] = round(sum(p.get("cores", 0) for p in data.get("processes", [])), 4)
    else:
        out["wsl"]["data"] = None
    print(json.dumps(out, separators=(",", ":")))
