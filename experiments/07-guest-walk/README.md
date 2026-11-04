# Experiment 07: does the collector's walk of `/proc` reach every guest process?

Question: can a `cores_total` and `rss_total` summed over every process the collector reads be trusted
as the whole of the WSL guest (`docs/design/panel-grammar-slice2.md` section 2)? Run 2026-11-04,
non-elevated, as uid 1000 in the one registered distro, `Ubuntu`. No GPU load. One CPU load of 20 s,
`churn.py`, which ends itself and is waited for.

Procedure, from the repo root:

    experiments/07-guest-walk/run.sh run1 <date>
    bash experiments/07-guest-walk/wslg.sh > experiments/07-guest-walk/results/run1-wslg-<date>.txt
    python3 -I experiments/07-guest-walk/analyze.py experiments/07-guest-walk/results run1 <date> \
      > experiments/07-guest-walk/results/analysis-<date>.txt

- `walk_probe.py` calls the collector's own `processes()` and records the `/proc` listing it made and
  every read that failed, with the errno. Beside it each round it reads `/proc/loadavg` field 4 (every
  task in the kernel), every `cgroup.procs` and `cgroup.threads` under `/sys/fs/cgroup`, and CPU time
  from `/proc/stat`, the root cgroup's `cpu.stat` and the top-level child cgroups' `cpu.stat`. This
  distro's cgroup namespace is the initial one, so the hierarchy holds every task in the VM, and a task
  outside this PID namespace is printed as pid 0. That is the count independent of the walk.
- `churn.py` starts a child every 0.25 s for 20 s; each spins 0.2 s and exits, so none lives a whole
  walk interval. Its CPU time comes from `os.times()`.
- `wslg.sh` records whether WSLg is running and whether its processes are in this distro's `/proc`.
- `run.sh` records the static facts, including the registered distros from the registry. `wsl.exe -l -v`
  was not run.

Verdict, one run: the walk reads every process in this distro's PID namespace and nothing outside it.
It misses 232-270 of 282-341 thread groups in the VM, short-lived processes' CPU time, and kernel
time. See the ledger section "Guest process walk checked".
