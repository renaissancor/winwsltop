#!/usr/bin/env python3
"""Cuts `winwsltop --record` lines down, as experiment 04's script did, and masks what else ran.

Kept whole: cpu, vm, cost_ms, the timing keys and the numeric wsl fields. Kept by name: the rows in
KEEP. Everything else on Windows becomes the sum of its cores and up to TOP rows with the name
replaced by `other-N`, numbered in order first seen in this run and steady across it, so a program can
be followed without being named. The same on the guest side as `guest-N`. The full lines stay
outside this repository. Usage: slim_record.py < full > slim
"""
import json
import sys

KEEP = {"vmmemWSL", "vmwp.exe", "vmcompute.exe", "wslhost.exe", "wslservice.exe", "wslrelay.exe", "wsl.exe",
        "winwsltop.exe", "cpu_burn.exe", "contention_probe.exe", "Idle", "System"}
GUEST_KEEP = {"cpu_burn"}
TOP = 5
TOP_KEYS = ("schema", "unix_ms", "interval_s", "cpu", "vm", "cost_ms")
WSL_DATA = ("kernel", "unix_s", "mono_s", "busy_pct", "core_pct", "steal_pct", "pressure", "process_count", "cost_ms")
labels, guest_labels = {}, {}


def mask(rows, keep, table, prefix, keys):
    kept = [{k: p.get(k) for k in keys} for p in rows if p.get("name") in keep]
    others = sorted((p for p in rows if p.get("name") not in keep), key=lambda p: p.get("cores", 0), reverse=True)
    out = []
    for p in others[:TOP]:
        name = table.setdefault(p.get("name"), f"{prefix}-{len(table) + 1}")
        row = {k: p.get(k) for k in keys}
        row["name"] = name
        row.pop("pid", None)
        row.pop("ppid", None)
        out.append(row)
    return kept, out, round(sum(p.get("cores", 0) for p in others), 4)


for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    r = json.loads(line)
    out = {k: r.get(k) for k in TOP_KEYS}
    procs = r.get("processes", [])
    out["windows_cores_sum"] = round(sum(p.get("cores", 0) for p in procs if p.get("name") != "Idle"), 4)
    out["windows_rows"] = len(procs)
    out["processes"], out["top_other"], out["other_cores_sum"] = mask(procs, KEEP, labels, "other", ("pid", "ppid", "name", "cores", "threads"))
    w = dict(r.get("wsl") or {})
    data = w.pop("data", None)
    out["wsl"] = w
    if data:
        out["wsl"]["data"] = {k: data.get(k) for k in WSL_DATA}
        rows = data.get("processes", [])
        out["wsl"]["data"]["guest_cores_sum"] = round(sum(p.get("cores", 0) for p in rows), 4)
        kept, top, others = mask(rows, GUEST_KEEP, guest_labels, "guest", ("pid", "name", "state", "cores"))
        out["wsl"]["data"]["burner_rows"], out["wsl"]["data"]["top_other"], out["wsl"]["data"]["other_cores_sum"] = kept, top, others
    else:
        out["wsl"]["data"] = None
    print(json.dumps(out, separators=(",", ":")))
