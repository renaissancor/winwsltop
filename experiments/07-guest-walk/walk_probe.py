"""Experiment 07 probe: does the collector's walk of /proc reach every guest process?

Read only. Run with python3 -I from the repo root:
    python3 -I experiments/07-guest-walk/walk_probe.py <seconds> > results/<run>-<date>.txt

Each round, about once a second, it calls the collector's own `processes()` (imported from
wsl-collector/collector.py, whose main() does not run on import), records the /proc listing that walk
made and every read inside it that failed, with the errno, and beside it reads three counts the walk
does not use:
  - /proc/loadavg field 4, the kernel's global count of tasks (threads), not limited to a PID namespace;
  - every cgroup.procs and cgroup.threads under /sys/fs/cgroup, which lists every task in the VM. A task
    outside the reader's PID namespace is printed as pid 0, so the zeros count tasks the walk cannot see;
  - /proc/stat busy ticks, the root cgroup's cpu.stat usage_usec (VM-wide) and the sum of the top-level
    child cgroups' usage_usec (which counts exited tasks too).
It prints no process names. One JSON line per round; the first line is the static facts.
"""
import errno, importlib.util, json, os, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location("collector", os.path.join(ROOT, "wsl-collector", "collector.py"))
collector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(collector)
HZ, PAGE = collector.HZ, collector.PAGE
CG = "/sys/fs/cgroup"


def rd(path):
    with open(path) as f:
        return f.read()


def loadavg():
    f = rd("/proc/loadavg").split()
    run, total = f[3].split("/")
    return int(run), int(total), int(f[4])


def stat():
    out = {}
    for line in rd("/proc/stat").splitlines():
        k, *v = line.split()
        if k == "cpu":
            v = [int(x) for x in v[:8]]
            out["busy_ticks"] = sum(v) - v[3] - v[4]  # less idle and iowait, as collector busy_pct
            out["total_ticks"] = sum(v)
        elif k in ("processes", "procs_running"):
            out[k] = int(v[0])
    return out


def usage(path):
    for line in rd(path + "/cpu.stat").splitlines():
        if line.startswith("usage_usec "):
            return int(line.split()[1])
    return None


def cgroups():
    """Every task in the hierarchy. pid 0 means outside this PID namespace."""
    procs_zero = procs_nz = threads_zero = threads_nz = 0
    visible, root_zero, root_threads_zero, children_usec, unreadable = set(), 0, 0, 0, 0
    for d, subdirs, files in os.walk(CG):
        try:
            p = [int(x) for x in rd(d + "/cgroup.procs").split()]
            t = [int(x) for x in rd(d + "/cgroup.threads").split()]
        except OSError:
            unreadable += 1
            continue
        z, tz = p.count(0), t.count(0)
        procs_zero += z
        procs_nz += len(p) - z
        threads_zero += tz
        threads_nz += len(t) - tz
        visible.update(x for x in p if x)
        if d == CG:
            root_zero, root_threads_zero = z, tz
            root_nz = len(p) - z
            for s in subdirs:
                u = usage(os.path.join(d, s))
                children_usec += u or 0
    return {"procs_zero": procs_zero, "procs_nonzero": procs_nz, "threads_zero": threads_zero,
            "threads_nonzero": threads_nz, "root_procs_zero": root_zero, "root_procs_nonzero": root_nz,
            "root_threads_zero": root_threads_zero, "unreadable": unreadable, "root_usec": usage(CG),
            "children_usec": children_usec}, visible


def walk():
    """The collector's processes(), with its /proc listing and its failed reads recorded."""
    listing, failed = [], {}
    real_listdir, real_read = os.listdir, collector.read

    def listdir(path):
        names = real_listdir(path)
        if path == "/proc":
            listing.append([n for n in names if n.isdigit()])
        return names

    def read(path):
        try:
            return real_read(path)
        except OSError as e:
            failed[path.split("/")[2]] = errno.errorcode.get(e.errno, str(e.errno))
            raise

    os.listdir, collector.read = listdir, read
    try:
        t0 = time.monotonic()
        rows = collector.processes()
        t1 = time.monotonic()
    finally:
        os.listdir, collector.read = real_listdir, real_read
    return rows, listing[0], failed, (t1 - t0) * 1000


def meminfo():
    m = {}
    for line in rd("/proc/meminfo").splitlines():
        k, _, rest = line.partition(":")
        m[k] = int(rest.split()[0]) * 1024 if rest.split() else 0
    return m


def static():
    ns = {k: os.readlink("/proc/self/ns/" + k) for k in ("pid", "cgroup", "mnt")}
    return {"static": True, "utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "kernel": os.uname().release, "distro": os.environ.get("WSL_DISTRO_NAME", ""),
            "nproc": os.cpu_count(), "hz": HZ, "page": PAGE, "ns": ns,
            "pid1_comm": rd("/proc/1/comm").strip(), "pid2_exists": os.path.exists("/proc/2"),
            "pid_max": int(rd("/proc/sys/kernel/pid_max")), "uid": os.getuid(),
            "proc_mount": [l for l in rd("/proc/mounts").splitlines() if l.split()[1] == "/proc"]}


def main():
    seconds = int(sys.argv[1])
    print(json.dumps(static()), flush=True)
    prev = None
    start = time.monotonic()
    for i in range(seconds + 1):
        target = start + i
        time.sleep(max(0.0, target - time.monotonic()))
        m0 = time.monotonic()
        la0, st0 = loadavg(), stat()
        rows, listing, failed, walk_ms = walk()
        cg, cg_visible = cgroups()
        la1, st1, mi = loadavg(), stat(), meminfo()
        m1 = time.monotonic()
        # The sums alone, as section 2 of the design defines them, timed apart from the reads.
        s0 = time.perf_counter()
        cores_total = rss_total = None
        if prev is not None:
            dt = m0 - prev["m0"]
            ticks = sum(r[2] - prev["rows"][k][2] for k, r in rows.items() if k in prev["rows"])
            cores_total = round(ticks / HZ / dt, 3)
        rss_total = sum(r[3] for r in rows.values())
        sum_ms = (time.perf_counter() - s0) * 1000
        line = {"i": i, "utc": round(time.time(), 3), "mono": round(m0, 3), "span_ms": round((m1 - m0) * 1000, 2),
                "walk_ms": round(walk_ms, 2), "sum_ms": round(sum_ms, 4),
                "listed": len(listing), "read": len(rows), "failed": failed,
                "threads_walk": sum(r[4] for r in rows.values()),
                "nr_threads": [la0[1], la1[1]], "last_pid": [la0[2], la1[2]],
                "cg": cg, "cg_not_in_walk": len(cg_visible - {k[0] for k in rows}),
                "walk_not_in_cg": len({k[0] for k in rows} - cg_visible),
                "rss_total": rss_total, "rss_max": max(r[3] for r in rows.values()),
                "mem_used": mi["MemTotal"] - mi["MemAvailable"], "mem_total_less_free": mi["MemTotal"] - mi["MemFree"],
                "forks": st1["processes"]}
        if prev is not None:
            dt = m0 - prev["m0"]
            per = [(r[2] - prev["rows"][k][2]) / HZ / dt for k, r in rows.items() if k in prev["rows"]]
            line.update({
                "dt": round(dt, 4), "cores_total": cores_total, "cores_max": round(max(per), 3),
                "rated": len(per), "unrated": len(rows) - len(per),
                "gone": len(set(prev["rows"]) - set(rows)),
                "busy_cores": round((st0["busy_ticks"] - prev["st0"]["busy_ticks"]) / HZ / dt, 3),
                "root_cg_cores": round((cg["root_usec"] - prev["cg"]["root_usec"]) / 1e6 / dt, 3),
                "child_cg_cores": round((cg["children_usec"] - prev["cg"]["children_usec"]) / 1e6 / dt, 3),
                "forks_delta": st1["processes"] - prev["forks"]})
        print(json.dumps(line, separators=(",", ":")), flush=True)
        prev = {"m0": m0, "rows": rows, "st0": st0, "cg": cg, "forks": st1["processes"]}


if __name__ == "__main__":
    main()
