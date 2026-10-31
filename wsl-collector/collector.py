"""Linux half of winwsltop. Sent over stdin to the bootstrap in PROTOCOL.md; never installed.
Reads /proc and prints one JSON line a second. Stdlib only, Python 3.8+. See PROTOCOL.md."""
import json, os, sys, threading, time

sys.dont_write_bytecode = True
HZ = os.sysconf("SC_CLK_TCK")
PAGE = os.sysconf("SC_PAGE_SIZE")
TOP = 30


def read(path):
    with open(path) as f:
        return f.read()


def cpu_times():
    out = []
    for line in read("/proc/stat").splitlines():
        if not line.startswith("cpu"):
            break
        v = [int(x) for x in line.split()[1:9]]
        out.append((v[3] + v[4], sum(v), v[7]))  # idle + iowait, total, steal
    return out


def memory():
    m = {}
    for line in read("/proc/meminfo").splitlines():
        k, _, rest = line.partition(":")
        m[k] = int(rest.split()[0]) * 1024 if rest.split() else 0
    return {"total": m["MemTotal"], "available": m["MemAvailable"], "cached": m.get("Cached", 0),
            "swap_used": m.get("SwapTotal", 0) - m.get("SwapFree", 0)}


def pressure():
    out = {}
    for kind in ("cpu", "memory", "io"):
        try:
            out[kind] = float(read("/proc/pressure/" + kind).split()[1].split("=")[1])
        except (OSError, IndexError, ValueError):
            out[kind] = None
    return out


def processes():
    rows = {}
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        try:
            text = read("/proc/%s/stat" % name)
        except OSError:
            continue
        end = text.rfind(")")
        f = text[end + 2:].split()
        rows[(int(name), int(f[19]))] = (text[text.find("(") + 1:end], f[0], int(f[11]) + int(f[12]), int(f[21]) * PAGE)
    return rows


def dxg_holders():
    """Identity only: an fd open on /dev/dxg says who is on the GPU, not how much it uses."""
    pids, denied = [], 0
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        try:
            for fd in os.listdir("/proc/%s/fd" % name):
                try:
                    if os.readlink("/proc/%s/fd/%s" % (name, fd)) == "/dev/dxg":
                        pids.append(int(name))
                        break
                except OSError:
                    pass
        except PermissionError:
            denied += 1
        except OSError:
            pass
    return pids, denied


OUT = threading.Lock()


def emit(text):
    with OUT:
        sys.stdout.write(text + "\n")
        sys.stdout.flush()


def answer_pings():
    """Reads `P <seq>` lines from stdin. EOF means Windows is gone, so the collector exits.
    `d` is the hold time on CLOCK_MONOTONIC; `wm` is wall minus monotonic, which moves when the wall clock steps."""
    stdin = sys.stdin.buffer
    while True:
        line = stdin.readline()
        if not line:
            os._exit(0)
        m2, u2 = time.monotonic(), time.time()
        parts = line.split()
        if len(parts) != 2 or parts[0] != b"P" or not parts[1].isdigit():
            continue
        wm = u2 - m2
        emit('{"pong":%s,"u2":%.6f,"d":%.6f,"wm":%.6f}' % (parts[1].decode(), u2, time.monotonic() - m2, wm))


def main():
    threading.Thread(target=answer_pings, daemon=True).start()
    prev_cpu, prev_proc, prev_t = cpu_times(), processes(), time.monotonic()
    distro = os.environ.get("WSL_DISTRO_NAME", "")
    while True:
        time.sleep(1)
        start = time.monotonic()
        now_cpu, now_proc = cpu_times(), processes()
        dt = start - prev_t
        pct, steal = [], 0.0
        for (i0, t0, s0), (i1, t1, s1) in zip(prev_cpu, now_cpu):
            pct.append(round(100.0 * (1 - (i1 - i0) / (t1 - t0)), 2) if t1 > t0 else 0.0)
        if now_cpu[0][1] > prev_cpu[0][1]:
            steal = round(100.0 * (now_cpu[0][2] - prev_cpu[0][2]) / (now_cpu[0][1] - prev_cpu[0][1]), 2)
        rows = []
        for key, (comm, state, ticks, rss) in now_proc.items():
            old = prev_proc.get(key)
            rows.append({"pid": key[0], "name": comm, "state": state, "rss": rss,
                         "cores": round((ticks - old[2]) / HZ / dt, 3) if old else 0.0})
        holders, denied = dxg_holders()
        rows.sort(key=lambda r: (r["cores"], r["rss"]), reverse=True)
        keep = rows[:TOP] + [r for r in rows[TOP:] if r["pid"] in holders]
        for r in keep:
            r["on_gpu"] = r["pid"] in holders
        emit(json.dumps({"schema": 1, "distro": distro, "unix_s": round(time.time(), 3),
                          "mono_s": round(start, 3), "kernel": os.uname().release, "busy_pct": pct[0],
                          "core_pct": pct[1:], "steal_pct": steal, "memory": memory(), "pressure": pressure(),
                          "process_count": len(rows), "processes": keep, "dxg_pids": holders,
                          "dxg_denied": denied, "cost_ms": round((time.monotonic() - start) * 1000, 2)},
                         separators=(",", ":")))
        prev_cpu, prev_proc, prev_t = now_cpu, now_proc, start


if __name__ == "__main__":
    try:
        main()
    except (BrokenPipeError, KeyboardInterrupt):
        os._exit(0)
