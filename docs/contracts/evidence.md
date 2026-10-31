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

## Current state (2026-11-01)

| item | value |
|---|---|
| experiments | 3: 01 and 02 run 2026-10-31 on an RTX 4070, driver 610.88, Windows 26200, WSL kernel 6.6.114.1; 03 run 2026-11-01, Windows 26200, same kernel, GPU and driver not rechecked |
| raw result files | 20, 10512 lines in total |
| run with no file | experiment 01 run 1, idle baseline (3 s probe, noted in the ledger only) |
| trials per case | 1; experiment 03 is one session of 3 windows and 10 network brackets |
| ledger rows | 72 table rows in 7 groups |
| numbers checked against raw files, 2026-11-01 audit | three ranges and four values were off and are corrected; three sections and four rows have no raw file and now say so |

## Open questions

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
  first NET row on the screen. The ledger's status for both is bindings only.
- PROPOSED: rerun experiment 03 with traffic on disk 1 and Wi-Fi connected, which closes those
  `Not checked` rows, in a different ambient state. It needs a load, so it needs the user's approval.
  Take one more NVMe read about a minute after the last window.
- PROPOSED: `compare.sh` has no `set -e`, so a failed step leaves a short file and the run carries on.

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
