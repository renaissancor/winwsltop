#!/usr/bin/env python3
"""Cuts `winwsltop --record` lines down to what this experiment compares, and masks other programs.

Kept: the timing keys, `memory`, `vm`, `wsl.age_s`, `wsl.clock_offset_s`, and from `wsl.data` its timing
keys, `cost_ms`, `memory` and `process_count`. Every Windows process row keeps `pid`, `working_set` and
`private`; every guest row keeps `pid`, `rss`, `cores` and `threads`. Names outside the KEEP lists
become `other`, as mem_probe.cpp and guest_sample.py print them. The kept values are copied unchanged.
The full lines stay outside this repository. Usage: slim_record.py < full > slim
"""
import json
import sys

KEEP = {"Idle", "System", "Registry", "smss.exe", "csrss.exe", "wininit.exe", "services.exe", "lsass.exe",
        "svchost.exe", "winlogon.exe", "dwm.exe", "explorer.exe", "fontdrvhost.exe", "conhost.exe", "vmmemWSL",
        "vmwp.exe", "vmcompute.exe", "wslservice.exe", "wslhost.exe", "wslrelay.exe", "wsl.exe", "ping.exe",
        "powershell.exe", "mem_probe.exe", "winwsltop.exe"}
GUEST_KEEP = {"python3", "sleep", "bash", "sh", "ps", "free", "init", "systemd", "wsl-collector"}

for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    r = json.loads(line)
    out = {k: r.get(k) for k in ("schema", "unix_ms", "interval_s", "memory", "vm")}
    out["processes"] = [{"pid": p.get("pid"), "name": p.get("name") if p.get("name") in KEEP else "other",
                         "working_set": p.get("working_set"), "private": p.get("private")}
                        for p in r.get("processes", [])]
    w = r.get("wsl") or {}
    out["wsl"] = {k: w.get(k) for k in ("age_s", "restarts", "clock_known", "clock_offset_s")}
    data = w.get("data")
    if data:
        d = {k: data.get(k) for k in ("unix_s", "mono_s", "cost_ms", "memory", "process_count")}
        d["processes"] = [{"pid": p.get("pid"), "name": p.get("name") if p.get("name") in GUEST_KEEP else "other",
                           "rss": p.get("rss"), "cores": p.get("cores"), "threads": p.get("threads")}
                          for p in data.get("processes", [])]
        out["wsl"]["data"] = d
    else:
        out["wsl"]["data"] = None
    print(json.dumps(out, separators=(",", ":")))
