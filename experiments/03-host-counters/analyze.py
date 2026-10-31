#!/usr/bin/env python3
"""Reads the raw files compare.sh saved and prints every comparison. Usage: analyze.py <results> <date>"""
import collections, glob, os, re, sys

UNIT = 512000           # NVMe Data Units are 1000 blocks of 512 bytes
TICKS = 10_000_000      # FILETIME ticks a second
MIB = 1048576.0

results, date = sys.argv[1], sys.argv[2]
def lines(name):
    with open(os.path.join(results, f"{name}-{date}.txt"), encoding="utf-8") as f:
        return [l.rstrip("\n").split("\t") for l in f]

def verdict(ok): return "yes" if ok else "NO"

# ---------------------------------------------------------------- identity and the row filter
print("# identity")
ident = lines("identity")
for f in ident:
    if f[0] in ("kernel", "windows", "elevated", "disk", "partition"): print(" ", " ".join(f))
adapters = {f[1]: f for f in ident if f[0] == "adapter"}
rows = [f for f in ident if f[0] == "if"]
nvme = [f for f in ident if f[0] == "nvme"]
for f in nvme: print(" ", " ".join(f))
print(f"if-table rows {len(rows)}, Get-NetAdapter rows {len(adapters)}")
reasons = collections.Counter(f[16] for f in rows)
print("filter: kept", reasons["-"], "dropped", {k: v for k, v in reasons.items() if k != "-"})
print("rows with FilterInterface set, any state:", sum(1 for f in rows if f[10] == "1"), "; of those with non-zero octets:", sum(1 for f in rows if f[10] == "1" and (int(f[11]) or int(f[12]))))
print("rows where TransmitLinkSpeed differs from ReceiveLinkSpeed:", sum(1 for f in rows if f[13] != f[14]))
print("# rows the filter keeps, with Get-NetAdapter beside them")
print("  index alias | tx rx hw filter in out | Speed rx tx HardwareInterface Virtual Hidden Status")
mismatch = 0
for f in rows:
    a = adapters.get(f[1])
    if f[15] != "1": continue
    side = "not listed" if a is None else f"{a[5] or '(empty)'} {a[6] or '(empty)'} {a[7] or '(empty)'} {a[8]} {a[9]} {a[10]} {a[4]}"
    print(f"  {f[1]} {f[3]} | {f[13]} {f[14]} {f[9]} {f[10]} {f[11]} {f[12]} | {side}")
    if a is not None:
        if a[7] and a[7] != f[13]: mismatch += 1
        if a[6] and a[6] != f[14]: mismatch += 1
        if (a[8] == "True") != (f[9] == "1"): mismatch += 1
print("kept rows that disagree with Get-NetAdapter on link speed or the hardware flag:", mismatch)
print("# every Get-NetAdapter row against its if-table row, all states")
by_index = {f[1]: f for f in rows}
hw_bad = speed_bad = 0
for i, a in adapters.items():
    f = by_index.get(i)
    if f is None:
        print(f"  {i} {a[2]} no if-table row"); continue
    if (a[8] == "True") != (f[9] == "1"): hw_bad += 1
    if a[7] and a[7] != f[13]: speed_bad += 1
print(f"  rows joined {sum(1 for i in adapters if i in by_index)}, hardware flag differs {hw_bad}, tx speed differs {speed_bad}")
print("  HardwareInterface true in Get-NetAdapter:", [a[2] for a in adapters.values() if a[8] == "True"])
print("  Virtual true and Hidden false:", [a[2] for a in adapters.values() if a[9] == "True" and a[10] == "False"])
print("# dropped filter rows of the kept adapters, octets beside the adapter's own")
for f in rows:
    if f[16] == "filter" and (int(f[11]) or int(f[12])):
        print(f"  {f[1]} {f[3]} in {f[11]} out {f[12]}")
for f in rows:
    if f[15] == "1" and (int(f[11]) or int(f[12])):
        print(f"  kept {f[1]} {f[3]} in {f[11]} out {f[12]}")
print("# loopback and not-up rows with non-zero octets")
print(" ", [f"{f[3]} {f[11]} {f[12]}" for f in rows if f[16] in ("loopback", "not-up") and (int(f[11]) or int(f[12]))])

# ---------------------------------------------------------------- Get-NetAdapterStatistics bracket
print("\n# Get-NetAdapterStatistics between two raw GetIfTable2 reads")
blocks, cur = [], None
for f in lines("net-bracket"):
    if f[0].startswith("== "): cur = collections.defaultdict(dict); blocks.append(cur)
    elif f[0] in ("before", "after"): cur[f[5]][f[0]] = (int(f[6]), int(f[7]))
    elif f[0] == "ns": cur[f[3]]["ns"] = (int(f[5]), int(f[6]))
print(f"rounds {len(blocks)}")
for alias in sorted({a for b in blocks for a, v in b.items() if "ns" in v and "before" in v}):
    for k, label in ((0, "received"), (1, "sent")):
        inside = moved = 0
        worst = 0
        for b in blocks:
            v = b[alias]
            lo, s, hi = v["before"][k], v["ns"][k], v["after"][k]
            inside += lo <= s <= hi
            moved += hi > lo
            worst = max(worst, lo - s, s - hi)
        print(f"  {alias} {label}: inside {inside} of {len(blocks)}, raw moved in {moved} rounds, furthest outside {worst} bytes")
print("  adapters in Get-NetAdapterStatistics without a kept row:", sorted({a for b in blocks for a, v in b.items() if "ns" in v and "before" not in v}))
print("  kept rows without Get-NetAdapterStatistics:", sorted({a for b in blocks for a, v in b.items() if "ns" not in v}))

# ---------------------------------------------------------------- one concurrent window
def series(rows_, name, col):
    return [(t, r[name][col]) for t, r in rows_ if name in r]

def bracket(points, t0, t1):
    """Delta of a cumulative series over the samples inside [t0,t1], and over the samples just outside."""
    inside = [v for t, v in points if t0 <= t <= t1]
    before = [v for t, v in points if t <= t0]
    after = [v for t, v in points if t >= t1]
    return (inside[-1] - inside[0], (after[0] - before[-1]) if before and after else None)

total = collections.Counter()
extremes = {}
gaps = collections.defaultdict(list)
windows = sorted(int(re.search(r"window(\d+)-probe", p).group(1)) for p in glob.glob(os.path.join(results, f"window*-probe-{date}.txt")))
for w in windows:
    print(f"\n# window {w}")
    probe = lines(f"window{w}-probe")
    counters = lines(f"window{w}-counters")
    D = {}; d = collections.defaultdict(dict); r = collections.defaultdict(dict)
    N = {}; n = collections.defaultdict(dict); b = collections.defaultdict(dict); a = collections.defaultdict(dict); c = collections.defaultdict(dict)
    nv = []
    for f in probe:
        k = f[0]
        if k == "nvme": nv.append((int(f[2]), int(f[4]), int(f[6])))
        elif k == "D": D[int(f[1])] = (int(f[2]), int(f[3]), float(f[4]))
        elif k == "N": N[int(f[1])] = (int(f[2]), int(f[3]), float(f[4]))
        elif k == "d": d[int(f[1])][f[2]] = (float(f[3]), float(f[4]), float(f[5]))
        elif k == "r": r[int(f[1])][f[2]] = (int(f[3]), int(f[4]), int(f[5]), int(f[6]), None if f[7] == "-" else float(f[7]))
        elif k == "n": n[int(f[1])][f[2]] = (float(f[3]), float(f[4]), int(f[5]), int(f[6]))
        elif k in ("b", "a"): (b if k == "b" else a)[int(f[1])][f[3]] = (f[5], int(f[6]), int(f[7]))
        elif k == "c": c[int(f[1])][f[3]] = (float(f[2]), int(f[4]), int(f[5]))
    last = max(D)
    gc = collections.defaultdict(lambda: collections.defaultdict(dict))
    wmi = collections.defaultdict(dict)
    for f in counters:
        if f[0] == "gc":
            kind = "read" if "read" in f[3] else "write" if "write" in f[3] else "idle"
            gc[int(f[1])][f[2]][kind] = (int(f[4]), int(f[5]), float(f[6]))
        elif f[0] == "wmi" and f[2].startswith("SCSI"):
            wmi[int(f[1])][f[4]] = (int(f[5]), int(f[6]), int(f[7]))
    gc_t = sorted(gc); wmi_t = sorted(wmi)
    t0 = D[1 - 1][0]; t1 = D[last][0]
    print(f"collector rounds {last}, {(t1 - t0) / TICKS:.3f} s; Get-Counter samples {len(gc_t)}, {(gc_t[-1] - gc_t[0]) / TICKS:.3f} s; WMI reads {len(wmi_t)}, {(wmi_t[-1] - wmi_t[0]) / TICKS:.3f} s")
    print(f"overlap of all three {(min(t1, gc_t[-1], wmi_t[-1]) - max(t0, gc_t[0], wmi_t[0])) / TICKS:.3f} s, collector inside both: {verdict(gc_t[0] <= t0 and wmi_t[0] <= t0 and t1 <= gc_t[-1] and t1 <= wmi_t[-1])}")
    ms = [v[2] for k, v in D.items() if k]; print(f"disk sample ms mean {sum(ms) / len(ms):.3f} max {max(ms):.3f}")
    ms = [v[2] for k, v in N.items() if k]; print(f"net sample ms mean {sum(ms) / len(ms):.3f} max {max(ms):.3f}")
    print(f"disk priming sample ok={D[0][1]}, rows {len(d[0])}; net priming sample ok={N[0][1]}, rows {len(n[0])}")
    print(f"_Total in collector rows: {verdict(any('_Total' in d[k] for k in d))}; _Total in raw array: {verdict(all('_Total' in r[k] for k in r))}")
    names = sorted(k for k in r[0] if k != "_Total")
    sum_bad = sum(1 for k in r for col in (0, 1) if r[k]["_Total"][col] != sum(r[k][x][col] for x in names))
    print(f"instances {names}; rounds where _Total differs from their sum: {sum_bad} of {2 * len(r)} readings")

    for name in names:
        dt = {k: (r[k][name][3] - r[k - 1][name][3]) / TICKS for k in range(1, last + 1)}
        rate_r = sum(d[k][name][0] * dt[k] for k in dt)
        rate_w = sum(d[k][name][1] * dt[k] for k in dt)
        raw_r = r[last][name][0] - r[0][name][0]
        raw_w = r[last][name][1] - r[0][name][1]
        print(f"disk {name}: collector read {rate_r:.0f} B ({rate_r / MIB:.2f} MiB), its raw delta {raw_r}, write {rate_w:.0f} B ({rate_w / MIB:.2f} MiB), its raw delta {raw_w}")
        print(f"  moved: read {verdict(raw_r > 0)}, write {verdict(raw_w > 0)}")
        lc = name.lower()
        gpoints = [(t, gc[t][lc]) for t in gc_t if lc in gc[t]]
        wpoints = [(t, wmi[t][name.split()[0]]) for t in wmi_t if name.split()[0] in wmi[t]]
        for col, kind, cv in ((0, "read", rate_r), (1, "write", rate_w)):
            g = [(t, v[kind][0]) for t, v in gpoints]
            wm = [(t, v[col]) for t, v in wpoints]
            gi, go = bracket(g, t0, t1)
            wi, wo = bracket(wm, t0, t1)
            rv = raw_r if col == 0 else raw_w
            ppm = (cv / rv - 1) * 1e6 if rv else 0
            print(f"  {kind}: Get-Counter inner {gi} outer {go}, inside {verdict(gi <= rv <= go)}; WMI inner {wi} outer {wo}, inside {verdict(wi <= rv <= wo)}; rate times time against the raw delta {ppm:.1f} ppm")
            total["inside", "yes"] += (gi <= rv <= go) + (wi <= rv <= wo)
            total["inside", "of"] += 2
            if rv:
                total["moved", "yes"] += (gi <= rv <= go) + (wi <= rv <= wo)
                total["moved", "of"] += 2
        same = sum(1 for t, v in gpoints for ww, vv in [min(wpoints, key=lambda p: abs(p[0] - t))] if v["read"][0] == vv[0])
        print(f"  Get-Counter raw read equals the nearest WMI BytesRead in {same} of {len(gpoints)} samples")
        total[name, "rounds"] += last
        # busy: collector's time-weighted mean against the idle counters over their own samples inside the window
        busy = sum(d[k][name][2] * dt[k] for k in dt) / sum(dt.values())
        raw_idle = 100 * (1 - (r[last][name][2] - r[0][name][2]) / (r[last][name][3] - r[0][name][3]))
        g = [(t, v["idle"]) for t, v in gpoints if t0 <= t <= t1]
        g_busy = 100 * (1 - (g[-1][1][0] - g[0][1][0]) / (g[-1][1][1] - g[0][1][1]))
        wm = [(t, v[2]) for t, v in wpoints if t0 <= t <= t1]
        w_busy = 100 * (1 - (wm[-1][1] - wm[0][1]) / (wm[-1][0] - wm[0][0]))
        print(f"  busy mean %: collector {busy:.3f}, its raw idle delta {raw_idle:.3f}, Get-Counter idle delta {g_busy:.3f}, WMI idle delta {w_busy:.3f}")
        print(f"  busy per round %: min {min(d[k][name][2] for k in dt):.3f} max {max(d[k][name][2] for k in dt):.3f}")
        over = [k for k in dt if r[k][name][4] > 100]
        at = [k for k in dt if r[k][name][4] == 100]
        vmax = max(r[k][name][4] for k in dt)
        wrong = sum(1 for k in dt if abs(d[k][name][2] - (0 if r[k][name][4] > 100 else 100 - r[k][name][4])) > 2e-6)
        print(f"  % Idle Time: rounds above 100 {len(over)} of {last}, exactly 100 {len(at)}, max {vmax:.6f}, min {min(r[k][name][4] for k in dt):.6f}; busy differs from the clamp formula in {wrong} rounds")
        print(f"  clamp rounds with traffic: {sum(1 for k in over if d[k][name][0] or d[k][name][1])}")
        gvals = [v["idle"][2] for t, v in gpoints]
        print(f"  Get-Counter cooked idle above 100 in {sum(1 for x in gvals if x > 100)} of {len(gvals)} samples, max {max(gvals):.6f}")
        total[name, "over"] += len(over)
        total[name, "gc_over"] += sum(1 for x in gvals if x > 100)
        total[name, "gc_samples"] += len(gvals)
        extremes[name, "vmax"] = max(extremes.get((name, "vmax"), 0), vmax)
        extremes[name, "busymax"] = max(extremes.get((name, "busymax"), 0), max(d[k][name][2] for k in dt))
        gaps[name].append(max(abs(busy - g_busy), abs(busy - w_busy)))
        if name.startswith("0"):
            ur = (nv[1][1] - nv[0][1]); uw = (nv[1][2] - nv[0][2])
            print(f"  NVMe log across the window, {(nv[1][0] - nv[0][0]) / TICKS:.3f} s: read units {ur} = {ur * UNIT} B ({ur * UNIT / MIB:.2f} MiB), write units {uw} = {uw * UNIT} B ({uw * UNIT / MIB:.2f} MiB)")
            print(f"  collector / NVMe: read {rate_r / (ur * UNIT) if ur else float('nan'):.4f}, write {rate_w / (uw * UNIT) if uw else float('nan'):.4f}")
            du = [abs(rate_r - ur * UNIT) / UNIT, abs(rate_w - uw * UNIT) / UNIT]
            print(f"  difference in data units: read {du[0]:.2f}, write {du[1]:.2f}")
            total["nvme", "maxdiff"] = max(total["nvme", "maxdiff"], *du)
            total["nvme", "cr"] += rate_r; total["nvme", "cw"] += rate_w; total["nvme", "ur"] += ur; total["nvme", "uw"] += uw

    # network, this window
    alias = {luid: v[0] for k in b for luid, v in b[k].items()}
    bad_b = bad_rate = bad_set = checked = 0
    for k in range(1, last + 1):
        if set(n[k]) != {alias[l] for l in b[k]}: bad_set += 1
        for luid, v in c[k].items():
            checked += 2
            for col in (1, 2):
                bad_b += not (b[k][luid][col] <= v[col] <= a[k][luid][col])
            el = v[0] - c[k - 1][luid][0]
            for col, rate in ((1, n[k][alias[luid]][0]), (2, n[k][alias[luid]][1])):
                want = (v[col] - c[k - 1][luid][col]) / el
                bad_rate += abs(rate - want) > max(1.0, want * 5e-6)
    print(f"net: octets the collector read outside its raw bracket {bad_b} of {checked}; rate differs from octet delta / elapsed {bad_rate} of {checked}; rounds whose rows differ from the kept set {bad_set} of {last}")
    rows_seen = sorted({x for k in n for x in n[k]})
    print("net rows returned:", rows_seen)
    for luid in alias:
        lo = [b[last][luid][x] - a[0][luid][x] for x in (1, 2)]
        hi = [a[last][luid][x] - b[0][luid][x] for x in (1, 2)]
        el = c[last][luid][0] - c[0][luid][0]
        ri = sum(n[k][alias[luid]][0] * (c[k][luid][0] - c[k - 1][luid][0]) for k in range(1, last + 1))
        ro = sum(n[k][alias[luid]][1] * (c[k][luid][0] - c[k - 1][luid][0]) for k in range(1, last + 1))
        ok = lo[0] <= round(ri) <= hi[0] and lo[1] <= round(ro) <= hi[1]
        print(f"  {alias[luid]}: raw in {lo[0]} to {hi[0]}, out {lo[1]} to {hi[1]}; collector in {ri:.0f} out {ro:.0f}, inside {verdict(ok)}, over {el:.3f} s, max rate in {max(n[k][alias[luid]][0] for k in range(1, last + 1)):.0f} out {max(n[k][alias[luid]][1] for k in range(1, last + 1)):.0f} B/s")

    # the virtual adapter against the physical NIC, second by second
    veth = next(l for l, x in alias.items() if x.startswith("vEthernet"))
    nic = next(l for l, x in alias.items() if n[1][x][3])
    def deltas(l, col): return [c[k][l][col] - c[k - 1][l][col] for k in range(1, last + 1)]
    def corr(x, y):
        mx, my = sum(x) / len(x), sum(y) / len(y)
        sx = sum((v - mx) ** 2 for v in x) ** .5; sy = sum((v - my) ** 2 for v in y) ** .5
        return sum((p - mx) * (q - my) for p, q in zip(x, y)) / (sx * sy) if sx and sy else float("nan")
    vi, vo, ni, no = deltas(veth, 1), deltas(veth, 2), deltas(nic, 1), deltas(nic, 2)
    nic_link = n[1][alias[nic]][2]
    peak = max(n[k][alias[nic]][0] for k in range(1, last + 1))
    print(f"NIC peak in {peak * 8 / nic_link * 100:.2f}% of its {nic_link} bit/s link")
    total["net", "vi_over"] += sum(1 for p, q in zip(vi, no) if p > q)
    total["net", "vo_over"] += sum(1 for p, q in zip(vo, ni) if p > q)
    total["net", "seconds"] += last
    print(f"seconds where vEthernet in exceeds NIC out {sum(1 for p, q in zip(vi, no) if p > q)} of {last}; vEthernet out exceeds NIC in {sum(1 for p, q in zip(vo, ni) if p > q)} of {last}")
    print(f"correlation per second: vEthernet in with NIC out {corr(vi, no):.3f}; vEthernet out with NIC in {corr(vo, ni):.3f}")

    # Windows against the guest
    edges = lines(f"window{w}-edges")
    e = {}; side = None
    for f in edges:
        if f[0].startswith("== "): side = f[0][3:]; e[side] = {}
        elif f[0] in ("before", "after"): e[side].setdefault(f[0], {})[f[5]] = (int(f[6]), int(f[7]))
        elif f[0] == "wsl": e[side]["wsl"] = (int(f[1]), int(f[9]))
    wsl_rx = e["end"]["wsl"][0] - e["start"]["wsl"][0]
    wsl_tx = e["end"]["wsl"][1] - e["start"]["wsl"][1]
    print(f"guest eth0 across the window: rx {wsl_rx} tx {wsl_tx}")
    for al in e["start"]["before"]:
        lo = [e["end"]["before"][al][x] - e["start"]["after"][al][x] for x in (0, 1)]
        hi = [e["end"]["after"][al][x] - e["start"]["before"][al][x] for x in (0, 1)]
        if hi[0] or hi[1]:
            print(f"  {al}: in {lo[0]} to {hi[0]}, out {lo[1]} to {hi[1]}")
        if al.startswith("vEthernet"):
            okt = lo[0] <= wsl_tx <= hi[0]; okr = lo[1] <= wsl_rx <= hi[1]
            print(f"  guest tx inside vEthernet in: {verdict(okt)}; guest rx inside vEthernet out: {verdict(okr)}")
            total["guest", "yes"] += okt + okr

print("\n# all windows")
for name in sorted({k[0] for k in total if k[1] == "rounds"}):
    print(f"{name}: rounds {total[name, 'rounds']}, % Idle Time above 100 in {total[name, 'over']}, largest {extremes[name, 'vmax']:.6f}; Get-Counter above 100 in {total[name, 'gc_over']} of {total[name, 'gc_samples']}; busy largest {extremes[name, 'busymax']:.3f}; mean busy gap to Get-Counter or WMI largest {max(gaps[name]):.3f}")
cr, cw, ur, uw = total["nvme", "cr"], total["nvme", "cw"], total["nvme", "ur"], total["nvme", "uw"]
print(f"disk 0 total: collector read {cr / MIB:.2f} MiB, NVMe {ur * UNIT / MIB:.2f} MiB; collector write {cw / MIB:.2f} MiB, NVMe {uw * UNIT / MIB:.2f} MiB")
print(f"disk 0 collector / NVMe: read {cr / (ur * UNIT):.4f}, write {cw / (uw * UNIT):.4f}")
print(f"disk 0 largest difference from NVMe in any window, either direction: {total['nvme', 'maxdiff']:.2f} data units of {UNIT} bytes")
print(f"raw delta inside the Get-Counter and WMI brackets: {total['inside', 'yes']} of {total['inside', 'of']}, including zero readings on disk 1")
print(f"of those, where the counter moved: {total['moved', 'yes']} of {total['moved', 'of']}")
print(f"seconds where vEthernet in exceeds NIC out {total['net', 'vi_over']} of {total['net', 'seconds']}; vEthernet out exceeds NIC in {total['net', 'vo_over']} of {total['net', 'seconds']}")
print(f"guest counters inside the vEthernet bracket: {total['guest', 'yes']} of {2 * len(windows)}")
