"""Experiment 07 analysis, from the raw files alone.
    python3 -I experiments/07-guest-walk/analyze.py <results dir> <run> <date>
A round's interval runs from the previous round's walk to its own. It is `churn` when it overlaps
churn.py's span, `idle` otherwise."""
import json, os, sys
from collections import Counter

d, run, date = sys.argv[1:4]
lines = [json.loads(l) for l in open(os.path.join(d, "%s-walk-%s.txt" % (run, date)))]
static, rounds = lines[0], lines[1:]
churn = json.loads(open(os.path.join(d, "%s-churn-%s.txt" % (run, date))).read())
c0, c1 = churn["churn_start_mono"], churn["churn_end_mono"]


def rng(xs, f="%.3f"):
    return (f + " to " + f) % (min(xs), max(xs))


def mean(xs):
    return sum(xs) / len(xs)


print("run %s %s, kernel %s, distro %s, %d vCPUs, HZ %d, uid %d" % (
    run, date, static["kernel"], static["distro"], static["nproc"], static["hz"], static["uid"]))
print("pid namespace %s, pid 1 comm %s, /proc mount %s" % (static["ns"]["pid"], static["pid1_comm"], static["proc_mount"]))
print("rounds %d, intervals %d; churn %.3f to %.3f mono, %d children, %.3f CPU-s, %.3f cores" % (
    len(rounds), len(rounds) - 1, c0, c1, churn["children"], churn["children_cpu_s"], churn["children_cores"]))

print("\n== 1. Does the walk read every entry it lists? ==")
mism = [r for r in rounds if r["listed"] != r["read"]]
fails = Counter()
for r in rounds:
    fails.update(r["failed"].values())
print("listed equals read in %d of %d rounds; listed %s, read %s" % (
    len(rounds) - len(mism), len(rounds), rng([r["listed"] for r in rounds], "%d"), rng([r["read"] for r in rounds], "%d")))
print("failed reads in the walk: %d in total, by errno %s" % (sum(fails.values()), dict(fails) or "none"))

print("\n== 2. Does the walk see every task in the VM? ==")
print("cgroup pids (this namespace) not in the walk: total %d over all rounds; walk pids not in cgroups: %d" % (
    sum(r["cg_not_in_walk"] for r in rounds), sum(r["walk_not_in_cg"] for r in rounds)))
print("cgroup procs printed as pid 0 (outside this PID namespace): %s; of them in the root cgroup: %s" % (
    rng([r["cg"]["procs_zero"] for r in rounds], "%d"), rng([r["cg"]["root_procs_zero"] for r in rounds], "%d")))
print("cgroup threads printed as 0: %s; visible processes in the root cgroup: %s; unreadable cgroups: %s" % (
    rng([r["cg"]["threads_zero"] for r in rounds], "%d"), rng([r["cg"]["root_procs_nonzero"] for r in rounds], "%d"),
    rng([r["cg"]["unreadable"] for r in rounds], "%d")))
print("thread groups: walk %s, cgroup total %s; walk share of all %s" % (
    rng([r["read"] for r in rounds], "%d"),
    rng([r["cg"]["procs_zero"] + r["cg"]["procs_nonzero"] for r in rounds], "%d"),
    rng([r["read"] / (r["cg"]["procs_zero"] + r["cg"]["procs_nonzero"]) for r in rounds])))
inside = sum(min(r["nr_threads"]) <= r["threads_walk"] + r["cg"]["threads_zero"] <= max(r["nr_threads"]) for r in rounds)
equal = sum(r["nr_threads"][0] == r["nr_threads"][1] == r["threads_walk"] + r["cg"]["threads_zero"] for r in rounds)
print("walk threads + cgroup threads at 0, against /proc/loadavg nr_threads read either side: inside %d of %d, "
      "equal with both sides equal %d" % (inside, len(rounds), equal))
print("walk threads %s, loadavg nr_threads %s" % (
    rng([r["threads_walk"] for r in rounds], "%d"), rng([x for r in rounds for x in r["nr_threads"]], "%d")))

print("\n== 3. Cores: the sum over rated processes, beside wider wholes ==")
iv = rounds[1:]
for name, sel in (("idle", [r for r in iv if r["mono"] <= c0 or r["mono"] - r["dt"] >= c1]),
                  ("churn", [r for r in iv if r["mono"] > c0 and r["mono"] - r["dt"] < c1]),
                  ("all", iv)):
    dt = sum(r["dt"] for r in sel)
    tot = {k: sum(r[k] * r["dt"] for r in sel) for k in ("cores_total", "child_cg_cores", "root_cg_cores", "busy_cores")}
    print("%s: %d intervals, %.3f s" % (name, len(sel), dt))
    print("  core-seconds: process sum %.3f, child cgroups %.3f, root cgroup %.3f, /proc/stat busy %.3f" % (
        tot["cores_total"], tot["child_cg_cores"], tot["root_cg_cores"], tot["busy_cores"]))
    print("  mean cores: process sum %.3f, child cgroups %.3f, root cgroup %.3f, /proc/stat busy %.3f" % tuple(
        tot[k] / dt for k in ("cores_total", "child_cg_cores", "root_cg_cores", "busy_cores")))
    print("  process sum over child cgroups %.3f, over busy %.3f; child cgroups less process sum %.3f core-s" % (
        tot["cores_total"] / tot["child_cg_cores"], tot["cores_total"] / tot["busy_cores"],
        tot["child_cg_cores"] - tot["cores_total"]))
    print("  per interval: process sum %s, child cgroups %s, unrated %s, gone %s, forks %s" % (
        rng([r["cores_total"] for r in sel]), rng([r["child_cg_cores"] for r in sel]),
        rng([r["unrated"] for r in sel], "%d"), rng([r["gone"] for r in sel], "%d"), rng([r["forks_delta"] for r in sel], "%d")))
over = [r for r in iv if r["cores_max"] > r["cores_total"]]
print("largest process cores above the sum: %d of %d intervals; above child cgroups: %d; process sum above child cgroups: %d" % (
    len(over), len(iv), sum(r["cores_max"] > r["child_cg_cores"] for r in iv), sum(r["cores_total"] > r["child_cg_cores"] for r in iv)))
idle = [r for r in iv if r["mono"] <= c0 or r["mono"] - r["dt"] >= c1]
ch = [r for r in iv if r["mono"] > c0 and r["mono"] - r["dt"] < c1]
gap_idle = sum((r["child_cg_cores"] - r["cores_total"]) * r["dt"] for r in idle) / sum(r["dt"] for r in idle)
gap_ch = sum((r["child_cg_cores"] - r["cores_total"]) * r["dt"] for r in ch)
dt_ch = sum(r["dt"] for r in ch)
print("churn: child cgroups less process sum %.3f core-s over %.3f s; less the idle rate %.3f x %.3f s: %.3f core-s, "
      "against churn children's own %.3f CPU-s" % (gap_ch, dt_ch, gap_idle, dt_ch, gap_ch - gap_idle * dt_ch, churn["children_cpu_s"]))

print("\n== 4. RSS: the sum over every process, beside /proc/meminfo ==")
print("rss_total %s B, mem used (MemTotal - MemAvailable) %s B, MemTotal - MemFree %s B" % (
    rng([r["rss_total"] for r in rounds], "%d"), rng([r["mem_used"] for r in rounds], "%d"), rng([r["mem_total_less_free"] for r in rounds], "%d")))
print("rss_total less mem used %s B; rss_total over mem used %s" % (
    rng([r["rss_total"] - r["mem_used"] for r in rounds], "%d"), rng([r["rss_total"] / r["mem_used"] for r in rounds])))
print("largest process rss above rss_total: %d of %d rounds" % (sum(r["rss_max"] > r["rss_total"] for r in rounds), len(rounds)))

print("\n== 5. Cost ==")
print("walk (collector processes()) ms: mean %.3f, max %.3f; sum alone ms: mean %.4f, max %.4f; whole probe round ms max %.2f" % (
    mean([r["walk_ms"] for r in rounds]), max(r["walk_ms"] for r in rounds),
    mean([r["sum_ms"] for r in rounds]), max(r["sum_ms"] for r in rounds), max(r["span_ms"] for r in rounds)))
