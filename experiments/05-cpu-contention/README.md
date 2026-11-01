# Experiment 05: when Windows and WSL both want the CPU, what shows it?

Question: is a VM figure linear with the guest up to saturation, does Windows busy include the VM's
time, what reveals that WSL lost CPU, and can the guest read 100% while it got less? Experiment 04
could not test these, because it started no load. This one starts one short CPU-only load, approved by
the user for 2026-11-01: no GPU load, no memory or disk load, nothing stopped, signalled or
reconfigured. One trial of each case. Build from the repo root, then run:

    scripts/build.sh experiments/05-cpu-contention/contention_probe.cpp
    scripts/build.sh experiments/05-cpu-contention/cpu_burn.cpp
    scripts/build.sh
    g++ -O2 -pthread -std=c++17 -o <dir>/cpu_burn experiments/05-cpu-contention/cpu_burn.cpp
    experiments/05-cpu-contention/run.sh <build dir as a /mnt/c path> <dir>/cpu_burn <run> <date> <dir for full record lines>
    python3 experiments/05-cpu-contention/analyze.py experiments/05-cpu-contention/results <run> <date> \
      > experiments/05-cpu-contention/results/analysis-<date>.txt

Run the builds before `run.sh`, never during it: MSVC is a CPU load. A last argument `dry` to `run.sh`
gives every step 1 s and every burner 0 s, which does no work, to prove the sequence and the file
formats. A real run needs the user's approval each time.

- `cpu_burn.cpp`: one source for both sides. Threads run a serial integer chain on the stack and
  report work per thread, thread CPU time as that side's OS counts it, the processor they were on, and
  work per processor, once a second. Each worker reads the clock after every unit and returns at the
  deadline, and `<seconds>` is cut to `kHardLimitSeconds`, 120, in the code. It sets no priority,
  affinity or QoS, and no driver is needed to stop it. The chain's steps depend on each other, so the
  rate follows time on the CPU and its clock; two threads on one core barely slow each other, which
  means this burner shows little of the cost of SMT.
- `contention_probe.cpp`: experiment 04's `cpu_probe.cpp` carried on, leaving that file as it was. New:
  `CPU Wait Time Per Dispatch`, `CPU Contention Time Per Dispatch`, `CPU Wake Up Time Per Dispatch`,
  `% Total Core Run Time`, `% Guest-Relative Utilization`; the status of a whole-array read that fails,
  which experiment 04 left unprinted; `cpu_burn.exe`'s process row; a count of threads in each
  `ThreadState` for the VM, the burner, the probe, the Idle process and the rest; a `clock` mode. Gone:
  `cost` mode and the capped re-read. `\Processor Information` and the rest are unchanged. No process
  is opened.
- `guest_sample.py`: experiment 04's sampler, plus memory and io pressure, a state count over every
  task, the top 8 processes with `num_threads` and `processor`, and the burner's own tasks. A name other
  than `cpu_burn` is written `other`.
- `run.sh`: the recorders throughout, then the cases below. It stops nothing.
- `slim_record.py`: the cut of experiment 04, and `top_other`: up to 5 other rows a record, with the
  name replaced by `other-N` (`guest-N` in the guest), numbered in order first seen. A program can be
  followed through the run without being named. The full lines stayed outside the repo and were not
  kept. A claim that needs a dropped process cannot cite these.
- `analyze.py`: turns the files above into `results/analysis-<date>.txt`. It needs no live machine.

The cases, in order. The driver recorded at 1 Hz through all of them, from 5 s before the first:

| case | seconds | load |
|---|---|---|
| `baseline` | 20 | none |
| `wsl-1`, `wsl-2`, `wsl-5`, `wsl-10`, `wsl-20` | 15 each | WSL burner, unpinned, that many threads |
| `settle-1` | 15 | none |
| `win-20` | 40 | Windows burner, 20 threads |
| `settle-2` | 15 | none |
| `both-before` | 10 | WSL 20, alone |
| `both-win-5`, `both-win-10` | 12 each | WSL 20 and Windows 5, then 10 |
| `both-win-20` | 60 | WSL 20 and Windows 20 |
| `both-after` | 8 | WSL 20, alone |
| `settle-3` | 15 | none |

The task as given asked for one both case at 20 threads each. The two lighter Windows steps and the
WSL-alone stretch either side were added inside the same 3 to 5 minutes, because one contended point
can only show a counter moved, not that it tracks: these give loss at three levels with a reference
beside them, from one unbroken WSL burner. A case mean leaves out its first 2 s and last 1 s.

How independent each comparison is:

| comparison | independent of | shows |
|---|---|---|
| Burner units per second against any counter | yes: wall clock and completed work, no OS accounting | ground truth for lost throughput |
| Guest `/proc/stat` and thread CPU time against `Hv VP` run time | yes: another kernel counts one side, the hypervisor the other | what the guest believes against what it got |
| `vmmemWSL` process time, `CycleTime` and `Hv VP` | no for the VM's time, all Windows' and the hypervisor's own count; the burner is the check | that they agree with each other, and which follows the work |
| NT per-LP call against `\Processor Information` | nothing: the same kernel counts | bindings, under load |
| `Ready` state count against `Processor Queue Length` | nothing: the same kernel counts | that they are the same figure |
| `Actual Frequency` against burner rate per LP | partly: the rate shows the clock did not fall, but not its value, since the cycles a unit takes are not measured | that P LPs did not slow from 1 to 20 threads |

Limits. The burner's work per second depends on the core type, so the WSL loss is against the same
20 threads alone either side of the contended cases, not against any Windows figure. Every contended
case had WSL at 20 threads: what a partly busy guest shows under contention is `Not checked`. Thermal
output was not read: no temperature exists here.

Other work during the run was not stopped. No game ran. A browser and a web view started on their
own during `both-win-10` and `both-win-20`; their rows are in the record as `other-30` and `other-47`,
and `both-win-10` carried 1.892 other Windows cores, against 0.313 to 0.762 in the other contended
cases. Read that step with that in mind.

**Result (2026-11-01):** see `docs/measurable.md`, section "CPU contention checked".
