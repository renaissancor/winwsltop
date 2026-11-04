# Contract: evidence

Owner: the `evidence` role. Paths: `experiments/` and `docs/measurable.md` (the `evidence` row of
[`../ARCHITECTURE.md`](../ARCHITECTURE.md)).

## Purpose

Turn a question about a counter into an observation on this machine, and record the observation so
that every other area can cite it. The ledger, `docs/measurable.md`, is the only place a measured fact
about this machine lives.

## What belongs here

- Experiment folders `experiments/NN-name/` with a `README.md` (question, procedure, verdict), the
  probe and load sources, and `results/` holding raw output named `<run>-<date>.txt`.
- Ledger rows: one claim, its status (`Observed`, `False here`, `Not checked`, `Unresolved`) and the
  raw file that holds the number.

## What does not belong here

- Changing a collector because of a finding. Write the ledger row; collect reads it.
- A number without a raw file. If the output was not saved, the row says so and the status is weaker.
- Interpretation presented as observation. "Observed once" and "must hold under runs 3 and 4" are the
  register.

## Rules

1. Every number in the ledger appears in the raw file it cites. `scripts/check.sh` verifies that each
   cited results file exists; that the number is in it is checked by the auditor.
2. A claim proven on one trial says so. Experiment 01 is one trial per case.
3. A GPU load runs only when the user has asked, one agent at a time, and is stopped afterwards.
   `experiments/02-gpu-device` starts no load at all.
4. `nvidia-smi` is an NVML client. Agreement with it proves bindings and units, not the hardware.
5. The Windows-versus-WSL split is proven at VM level only. Per-Linux-process GPU use is not measured.
   No row may be read as if it were.
6. The machine line at the top of the ledger is updated when hardware, driver or kernel changes.

## How to validate

- Rebuild the probe and rerun it: `scripts/build.sh experiments/02-gpu-device/gpu_probe.cpp`, then
  the steps in that experiment's README. The new output goes in `results/` under today's date; the
  ledger row is updated only if the reading changed.
- For every ledger row, open the cited file and find the number. This is the auditor's job after a
  stage; the author does not do it alone.

## Current state (2026-11-04)

| item | value |
|---|---|
| experiments | 7: 07 run 2026-11-04, one run, one 20 s CPU load, no GPU load, read as uid 1000; 06 run 2026-11-02, no load, read only, one idle state, two runs (row updated 2026-11-02; the rows below it were not recounted); 05 run 2026-11-01, one CPU-only load, no GPU load, same machine line; 04 run 2026-11-01, no load started, i5-13600K; 01 and 02 run 2026-10-31 on an RTX 4070, driver 610.88, Windows 26200, WSL kernel 6.6.114.1; 03 run 2026-11-01, Windows 26200, same kernel, GPU and driver not rechecked |
| raw result files | 56 tracked files under `experiments/*/results/`, 151786 lines in total, counted 2026-11-04 |
| run with no file | experiment 01 run 1, idle baseline (3 s probe, noted in the ledger only) |
| trials per case | 1; experiment 03 is one session of 3 windows and 10 network brackets |
| ledger rows | 161 table rows in 20 tables |
| numbers checked against raw files, 2026-11-01 audit | three ranges and four values were off and are corrected; three sections and four rows have no raw file and now say so |

## Open questions

- From experiment 06: what `GetPerformanceInfo` `SystemCache` holds beyond standby plus `\Memory\Cache Bytes`,
  about 161 MiB (160.3 to 161.8) in every round of both runs, matching no counter run 2 added. Task Manager's
  "Cached" was not read. Until known, Windows cache keeps its `?` on screen.
- From experiment 06: why `/proc/pid/stat` rss reads below `/proc/pid/status` `VmRSS`, by 73728 to 2568192 B in
  every read, and whether `/proc/pid/statm` equals `VmRSS`. The `statm` comparison was looked at but its output
  was not saved, so it is not evidence; rerun it with the output saved. `contracts/collect.md` holds any change
  of the guest RSS source until then.
- From experiment 06: what the 522.9 to 541.7 MiB of `vmmemWSL` working set beyond guest used plus cached is,
  VM overhead or cache. One idle state, cache moved 4.2 MiB.
- PROPOSED, needs the owner's approval because it is a load: a page-cache test. Change the guest's page cache on
  purpose, by reading a large file in the guest and then dropping or letting it age, and read `vmmemWSL` working
  set and the guest's used and cached in one record line throughout, so whether the working set follows the cache
  can be seen. Disk and memory load, no GPU.
- Raised by core from the final audit of slice 1, 2026-11-02: two figures that never moved are graded differently.
  "`/proc/pressure` memory and io" records `memory` as Observed while `some` and `full` were 0 in every case, and
  "Guest swap used is `free -b` Swap used" is `Not checked` because it read 0 throughout. The screen follows the
  grades: PSI memory is drawn without `?`, swap with it. Reconcile the two: either PSI memory becomes `Not checked`
  until it is seen to move, or the ledger says why a constant 0 is enough for one and not the other.

- Done as experiment 06, 2026-11-02 (decision log). Was requested by core for slice 1 (`design/panel-grammar.md`,
  part 1) as experiment 0: read only, no load, no GPU. Windows available, commit and cache against
  `Get-Counter` and Task Manager; several processes' working set and private bytes against `Get-Process`;
  guest swap against `free -b`; guest RSS against `/proc/pid/status` `VmRSS` and `ps`; guest per-process
  cores against `ps` `TIME` over the same window; guest used and cache beside `vmmemWSL` working set in one
  sample; Windows create time and guest `starttime` constant while a process lives and different after a pid
  is reused. Each row clears a `?` on screen and closes part of `TODO.md` item 28.

- Which clock is right: WSL `CLOCK_MONOTONIC` ran 3.68 percent fast against the Windows wall clock on
  one machine, in one state. Neither has been compared with an outside reference.
- The 645-matmul second at the end of the WSL load in experiment 01. The load uses `time.monotonic()`,
  so a wall-clock step does not explain it. The WSL clock rate might. Unconfirmed.
- Why NVML utilization and engine counters disagree at idle (also in `contracts/collect.md`).
- The second LUID in `GPU Process Memory`.
- PROPOSED (`ROADMAP.md`): repeat experiment 01 with a game or a second workload, more than once.
- PROPOSED: an idle-baseline file for experiment 01, so run 1 has evidence like the others.
- PROPOSED: rerun the three sections that have no raw file (Windows collector, WSL collector, clock
  rate) with output saved, so their numbers can be checked.
- Why window 3 write differs from the drive by 5 Data Units when the other five comparisons are under
  one (ledger, "Disk and network collectors checked"). No NVMe read follows that window, so whether
  the gap lasted is unknown.
- Why `% Idle Time` exceeds 100 on disk 1, and whether the clamp fires on a disk with traffic.
- Whether `vEthernet` traffic is also counted on the NIC, and whether `Get-NetAdapterStatistics` and
  `GetIfTable2` share a source.
- No reading outside Windows was found for `% Idle Time` or the physical NIC, and the NIC is the
  first NET row on the screen. `% Idle Time` is bindings only in the ledger. The physical NIC is
  `Not checked` outside Windows, with bindings from `Get-NetAdapterStatistics`.
- PROPOSED: rerun experiment 03 with traffic on disk 1 and Wi-Fi connected, in a different ambient
  state, which gives those rows bindings at most: the NVMe query fails on disk 1 and Wi-Fi has no
  reading outside Windows. It needs a load, so it needs the user's approval.
  Take one more NVMe read about a minute after the last window.
- PROPOSED: `compare.sh` has no `set -e`, so a failed step leaves a short file and the run carries on.
- `scripts/check.sh --windows` does not compile `host_probe.cpp`. The orchestrator built the committed
  source and ran its `raw` mode on 2026-11-01, but the full run was not repeated, so that the saved
  output came from that source rests on format agreement.

- Experiment 04 left five items unresolved: the status of 4 whole-array PDH read failures, the
  9452541.6 `System Calls/sec` peak, `cpu0` reading 2.260% in the guest against 1.021% on `Hv VP 0`,
  the gap between process cores and busy, and the clock rate slope that does not repeat 3.68 percent.
- PROPOSED: rerun experiment 04's probe printing the whole-array PDH status (experiment 05 printed it for its own failures; 04's four stay unconfirmed), and repeat the clock-rate
  check alone over a longer span.
- Why `vmmemWSL` threads in `Ready` sit at priority 1 and running ones at 15, and whether that is why WSL
  lost 71.143 percent against Windows' 30.625 percent (experiment 05, one trial).
- Why vCPUs 10 to 17 ran at about the E rate at saturation, when at low load vCPUs 12 and 13 ran at the
  P rate.
- The guest io pressure at `win-20`, 105271.2 us/s, not repeated at `both-win-20`.
- The unit of `CPU Wait Time Per Dispatch`.
- What a partly busy guest shows under contention: not tested, every contended case had WSL at 20
  threads.
- PROPOSED, needs the user's approval: repeat experiment 05, with a partly busy guest under contention
  and a Windows-alone reference at 5 and 10 threads.

- From experiment 07: which of the thread groups outside this distro's PID namespace are kernel threads, the VM's own
  init or WSLg (ledger, "Guest process walk checked", "The collector's walk reaches every process in the WSL VM"). Not established without privileges.
- From experiment 07, a future question: is the root cgroup's `cpu.stat`, which read close to `/proc/stat` busy at idle
  in one run (same section, "Process cores summed over the walk are the guest's CPU"), a whole for the guest's CPU that a later design could use? It would need its own
  rows, more than one run and a load that is not churn alone, before anything is built on it.
- From experiment 07: experiment 07 is one run of 121 rounds in one idle state plus one 20 s churn. Repeat it before a
  guest whole is reconsidered.

## Decision log

- 2026-10-31. Experiment 01 verdict: Windows separates WSL's GPU work at VM level, under `vmwp.exe`,
  not `vmmemWSL`; shares do not add to 100. One trial per case.
- 2026-10-31. Experiment 02 verdict: NVML bindings and units agree with `nvidia-smi` in 5 of 5 rounds;
  `Dedicated Usage` is not VRAM in use; `Local Usage` is the closest per-process figure.
- 2026-11-01. Three ledger corrections (clock alignment exists; `time.monotonic()`; 15.1 MiB) made
  after an audit against the raw files. Approved by the user.
- 2026-11-01. A second audit re-read every number: experiment 01 ranges became 91-105, 71-97 and
  34-35 percent; `dwm` 3.3 GiB and 243-271 MiB; dedicated sum 4476-4564 MiB; Chrome in MB in read
  order; 685 ms and the second LUID's instance count have no raw file. Rows and sections without a
  raw file say so instead of citing `same` or `run 2`.
- 2026-11-01. Experiment 03 verdict: disk 0 bytes are checked against the drive's NVMe health log and
  `vEthernet` octets against guest `eth0`. `Get-Counter` proves bindings only, and WMI
  `MSDiskDriver_Performance` is a second path to the same count, not a second measurement.
- 2026-11-01. The probe opens the collector's private members with `#define private public`, so it
  prints what the collector read rather than a second reading a moment later. `IOCTL_DISK_PERFORMANCE`
  was not called: Microsoft documents it as enabling performance counters, and the experiment
  changes nothing.
- 2026-11-01. An audit found four experiment 03 rows overstated, the worst padding its checks with
  rows at zero. A comparison on a value that never moved is `Not checked`, and analysis scripts
  count it apart. A figure printed as a count is a counter delta, not rate times time.
- 2026-11-01. The ledger section "Disk and network collectors checked" has no date in its heading,
  because the collectors cite it by name and a rerun must not rename it.
- 2026-11-01. An earlier full run of experiment 03 was overwritten before the scripts were final. No
  ledger number comes from it. The index row in `docs/README.md` was added by the orchestrator in the
  same commit as the experiment (622f874), since `scripts/check.sh` needs them together.
- 2026-11-01. Approved by the user before the repo goes public: one GUID in the
  `STORAGE\Volume\{...}` instance names of
  `experiments/03-host-counters/results/window{1,2,3}-counters-2026-11-01.txt` was redacted across
  the history. It was a version 1 UUID whose last 12 hex digits were this PC's Bluetooth adapter MAC
  address; those digits now read `xxxxxxxxxxxx` (225 lines) and the rest of the GUID is kept. The
  other GUID in those `STORAGE\Volume` rows was kept, because its last 12 digits are the Windows
  "no NIC" placeholder, not an adapter address (decoded and compared with this machine's adapters by
  the orchestrator on 2026-11-01). No measured number changed, and `analyze.py` reruns on these
  files to output identical to `analysis-2026-11-01.txt`. This is the one sanctioned edit to a file
  holding measured output; derived analysis output is outside that claim, and
  `analysis-2026-11-01.txt` changed in 36ed08b. Commit hashes across the repo changed in the same
  rewrite, and citations were remapped.
- 2026-11-01. Experiment 04 record files are cut down at capture, because full `--record` lines are
  about 100 KB each and name this PC's programs in a public repo. The kept fields are unchanged.
- 2026-11-01. Experiment 04 ledger rows cite full paths, because `analysis-` and `window1-probe-` file
  names repeat experiment 03's.
- 2026-11-01. Experiment 05 verdict: VM figures are linear to saturation, Windows busy includes the VM's
  time as kernel time, guest busy does not show lost CPU, and guest busy times threads less `Hv VP`
  total tracks lost work. One trial per case, at three loss levels.
- 2026-11-01. Experiment 05 added two lighter Windows steps and a WSL-alone stretch inside the approved
  time, so that tracking could be tested, not only movement. No repeat was run.
- 2026-11-01. Experiment 05 record files mask other programs as `other-N`, numbered in order first seen,
  so a program can be followed without being named.
- 2026-11-02. Experiment 06, "Experiment 0" of `design/panel-grammar.md`, run read only with no load. Verdict
  (ledger, "Memory and process collectors checked"): Windows available, commit and commit limit, per-process
  working set and private bytes, and guest per-process cores read what they claim, bindings and units only, since
  on both sides the second reading comes from the same OS's own counts; Windows create time and guest `starttime`
  are constant while a process lives, reuse not seen; Windows cache Unresolved; guest swap Not checked, it read 0;
  guest RSS is No, it is `/proc/pid/stat` rss, below `VmRSS`; the guest cache in `vmmemWSL` Unresolved. Only the
  `vmmemWSL` row compares two kernels. One idle state, not a range of states.
- 2026-11-04. Experiment 07, requested by core for slice 2 (`design/panel-grammar-slice2.md`, step D). One run, 121
  rounds, non-elevated, no GPU load, one CPU load of 20 s (`churn.py`) that ended itself. Verdict (ledger, "Guest
  process walk checked"): the collector's walk reads every process in this distro's PID namespace and nothing outside
  it. It misses most thread groups in the VM and all of WSLg, and the CPU summed over it falls short of `/proc/stat` busy,
  far short under churn, since a process that exits between walks is lost. So the sum is not the guest's whole,
  and slice 2 drops `cores_total` and `rss_total`. One run, one idle state: a figure here is not a range of states.
