#!/usr/bin/env python3
"""Turns one run's files into the comparisons the ledger cites. Needs no live machine.
Usage: analyze.py <results dir> <run> <date>

Bracket rule, used throughout: a reading of the collector's sits between an independent reading taken
before it and one taken after it. A value is `inside` when it lies in [min, max] of those two, `equal`
when the two agree and the collector's equals them, and `outside` with its distance otherwise. A value
that never moved in the run is counted apart: agreement on it proves bindings at that value only.
"""
import json
import os
import sys
from collections import defaultdict

d, run, date = sys.argv[1], sys.argv[2], sys.argv[3]
path = lambda kind: os.path.join(d, "%s-%s-%s.txt" % (run, kind, date))
MiB = 1024 * 1024


def bracket(v, a, b):
    lo, hi = min(a, b), max(a, b)
    if lo <= v <= hi:
        return "equal" if a == b == v else "inside", 0
    return "outside", (lo - v) if v < lo else (v - hi)


# ---------------------------------------------------------------- Windows
rounds = []
cur = None
long_ping = None
for line in open(path("windows")):
    f = line.rstrip("\n").split("\t")
    if f[0].startswith("== round"):
        cur = {"n": int(f[0].split()[2]), "gc": {"c1": {}, "c2": {}}, "gp": {"p1": {}, "p2": {}}, "proc": {}, "t": {}}
        rounds.append(cur)
    elif f[0] == "long_ping":
        long_ping = (int(f[1]), int(f[2]))
    elif cur is None:
        continue
    elif f[0] == "short_ping":
        cur["short"] = (int(f[1]), int(f[2]))
    elif f[0] == "t":
        cur["t"][f[1]] = int(f[2])
    elif f[0] == "gc":
        cur["gc"][f[1]][f[2]] = int(f[3])
    elif f[0] == "gp":
        cur["gp"][f[1]][int(f[2])] = (f[3], int(f[4]), int(f[5]), int(f[6]))
    elif f[0] == "mem":
        cur["mem"] = dict(zip(("ok", "total", "available", "commit_used", "commit_limit", "cache"), map(int, f[1:])))
    elif f[0] == "proc":
        cur["proc"][int(f[1])] = (f[2], int(f[3]), int(f[4]), int(f[5]))
    elif f[0] == "vm":
        cur["vm"] = tuple(map(int, f[1:]))

print("== Windows: %d rounds of c1 Get-Counter, p1 Get-Process, mem_probe, p2 Get-Process, c2 Get-Counter" % len(rounds))
print("mem_probe ok in all rounds: %s" % all(r["mem"]["ok"] == 1 for r in rounds))
print("seconds from c1_start to c2_end: %.3f-%.3f" % (
    min((r["t"]["c2_end"] - r["t"]["c1_start"]) / 1e7 for r in rounds),
    max((r["t"]["c2_end"] - r["t"]["c1_start"]) / 1e7 for r in rounds)))

print("\n-- Windows memory, collector against \\Memory counters c1 and c2 (bytes)")
pairs = [("available", "available bytes"), ("commit_used", "committed bytes"), ("commit_limit", "commit limit")]
for field, ctr in pairs:
    res = [bracket(r["mem"][field], r["gc"]["c1"][ctr], r["gc"]["c2"][ctr]) for r in rounds]
    vals = [r["mem"][field] for r in rounds]
    gap = [max(abs(r["mem"][field] - r["gc"]["c1"][ctr]), abs(r["mem"][field] - r["gc"]["c2"][ctr])) for r in rounds]
    move = [abs(r["gc"]["c2"][ctr] - r["gc"]["c1"][ctr]) for r in rounds]
    print("%-13s vs %-17s equal %2d inside %2d outside %2d, max outside %d B; collector %d-%d; "
          "max |collector - c| %d B (%.3f MiB); c2-c1 moved %.3f-%.3f MiB" % (
              field, ctr, sum(1 for x, _ in res if x == "equal"), sum(1 for x, _ in res if x == "inside"),
              sum(1 for x, _ in res if x == "outside"), max(g for _, g in res), min(vals), max(vals),
              max(gap), max(gap) / MiB, min(move) / MiB, max(move) / MiB))

print("\n-- Windows cache: GetPerformanceInfo SystemCache * PageSize against candidate counter sums")
standby = ("standby cache reserve bytes", "standby cache normal priority bytes", "standby cache core bytes")
cands = {
    "Cache Bytes": ("cache bytes",),
    "standby (reserve+normal+core)": standby,
    "standby + modified": standby + ("modified page list bytes",),
    "standby + Cache Bytes": standby + ("cache bytes",),
    "standby + modified + Cache Bytes": standby + ("modified page list bytes", "cache bytes"),
}
for name, keys in cands.items():
    res, diffs = [], []
    for r in rounds:
        a = sum(r["gc"]["c1"][k] for k in keys)
        b = sum(r["gc"]["c2"][k] for k in keys)
        res.append(bracket(r["mem"]["cache"], a, b))
        diffs.append(r["mem"]["cache"] - (a + b) / 2)
    print("%-34s inside or equal %2d of %d; collector minus mean(c1,c2) %.1f to %.1f MiB" % (
        name, sum(1 for x, _ in res if x != "outside"), len(res), min(diffs) / MiB, max(diffs) / MiB))
print("collector cache %d-%d B" % (min(r["mem"]["cache"] for r in rounds), max(r["mem"]["cache"] for r in rounds)))
extra = [k for k in ("pool paged resident bytes", "pool paged bytes", "pool nonpaged bytes",
                     "system code resident bytes", "system driver resident bytes") if k in rounds[0]["gc"]["c1"]]
res = [(r["mem"]["cache"] - sum(r["gc"][c][k] for c in ("c1", "c2") for k in standby + ("cache bytes",)) / 2) / MiB
       for r in rounds]
print("collector cache minus mean(c1,c2) of standby + Cache Bytes, per round, MiB: %s" % " ".join("%.1f" % x for x in res))
for k in extra:
    v = [(r["gc"]["c1"][k] + r["gc"]["c2"][k]) / 2 / MiB for r in rounds]
    print("  for comparison, %s %.1f-%.1f MiB" % (k, min(v), max(v)))
for r in (rounds[0], rounds[-1]):
    print("round %d: collector cache %d; c1 %s; c2 %s" % (r["n"], r["mem"]["cache"],
          " ".join("%s=%d" % (k, v) for k, v in r["gc"]["c1"].items() if k in standby + ("modified page list bytes", "cache bytes")),
          " ".join("%s=%d" % (k, v) for k, v in r["gc"]["c2"].items() if k in standby + ("modified page list bytes", "cache bytes"))))

print("\n-- Windows per-process working set and private bytes, collector against Get-Process p1 and p2")
for idx, field in ((2, "working_set"), (3, "private")):
    gi = idx  # gp tuple: name, start, ws, private
    tally = defaultdict(int)
    worst = 0
    pids_named, pids_all, moved = set(), set(), 0
    for r in rounds:
        for pid, row in r["proc"].items():
            a, b = r["gp"]["p1"].get(pid), r["gp"]["p2"].get(pid)
            if not a or not b or a[1] != b[1]:
                tally["missing"] += 1
                continue
            v = row[idx]
            kind, gap = bracket(v, a[gi], b[gi])
            tally[kind] += 1
            if a[gi] != b[gi]:
                moved += 1
            worst = max(worst, gap)
            pids_all.add(pid)
            if row[0] != "other":
                pids_named.add(pid)
    n = sum(tally[k] for k in ("equal", "inside", "outside"))
    print("%-11s comparisons %d over %d pids (%d named): equal %d, inside %d, outside %d, max outside %d B; "
          "p1 != p2 in %d; not in both p1 and p2 %d" % (field, n, len(pids_all), len(pids_named), tally["equal"],
                                                        tally["inside"], tally["outside"], worst, moved, tally["missing"]))
r = rounds[len(rounds) // 2]
print("round %d, named samples (pid name collector_ws p1_ws p2_ws collector_private p1_private p2_private):" % r["n"])
shown = 0
for pid, row in sorted(r["proc"].items()):
    a, b = r["gp"]["p1"].get(pid), r["gp"]["p2"].get(pid)
    if row[0] in ("vmmemWSL", "vmwp.exe", "System", "Registry", "dwm.exe", "explorer.exe", "lsass.exe", "ping.exe") and a and b:
        print("  %d %s %d %d %d %d %d %d" % (pid, row[0], row[2], a[2], b[2], row[3], a[3], b[3]))
        shown += 1

print("\n-- Windows create time: collector key against Get-Process StartTime (100 ns UTC)")
eq = ne = unread = 0
for r in rounds:
    for pid, row in r["proc"].items():
        for p in ("p1", "p2"):
            g = r["gp"][p].get(pid)
            if not g:
                continue
            if g[1] == -1:
                unread += 1
            elif g[1] == row[1]:
                eq += 1
            else:
                ne += 1
print("equal %d, different %d, StartTime unreadable (access denied, non-elevated) %d" % (eq, ne, unread))
seen = defaultdict(list)  # pid -> [(round, create)]
for r in rounds:
    for pid, row in r["proc"].items():
        seen[pid].append((r["n"], row[1]))
const = sum(1 for v in seen.values() if len({c for _, c in v}) == 1)
print("pids seen %d; with one create time across every round they were seen in %d" % (len(seen), const))
lp_rows = [r["proc"].get(long_ping[0]) for r in rounds]
print("long ping pid %d StartTime %d: in %d of %d rounds, collector create time values %s" % (
    long_ping[0], long_ping[1], sum(1 for x in lp_rows if x), len(rounds), sorted({x[1] for x in lp_rows if x})))
reuse = []
for pid, v in seen.items():
    creates = []
    for n, c in v:
        if not creates or creates[-1][1] != c:
            creates.append((n, c))
    if len(creates) > 1:
        rounds_seen = [n for n, _ in v]
        gaps = [n for n in range(rounds_seen[0], rounds_seen[-1] + 1) if n not in rounds_seen]
        reuse.append((pid, creates, gaps))
print("pids whose create time changed between rounds: %d" % len(reuse))
for pid, creates, gaps in reuse:
    names = sorted({r["proc"][pid][0] for r in rounds if pid in r["proc"]})
    print("  pid %d names %s create times (first round, value) %s; rounds absent between %s" % (pid, names, creates, gaps))
shorts = [(r["n"], r["short"]) for r in rounds]
short_pids = defaultdict(list)
for n, (pid, st) in shorts:
    short_pids[pid].append((n, st))
print("short pings started %d, distinct pids %d" % (len(shorts), len(short_pids)))
for pid, v in short_pids.items():
    if len(v) > 1:
        print("  short ping pid %d reused: (round, StartTime) %s" % (pid, v))
for n, (pid, st) in shorts:
    for r in rounds:
        if pid in r["proc"] and r["proc"][pid][1] != st:
            print("  short ping pid %d (round %d, StartTime %d) seen in round %d as %s create %d" % (
                pid, n, st, r["n"], r["proc"][pid][0], r["proc"][pid][1]))
            break

# ---------------------------------------------------------------- guest
if not os.path.exists(path("guest")):
    print("\n(no guest file for this run: Windows only)")
    sys.exit(0)
consts = {}
groups = []
cur = None
for line in open(path("guest")):
    f = line.rstrip("\n").split("\t")
    if f[0] == "const":
        consts = {"HZ": int(f[2]), "PAGE": int(f[4])}
    elif f[0].startswith("== round"):
        cur = {"n": int(f[0].split()[2]), "t": {}, "stat": {}, "ps": {}, "free": {}, "mem": {}}
        groups.append(cur)
    elif cur is None:
        continue
    elif f[0] == "t":
        cur["t"][f[1]] = float(f[2])
    elif f[0] == "meminfo":
        cur["mem"] = {k: int(v) for k, v in (x.split("=") for x in f[2:])}
    elif f[0] == "stat":
        cur["stat"][int(f[1])] = (f[2], int(f[3]), int(f[4]), int(f[5]) * consts["PAGE"], int(f[6]))
    elif f[0] == "ps":
        cur["ps"][int(f[1])] = (int(f[2]) * 1024, int(f[3]))
    elif f[0] == "free":
        cur["free"][f[1].rstrip(":")] = [int(x) for x in f[2:]]
HZ = consts["HZ"]
groups = [g for g in groups if "free_end" in g["t"]]
static = dict(l.rstrip("\n").split("\t", 1) for l in open(path("static")))
print("\n== Guest: %d sampler rounds, HZ %d, PAGE %d" % (len(groups), HZ, consts["PAGE"]))

recs = [json.loads(l) for l in open(path("record"))]
lines = [x for x in recs if x["wsl"]["data"]]
print("record lines %d, with guest data %d" % (len(recs), len(lines)))
monos = [x["wsl"]["data"]["mono_s"] for x in lines]
steps = [round(b - a, 3) for a, b in zip(monos, monos[1:])]
print("collector mono_s steps between record lines: min %.3f max %.3f, repeats %d, over 1.5 s %d" % (
    min(steps), max(steps), sum(1 for s in steps if s == 0), sum(1 for s in steps if s > 1.5)))
print("first and last collector mono_s %.3f %.3f; sampler rounds span %.3f-%.3f" % (
    monos[0], monos[-1], groups[0]["t"]["stat_start"], groups[-1]["t"]["free_end"]))


def around(m0, m1, start_key, end_key):
    """Last sampler round whose reading ended before m0, first whose reading started after m1."""
    before = [g for g in groups if g["t"][end_key] <= m0]
    after = [g for g in groups if g["t"][start_key] >= m1]
    return (before[-1] if before else None), (after[0] if after else None)


print("\n-- Guest swap used, collector against free -b and /proc/meminfo")
sw = [x["wsl"]["data"]["memory"]["swap_used"] for x in lines]
fs = [g["free"]["Swap"][1] for g in groups]
ms = [g["mem"]["SwapTotal"] - g["mem"]["SwapFree"] for g in groups]
print("collector swap_used %d-%d; free -b Swap used %d-%d; meminfo SwapTotal-SwapFree %d-%d; SwapTotal %d" % (
    min(sw), max(sw), min(fs), max(fs), min(ms), max(ms), groups[0]["mem"]["SwapTotal"]))
print("never moved: %s" % (len(set(sw + fs + ms)) == 1))

print("\n-- Guest RSS, collector against /proc/pid/stat rss, /proc/pid/status VmRSS and ps rss, bracketing reads")
tally = {k: defaultdict(int) for k in ("stat", "VmRSS", "ps")}
worst = defaultdict(int)
pids, moved = set(), 0
for x in lines:
    dd = x["wsl"]["data"]
    m0, m1 = dd["mono_s"], dd["mono_s"] + dd["cost_ms"] / 1000
    b4s, afs = around(m0, m1, "stat_start", "stat_end")
    b4p, afp = around(m0, m1, "ps_start", "ps_end")
    for p in dd["processes"]:
        pid = p["pid"]
        if not (b4s and afs and pid in b4s["stat"] and pid in afs["stat"]
                and b4s["stat"][pid][2] == afs["stat"][pid][2]):
            tally["stat"]["missing"] += 1
            continue
        pids.add(pid)
        a, b = b4s["stat"][pid], afs["stat"][pid]
        if a[3] != b[3]:
            moved += 1
        for src, va, vb in (("stat", a[3], b[3]), ("VmRSS", a[4], b[4])):
            k, gap = bracket(p["rss"], va, vb)
            tally[src][k] += 1
            worst[src] = max(worst[src], gap)
        if b4p and afp and pid in b4p["ps"] and pid in afp["ps"]:
            k, gap = bracket(p["rss"], b4p["ps"][pid][0], afp["ps"][pid][0])
            tally["ps"][k] += 1
            worst["ps"] = max(worst["ps"], gap)
        else:
            tally["ps"]["missing"] += 1
for src in ("stat", "VmRSS", "ps"):
    t = tally[src]
    print("%-6s equal %d inside %d outside %d (max outside %d B), not bracketed %d" % (
        src, t["equal"], t["inside"], t["outside"], worst[src], t["missing"]))
print("pids compared %d; comparisons where the bracketing stat rss moved %d" % (len(pids), moved))
gap = [g["stat"][pid][4] - g["stat"][pid][3] for g in groups for pid in g["stat"] if g["stat"][pid][4] >= 0]
print("same sampler read, VmRSS minus stat rss over %d reads: min %d max %d B; negative %d, zero %d, positive %d" % (
    len(gap), min(gap), max(gap), sum(1 for x in gap if x < 0), sum(1 for x in gap if x == 0), sum(1 for x in gap if x > 0)))
rss = sorted({p["rss"] for x in lines for p in x["wsl"]["data"]["processes"]})
print("collector rss values range %d-%d B" % (rss[0], rss[-1]))

print("\n-- Guest per-process cores, collector against /proc/pid/stat utime+stime and ps cputimes")
print("Per interval: consecutive record lines whose mono_s step is under 1.5 s are consecutive collector samples,")
print("so the collector's ticks over the step are cores * step * HZ. The sampler brackets both ends.")
tick_tally = defaultdict(int)
tick_worst = 0
frac = []
for x0, x1 in zip(lines, lines[1:]):
    d0, d1 = x0["wsl"]["data"], x1["wsl"]["data"]
    step = d1["mono_s"] - d0["mono_s"]
    if step <= 0 or step > 1.5:
        continue
    a, b = around(d0["mono_s"], d0["mono_s"] + d0["cost_ms"] / 1000, "stat_start", "stat_end")
    c, e = around(d1["mono_s"], d1["mono_s"] + d1["cost_ms"] / 1000, "stat_start", "stat_end")
    if not (a and b and c and e):
        continue
    prev = {p["pid"] for p in d0["processes"]}
    for p in d1["processes"]:
        pid = p["pid"]
        if pid not in prev or not all(pid in g["stat"] for g in (a, b, c, e)):
            continue
        if len({g["stat"][pid][2] for g in (a, b, c, e)}) != 1:
            continue
        t = p["cores"] * step * HZ
        frac.append(abs(t - round(t)))
        lo = c["stat"][pid][1] - b["stat"][pid][1]
        hi = e["stat"][pid][1] - a["stat"][pid][1]
        if lo <= round(t) <= hi:
            tick_tally["inside" if hi > 0 else "zero"] += 1
        else:
            tick_tally["outside"] += 1
            tick_worst = max(tick_worst, lo - round(t), round(t) - hi)
print("intervals x processes: inside with ticks moving %d, at zero (never moved) %d, outside %d (max %d ticks)" % (
    tick_tally["inside"], tick_tally["zero"], tick_tally["outside"], tick_worst))
print("|cores*step*HZ - nearest whole tick|: max %.4f, so the unit is ticks of 1/HZ s" % max(frac))

print("Whole window: processes in every record line, collector seconds summed over the chain of lines,")
print("against stat ticks and ps cputimes between the rounds bracketing the first and last line")
chain = [lines[0]]
for x in lines[1:]:
    if 0 < x["wsl"]["data"]["mono_s"] - chain[-1]["wsl"]["data"]["mono_s"] <= 1.5:
        chain.append(x)
    else:
        break
first, last = chain[0]["wsl"]["data"], chain[-1]["wsl"]["data"]
a, b = around(first["mono_s"], first["mono_s"] + first["cost_ms"] / 1000, "stat_start", "stat_end")
c, e = around(last["mono_s"], last["mono_s"] + last["cost_ms"] / 1000, "stat_start", "stat_end")
pa, pb = around(first["mono_s"], first["mono_s"] + first["cost_ms"] / 1000, "ps_start", "ps_end")
pc, pe = around(last["mono_s"], last["mono_s"] + last["cost_ms"] / 1000, "ps_start", "ps_end")
print("chain of %d consecutive lines, %.3f s" % (len(chain), last["mono_s"] - first["mono_s"]))
common = set.intersection(*({p["pid"] for p in x["wsl"]["data"]["processes"]} for x in chain))
print("pid name collector_s stat_lo_s stat_hi_s ps_lo_s ps_hi_s verdict (ps cputimes is whole seconds, so its bounds are widened by 1 s)")
busy = 0
for pid in sorted(common):
    s = 0.0
    for x0, x1 in zip(chain, chain[1:]):
        step = x1["wsl"]["data"]["mono_s"] - x0["wsl"]["data"]["mono_s"]
        s += round(next(p["cores"] for p in x1["wsl"]["data"]["processes"] if p["pid"] == pid) * step * HZ)
    s /= HZ  # whole ticks per interval, so rounding cores to 3 decimals does not pile up over the chain
    if not all(g and pid in g["stat"] for g in (a, b, c, e)):
        continue
    lo = (c["stat"][pid][1] - b["stat"][pid][1]) / HZ
    hi = (e["stat"][pid][1] - a["stat"][pid][1]) / HZ
    pslo = pshi = None
    if all(g and pid in g["ps"] for g in (pa, pb, pc, pe)):
        pslo = pc["ps"][pid][1] - pb["ps"][pid][1] - 1   # cputimes is whole seconds, truncated
        pshi = pe["ps"][pid][1] - pa["ps"][pid][1] + 1
    name = next(p["name"] for p in last["processes"] if p["pid"] == pid)
    ok = lo - 1e-9 <= s <= hi + 1e-9
    if hi > 0:
        busy += 1
    print("%d %s %.3f %.2f %.2f %s %s %s" % (pid, name, s, lo, hi, pslo, pshi,
                                             ("inside" if ok else "OUTSIDE") + ("" if hi > 0 else " (zero)")))
print("processes with ticks moving over the window: %d of %d" % (busy, len(common)))

print("\n-- Guest memory beside vmmemWSL working set, one record line each (bytes)")
print("used = total - available, as the screen draws it; free_mem from the nearest sampler round's MemFree")
print("line unix_ms vmmem_ws guest_total guest_used guest_cached nearest_MemFree total-MemFree "
      "ws-used ws-(used+cached) ws-(total-MemFree)")
rows = []
for i, x in enumerate(lines):
    dd = x["wsl"]["data"]
    m = dd["memory"]
    used = m["total"] - m["available"]
    g = min(groups, key=lambda g: abs(g["t"]["stat_start"] - dd["mono_s"]))
    tmf = m["total"] - g["mem"]["MemFree"]
    ws = x["vm"]["vmmem_working_set"]
    rows.append((ws, used, m["cached"], tmf))
    if i in (0, len(lines) // 2, len(lines) - 1):
        print("%d %d %d %d %d %d %d %d %d %d %d" % (i + 1, x["unix_ms"], ws, m["total"], used, m["cached"],
                                                   g["mem"]["MemFree"], tmf, ws - used, ws - used - m["cached"], ws - tmf))
for name, k in (("vmmem_ws", 0), ("guest_used", 1), ("guest_cached", 2), ("total-MemFree", 3)):
    v = [r[k] for r in rows]
    print("%-14s range %.1f-%.1f MiB, moved %.1f MiB" % (name, min(v) / MiB, max(v) / MiB, (max(v) - min(v)) / MiB))
for name, f in (("ws - used", lambda r: r[0] - r[1]), ("ws - (used + cached)", lambda r: r[0] - r[1] - r[2]),
                ("ws - (total - MemFree)", lambda r: r[0] - r[3])):
    v = [f(r) for r in rows]
    print("%-24s %.1f to %.1f MiB" % (name, min(v) / MiB, max(v) / MiB))

print("\n-- Guest starttime: constant while a process lives")
seen = defaultdict(set)
for g in groups:
    for pid, row in g["stat"].items():
        seen[pid].add(row[2])
sp = int(static["sleep_pid"])
sleep_st = sorted(seen.get(sp, set()))
print("sleep pid %d (started by run.sh): starttime values over %d rounds %s, rounds present %d" % (
    sp, len(groups), sleep_st, sum(1 for g in groups if sp in g["stat"])))
print("pids seen %d, with one starttime in every round they were seen %d" % (
    len(seen), sum(1 for v in seen.values() if len(v) == 1)))
print("pid reuse: ns_last_pid %s to %s with pid_max %s, so no pid wrapped in this run" % (
    static["ns_last_pid_start"], static["ns_last_pid_end"], static["pid_max"]))
