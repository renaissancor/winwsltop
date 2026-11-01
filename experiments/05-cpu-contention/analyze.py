#!/usr/bin/env python3
"""Turns the raw files of experiment 05 into analysis-<date>.txt. Reads only those files.

Usage: analyze.py <results dir> <run> <date>. Rerun it alone at any time; it needs no live machine.

A rate at round r covers rounds r-1 to r. A case mean uses only the rounds that lie wholly inside the
case, at least MARGIN_HEAD s after it starts and MARGIN_TAIL s before it ends. Windows times are moved
onto the WSL clock by the offset the clock pings give. One trial of each case.
"""
import functools
import json
import math
import statistics as st
import sys

LPS = 20
P_LPS = list(range(0, 12))   # topology file: efficiency class 1, two threads a core
E_LPS = list(range(12, 20))  # efficiency class 0, one thread a core
MARGIN_HEAD, MARGIN_TAIL = 2.0, 1.0
PI = "\\Processor Information(*)\\"
VP = "\\Hyper-V Hypervisor Virtual Processor(*)\\"
HL = "\\Hyper-V Hypervisor Logical Processor(*)\\"
RV = "\\Hyper-V Hypervisor Root Virtual Processor(*)\\"
SY = "\\System\\"
TZ = "\\Thermal Zone Information(*)\\"
CASES = ["baseline", "wsl-1", "wsl-2", "wsl-5", "wsl-10", "wsl-20", "settle-1", "win-20", "settle-2", "both-before",
         "both-win-5", "both-win-10", "both-win-20", "both-after", "settle-3"]
GRADES = [("baseline", 0), ("wsl-1", 1), ("wsl-2", 2), ("wsl-5", 5), ("wsl-10", 10), ("wsl-20", 20)]
READY = (1, 7)  # Ready, DeferredReady


def lines(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return [x.rstrip("\n").split("\t") for x in f]


def num(x):
    return None if x.startswith("!") or x == "" else float(x)


def mean(v):
    v = [x for x in v if x is not None and not (isinstance(x, float) and math.isnan(x))]
    return sum(v) / len(v) if v else float("nan")


def pearson(a, b):
    p = [(x, y) for x, y in zip(a, b) if x is not None and y is not None and not math.isnan(x) and not math.isnan(y)]
    if len(p) < 3:
        return float("nan")
    ma, mb = mean([x for x, _ in p]), mean([y for _, y in p])
    sa = sum((x - ma) ** 2 for x, _ in p)
    sb = sum((y - mb) ** 2 for _, y in p)
    if sa == 0 or sb == 0:
        return float("nan")
    return sum((x - ma) * (y - mb) for x, y in p) / math.sqrt(sa * sb)


def ranks(v):
    order = sorted(range(len(v)), key=lambda i: v[i])
    r = [0.0] * len(v)
    i = 0
    while i < len(order):
        j = i
        while j + 1 < len(order) and v[order[j + 1]] == v[order[i]]:
            j += 1
        for k in range(i, j + 1):
            r[order[k]] = (i + j) / 2
        i = j + 1
    return r


def spearman(a, b):
    p = [(x, y) for x, y in zip(a, b) if x is not None and y is not None and not math.isnan(x) and not math.isnan(y)]
    if len(p) < 3:
        return float("nan")
    return pearson(ranks([x for x, _ in p]), ranks([y for _, y in p]))


def fit(xs, ys):
    mx, my = mean(xs), mean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    return slope, my - slope * mx


# ---- loading ------------------------------------------------------------------------------------

def load_marks(path):
    marks, pings = {}, {}
    for f in lines(path):
        if f[0] == "mark":
            marks[f[1]] = float(f[2])
        elif f[0] == "clock":
            pings[(f[1], f[2], f[3])] = float(f[4])
        elif f[0] in ("start", "end") and f[2] == "clock" or (len(f) == 4 and f[2] == "clock"):
            pings[(f[0], f[1], "win")] = int(f[3]) / 1e7 - 11644473600.0
    return marks, pings


def load_probe(path):
    p = dict(round={}, nt={}, proc={}, thr={}, states={}, pdh={}, names={})
    for f in lines(path):
        k = f[0]
        if k == "round":
            p["round"][int(f[1])] = int(f[2]) / 1e7 - 11644473600.0
        elif k == "nt":
            p["nt"].setdefault(f[2], {})[int(f[1])] = [int(x) for x in f[3:]]
        elif k == "proc":
            p["proc"].setdefault(f[2], {})[int(f[1])] = dict(kernel=int(f[4]), user=int(f[5]), cycles=int(f[6]), threads=int(f[7]))
        elif k == "thr":
            p["thr"].setdefault(int(f[1]), {})[int(f[2])] = dict(kernel=int(f[3]), user=int(f[4]), cs=int(f[5]), state=int(f[6]), wait=int(f[7]), prio=int(f[8]))
        elif k == "states":
            p["states"].setdefault(f[2], {})[int(f[1])] = [int(x) for x in f[3:]]
        elif k == "names":
            p["names"][f[2]] = f[3:]
        elif k == "pdh":
            names = p["names"].get(f[2], [])
            p["pdh"].setdefault(f[2], {})[int(f[1])] = dict(zip(names, [num(x) for x in f[3:]]))
    return p


def load_guest(path):
    g = dict(round={}, stat={}, ctxt={}, running={}, blocked={}, psi={}, tasks={}, top={}, burn={}, procs={}, spent={}, mhz={}, loadavg={}, cgroup={})
    for f in lines(path):
        k = f[0]
        if k == "round":
            g["round"][int(f[1])] = (float(f[2]), float(f[3]))
        elif k == "stat":
            r, key = int(f[1]), f[2]
            if key.startswith("cpu"):
                g["stat"].setdefault(key, {})[r] = [int(x) for x in f[3:]]
            elif key == "ctxt":
                g["ctxt"][r] = int(f[3])
            elif key == "procs_running":
                g["running"][r] = int(f[3])
            elif key == "procs_blocked":
                g["blocked"][r] = int(f[3])
        elif k == "psi":
            r, res = int(f[1]), f[2]
            w = f[3].split()
            g["psi"].setdefault((res, w[0]), {})[r] = dict(x.split("=") for x in w[1:])
        elif k == "tasks":
            g["tasks"][int(f[1])] = dict(x.split("=") for x in f[3:] if x)
        elif k == "top":
            g["top"].setdefault(int(f[1]), []).append(dict(pid=int(f[2]), name=f[3], state=f[4], threads=int(f[5]), cpu=int(f[6]), d=int(f[7])))
        elif k == "burntask":
            g["burn"].setdefault(int(f[1]), []).append(dict(tid=int(f[2]), state=f[3], cpu=int(f[4]), ticks=int(f[5])))
        elif k == "procs":
            g["procs"][int(f[1])] = int(f[2])
        elif k == "spent":
            g["spent"][int(f[1])] = float(f[2])
        elif k == "mhz":
            g["mhz"][int(f[1])] = sorted(set(f[2:]))
        elif k == "loadavg":
            g["loadavg"][int(f[1])] = f[2].split()
        elif k == "cgroup":
            g["cgroup"][int(f[1])] = f[2]
    return g


def load_burn(path):
    """Runs of one burner, in order: label, threads, seq -> row."""
    runs, cur = [], None
    for f in lines(path):
        k = f[0]
        if k == "start":
            cur = dict(label=f[1], threads=int(f[2]), seconds=int(f[3]), start=int(f[4]) / 1000, rows={}, end=None)
            runs.append(cur)
        elif k == "end":
            cur["end"] = int(f[2]) / 1000
        elif k in ("units", "cpu_us", "cycles", "cpu", "moves", "bycpu"):
            s = int(f[1])
            r = cur["rows"].setdefault(s, {})
            r["t"] = int(f[2]) / 1000
            r[k] = [int(x) for x in f[3:]]
    return runs


# ---- the run, reduced to rate series on the WSL clock ---------------------------------------------

class Run:
    def __init__(self, results, run, date):
        path = f"{results}/{run}-%s-{date}.txt"
        self.probe_path = path % "probe"
        self.marks, self.pings = load_marks(path % "marks")
        self.p = load_probe(path % "probe")
        self.g = load_guest(path % "guest")
        self.wb = load_burn(path % "burn-windows")
        self.lb = load_burn(path % "burn-wsl")
        self.rec = [json.loads(x) for x in open(path % "record", encoding="utf-8") if x.strip()]
        self.off = []
        for s in ("start", "end"):
            for i in "123":
                a, b = self.pings[(s, i, "before")], self.pings[(s, i, "after")]
                self.off.append((s, i, self.pings[(s, i, "win")] - (a + b) / 2, (b - a) / 2))
        self.offset = mean([x[2] for x in self.off])   # Windows less WSL
        m = self.marks
        self.case = {}
        for c in CASES:
            self.case[c] = (m[c], m[f"{c}-end"]) if f"{c}-end" in m else None
        self.case["both-before"] = (m["both-before"], m["both-win-5"])
        self.case["both-after"] = (m["both-after"], m["both-after-end"])
        self.s = {}
        self.build()

    def put(self, name, t0, t1, v):
        self.s.setdefault(name, []).append((t0, t1, v))

    def build(self):
        p, g = self.p, self.g
        # guest
        rounds = sorted(g["round"])
        keys = sorted(g["stat"], key=lambda k: -1 if k == "cpu" else int(k[3:]))
        for r in rounds[1:]:
            t0, t1 = g["round"][r - 1][0], g["round"][r][0]
            dt = g["round"][r][1] - g["round"][r - 1][1]
            for k in keys:
                a, b = g["stat"][k].get(r - 1), g["stat"][k].get(r)
                d = [y - x for x, y in zip(a, b)]
                total = sum(d[:8])
                busy = total - d[3] - d[4]
                if k == "cpu":
                    self.put("g_busy_pct", t0, t1, 100 * busy / total)
                    self.put("g_busy_cores", t0, t1, LPS * busy / total)   # the collector's busy, times threads
                    self.put("g_ticks", t0, t1, total / dt)
                    self.put("g_busy_ticks_cores", t0, t1, busy / dt / 100)
                    self.put("g_user", t0, t1, d[0] / dt)
                    self.put("g_sys", t0, t1, d[2] / dt)
                    self.put("g_idle", t0, t1, d[3] / dt)
                    self.put("g_iowait", t0, t1, d[4] / dt)
                    self.put("g_irq", t0, t1, d[5] / dt)
                    self.put("g_softirq", t0, t1, d[6] / dt)
                    self.put("g_steal", t0, t1, d[7] / dt)
                    self.put("g_guest", t0, t1, d[8] / dt)
                else:
                    self.put("gc_busy_" + k[3:], t0, t1, busy / dt)
                    self.put("gc_ticks_" + k[3:], t0, t1, total / dt)
                    self.put("gc_steal_" + k[3:], t0, t1, d[7] / dt)
            self.put("g_ctxt", t0, t1, (g["ctxt"][r] - g["ctxt"][r - 1]) / dt)
            self.put("g_running", t0, t1, g["running"][r])
            self.put("g_blocked", t0, t1, g["blocked"][r])
            self.put("g_tasks_D", t0, t1, int(g["tasks"][r].get("D", 0)))
            self.put("g_tasks_R", t0, t1, int(g["tasks"][r].get("R", 0)))
            self.put("g_spent", t0, t1, g["spent"][r])
            self.put("g_load1", t0, t1, float(g["loadavg"][r][0]))
            for key, series in g["psi"].items():
                a, b = series.get(r - 1), series.get(r)
                self.put(f"psi_{key[0]}_{key[1]}_rate", t0, t1, (int(b["total"]) - int(a["total"])) / dt)
                self.put(f"psi_{key[0]}_{key[1]}_avg10", t0, t1, float(b["avg10"]))
        # Windows
        off = self.offset
        for r in sorted(p["round"])[1:]:
            t0, t1 = p["round"][r - 1] - off, p["round"][r] - off
            dt = p["round"][r] - p["round"][r - 1]
            nt = p["nt"]
            d = {k: [y - x for x, y in zip(nt[k][r - 1], nt[k][r])] for k in ("idle", "kernel", "user", "dpc", "interrupt")}
            k, u, i = sum(d["kernel"]), sum(d["user"]), sum(d["idle"])
            self.put("w_busy_pct", t0, t1, 100 * (1 - i / (k + u)))
            self.put("w_busy_cores", t0, t1, LPS * (1 - i / (k + u)))
            self.put("w_user_pct", t0, t1, 100 * u / (k + u))
            self.put("w_kernel_pct", t0, t1, 100 * (k - i) / (k + u))
            self.put("w_dpc_pct", t0, t1, 100 * sum(d["dpc"]) / (k + u))
            self.put("w_int_pct", t0, t1, 100 * sum(d["interrupt"]) / (k + u))
            self.put("w_total_vs_wall", t0, t1, (k + u) / 1e7 / dt / LPS)
            for lp in range(LPS):
                tot = d["kernel"][lp] + d["user"][lp]
                if tot:
                    self.put(f"lp_busy_{lp}", t0, t1, 100 * (1 - d["idle"][lp] / tot))
                    self.put(f"lp_user_{lp}", t0, t1, 100 * d["user"][lp] / tot)
                    self.put(f"lp_kern_{lp}", t0, t1, 100 * (d["kernel"][lp] - d["idle"][lp]) / tot)
                    self.put(f"lp_dpc_{lp}", t0, t1, 100 * d["dpc"][lp] / tot)
                    self.put(f"lp_int_{lp}", t0, t1, 100 * d["interrupt"][lp] / tot)
            for name in ("vmmemWSL", "vmwp", "cpu_burn"):
                a, b = p["proc"].get(name, {}).get(r - 1), p["proc"].get(name, {}).get(r)
                if a and b:
                    self.put(f"{name}_kernel", t0, t1, (b["kernel"] - a["kernel"]) / 1e7 / dt)
                    self.put(f"{name}_user", t0, t1, (b["user"] - a["user"]) / 1e7 / dt)
                    self.put(f"{name}_cores", t0, t1, (b["kernel"] + b["user"] - a["kernel"] - a["user"]) / 1e7 / dt)
                    self.put(f"{name}_cycles", t0, t1, (b["cycles"] - a["cycles"]) / dt)
                elif name == "cpu_burn":
                    self.put(f"{name}_cores", t0, t1, 0.0)
            for scope in ("vmmemWSL", "cpu_burn", "rest", "idle", "probe"):
                s = p["states"][scope][r]
                self.put(f"st_{scope}_ready", t0, t1, sum(s[x] for x in READY))
                self.put(f"st_{scope}_running", t0, t1, s[2])
                self.put(f"st_{scope}_standby", t0, t1, s[3])
            self.put("st_all_ready", t0, t1, sum(sum(p["states"][x][r][y] for y in READY) for x in ("vmmemWSL", "cpu_burn", "rest", "probe")))
            self.put("st_win_ready", t0, t1, sum(sum(p["states"][x][r][y] for y in READY) for x in ("cpu_burn", "rest", "probe")))
            self.pdh_round(r, t0, t1)
        # record
        for x in self.rec:
            t1 = x["unix_ms"] / 1000 - off
            t0 = t1 - (x.get("interval_s") or 1.0)
            self.put("r_vmmem_cores", t0, t1, x["vm"]["vmmem_cores"])
            self.put("r_other_cores", t0, t1, x["other_cores_sum"])
            self.put("r_busy", t0, t1, x["cpu"]["busy_pct"])
            d = (x.get("wsl") or {}).get("data")
            if d:
                self.put("r_wsl_busy", t0, t1, d.get("busy_pct"))
                self.put("r_wsl_steal", t0, t1, d.get("steal_pct"))
                self.put("r_wsl_other_cores", t0, t1, d.get("other_cores_sum"))
        # burners
        for side, runs in (("wsl", self.lb), ("win", self.wb)):
            for b in runs:
                shift = off if side == "win" else 0.0
                ks = sorted(b["rows"])
                b["t"] = [b["rows"][k]["t"] - shift for k in ks]
                b["u"] = [sum(b["rows"][k]["units"]) for k in ks]
                for k in ks[1:]:
                    a, c = b["rows"][k - 1], b["rows"][k]
                    t0, t1 = a["t"] - shift, c["t"] - shift
                    dt = t1 - t0
                    if dt <= 0:
                        continue
                    self.put(f"{side}_units", t0, t1, (sum(c["units"]) - sum(a["units"])) / dt)
                    self.put(f"{side}_cpu_cores", t0, t1, (sum(c["cpu_us"]) - sum(a["cpu_us"])) / 1e6 / dt)
                    self.put(f"{side}_moves", t0, t1, (sum(c["moves"]) - sum(a["moves"])) / dt)

    def pdh_round(self, r, t0, t1):
        pdh = self.p["pdh"]

        def inst(path):
            return pdh.get(path, {}).get(r, {})

        def total(path, name="_Total"):
            return inst(path).get(name)

        def summed(path):
            return sum(v for k, v in inst(path).items() if v is not None and "_Total" not in k)

        def over(path, lps):
            vals = inst(path)
            return [vals.get(f"0,{i}") for i in lps]

        for key, path in (("util", PI + "% Processor Utility"), ("perf", PI + "% Processor Performance"), ("ptime", PI + "% Processor Time"),
                          ("freq", PI + "Actual Frequency"), ("maxf", PI + "% of Maximum Frequency"), ("limit", PI + "% Performance Limit"),
                          ("limflags", PI + "Performance Limit Flags")):
            self.put("pi_" + key, t0, t1, total(path))
            self.put("pi_" + key + "_P", t0, t1, mean(over(path, P_LPS)))
            self.put("pi_" + key + "_E", t0, t1, mean(over(path, E_LPS)))
            for lp, v in zip(range(LPS), over(path, range(LPS))):
                self.put(f"pl_{key}_{lp}", t0, t1, v)
        for key, name in (("queue", "Processor Queue Length"), ("ctx", "Context Switches/sec"), ("calls", "System Calls/sec")):
            v = inst(SY + name)
            self.put("sys_" + key, t0, t1, list(v.values())[0] if v else None)
        for key, name in (("temp", "Temperature"), ("hptemp", "High Precision Temperature"), ("passive", "% Passive Limit"), ("reasons", "Throttle Reasons")):
            v = inst(TZ + name)
            self.put("tz_" + key, t0, t1, list(v.values())[0] if v else None)
        for key, name in (("guest", "% Guest Run Time"), ("hyp", "% Hypervisor Run Time"), ("total", "% Total Run Time"), ("corerun", "% Total Core Run Time"),
                          ("relutil", "% Guest-Relative Utilization")):
            self.put("vp_" + key, t0, t1, summed(VP + name) / 100)
            self.put("vp_" + key + "_t", t0, t1, total(VP + name))
            for k, v in inst(VP + name).items():
                if "_Total" not in k:
                    self.put(f"vpn_{key}_{k.split('Hv VP ')[-1]}", t0, t1, v)
        for key, name in (("wait", "CPU Wait Time Per Dispatch"), ("cont", "CPU Contention Time Per Dispatch"), ("wake", "CPU Wake Up Time Per Dispatch")):
            self.put("vp_" + key, t0, t1, total(VP + name))
            vals = [v for k, v in inst(VP + name).items() if "_Total" not in k and v is not None]
            self.put("vp_" + key + "_inst_mean", t0, t1, mean(vals))
            self.put("vp_" + key + "_inst_max", t0, t1, max(vals) if vals else None)
        self.put("vp_disp", t0, t1, summed(VP + "Logical Processor Dispatches/sec"))
        self.put("vp_migr", t0, t1, summed(VP + "Logical Processor Migrations/sec"))
        for k, v in inst(VP + "Logical Processor Migrations/sec").items():
            if "_Total" not in k:
                self.put(f"vpn_migr_{k.split('Hv VP ')[-1]}", t0, t1, v)
        for key, name in (("guest", "% Guest Run Time"), ("hyp", "% Hypervisor Run Time"), ("total", "% Total Run Time"), ("idle", "% Idle Time")):
            self.put("lp_" + key, t0, t1, total(HL + name))
        for lp in range(LPS):
            self.put(f"hlp_total_{lp}", t0, t1, inst(HL + "% Total Run Time").get(f"Hv LP {lp}"))
        self.put("rv_guest", t0, t1, summed(RV + "% Guest Run Time") / 100)
        self.put("rv_total", t0, t1, summed(RV + "% Total Run Time") / 100)

    def rows(self, name, case):
        a, b = self.case[case]
        a += MARGIN_HEAD
        b -= MARGIN_TAIL
        return [v for t0, t1, v in self.s.get(name, []) if t0 >= a and t1 <= b and v is not None]

    def val(self, name, case):
        return mean(self.rows(name, case))

    def nrows(self, name, case):
        return len(self.rows(name, case))

    def rng(self, name, case):
        v = self.rows(name, case)
        return (min(v), max(v)) if v else (float("nan"), float("nan"))

    def over(self, side, t0, t1):
        """Units a second over [t0, t1], from the cumulative count of the burner running then."""
        for b in (self.lb if side == "wsl" else self.wb):
            if b["t"] and b["t"][0] <= t0 and t1 <= b["t"][-1]:
                return (interp(b["t"], b["u"], t1) - interp(b["t"], b["u"], t0)) / (t1 - t0)
        return None


def interp(ts, vs, t):
    lo, hi = 0, len(ts) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if ts[mid] <= t:
            lo = mid
        else:
            hi = mid
    return vs[lo] + (vs[hi] - vs[lo]) * (t - ts[lo]) / (ts[hi] - ts[lo])


def table(head, rows, fmt=None):
    w = [max(len(str(head[i])), *(len(str(r[i])) for r in rows)) if rows else len(str(head[i])) for i in range(len(head))]
    print("  " + "  ".join(str(h).rjust(w[i]) for i, h in enumerate(head)))
    for r in rows:
        print("  " + "  ".join(str(x).rjust(w[i]) for i, x in enumerate(r)))


def f(x, n=3):
    if x is None or (isinstance(x, float) and math.isnan(x)):
        return "n/a"
    return f"{x:.{n}f}"


def fs(x, n=3):
    if x is None or (isinstance(x, float) and math.isnan(x)):
        return "n/a"
    return f"{x:+.{n}f}"


# ---- the report ----------------------------------------------------------------------------------

def section(t):
    print()
    print("=== " + t)


def header(x):
    section("run")
    print("margins: first %.1f s and last %.1f s of a case left out. One trial of each case." % (MARGIN_HEAD, MARGIN_TAIL))
    print("clock: Windows less WSL, from the pings, s:", " ".join(f"{s}{i}={v:+.3f}(+-{e:.3f})" for s, i, v, e in x.off))
    print(f"clock offset used, mean of the six: {x.offset:+.3f} s")
    m = x.marks
    print(f"first mark {m['lead-end']:.3f}, load from {x.lb[0]['start']:.3f} to {x.lb[-1]['end']:.3f} unix s, {x.lb[-1]['end'] - x.lb[0]['start']:.1f} s of which burners ran "
          f"{sum(b['end'] - b['start'] for b in x.lb) + sum(b['end'] - b['start'] for b in x.wb if b['label'] == 'win-20'):.1f} s")
    print("recorded rounds: probe %d, guest %d, record %d" % (len(x.p["round"]), len(x.g["round"]), len(x.rec)))
    rows = []
    for c in CASES:
        a, b = x.case[c]
        rows.append([c, f(b - a, 1), x.nrows("w_busy_pct", c), x.nrows("g_busy_pct", c), x.nrows("r_vmmem_cores", c)])
    table(["case", "s", "probe rounds", "guest rounds", "records"], rows)


def work(x):
    section("ground truth: work per second")
    print("units/s over all threads, the mean of the burner's per-second rows; cpu = thread CPU cores as that side's OS counts them")
    rows = []
    for c in CASES:
        if x.nrows("wsl_units", c) == 0 and x.nrows("win_units", c) == 0:
            continue
        rows.append([c, f(x.val("wsl_units", c), 1), x.nrows("wsl_units", c), f(x.val("wsl_cpu_cores", c)), f(x.val("win_units", c), 1), x.nrows("win_units", c), f(x.val("win_cpu_cores", c)),
                     f(x.val("wsl_moves", c), 1), f(x.val("win_moves", c), 1)])
    table(["case", "wsl units/s", "n", "wsl cpu", "win units/s", "n", "win cpu", "wsl moves/s", "win moves/s"], rows)
    ref = mean([x.val("wsl_units", "both-before"), x.val("wsl_units", "both-after")])
    x.ref = ref
    print(f"WSL reference, mean of both-before and both-after: {ref:.1f}; wsl-20 alone {x.val('wsl_units', 'wsl-20'):.1f}")
    rows = []
    for c in ("both-before", "both-win-5", "both-win-10", "both-win-20", "both-after"):
        u = x.val("wsl_units", c)
        rows.append([c, f(u, 1), f(100 * (1 - u / ref)), f(100 * (1 - u / x.val("wsl_units", "wsl-20")))])
    table(["case", "wsl units/s", "loss % of reference", "loss % of wsl-20"], rows)
    wa, wb = x.val("win_units", "win-20"), x.val("win_units", "both-win-20")
    print(f"Windows burner, 20 threads: alone {wa:.1f}, with WSL 20 {wb:.1f}, loss {100 * (1 - wb / wa):.3f}%")
    print(f"Windows 20 alone over WSL 20 alone: {wa / x.val('wsl_units', 'wsl-20'):.4f}")
    print("per-second range of WSL units/s in both-win-20:", " to ".join(f(v, 1) for v in x.rng("wsl_units", "both-win-20")),
          "; Windows:", " to ".join(f(v, 1) for v in x.rng("win_units", "both-win-20")))
    print()
    print("units/s per processor the burner was on, from bycpu rows, whole run of that burner less the first 2 and last rows")
    for side, runs in (("wsl", x.lb), ("win", x.wb)):
        for b in runs:
            ks = sorted(b["rows"])
            a, c = b["rows"][ks[2]], b["rows"][ks[-2]]
            dt = c["t"] - a["t"]
            print(f"  {side} {b['label']:12s}", " ".join(str(round((y - z) / dt)) for y, z in zip(c["bycpu"], a["bycpu"])))


def question_a(x):
    section("A: is a VM figure linear with the guest up to saturation?")
    print("figures in cores. guest = collector busy % times 20 / 100. cycles in 1e9/s. Mean over the case")
    names = [("g_busy_cores", "guest"), ("r_vmmem_cores", "vmmem_cores"), ("vmmemWSL_cores", "proc cores"), ("vp_guest", "VP guest"), ("vp_total", "VP total")]
    rows = []
    for c, n in GRADES:
        rows.append([c, n] + [f(x.val(k, c)) for k, _ in names] + [f(x.val("vmmemWSL_cycles", c) / 1e9), f(x.val("wsl_units", c), 1)])
    table(["case", "threads"] + [h for _, h in names] + ["cycles", "units/s"], rows)
    gx = [x.val("g_busy_cores", c) for c, _ in GRADES]
    print("straight line through the six case means, against guest:")
    for k, h in names[1:] + [("vmmemWSL_cycles", "cycles")]:
        ys = [x.val(k, c) for c, _ in GRADES]
        sl, ic = fit(gx, ys)
        res = [y - (sl * g + ic) for g, y in zip(gx, ys)]
        print(f"  {h:12s} slope {sl:.5g} intercept {ic:.5g} largest residual {max(abs(r) for r in res):.5g}")
    print("same, up to wsl-10 only (below saturation):")
    for k, h in names[1:] + [("vmmemWSL_cycles", "cycles")]:
        ys = [x.val(k, c) for c, _ in GRADES[:5]]
        sl, ic = fit(gx[:5], ys)
        print(f"  {h:12s} slope {sl:.5g} intercept {ic:.5g} predicts at 20.000: {sl * 20 + ic:.5g}, read {x.val(k, 'wsl-20'):.5g}")
    print("per-round r against guest over baseline to wsl-20 (all rounds in those cases):")
    for k, h in names[1:] + [("vmmemWSL_cycles", "cycles")]:
        a, b = [], []
        for c, _ in GRADES:
            pairs = pair_rounds(x, "g_busy_cores", k, c)
            a += [p[0] for p in pairs]
            b += [p[1] for p in pairs]
        print(f"  {h:12s} r {pearson(a, b):+.4f} rank r {spearman(a, b):+.4f} n {len(a)}")
    print()
    print("CycleTime: cycles per second over cores, by case")
    rows = []
    for c in CASES:
        cy = x.val("vmmemWSL_cycles", c)
        rows.append([c, f(cy / 1e9, 4), f(cy / x.val("vmmemWSL_cores", c) / 1e9, 4), f(cy / x.val("vp_total", c) / 1e9, 4), f(cy / x.val("vp_guest", c) / 1e9, 4)])
    table(["case", "cycles 1e9/s", "per proc core", "per VP total", "per VP guest"], rows)
    wr = [(c, x.val("cpu_burn_cycles", c) / x.val("cpu_burn_cores", c)) for c in ("win-20", "both-win-5", "both-win-10", "both-win-20")]
    print("cpu_burn process, same ratio:", " ".join(f"{c} {v / 1e9:.4f}" for c, v in wr))
    print("guest cpuinfo MHz, distinct values over the run:", sorted({v for m in x.g["mhz"].values() for v in m}))
    tsc = float(sorted({v for m in x.g["mhz"].values() for v in m})[0]) * 1e6
    print(f"vmmemWSL cycles divided by {tsc / 1e9:.6f}e9, in cores, against process cores and VP total:")
    rows = []
    for c in CASES:
        cy = x.val("vmmemWSL_cycles", c)
        rows.append([c, f(cy / tsc), f(x.val("vmmemWSL_cores", c)), f(x.val("vp_total", c)), f(x.val("r_vmmem_cores", c))])
    table(["case", "cycles as cores", "proc cores", "VP total", "vmmem_cores"], rows)
    gaps = [abs(x.val("vmmemWSL_cycles", c) / tsc - x.val("vmmemWSL_cores", c)) for c in CASES]
    print(f"largest gap, cycles as cores against process cores, any case: {max(gaps):.3f}")
    print("VP total less VP guest, cores, 1 to 20 threads:", " ".join(f(x.val("vp_total", c) - x.val("vp_guest", c)) for c, n in GRADES[1:]))
    print()
    print("the contended cases: each figure and the work, cores equivalent = units/s over WSL units per VP-guest core in both-before")
    per = x.val("wsl_units", "both-before") / x.val("vp_guest", "both-before")
    rows = []
    for c in ("both-before", "both-win-5", "both-win-10", "both-win-20", "both-after"):
        u = x.val("wsl_units", c)
        rows.append([c, f(u / per)] + [f(x.val(k, c)) for k in ("r_vmmem_cores", "vp_guest", "vp_total", "g_busy_cores")] + [f(x.val("vmmemWSL_cycles", c) / tsc), f(u / x.val("vp_guest", c), 1), f(u / x.val("r_vmmem_cores", c), 1)])
    table(["case", "work as cores", "vmmem_cores", "VP guest", "VP total", "guest", "cycles as cores", "units per VP core", "units per vmmem core"], rows)


def pair_rounds(x, a, b, case):
    """Pairs of two series by nearest round end inside a case."""
    ra = {round(t1): v for t0, t1, v in x.s[a] if x.case[case][0] + MARGIN_HEAD <= t0 and t1 <= x.case[case][1] - MARGIN_TAIL and v is not None}
    rb = {round(t1): v for t0, t1, v in x.s[b] if x.case[case][0] + MARGIN_HEAD <= t0 and t1 <= x.case[case][1] - MARGIN_TAIL and v is not None}
    return [(ra[k], rb[k]) for k in ra if k in rb]


def question_b(x):
    section("B: does Windows busy include the VM's time?")
    rows = []
    for c, n in GRADES:
        rows.append([c, n, f(x.val("w_busy_cores", c)), f(x.val("w_busy_cores", c) - x.val("w_busy_cores", "baseline")), f(x.val("vp_total", c) - x.val("vp_total", "baseline")),
                     f(x.val("w_user_pct", c)), f(x.val("w_kernel_pct", c)), f(x.val("rv_total", c)), f(x.val("lp_total", c)), f(x.val("lp_idle", c)), f(x.val("vmmemWSL_kernel", c)), f(x.val("vmmemWSL_user", c)),
                     f(x.val("r_other_cores", c))])
    table(["case", "threads", "win busy cores", "less baseline", "VP total less baseline", "user %", "kernel %", "Root VP total", "Hv LP total %", "Hv LP idle %", "vmmem kernel", "vmmem user", "other procs"], rows)
    xs = [x.val("vp_total", c) for c, _ in GRADES]
    ys = [x.val("w_busy_cores", c) for c, _ in GRADES]
    sl, ic = fit(xs, ys)
    print(f"Windows busy cores against VP total, six cases: slope {sl:.4f} intercept {ic:.4f}; up to wsl-10: slope {fit(xs[:5], ys[:5])[0]:.4f}")
    a, b = [], []
    for c, _ in GRADES:
        p = pair_rounds(x, "vp_total", "w_busy_cores", c)
        a += [q[0] for q in p]
        b += [q[1] for q in p]
    print(f"per round over the same cases: r {pearson(a, b):+.4f} rank r {spearman(a, b):+.4f} n {len(a)}")
    for c in ("wsl-20", "both-before"):
        print(f"{c}: `_Total` % Processor Time {x.val('pi_ptime', c):.3f}, NT busy {x.val('w_busy_pct', c):.3f}, guest user ticks/s {x.val('g_user', c):.2f} of {x.val('g_ticks', c):.2f}, Windows user {x.val('w_user_pct', c):.3f}%, cpu_burn cores {x.val('cpu_burn_cores', c):.3f}")
    print(f"guest user share of its ticks at wsl-20: {100 * x.val('g_user', 'wsl-20') / x.val('g_ticks', 'wsl-20'):.3f}%")
    print("Hv LP guest time holds Windows as well: LP guest %, as cores, against Root VP guest plus VP guest, cores")
    rows = []
    for c in ("baseline", "wsl-20", "win-20", "both-win-20"):
        rows.append([c, f(x.val("lp_guest", c)), f(x.val("lp_guest", c) * LPS / 100), f(x.val("rv_guest", c) + x.val("vp_guest", c)), f(x.val("rv_guest", c)), f(x.val("vp_guest", c))])
    table(["case", "LP guest %", "as cores", "Root + VP", "Root VP guest", "VP guest"], rows)


CANDIDATES = [
    ("steal ticks/s", "g_steal"),
    ("PSI cpu some avg10", "psi_cpu_some_avg10"),
    ("PSI cpu some total us/s", "psi_cpu_some_rate"),
    ("PSI cpu full total us/s", "psi_cpu_full_rate"),
    ("guest busy %", "g_busy_pct"),
    ("guest ticks/s, all fields", "g_ticks"),
    ("guest sys ticks/s", "g_sys"),
    ("VP % Total Run Time, sum", "vp_total"),
    ("VP % Guest Run Time, sum", "vp_guest"),
    ("vm.vmmem_cores", "r_vmmem_cores"),
    ("vmmemWSL process time, probe", "vmmemWSL_cores"),
    ("vmmemWSL CycleTime, probe", "vmmemWSL_cycles"),
    ("VP % Guest-Relative Utilization, sum", "vp_relutil"),
    ("VP Contention/Dispatch, _Total", "vp_cont"),
    ("VP Contention/Dispatch, largest", "vp_cont_inst_max"),
    ("VP Wait/Dispatch, _Total", "vp_wait"),
    ("VP Wake Up/Dispatch, _Total", "vp_wake"),
    ("VP dispatches/s, sum", "vp_disp"),
    ("VP migrations/s, sum", "vp_migr"),
    ("vmmem threads Ready", "st_vmmemWSL_ready"),
    ("all threads Ready", "st_all_ready"),
    ("Processor Queue Length", "sys_queue"),
    ("deficit, guest*20 less VP total", "deficit_vp"),
    ("deficit, guest*20 less vmmem_cores", "deficit_vmmem"),
    ("guest procs_running", "g_running"),
    ("load 1 min", "g_load1"),
    ("guest ctxt/s", "g_ctxt"),
]


def question_c(x):
    section("C: what reveals that WSL lost CPU?")
    # guest busy against Windows' figures, rounds joined by when they end
    for name, key in (("deficit_vp", "vp_total"), ("deficit_vmmem", "r_vmmem_cores")):
        gs = x.s["g_busy_cores"]
        for t0, t1, v in x.s[key]:
            near = min(gs, key=lambda r: abs(r[1] - t1))
            if abs(near[1] - t1) <= 0.75:
                x.put(name, t0, t1, near[2] - v)
    cases = ("baseline", "wsl-1", "wsl-5", "wsl-10", "wsl-20", "win-20", "both-before", "both-win-5", "both-win-10", "both-win-20", "both-after")
    rows = []
    for n, k in CANDIDATES:
        rows.append([n] + [f(x.val(k, c), 2) for c in cases])
    table(["candidate, case mean"] + list(cases), rows)
    print("VP Wait/Dispatch _Total, every case:", " ".join(f"{c} {x.val('vp_wait', c):.2f}" for c in CASES))
    print("WSL work loss, % of reference:", " ".join(f"{c} {100 * (1 - x.val('wsl_units', c) / x.ref):.3f}" for c in ("both-before", "both-win-5", "both-win-10", "both-win-20", "both-after")))
    for k, n in (("vp_cont_inst_max", "VP CPU Contention Time Per Dispatch, any VP"), ("vp_wake_inst_max", "VP CPU Wake Up Time Per Dispatch, any VP"), ("vp_corerun", "VP % Total Core Run Time, sum"), ("g_steal", "steal ticks/s")):
        v = [q for _, _, q in x.s[k] if q is not None]
        print(f"{n}: largest over {len(v)} rounds of the whole run {max(v):.6f}")
    print()
    print("round by round against the loss in that round's own span. section = every round of both-before to both-after inside a case;")
    print("win-20 only = both-win-20 rounds, where the loss barely steps, so r there is a test of following small changes. Loss computed from the burner's cumulative count.")
    sec = ("both-before", "both-win-5", "both-win-10", "both-win-20", "both-after")
    rows = []
    for n, k in CANDIDATES:
        pa, pb, wa, wb = [], [], [], []
        for c in sec:
            a0, b0 = x.case[c]
            for t0, t1, v in x.s.get(k, []):
                if t0 >= a0 + MARGIN_HEAD and t1 <= b0 - MARGIN_TAIL and v is not None:
                    u = x.over("wsl", t0, t1)
                    if u is None:
                        continue
                    pa.append(1 - u / x.ref)
                    pb.append(v)
                    if c == "both-win-20":
                        wa.append(1 - u / x.ref)
                        wb.append(v)
        rows.append([n, len(pa), fs(pearson(pa, pb)), fs(spearman(pa, pb)), fs(pearson(wa, wb)), fs(spearman(wa, wb))])
    table(["candidate", "n", "section r", "section rank r", "win-20 r", "win-20 rank r"], rows)
    print()
    print("does a figure equal the loss? (each in cores of the 20; work lost = 20 less work as cores, with work per core from both-before)")
    per = x.val("wsl_units", "both-before") / 20
    rows = []
    for c in sec:
        u = x.val("wsl_units", c)
        rows.append([c, f(20 - u / per), f(x.val("deficit_vp", c)), f(x.val("deficit_vmmem", c)), f(x.val("st_vmmemWSL_ready", c)), f(x.rng("st_vmmemWSL_ready", c)[0], 0) + "-" + f(x.rng("st_vmmemWSL_ready", c)[1], 0),
                     f((2000 - x.val("g_ticks", c)) / 100)])
    table(["case", "work lost, cores", "guest*20 less VP total", "guest*20 less vmmem_cores", "vmmem Ready", "range", "ticks missing, cores"], rows)
    print()
    print("the same differences with no Windows burner, where no contention was started (raw observation, cores):")
    rows = []
    for c, _ in GRADES[1:] + [("win-20", 0), ("settle-1", 0), ("settle-3", 0)]:
        rows.append([c, f(x.val("deficit_vp", c)), f(x.val("deficit_vmmem", c)), f(x.rng("deficit_vp", c)[0]) + " to " + f(x.rng("deficit_vp", c)[1])])
    table(["case", "guest*20 less VP total", "guest*20 less vmmem_cores", "range of first"], rows)
    print()
    print("reverse: the Windows burner. threads wanted, thread CPU cores Windows counted, work, threads Ready at the sample")
    rows = []
    alone = x.val("win_units", "win-20") / 20
    for c, n in (("win-20", 20), ("both-win-5", 5), ("both-win-10", 10), ("both-win-20", 20)):
        rows.append([c, n, f(x.val("cpu_burn_cores", c)), f(x.val("win_cpu_cores", c)), f(x.val("win_units", c), 1), f(x.val("win_units", c) / n, 1), f(x.val("st_cpu_burn_ready", c)), f(x.val("st_cpu_burn_running", c)), f(x.val("st_rest_ready", c)), f(x.val("sys_queue", c)), f(x.val("w_busy_pct", c))])
    table(["case", "threads", "proc cores", "burner cpu", "units/s", "units/s per thread", "burner Ready", "burner Running", "rest Ready", "queue", "win busy %"], rows)
    print(f"Windows burner per thread, alone: {alone:.1f} units/s")
    print()
    print("vmmemWSL threads by state and priority, mean count per sample, over the 20 threads that carried the load")
    active = vcpu_threads(x)
    rows = []
    for c in ("baseline", "wsl-1", "wsl-10", "wsl-20", "win-20", "both-before", "both-win-5", "both-win-10", "both-win-20", "both-after"):
        a0, b0 = x.case[c]
        rs = [r for r, t in x.p["round"].items() if a0 + MARGIN_HEAD <= t - x.offset <= b0 - MARGIN_TAIL and r in x.p["thr"]]
        cells = {}
        for r in rs:
            for tid in active:
                v = x.p["thr"][r].get(tid)
                if v:
                    key = (v["state"], v["prio"])
                    cells[key] = cells.get(key, 0) + 1
        rows.append([c, len(rs), " ".join(f"s{k[0]}p{k[1]}={v / len(rs):.2f}" for k, v in sorted(cells.items(), key=lambda z: -z[1]))])
    table(["case", "n", "state s, priority p: mean threads"], rows)


def vcpu_threads(x):
    rs = sorted(x.p["thr"])
    first, last = x.p["thr"][rs[0]], x.p["thr"][rs[-1]]
    moved = {t: last[t]["kernel"] + last[t]["user"] - first[t]["kernel"] - first[t]["user"] for t in first if t in last}
    return sorted(moved, key=lambda t: -moved[t])[:20]


def question_d(x):
    section("D: does guest busy stay high when the host has taken the vCPU?")
    rows = []
    for c in ("wsl-20", "both-before", "both-win-5", "both-win-10", "both-win-20", "both-after"):
        a, b = x.rng("g_busy_pct", c)
        rows.append([c, f(x.val("g_busy_pct", c)), f(a), f(b), f(x.val("wsl_cpu_cores", c)), f(x.val("vp_total", c)), f(x.val("wsl_units", c), 1), f(x.val("g_ticks", c), 2), f(x.val("g_user", c), 2), f(x.val("g_sys", c), 2), f(x.val("g_idle", c), 2), f(x.val("g_steal", c), 2)])
    table(["case", "guest busy %", "low", "high", "guest thread cpu cores", "VP total", "units/s", "ticks/s", "user", "sys", "idle", "steal"], rows)
    print("per vCPU in both-win-20: guest busy ticks/s, VP % Total Run Time, burner units/s on that vCPU")
    b = [r for r in x.lb if r["label"] == "both"][0]
    a0, b0 = x.case["both-win-20"]
    ks = [k for k in sorted(b["rows"]) if a0 + MARGIN_HEAD <= b["rows"][k]["t"] <= b0 - MARGIN_TAIL]
    A, C = b["rows"][ks[0]], b["rows"][ks[-1]]
    dt = C["t"] - A["t"]
    rows = []
    for i in range(LPS):
        rows.append([i, f(x.val(f"gc_busy_{i}", "both-win-20"), 2), f(x.val(f"vpn_total_{i}", "both-win-20"), 2), round((C["bycpu"][i] - A["bycpu"][i]) / dt)])
    table(["vCPU", "guest ticks/s", "VP total %", "units/s"], rows)
    print("r over 20 vCPUs, VP total against units/s: %+.3f; guest ticks against units/s: %+.3f" % (pearson([float(r[2]) for r in rows], [float(r[3]) for r in rows]), pearson([float(r[1]) for r in rows], [float(r[3]) for r in rows])))


def also(x):
    section("also: per-LP DPC, interrupt and the user/kernel split")
    print("per-LP percent of that LP's time, over all rounds of a case")
    rows = []
    for c in ("baseline", "wsl-20", "win-20", "both-win-20"):
        dp = [v for lp in range(LPS) for v in x.rows(f"lp_dpc_{lp}", c)]
        it = [v for lp in range(LPS) for v in x.rows(f"lp_int_{lp}", c)]
        us = [v for lp in range(LPS) for v in x.rows(f"lp_user_{lp}", c)]
        ke = [v for lp in range(LPS) for v in x.rows(f"lp_kern_{lp}", c)]
        rows.append([c, len(dp), f(mean(dp)), f(max(dp)), sum(1 for v in dp if v > 0), f(mean(it)), f(max(it)), sum(1 for v in it if v > 0), f(mean(us)), f(mean(ke))])
    table(["case", "n", "dpc mean", "dpc max", "dpc>0", "int mean", "int max", "int>0", "user mean", "kernel mean"], rows)
    nt, pdh = x.p["nt"], x.p["pdh"]
    worst = dict(user=0.0, priv=0.0, ptime=0.0)
    gaps = dict(user=[], priv=[], ptime=[])
    loaded = [r for r in sorted(x.p["round"])[1:] if r in pdh[PI + "% User Time"]]
    miss = 0
    for r in loaded:
        for lp in range(LPS):
            d = {k: nt[k][r][lp] - nt[k][r - 1][lp] for k in ("idle", "kernel", "user")}
            tot = d["kernel"] + d["user"]
            n = f"0,{lp}"
            for key, path, val in (("user", "% User Time", 100 * d["user"] / tot), ("priv", "% Privileged Time", 100 * (d["kernel"] - d["idle"]) / tot), ("ptime", "% Processor Time", 100 * (tot - d["idle"]) / tot)):
                v = pdh[PI + path].get(r, {}).get(n)
                if v is None:
                    miss += 1
                    continue
                gaps[key].append(abs(v - val))
    print("NT per-LP against PDH per-LP over the whole run, percent points, n and mean and p99 and largest gap:")
    for k, v in gaps.items():
        v.sort()
        print(f"  {k}: n {len(v)} mean {mean(v):.3f} p99 {v[int(len(v) * 0.99)]:.3f} largest {v[-1]:.3f}")
    print(f"  LP readings missing on the PDH side: {miss}")
    for k in ("dpc", "interrupt"):
        ds = [nt[k][r][lp] - nt[k][r - 1][lp] for r in sorted(x.p["round"])[1:] for lp in range(LPS)]
        print(f"  {k}: gcd of round deltas {functools.reduce(math.gcd, ds)}, deltas above 0: {sum(1 for d in ds if d)} of {len(ds)}")
    print("  interrupt_count per second, _sum over LPs_, case means:", " ".join(f"{c} {mean_int(x, c):.0f}" for c in ("baseline", "wsl-20", "win-20", "both-win-20")))

    section("also: ready threads on Windows")
    rows = []
    for c in CASES:
        lo, hi = x.rng("st_all_ready", c)
        rows.append([c, f(x.val("st_all_ready", c), 2), f(lo, 0), f(hi, 0), f(x.val("st_vmmemWSL_ready", c), 2), f(x.val("st_cpu_burn_ready", c), 2), f(x.val("st_rest_ready", c), 2), f(x.val("sys_queue", c), 2)])
    table(["case", "all Ready", "low", "high", "vmmem", "cpu_burn", "rest", "Processor Queue Length"], rows)
    a, b = [], []
    for c in CASES:
        p = pair_rounds(x, "st_all_ready", "sys_queue", c)
        a += [q[0] for q in p]
        b += [q[1] for q in p]
    print(f"all Ready against Processor Queue Length, every round in a case: r {pearson(a, b):+.4f} rank r {spearman(a, b):+.4f} n {len(a)}")

    section("also: guest run state")
    rows = []
    for c in CASES:
        lo, hi = x.rng("g_running", c)
        rows.append([c, f(x.val("g_running", c), 2), f(lo, 0), f(hi, 0), f(x.val("g_blocked", c), 2), f(x.val("g_tasks_D", c), 2), f(x.rng("g_tasks_D", c)[1], 0), f(x.val("g_tasks_R", c), 2), f(x.val("g_iowait", c), 2), f(x.val("g_spent", c), 3)])
    table(["case", "procs_running", "low", "high", "procs_blocked", "D tasks", "D max", "R tasks", "iowait ticks/s", "sampler s"], rows)
    print("burner process row: num_threads, and whether /proc/pid/stat processor is where the work ran")
    rows = []
    for b in x.lb:
        rs = [r for r, t in x.g["round"].items() if b["start"] + 2 <= t[0] <= b["end"] - 1]
        tops = [t for r in rs for t in x.g["top"].get(r, []) if t["name"] == "cpu_burn"]
        ths = sorted({t["threads"] for t in tops})
        proc_cpus = sorted({t["cpu"] for t in tops})
        task_cpus = sorted({c["cpu"] for r in rs for c in x.g["burn"].get(r, [])})
        ks = sorted(b["rows"])
        worked = [i for i, (y, z) in enumerate(zip(b["rows"][ks[-1]]["bycpu"], b["rows"][ks[0]]["bycpu"])) if y > z]
        rows.append([b["label"], len(tops), str(ths), ",".join(map(str, proc_cpus)), ",".join(map(str, task_cpus)) if len(task_cpus) < 12 else f"{len(task_cpus)} vCPUs", ",".join(map(str, worked)) if len(worked) < 12 else f"{len(worked)} vCPUs"])
    table(["burner", "rows", "num_threads", "stat processor", "task processors", "vCPUs that got work"], rows)
    agree = tot = 0
    for b in x.lb:
        ks = sorted(b["rows"])
        for r, tl in x.g["burn"].items():
            t = x.g["round"][r][0]
            if not (b["start"] + 2 <= t <= b["end"] - 1):
                continue
            k = min(ks, key=lambda q: abs(b["rows"][q]["t"] - t))
            own = set(b["rows"][k]["cpu"])
            for c in tl:
                tot += 1
                agree += c["cpu"] in own
    print(f"task row processor is among the vCPUs the burner's threads reported in its nearest row: {agree} of {tot}")

    section("also: pressure, `total=` as us per second, and avg10 at the end of the case")
    rows = []
    for c in CASES:
        row = [c]
        for res in ("cpu", "memory", "io"):
            for kind in ("some", "full"):
                row.append(f(x.val(f"psi_{res}_{kind}_rate", c), 1))
        for res in ("cpu", "io"):
            v = x.rows(f"psi_{res}_some_avg10", c)
            row.append(f(v[-1] if v else None, 2))
        rows.append(row)
    table(["case", "cpu some", "cpu full", "mem some", "mem full", "io some", "io full", "cpu avg10", "io avg10"], rows)

    section("also: heat, limits and clock at all-core load")
    for k, n in (("tz_temp", "Temperature"), ("tz_hptemp", "High Precision Temperature"), ("tz_passive", "% Passive Limit"), ("tz_reasons", "Throttle Reasons"), ("pi_limit", "% Performance Limit _Total"), ("pi_limflags", "Performance Limit Flags _Total")):
        vals = sorted({v for _, _, v in x.s[k] if v is not None})
        print(f"{n}: distinct values over the run {vals}, rounds {len(x.s[k])}")
    allv = {key: sorted({v for lp in range(LPS) for _, _, v in x.s[f'pl_{key}_{lp}'] if v is not None}) for key in ("limit", "limflags")}
    print(f"per LP: % Performance Limit {allv['limit']}, Performance Limit Flags {allv['limflags']}")
    rows = []
    for c in ("baseline", "wsl-1", "wsl-5", "wsl-10", "wsl-20", "win-20", "both-win-20", "settle-3"):
        rows.append([c, f(x.val("w_busy_pct", c)), f(x.val("pi_ptime", c)), f(x.val("pi_util", c)), f(x.val("pi_perf", c)), f(x.val("pi_freq_P", c), 1), f(x.val("pi_freq_E", c), 1), f(x.val("pi_perf_P", c)), f(x.val("pi_perf_E", c)), f(x.val("pi_maxf_P", c)), f(x.val("pi_maxf_E", c)), f(x.val("pi_util_P", c)), f(x.val("pi_util_E", c))])
    table(["case", "NT busy", "% Proc Time", "Utility", "Perf", "freq P", "freq E", "perf P", "perf E", "max% P", "max% E", "util P", "util E"], rows)
    for c in ("wsl-20", "win-20", "both-win-20"):
        fp = [v for lp in P_LPS for v in x.rows(f"pl_freq_{lp}", c)]
        fe = [v for lp in E_LPS for v in x.rows(f"pl_freq_{lp}", c)]
        up = [v for lp in range(LPS) for v in x.rows(f"pl_util_{lp}", c)]
        print(f"{c}: Actual Frequency P {min(fp):.1f}-{max(fp):.1f}, E {min(fe):.1f}-{max(fe):.1f}; Utility per LP {min(up):.3f}-{max(up):.3f}; _Total Utility over busy {x.val('pi_util', c) / x.val('w_busy_pct', c):.4f}")
    print("guest cpu MHz distinct:", sorted({v for m in x.g["mhz"].values() for v in m}))
    b = [r for r in x.wb if r["label"] == "win-20"][0]
    ks = sorted(b["rows"])
    A, C = b["rows"][ks[2]], b["rows"][ks[-2]]
    dt = C["t"] - A["t"]
    rate = [(y - z) / dt for y, z in zip(C["bycpu"], A["bycpu"])]
    print(f"win-20, burner units/s per LP: P mean {mean(rate[:12]):.1f} ({min(rate[:12]):.1f}-{max(rate[:12]):.1f}), E mean {mean(rate[12:]):.1f} ({min(rate[12:]):.1f}-{max(rate[12:]):.1f}), E over P {mean(rate[12:]) / mean(rate[:12]):.4f}, frequency E over P {x.val('pi_freq_E', 'win-20') / x.val('pi_freq_P', 'win-20'):.4f}")
    print(f"one WSL thread alone, wsl-1: {x.val('wsl_units', 'wsl-1'):.1f} units/s. Windows P-LP rate over that: {mean(rate[:12]) / x.val('wsl_units', 'wsl-1'):.4f}")

    section("also: migration and where the VM ran")
    rows = []
    for c in CASES:
        dis, mig = x.val("vp_disp", c), x.val("vp_migr", c)
        rows.append([c, f(dis, 1), f(mig, 1), f(mig / dis), f(x.val("wsl_moves", c), 2)])
    table(["case", "dispatches/s", "migrations/s", "per dispatch", "guest thread vCPU moves/s, burner's own"], rows)
    print("by step: the vCPUs with VP total above 20% and the LPs with busy above 20%, percent")
    for c, _ in GRADES[1:5]:
        vps = sorted(((x.val(f"vpn_total_{i}", c), i) for i in range(LPS)), reverse=True)
        lps = sorted(((x.val(f"lp_busy_{i}", c), i) for i in range(LPS)), reverse=True)
        print(f"  {c}: VP " + " ".join(f"{i}:{v:.1f}" for v, i in vps if v > 20) + " | LP " + " ".join(f"{i}:{v:.1f}" for v, i in lps if v > 20))
        print(f"  {c}: migrations/s of the busiest VP {x.val(f'vpn_migr_{vps[0][1]}', c):.1f}; P LP busy {mean([x.val(f'lp_busy_{i}', c) for i in P_LPS]):.2f}, E LP busy {mean([x.val(f'lp_busy_{i}', c) for i in E_LPS]):.2f}")
    print("guest cpuN against LP busy over the rounds of wsl-1 to wsl-10, 20 by 20 pairs, r on the same number against the rest; the largest in its row:")
    c = ("wsl-1", "wsl-2", "wsl-5", "wsl-10")
    same, off = [], []
    top_same = 0
    tested = 0
    for g_ in range(LPS):
        rs = []
        for lp in range(LPS):
            a, b_ = [], []
            for k in c:
                p = pair_rounds(x, f"gc_busy_{g_}", f"lp_busy_{lp}", k)
                a += [q[0] for q in p]
                b_ += [q[1] for q in p]
            rs.append(pearson(a, b_))
        if all(math.isnan(v) for v in rs):
            continue
        tested += 1
        valid = [(0.0 if math.isnan(v) else v) for v in rs]
        same.append(valid[g_])
        off += [v for i, v in enumerate(valid) if i != g_]
        top_same += valid.index(max(valid)) == g_
    print(f"  vCPUs with any movement {tested}: mean r same number {mean(same):+.3f}, off {mean(off):+.3f}, largest on the same number in {top_same} of {tested}")
    print("vmmemWSL threads against VP total, r over baseline to wsl-10 rounds; each thread's best VP:")
    active = vcpu_threads(x)
    rounds = sorted(x.p["thr"])
    a0, b0 = x.case["baseline"][0], x.case["wsl-10"][1]
    use = [r for r in rounds if r - 1 in x.p["thr"] and a0 + 2 <= x.p["round"][r - 1] - x.offset and x.p["round"][r] - x.offset <= b0 - 1]
    series = {t: [(x.p["thr"][r][t]["kernel"] + x.p["thr"][r][t]["user"] - x.p["thr"][r - 1][t]["kernel"] - x.p["thr"][r - 1][t]["user"]) / 1e5 for r in use] for t in active}
    vp = {}
    for i in range(LPS):
        d = {round(t1): v for _, t1, v in x.s[f"vpn_total_{i}"]}
        vp[i] = [d.get(round(x.p["round"][r] - x.offset)) for r in use]
    best = {}
    for t in active:
        rs = [(pearson(series[t], vp[i]), i) for i in range(LPS)]
        rs = [(r, i) for r, i in rs if not math.isnan(r)]
        if rs:
            best[t] = max(rs)
    print("  thread: best VP (r):", " ".join(f"{t}:{v[1]}({v[0]:.3f})" for t, v in sorted(best.items(), key=lambda z: z[1][1])))
    print(f"  distinct VPs named: {len({v[1] for v in best.values()})} of {len(best)}")

    section("also: other work during the run")
    rows = []
    for c in CASES:
        rows.append([c, f(x.val("r_other_cores", c)), f(x.rng("r_other_cores", c)[1]), f(x.val("r_wsl_other_cores", c)), f(x.val("sys_calls", c), 0), f(x.val("sys_ctx", c), 0)])
    table(["case", "Windows other cores", "largest", "guest other cores", "System Calls/sec", "Context Switches/sec"], rows)
    seen = {}
    for r in x.rec:
        t = r["unix_ms"] / 1000 - x.offset
        for o in r.get("top_other", []):
            cc = next((c for c in CASES if x.case[c][0] <= t <= x.case[c][1]), None)
            if cc:
                e = seen.setdefault(o["name"], {})
                e[cc] = max(e.get(cc, 0), o["cores"])
    print("masked Windows rows that reached 0.25 cores in any record: label, case, largest cores")
    for k, e in sorted(seen.items(), key=lambda z: -max(z[1].values())):
        if max(e.values()) >= 0.25:
            print("  ", k, " ".join(f"{c}={v:.3f}" for c, v in e.items() if v >= 0.25))
    print("winwsltop cost_ms.processes:", " ".join(f"{c} {mean([r['cost_ms']['processes'] for r in x.rec if x.case[c][0] <= r['unix_ms'] / 1000 - x.offset <= x.case[c][1]]):.2f}" for c in ("baseline", "wsl-20", "win-20", "both-win-20")))
    intervals = [r["interval_s"] for r in x.rec if r.get("interval_s")]
    print(f"winwsltop interval_s {min(intervals):.3f} to {max(intervals):.3f}")
    fails = {}
    for row in lines(x.probe_path):
        if row[0] == "array-status":
            fails.setdefault((row[2], row[4]), []).append(int(row[1]))
    print("whole-array reads that failed, with their status (pdhmsg.h: 0x800007d8 PDH_CALC_NEGATIVE_VALUE, 0x800007d6 PDH_CALC_NEGATIVE_DENOMINATOR):")
    for (path, status), rs in fails.items():
        print(f"  {path} {status}: {len(rs)} reads, rounds {rs[0]} to {rs[-1]}")
    print(f"Hv LP `_Total` % Idle Time at rounds that read, lowest {min(v for _, _, v in x.s['lp_idle'] if v is not None):.3f}")


def mean_int(x, c):
    a0, b0 = x.case[c]
    nt = x.p["nt"]["interrupt_count"]
    out = []
    for r in sorted(x.p["round"])[1:]:
        t0, t1 = x.p["round"][r - 1] - x.offset, x.p["round"][r] - x.offset
        if t0 >= a0 + MARGIN_HEAD and t1 <= b0 - MARGIN_TAIL:
            out.append((sum(nt[r]) - sum(nt[r - 1])) / (t1 - t0))
    return mean(out)


def main():
    x = Run(sys.argv[1], sys.argv[2], sys.argv[3])
    header(x)
    work(x)
    question_a(x)
    question_b(x)
    question_c(x)
    question_d(x)
    also(x)


if __name__ == "__main__":
    main()
