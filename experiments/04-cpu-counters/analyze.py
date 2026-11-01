#!/usr/bin/env python3
"""Turns the raw files of experiment 04 into analysis-<date>.txt. Reads only those files.

Usage: analyze.py <results dir> <date>. Rerun it alone at any time; it needs no live machine.
Window means one of the 3 concurrent windows. Round r of a rate is the interval from round r-1 to r.
"""
import functools
import json
import math
import os
import statistics as st
import sys

WINDOWS = (1, 2, 3)
LPS = 20
P_LPS = range(0, 12)   # topology-<date>.txt: efficiency class 1, two threads a core
E_LPS = range(12, 20)  # efficiency class 0, one thread a core
PI = "\\Processor Information(*)\\"
VP = "\\Hyper-V Hypervisor Virtual Processor(*)\\"
HL = "\\Hyper-V Hypervisor Logical Processor(*)\\"
RV = "\\Hyper-V Hypervisor Root Virtual Processor(*)\\"
SYS = "\\System\\"
TZ = "\\Thermal Zone Information(*)\\"


def lines(path):
    with open(path, encoding="utf-8") as f:
        return [x.rstrip("\n").split("\t") for x in f]


def num(x):
    return None if x.startswith("!") or x == "" else float(x)


def load_probe(path):
    p = {"round": {}, "nt": {}, "proc": {}, "thr": {}, "power": {}, "pdh": {}, "capped": {}, "names": {}, "name_changes": 0, "fail": [], "status": {}}
    for f in lines(path):
        k = f[0]
        if k == "round":
            p["round"][int(f[1])] = (int(f[2]), float(f[3]))
        elif k == "nt":
            p["nt"].setdefault(f[2], {})[int(f[1])] = [int(x) for x in f[3:]]
        elif k == "proc":
            p["proc"].setdefault(f[2], {})[int(f[1])] = dict(pid=int(f[3]), kernel=int(f[4]), user=int(f[5]), cycles=int(f[6]), threads=int(f[7]))
        elif k == "thr":
            p["thr"].setdefault(int(f[1]), {})[int(f[2])] = dict(kernel=int(f[3]), user=int(f[4]), cs=int(f[5]), state=int(f[6]), wait=int(f[7]), prio=int(f[8]))
        elif k == "power":
            p["power"].setdefault(f[2], {})[int(f[1])] = [int(x) for x in f[3:]]
        elif k == "names":
            if f[2] in p["names"]:
                p["name_changes"] += 1
            p["names"][f[2]] = f[3:]
        elif k in ("pdh", "capped"):
            p[k].setdefault(f[2], {})[int(f[1])] = [num(x) for x in f[3:]]
            if k == "pdh":
                for x in f[3:]:
                    if x.startswith("!"):
                        p["status"][(f[2], x[1:])] = p["status"].get((f[2], x[1:]), 0) + 1
        elif k.endswith("failed"):
            p["fail"].append(f)
    return p


def load_guest(path):
    g = {"round": {}, "stat": {}, "ctxt": {}, "running": {}, "blocked": {}, "load": {}, "psi": {}, "mhz": {}, "cgroup": {}}
    for f in lines(path):
        k = f[0]
        if k == "round":
            g["round"][int(f[1])] = (float(f[2]), float(f[3]))
        elif k == "stat":
            r, name = int(f[1]), f[2]
            if name.startswith("cpu"):
                g["stat"].setdefault(name, {})[r] = [int(x) for x in f[3:]]
            elif name == "ctxt":
                g["ctxt"][r] = int(f[3])
            elif name == "procs_running":
                g["running"][r] = int(f[3])
            elif name == "procs_blocked":
                g["blocked"][r] = int(f[3])
        elif k == "loadavg":
            g["load"][int(f[1])] = f[2]
        elif k == "psi":
            d = dict(x.split("=") for x in f[2].split()[1:])
            g["psi"].setdefault(f[2].split()[0], {})[int(f[1])] = d
        elif k == "mhz":
            g["mhz"][int(f[1])] = f[2:]
        elif k == "cgroup":
            w = f[2].split()
            g["cgroup"][int(f[1])] = dict(zip(w[::2], w[1::2]))
    return g


def load_record(path):
    with open(path) as f:
        return [json.loads(x) for x in f if x.strip()]


def rng(v, nd=3):
    v = [x for x in v if x is not None]
    if not v:
        return "no values"
    return f"min {min(v):.{nd}f} mean {st.fmean(v):.{nd}f} max {max(v):.{nd}f} n {len(v)}"


def pearson(a, b):
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    if n < 3:
        return None
    ma, mb = st.fmean(a), st.fmean(b)
    sa = math.sqrt(sum((x - ma) ** 2 for x in a))
    sb = math.sqrt(sum((x - mb) ** 2 for x in b))
    if sa == 0 or sb == 0:
        return None
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / (sa * sb)


def lagged(a, b, lag):
    """r of a[t] with b[t + lag]."""
    if lag >= 0:
        return pearson(a[: len(a) - lag] if lag else a, b[lag:])
    return pearson(a[-lag:], b[: len(b) + lag])


def ranks(v):
    order = sorted(range(len(v)), key=lambda i: v[i])
    out = [0.0] * len(v)
    i = 0
    while i < len(order):
        j = i
        while j + 1 < len(order) and v[order[j + 1]] == v[order[i]]:
            j += 1
        for k in range(i, j + 1):
            out[order[k]] = (i + j) / 2
        i = j + 1
    return out


def spearman(a, b):
    n = min(len(a), len(b))
    return pearson(ranks(a[:n]), ranks(b[:n]))


def fmt_r(x):
    return "undefined" if x is None else f"{x:+.3f}"


def by_inst(p, path, kind="pdh"):
    """{instance: {round: value}}"""
    names = p["names"].get(path, [])
    out = {n: {} for n in names}
    for r, vals in p[kind].get(path, {}).items():
        for n, v in zip(names, vals):
            if v is not None:   # the probe printed a status instead of a value
                out[n][r] = v
    return out


def lp_of(name):
    return int(name.split(",")[1]) if "," in name and not name.endswith("_Total") else None


def lp_series(p, path):
    """{windows LP: {round: value}}"""
    return {lp_of(n): v for n, v in by_inst(p, path).items() if lp_of(n) is not None}


def hv_series(p, path):
    """{number after the last space: {round: value}} for 'Hv VP n', 'Hv LP n', 'Root VP n'."""
    out = {}
    for n, v in by_inst(p, path).items():
        if n != "_Total":
            out[int(n.rsplit(" ", 1)[1])] = v
    return out


def rounds(p):
    return sorted(r for r in p["round"] if r > 0 and r - 1 in p["round"])


def dt(p, r):
    return p["round"][r][1] - p["round"][r - 1][1]


def nt_delta(p, field, r):
    a, b = p["nt"][field][r - 1], p["nt"][field][r]
    return [y - x for x, y in zip(a, b)]


def gcd_of(v):
    v = [abs(x) for x in v if x]
    return functools.reduce(math.gcd, v) if v else 0


def section(title):
    print(f"\n## {title}\n")


def guest_busy(g, name, r):
    """Non-idle over all ticks, idle including iowait as wsl-collector/collector.py counts it."""
    a, b = g["stat"][name][r - 1], g["stat"][name][r]
    d = [y - x for x, y in zip(a, b)]
    total = sum(d[:8])
    return (d[3] + d[4], total)


def guest_cores(g, r):
    idle, total = guest_busy(g, "cpu", r)
    return LPS * (total - idle) / total if total else 0.0


def main():
    root, date = sys.argv[1], sys.argv[2]
    P = {w: load_probe(f"{root}/window{w}-probe-{date}.txt") for w in WINDOWS}
    G = {w: load_guest(f"{root}/window{w}-guest-{date}.txt") for w in WINDOWS}
    R = {w: load_record(f"{root}/window{w}-record-{date}.txt") for w in WINDOWS}

    print(f"# analysis of experiment 04, {date}, computed by analyze.py from the other files in results/")
    section("Coverage")
    for w in WINDOWS:
        p = P[w]
        print(f"window {w}: probe rounds {len(p['round'])}, guest rounds {len(G[w]['round'])}, record lines {len(R[w])}, "
              f"failed reads {len(p['fail'])}, instance-list changes after the first {p['name_changes']}, "
              f"counters read {len(p['pdh'])}, wsl.data null in {sum(1 for x in R[w] if not x['wsl']['data'])} lines")
        print(f"window {w}: wsl.age_s {rng([x['wsl']['age_s'] for x in R[w]])}; record interval_s {rng([x['interval_s'] for x in R[w]])}")

    section("Reads that returned no value")
    for w in WINDOWS:
        p = P[w]
        print(f"window {w}: whole counter arrays that failed, by round: {[(f[1], f[2].rsplit(chr(92), 1)[1]) for f in p['fail']]}; the probe did not print their status")
        for (path, code), n in sorted(p["status"].items()):
            print(f"window {w}: instance status 0x{code} on {path.rsplit(chr(92), 1)[1]}: {n} of {len(p['pdh'][path]) * len(p['names'][path])} instance readings")

    section("Processor Information: every counter, every window")
    print("total is the _Total instance over the window; lps is every LP instance over the window.")
    all_counters = ["% Processor Time", "% User Time", "% Privileged Time", "% Interrupt Time", "% DPC Time", "% Idle Time", "% Processor Performance",
                    "% Processor Utility", "Processor Frequency", "Parking Status", "Actual Frequency", "% of Maximum Frequency", "% Privileged Utility",
                    "% Performance Limit", "Performance Limit Flags"]
    for c in all_counters:
        path = PI + c
        for w in WINDOWS:
            ins = by_inst(P[w], path)
            lp = [v for n, s in ins.items() if lp_of(n) is not None for v in s.values()]
            total = list(ins["_Total"].values())
            node = list(ins["0,_Total"].values())
            eq = sum(1 for a, b in zip(total, node) if a != b)
            print(f"{c}, window {w}: total {rng(total)}; lps {rng(lp)}; distinct lp values {len(set(lp))}; 0,_Total differs from _Total in {eq} rounds")
    names = sorted(P[1]["names"][PI + "% Processor Time"])
    print(f"instances: {len(P[1]['names'][PI + '% Processor Time'])}, array order: {' '.join(P[1]['names'][PI + '% Processor Time'])}")

    section("Tick size of the times behind busy, user and kernel")
    for field in ("idle", "kernel", "user", "dpc", "interrupt"):
        g = gcd_of([d for w in WINDOWS for r in rounds(P[w]) for d in nt_delta(P[w], field, r)])
        print(f"NT {field}: gcd of every per-LP round delta, all windows: {g} (100 ns units), that is {g / 1e4:.4f} ms")
    for name in ("vmmemWSL", "vmwp"):
        v = []
        for w in WINDOWS:
            s = P[w]["proc"][name]
            for r in rounds(P[w]):
                v.append(s[r]["kernel"] - s[r - 1]["kernel"])
                v.append(s[r]["user"] - s[r - 1]["user"])
        print(f"{name} process times: gcd of round deltas {gcd_of(v)} (100 ns units)")
    cyc = [P[w]["proc"]["vmmemWSL"][r]["cycles"] - P[w]["proc"]["vmmemWSL"][r - 1]["cycles"] for w in WINDOWS for r in rounds(P[w])]
    print(f"vmmemWSL CycleTime: gcd of round deltas {gcd_of(cyc)}, distinct deltas {len(set(cyc))} of {len(cyc)}, smallest {min(cyc)}")
    interval = [dt(P[w], r) for w in WINDOWS for r in rounds(P[w])]
    print(f"probe round length, s: {rng(interval, 4)}")
    print("one 156250 tick in a 1.0 s interval is 1.5625 points on one LP and 0.078125 points on the 20 LP total")

    section("cpu.cpp's formula against PDH, on the _Total of each round")
    print("nt_busy is 100*(1-idle/(kernel+user)) as src/host/cpu.cpp computes it, from the same NT call, summed over LPs.")
    print("nt_user is 100*user/(kernel+user); nt_kernel is 100*(kernel-idle)/(kernel+user). KernelTime includes idle.")
    for w in WINDOWS:
        p = P[w]
        tot = lambda c: by_inst(p, PI + c)["_Total"]
        pt, pu, pp, pd, pi, pidle = (tot(c) for c in ("% Processor Time", "% User Time", "% Privileged Time", "% DPC Time", "% Interrupt Time", "% Idle Time"))
        rows = []
        for r in rounds(p):
            di, dk, du = sum(nt_delta(p, "idle", r)), sum(nt_delta(p, "kernel", r)), sum(nt_delta(p, "user", r))
            dd, dn = sum(nt_delta(p, "dpc", r)), sum(nt_delta(p, "interrupt", r))
            t = dk + du
            rows.append(dict(busy=100 * (1 - di / t), user=100 * du / t, kernel=100 * (dk - di) / t, dpc=100 * dd / t, irq=100 * dn / t,
                             wall=100 * t / (LPS * 1e7 * dt(p, r)), r=r))
        print(f"window {w}: nt_busy {rng([x['busy'] for x in rows])}")
        print(f"window {w}: nt_user {rng([x['user'] for x in rows])}")
        print(f"window {w}: nt_kernel {rng([x['kernel'] for x in rows])}")
        print(f"window {w}: nt_user+nt_kernel-nt_busy largest {max(abs(x['user'] + x['kernel'] - x['busy']) for x in rows):.9f}")
        print(f"window {w}: (kernel+user) as percent of 20 LPs times the round length {rng([x['wall'] for x in rows])}")
        print(f"window {w}: nt_busy-%ProcessorTime {rng([x['busy'] - pt[x['r']] for x in rows])}")
        print(f"window {w}: nt_user-%UserTime {rng([x['user'] - pu[x['r']] for x in rows])}")
        print(f"window {w}: nt_kernel-%PrivilegedTime {rng([x['kernel'] - pp[x['r']] for x in rows])}")
        print(f"window {w}: nt dpc-%DPCTime {rng([x['dpc'] - pd[x['r']] for x in rows])}")
        print(f"window {w}: nt interrupt-%InterruptTime {rng([x['irq'] - pi[x['r']] for x in rows])}")
        print(f"window {w}: nt_kernel minus nt dpc and interrupt, against %PrivilegedTime {rng([x['kernel'] - x['dpc'] - x['irq'] - pp[x['r']] for x in rows])}")
        print(f"window {w}: %UserTime+%PrivilegedTime-%ProcessorTime {rng([pu[x['r']] + pp[x['r']] - pt[x['r']] for x in rows])}")
        print(f"window {w}: %ProcessorTime+%IdleTime {rng([pt[x['r']] + pidle[x['r']] for x in rows])}")
        lp_idle = [v for s in lp_series(p, PI + "% Idle Time").values() for v in s.values()]
        print(f"window {w}: LP %IdleTime above 100 in {sum(1 for v in lp_idle if v > 100)} of {len(lp_idle)}, largest {max(lp_idle):.3f}")
        lp_pt = [v for s in lp_series(p, PI + "% Processor Time").values() for v in s.values()]
        print(f"window {w}: LP %ProcessorTime at exactly 0 in {sum(1 for v in lp_pt if v == 0)} of {len(lp_pt)}")
        rec = [x["cpu"]["busy_pct"] for x in R[w]]
        print(f"window {w}: winwsltop cpu.busy_pct {rng(rec)}; cost_ms.cpu {rng([x['cost_ms']['cpu'] for x in R[w]])}")
        procs = [x["windows_cores_sum"] for x in R[w]]
        print(f"window {w}: sum of Windows process cores {rng(procs)}; busy_pct times 20/100 {rng([b / 5 for b in rec])}")

    section("% Processor Utility and % Processor Performance")
    for w in WINDOWS:
        p = P[w]
        u, f, t = (by_inst(p, PI + c)["_Total"] for c in ("% Processor Utility", "% Processor Performance", "% Processor Time"))
        rs = sorted(set(u) & set(f) & set(t))
        print(f"window {w}: _Total utility over _Total processor time {rng([u[r] / t[r] for r in rs if t[r]])}")
        print(f"window {w}: _Total utility minus time times performance/100 {rng([u[r] - t[r] * f[r] / 100 for r in rs])}")
        lu, lt = lp_series(p, PI + "% Processor Utility"), lp_series(p, PI + "% Processor Time")
        allu = [v for s in lu.values() for v in s.values()]
        print(f"window {w}: LP utility above 100 in {sum(1 for v in allu if v > 100)} of {len(allu)}, largest {max(allu):.3f}; _Total largest {max(u.values()):.3f}")
        zero = [(lu[l][r]) for l in lu for r in lu[l] if lt[l].get(r) == 0]
        print(f"window {w}: utility on LP rounds whose processor time is exactly 0: {rng(zero)}")
        for c in ("% Processor Utility", "% Processor Performance"):
            a, b = p["pdh"][PI + c], p["capped"][PI + c]
            diff = sum(1 for r in a if r in b for x, y in zip(a[r], b[r]) if x != y)
            over = sum(1 for r in a for x in a[r] if x is not None and x > 100)
            top = max(x for r in b for x in b[r] if x is not None)
            print(f"window {w}: {c}: above 100 uncapped {over}, values changed by leaving out PDH_FMT_NOCAP100 {diff}, largest capped {top:.3f}")

    section("Frequency")
    for w in WINDOWS:
        p = P[w]
        fq, ac, pf, mx = (lp_series(p, PI + c) for c in ("Processor Frequency", "Actual Frequency", "% Processor Performance", "% of Maximum Frequency"))
        print(f"window {w}: Processor Frequency distinct values {sorted({v for s in fq.values() for v in s.values()})}")
        worst = max(abs(ac[l][r] - fq[l][r] * pf[l][r] / 100) for l in ac for r in ac[l] if r in pf[l] and r in fq[l])
        print(f"window {w}: largest |Actual Frequency - Processor Frequency*Performance/100| {worst:.3f}")
        for label, lps in (("P LPs 0-11", P_LPS), ("E LPs 12-19", E_LPS)):
            print(f"window {w}: {label}: Actual Frequency {rng([v for l in lps for v in ac[l].values()], 1)}")
            print(f"window {w}: {label}: % Processor Performance {rng([v for l in lps for v in pf[l].values()], 1)}")
            print(f"window {w}: {label}: % of Maximum Frequency {rng([v for l in lps for v in mx[l].values()], 1)}")
        rs = sorted(set.intersection(*[set(x) for x in ac.values()], *[set(x) for x in pf.values()], *[set(x) for x in mx.values()]))
        print(f"window {w}: r of % Processor Performance with % of Maximum Frequency, pooled over LP rounds "
              f"{fmt_r(pearson([pf[l][r] for l in ac for r in rs], [mx[l][r] for l in ac for r in rs]))}")
        for fld in ("max_mhz", "current_mhz", "limit_mhz"):
            print(f"window {w}: CallNtPowerInformation {fld} distinct values {sorted({v for s in p['power'][fld].values() for v in s})}")

    section("Parking")
    topo = lines(f"{root}/topology-{date}.txt")
    parked_flag = {int(f[3]): int(f[9], 16) & 1 for f in topo if f[0] == "cpuset"}
    print(f"topology file, CPU set Parked flag: parked LPs {[l for l, v in parked_flag.items() if v]}, unparked {[l for l, v in parked_flag.items() if not v]}")
    for w in WINDOWS:
        p = P[w]
        ps, lt, lu = (lp_series(p, PI + c) for c in ("Parking Status", "% Processor Time", "% Processor Utility"))
        rs = sorted(set.intersection(*[set(x) for x in ps.values()], *[set(x) for x in lt.values()]))
        share = {l: 100 * sum(ps[l][r] for r in rs) / len(rs) for l in sorted(ps)}
        print(f"window {w}: percent of rounds parked by LP {' '.join(f'{l}:{v:.0f}' for l, v in share.items())}")
        flips = sum(1 for l in ps for a, b in zip(rs, rs[1:]) if ps[l][a] != ps[l][b])
        print(f"window {w}: parking state changes between rounds, all LPs: {flips}; distinct values {sorted({v for s in ps.values() for v in s.values()})}")
        print(f"window {w}: parked LP rounds: processor time {rng([lt[l][r] for l in ps for r in rs if ps[l][r]])}")
        print(f"window {w}: unparked LP rounds: processor time {rng([lt[l][r] for l in ps for r in rs if not ps[l][r]])}")
        print(f"window {w}: parked LP rounds with processor time above 0: {sum(1 for l in ps for r in rs if ps[l][r] and lt[l][r] > 0)} of {sum(1 for l in ps for r in rs if ps[l][r])}")

    section("System")
    for c in ("Processor Queue Length", "Context Switches/sec", "System Calls/sec"):
        for w in WINDOWS:
            v = [x[0] for x in P[w]["pdh"][SYS + c].values()]
            print(f"{c}, window {w}: {rng(v)}; distinct {len(set(v))}")

    for w in WINDOWS:
        c = {r: x[0] for r, x in P[w]["pdh"][SYS + "System Calls/sec"].items()}
        top = sorted(c, key=lambda r: -c[r])[:2]
        pt = by_inst(P[w], PI + "% Processor Time")["_Total"]
        q = P[w]["pdh"][SYS + "Processor Queue Length"]
        cs = P[w]["pdh"][SYS + "Context Switches/sec"]
        print(f"window {w}: largest System Calls/sec rounds: " + "; ".join(f"round {r} {c[r]:.1f} with %ProcessorTime {pt.get(r, 0):.3f}, context switches {cs[r][0]:.1f}" for r in top))
        print(f"window {w}: Processor Queue Length above 0 in {sum(1 for x in q.values() if x[0] > 0)} of {len(q)} rounds; r with _Total %ProcessorTime {fmt_r(pearson([q[r][0] for r in sorted(q) if r in pt], [pt[r] for r in sorted(q) if r in pt]))}")

    section("Thermal zone")
    for c in ("Temperature", "High Precision Temperature", "% Passive Limit", "Throttle Reasons"):
        d, n = set(), 0
        for w in WINDOWS:
            for x in P[w]["pdh"][TZ + c].values():
                d.update(x)
                n += 1
        print(f"{c}: instances {P[1]['names'][TZ + c]}; distinct values over {n} rounds in 3 windows: {sorted(d)}")
    t = sorted({x for w in WINDOWS for v in P[w]["pdh"][TZ + "Temperature"].values() for x in v})
    print(f"Temperature in Celsius if the unit is Kelvin: {[round(x - 273.15, 2) for x in t]}")

    section("Hyper-V counters")
    for w in WINDOWS:
        p = P[w]
        for label, base, cs in (("VP", VP, ("% Guest Run Time", "% Hypervisor Run Time", "% Total Run Time")), ("LP", HL, ("% Guest Run Time", "% Hypervisor Run Time", "% Total Run Time", "% Idle Time")), ("root VP", RV, ("% Guest Run Time", "% Total Run Time"))):
            for c in cs:
                ins = by_inst(p, base + c)
                s = hv_series(p, base + c)
                tot = ins["_Total"]
                gap = max(abs(tot[r] - st.fmean(x[r] for x in s.values())) for r in tot if all(r in x for x in s.values()))
                print(f"{label} {c}, window {w}: _Total {rng(list(tot.values()))}; instances {len(s)}; largest |_Total - mean of instances| {gap:.6f}; largest instance {max(v for x in s.values() for v in x.values()):.3f}")
        mig, dis = hv_series(p, VP + "Logical Processor Migrations/sec"), hv_series(p, VP + "Logical Processor Dispatches/sec")
        rs = sorted(next(iter(mig.values())))
        tm = [sum(mig[v][r] for v in mig) for r in rs]
        td = [sum(dis[v][r] for v in dis) for r in rs]
        print(f"window {w}: migrations/s summed over VPs {rng(tm, 1)}; dispatches/s summed {rng(td, 1)}; migrations over dispatches {sum(tm) / sum(td):.3f}")
        zero = sum(1 for v in mig if sum(mig[v].values()) == 0)
        print(f"window {w}: VPs with no migration in the window {zero} of {len(mig)}; per-VP mean migrations/s {rng([st.fmean(x.values()) for x in mig.values()], 1)}")
        lg, rg, vg = by_inst(p, HL + "% Guest Run Time")["_Total"], by_inst(p, RV + "% Guest Run Time")["_Total"], by_inst(p, VP + "% Guest Run Time")["_Total"]
        common = sorted(set(lg) & set(rg) & set(vg))
        print(f"window {w}: LP guest minus (root VP guest + VP guest), points of 20 LPs {rng([lg[r] - rg[r] - vg[r] for r in common])}")
        pt = by_inst(p, PI + "% Processor Time")["_Total"]
        ut = by_inst(p, PI + "% Processor Utility")["_Total"]
        common = sorted(set(common) & set(pt) & set(ut))
        print(f"window {w}: %ProcessorTime minus root VP guest {rng([pt[r] - rg[r] for r in common])}")
        print(f"window {w}: %ProcessorTime minus LP guest {rng([pt[r] - lg[r] for r in common])}")
        print(f"window {w}: % Processor Utility minus root VP guest {rng([ut[r] - rg[r] for r in common])}")
        print(f"window {w}: VP guest _Total for scale {rng(list(vg.values()))}")
        x, y = [vg[r] for r in common], [pt[r] - rg[r] for r in common]
        mx, my = st.fmean(x), st.fmean(y)
        slope = sum((a - mx) * (b - my) for a, b in zip(x, y)) / sum((a - mx) ** 2 for a in x)
        print(f"window {w}: %ProcessorTime minus root VP guest against VP guest: slope {slope:.3f}, r {fmt_r(pearson(x, y))}, rank r {fmt_r(spearman(x, y))}, n {len(x)}")

    section("Does Hv LP n line up with Windows LP n?")
    print("r of % Processor Time on Windows LP n against % Total Run Time on Hv LP m, pooled over the three windows.")
    def pooled(a_get, b_get):
        a, b = [], []
        for w in WINDOWS:
            ra = a_get(w)
            rb = b_get(w)
            for r in sorted(set(ra) & set(rb)):
                a.append(ra[r])
                b.append(rb[r])
        return a, b
    def matrix(a_for, b_for, n=LPS):
        m = [[None] * n for _ in range(n)]
        for i in range(n):
            for j in range(n):
                x, y = pooled(lambda w: a_for(w, i), lambda w: b_for(w, j))
                m[i][j] = pearson(x, y)
        return m
    def report(m, label):
        diag = [m[i][i] for i in range(len(m)) if m[i][i] is not None]
        off = [m[i][j] for i in range(len(m)) for j in range(len(m)) if i != j and m[i][j] is not None]
        best = sum(1 for i in range(len(m)) if all((m[i][i] or -2) >= (x if x is not None else -2) for x in m[i]))
        print(f"{label}: diagonal {rng(diag)}; off-diagonal {rng(off)}; rows whose largest r is on the diagonal {best} of {len(m)}")
        return best
    lpt = {w: lp_series(P[w], PI + "% Processor Time") for w in WINDOWS}
    hlt = {w: hv_series(P[w], HL + "% Total Run Time") for w in WINDOWS}
    rvt = {w: hv_series(P[w], RV + "% Total Run Time") for w in WINDOWS}
    report(matrix(lambda w, i: lpt[w][i], lambda w, j: hlt[w][j]), "Windows LP n against Hv LP n")
    report(matrix(lambda w, i: lpt[w][i], lambda w, j: rvt[w][j]), "Windows LP n against Root VP n")

    section("The VM question, from winwsltop --record")
    print("guest_cores is wsl.data.busy_pct*20/100. vmmem is vm.vmmem_cores. vmwp is the cores of the process row vm.vmwp_pid.")
    print("Lag is in records: r of vmmem[t] with guest_cores[t+lag]. Chance is the largest |r| at lags 5 to 15 either way, in the same series.")
    series = {}
    for w in WINDOWS:
        rec = [x for x in R[w] if x["wsl"]["data"]]
        vmm = [x["vm"]["vmmem_cores"] for x in rec]
        gst = [x["wsl"]["data"]["busy_pct"] * 20 / 100 for x in rec]
        wp = [next((p["cores"] for p in x["processes"] if p["pid"] == x["vm"]["vmwp_pid"]), 0.0) for x in rec]
        gs = [x["wsl"]["data"]["guest_cores_sum"] for x in rec]
        win = [x["cpu"]["busy_pct"] * 20 / 100 for x in rec]
        iv = [x["interval_s"] for x in rec]
        series[w] = (vmm, gst, wp, win, iv)
        print(f"window {w}: n {len(rec)}; vmmem_cores {rng(vmm, 4)}; zero in {sum(1 for v in vmm if v == 0)}")
        print(f"window {w}: vmwp cores {rng(wp, 4)}; zero in {sum(1 for v in wp if v == 0)}")
        print(f"window {w}: guest_cores {rng(gst, 4)}; range {max(gst) - min(gst):.4f} cores, {100 * (max(gst) - min(gst)) / 20:.3f} points of 20; guest process cores sum {rng(gs, 4)}")
        print(f"window {w}: vmmem core-seconds {sum(a * b for a, b in zip(vmm, iv)):.3f}; vmwp {sum(a * b for a, b in zip(wp, iv)):.3f}; guest {sum(a * b for a, b in zip(gst, iv)):.3f}; Windows busy {sum(a * b for a, b in zip(win, iv)):.3f}")
        print(f"window {w}: r vmmem-guest by lag " + " ".join(f"{l:+d}:{fmt_r(lagged(vmm, gst, l))}" for l in range(-2, 3)))
        print(f"window {w}: r vmwp-guest by lag " + " ".join(f"{l:+d}:{fmt_r(lagged(wp, gst, l))}" for l in range(-2, 3)))
        print(f"window {w}: rank r vmmem-guest by lag " + " ".join(f"{l:+d}:{fmt_r(spearman(vmm[max(0, -l):len(vmm) - max(0, l)], gst[max(0, l):len(gst) - max(0, -l)]))}" for l in range(-2, 3)))
        ch = [abs(x) for l in list(range(5, 16)) + list(range(-15, -4)) if (x := lagged(vmm, gst, l)) is not None]
        print(f"window {w}: chance level for vmmem-guest, largest |r| at lags 5 to 15 either way {max(ch):.3f}")
    va, gb, wa = [], [], []
    for w in WINDOWS:
        va += series[w][0]
        gb += series[w][1]
        wa += series[w][2]
    print(f"pooled, n {len(va)}: r vmmem-guest lag 0 {fmt_r(pearson(va, gb))}; r vmwp-guest lag 0 {fmt_r(pearson(wa, gb))}")

    section("The VM question, from the raw counters in the probe and guest files")
    print("vp is the sum over VPs of % Guest Run Time/100, cores on the hypervisor's own account. vmmem is delta (kernel+user)/round length.")
    print("cycles is the delta of the process CycleTime field per second, in units of 1e9. guest is the jiffy busy share of /proc/stat times 20,")
    print("from the guest round whose midpoint is nearest in Windows time, using the median clock_offset_s of that window's records.")
    pooled_vp, pooled_guest, pooled_vm, pooled_cyc, pooled_wp = [], [], [], [], []
    for w in WINDOWS:
        p, g = P[w], G[w]
        off = st.median(x["wsl"]["clock_offset_s"] for x in R[w] if x["wsl"]["clock_known"])
        print(f"window {w}: median clock_offset_s {off:.3f}, spread {rng([x['wsl']['clock_offset_s'] for x in R[w] if x['wsl']['clock_known']])}")
        ug = sorted(r for r in g["round"] if r > 0 and r - 1 in g["round"])
        mid_g = {r: (g["round"][r][0] + g["round"][r - 1][0]) / 2 + off for r in ug}
        vpg = hv_series(p, VP + "% Guest Run Time")
        rs = [r for r in rounds(p) if r in next(iter(vpg.values()))]
        vp, vm, wp, cy, gs, gap = [], [], [], [], [], []
        for r in rs:
            win_t = ((p["round"][r][0] + p["round"][r - 1][0]) / 2) / 1e7 - 11644473600
            near = min(ug, key=lambda x: abs(mid_g[x] - win_t))
            d = abs(mid_g[near] - win_t)
            if d > 1.0:
                continue
            gap.append(d)
            t = dt(p, r)
            a, b = p["proc"]["vmmemWSL"][r - 1], p["proc"]["vmmemWSL"][r]
            c, e = p["proc"]["vmwp"][r - 1], p["proc"]["vmwp"][r]
            vm.append((b["kernel"] + b["user"] - a["kernel"] - a["user"]) / 1e7 / t)
            wp.append((e["kernel"] + e["user"] - c["kernel"] - c["user"]) / 1e7 / t)
            cy.append((b["cycles"] - a["cycles"]) / 1e9 / t)
            vp.append(sum(s[r] for s in vpg.values()) / 100)
            gs.append(guest_cores(g, near))
        print(f"window {w}: n {len(vp)}, round pairing distance s {rng(gap)}")
        print(f"window {w}: vp {rng(vp, 4)}; vmmem {rng(vm, 4)}; vmwp {rng(wp, 4)}; cycles {rng(cy, 4)}; guest {rng(gs, 4)}")
        print(f"window {w}: means over guest: vp {st.fmean(vp) / st.fmean(gs):.3f}, vmmem {st.fmean(vm) / st.fmean(gs):.3f}; vmmem over vp {st.fmean(vm) / st.fmean(vp):.3f}; cycles over vp {st.fmean(cy) / st.fmean(vp):.3f}")
        for name, s in (("vp-guest", (vp, gs)), ("vmmem-guest", (vm, gs)), ("cycles-guest", (cy, gs)), ("vmmem-vp", (vm, vp)), ("cycles-vp", (cy, vp)), ("vmwp-guest", (wp, gs))):
            print(f"window {w}: r {name} by lag " + " ".join(f"{l:+d}:{fmt_r(lagged(s[0], s[1], l))}" for l in range(-2, 3)))
        print(f"window {w}: rank r at lag 0: vp-guest {fmt_r(spearman(vp, gs))}, vmmem-guest {fmt_r(spearman(vm, gs))}, cycles-guest {fmt_r(spearman(cy, gs))}, cycles-vp {fmt_r(spearman(cy, vp))}")
        calm = [i for i, x in enumerate(gs) if x < 0.5]
        print(f"window {w}: rounds with guest under 0.5 cores {len(calm)}: r vp-guest {fmt_r(pearson([vp[i] for i in calm], [gs[i] for i in calm]))}, r vmmem-guest {fmt_r(pearson([vm[i] for i in calm], [gs[i] for i in calm]))}, r cycles-vp {fmt_r(pearson([cy[i] for i in calm], [vp[i] for i in calm]))}")
        pooled_vp += vp; pooled_guest += gs; pooled_vm += vm; pooled_cyc += cy; pooled_wp += wp
    print(f"pooled, n {len(pooled_vp)}: r vp-guest {fmt_r(pearson(pooled_vp, pooled_guest))}; r vmmem-guest {fmt_r(pearson(pooled_vm, pooled_guest))}; r cycles-guest {fmt_r(pearson(pooled_cyc, pooled_guest))}; r vmmem-vp {fmt_r(pearson(pooled_vm, pooled_vp))}; r cycles-vp {fmt_r(pearson(pooled_cyc, pooled_vp))}; r vmwp-guest {fmt_r(pearson(pooled_wp, pooled_guest))}")
    print(f"pooled range: guest {max(pooled_guest) - min(pooled_guest):.4f} cores, vp {max(pooled_vp) - min(pooled_vp):.4f}, vmmem {max(pooled_vm) - min(pooled_vm):.4f}")

    section("Numbers across the partition: guest cpuN against Hv VP n")
    print("Per-VP cumulative guest seconds against guest cpuN busy seconds, and r of the two series, pooled over the windows.")
    def guest_cpu_series(w, i):
        g = G[w]
        out = {}
        off = st.median(x["wsl"]["clock_offset_s"] for x in R[w] if x["wsl"]["clock_known"])
        ug = sorted(r for r in g["round"] if r > 0 and r - 1 in g["round"])
        mids = [(r, (g["round"][r][0] + g["round"][r - 1][0]) / 2 + off) for r in ug]
        p = P[w]
        for r in rounds(p):
            win_t = ((p["round"][r][0] + p["round"][r - 1][0]) / 2) / 1e7 - 11644473600
            near, when = min(mids, key=lambda x: abs(x[1] - win_t))
            if abs(when - win_t) > 1.0:
                continue
            idle, total = guest_busy(g, f"cpu{i}", near)
            out[r] = 100 * (total - idle) / total if total else 0.0
        return out
    gc = {(w, i): guest_cpu_series(w, i) for w in WINDOWS for i in range(LPS)}
    vpg = {w: hv_series(P[w], VP + "% Guest Run Time") for w in WINDOWS}
    m = matrix(lambda w, i: gc[(w, i)], lambda w, j: vpg[w][j])
    report(m, "guest cpuN against Hv VP n")
    print("vcpu  r_with_same_number  guest_busy_pct_mean  vp_guest_pct_mean")
    for i in range(LPS):
        x, y = pooled(lambda w: gc[(w, i)], lambda w: vpg[w][i])
        print(f"{i:4d}  {fmt_r(m[i][i]):>8}  {st.fmean(x):8.3f}  {st.fmean(y):8.3f}")

    section("Guest vCPU against host core")
    hlg = {w: hv_series(P[w], HL + "% Guest Run Time") for w in WINDOWS}
    report(matrix(lambda w, i: vpg[w][i], lambda w, j: hlg[w][j]), "Hv VP n against Hv LP n, % Guest Run Time (LP figure includes the root's guest time)")
    report(matrix(lambda w, i: gc[(w, i)], lambda w, j: lpt[w][j]), "guest cpuN against Windows LP n, % Processor Time")
    ct = [(i, j, m2) for i, row in enumerate(matrix(lambda w, i: vpg[w][i], lambda w, j: lpt[w][j])) for j, m2 in enumerate(row) if m2 is not None]
    top = sorted(ct, key=lambda x: -x[2])[:5]
    print("five largest r of Hv VP i with Windows LP j: " + " ".join(f"VP{i}-LP{j}:{x:+.3f}" for i, j, x in top) + f"; n {sum(len(next(iter(vpg[w].values()))) for w in WINDOWS)}, pairs tried {len(ct)}")

    section("vmmemWSL threads")
    for w in WINDOWS:
        p = P[w]
        rs = sorted(p["thr"])
        first, last = p["thr"][rs[0]], p["thr"][rs[-1]]
        moved = {t: last[t]["kernel"] + last[t]["user"] - first[t]["kernel"] - first[t]["user"] for t in first if t in last}
        active = sorted((v for v in moved.values() if v), reverse=True)
        a, b = p["proc"]["vmmemWSL"][rs[0]], p["proc"]["vmmemWSL"][rs[-1]]
        proc_delta = b["kernel"] + b["user"] - a["kernel"] - a["user"]
        print(f"window {w}: threads {len(first)}; pid {a['pid']}; threads whose time moved {len(active)}; thread time sum {sum(active)}; process time delta {proc_delta}; "
              f"sum over process {sum(active) / proc_delta if proc_delta else 0:.4f}; threads in first and last round {len(set(first) & set(last))}")
    tids = sorted(set(P[1]["thr"][min(P[1]["thr"])]) & set(P[3]["thr"][max(P[3]["thr"])]))
    print(f"threads present in window 1 and window 3: {len(tids)}")

    section("WSL: /proc/stat fields, per window, ticks per second of the whole guest")
    fields = ("user", "nice", "system", "idle", "iowait", "irq", "softirq", "steal", "guest", "guest_nice")
    for w in WINDOWS:
        g = G[w]
        rs = sorted(r for r in g["round"] if r > 0 and r - 1 in g["round"])
        for i, f in enumerate(fields):
            d = [(g["stat"]["cpu"][r][i] - g["stat"]["cpu"][r - 1][i]) / (g["round"][r][1] - g["round"][r - 1][1]) for r in rs]
            print(f"{f}, window {w}: {rng(d, 1)}; total ticks {g['stat']['cpu'][rs[-1]][i] - g['stat']['cpu'][rs[0] - 1][i]}")
        tot = sum(sum(g["stat"]["cpu"][rs[-1]][:8])for _ in [0]) - sum(g["stat"]["cpu"][rs[0] - 1][:8])
        span = g["round"][rs[-1]][1] - g["round"][rs[0] - 1][1]
        print(f"window {w}: ticks over all fields per guest second {tot / span:.1f}, over {span:.3f} guest s")
        cts = [(g["ctxt"][r] - g["ctxt"][r - 1]) / (g["round"][r][1] - g["round"][r - 1][1]) for r in rs]
        print(f"ctxt/s, window {w}: {rng(cts, 1)}")
        print(f"procs_running, window {w}: {rng(list(g['running'].values()), 2)}; distinct {sorted(set(g['running'].values()))}")
        print(f"procs_blocked, window {w}: {rng(list(g['blocked'].values()), 2)}; distinct {sorted(set(g['blocked'].values()))}")
        l1 = [float(x.split()[0]) for x in g["load"].values()]
        print(f"loadavg 1 min, window {w}: {rng(l1, 2)}; distinct {len(set(l1))}")
        mz = {x for v in g["mhz"].values() for x in v}
        print(f"cpuinfo MHz, window {w}: distinct values {sorted(mz)}, lines per round {sorted({len(v) for v in g['mhz'].values()})}")
        some = g["psi"]["some"]
        print(f"psi some avg10, window {w}: {rng([float(x['avg10']) for x in some.values()], 2)}; total delta {int(some[max(some)]['total']) - int(some[min(some)]['total'])} us")
        full = g["psi"]["full"]
        print(f"psi full avg10, window {w}: {rng([float(x['avg10']) for x in full.values()], 2)}; total delta {int(full[max(full)]['total']) - int(full[min(full)]['total'])} us")
        cg = g["cgroup"]
        print(f"cgroup cpu.stat usage_usec delta, window {w}: {int(cg[max(cg)]['usage_usec']) - int(cg[min(cg)]['usage_usec'])}; keys {sorted(next(iter(cg.values())))}")

    section("Guest clock, as a side check on the rates above")
    for w in WINDOWS:
        rec = [x for x in R[w] if x["wsl"]["data"]]
        xs = [x["unix_ms"] / 1000 - x["wsl"]["age_s"] for x in rec]
        ys = [x["wsl"]["data"]["mono_s"] for x in rec]
        mx, my = st.fmean(xs), st.fmean(ys)
        slope = sum((a - mx) * (b - my) for a, b in zip(xs, ys)) / sum((a - mx) ** 2 for a in xs)
        print(f"window {w}: least-squares slope of wsl.data.mono_s on Windows unix time less age_s: {slope:.5f}, over {xs[-1] - xs[0]:.1f} s, n {len(xs)}")

    section("Cost")
    rows = {}
    setup = []
    seen = set()
    for f in lines(f"{root}/cost-{date}.txt"):
        if f[0] == "cost":
            first = int(f[1]) not in seen
            seen.add(int(f[1]))
            rows.setdefault(f[2], []).append((int(f[1]), float(f[3]) + float(f[4]), int(f[5]), first))
        elif f[0] == "setup":
            setup.append(f)
    for f in setup:
        print(f"setup {f[1]}: open and add {f[3]} ms, first collect {f[5]} ms")
    print("Group order rotates each round. first means the group ran first in its round, after a gap of about a second.")
    for name, v in rows.items():
        ms = sorted(x[1] for x in v)
        p95 = ms[min(len(ms) - 1, int(0.95 * len(ms)))]
        one = [x[1] for x in v if x[3]]
        print(f"{name}: n {len(ms)}, instances {sorted({x[2] for x in v})}, rounds below the largest count {sum(1 for x in v if x[2] != max(y[2] for y in v))}, collect+format ms mean {st.fmean(ms):.3f} median {st.median(ms):.3f} p95 {p95:.3f} max {ms[-1]:.3f}; when first in its round, {rng(one)}")


main()
