# Experiment 06: do the memory and process figures read true?

Question: do the figures the screen shows with no ledger row read what they claim? Those are Windows
available, commit and cache (`src/host/memory.cpp`), per-process working set and private bytes, and
the create time each row is keyed on (`src/host/processes.cpp`). On the guest side they are swap used,
per-process RSS and per-process cores (`wsl-collector/collector.py`). The experiment also checks whether
the guest's page cache shows up as `vmmemWSL` working set, and whether create time and `starttime` stay
constant while a process lives. This is "Experiment 0" in `docs/design/panel-grammar.md`, approved by the
owner on 2026-11-02 as read only.

No load is started, GPU or CPU, and no setting is changed. The run starts three idle processes of its
own and ends each one: `sleep 900` in the guest, one long `ping.exe -n 600 127.0.0.1`, and one
`ping.exe -n 1 127.0.0.1` a round, so that pids are freed and could be reused. Build from the repo root,
then run:

    scripts/build.sh experiments/06-memory-process/mem_probe.cpp
    scripts/build.sh
    experiments/06-memory-process/run.sh <build dir as a /mnt/c path> run1 <date> <dir for full record lines>
    python3 experiments/06-memory-process/analyze.py experiments/06-memory-process/results run1 <date> \
      > experiments/06-memory-process/results/analysis-<date>.txt

Run the builds before `run.sh`, never during it: MSVC is a CPU load. Run 2 is `windows.ps1` alone
with `-AllMemory`, 10 rounds, and no guest side. It reads the `\Memory` byte counts that run 1 left out,
to look for what `SystemCache` holds. Its analysis is `results/analysis-run2-<date>.txt`.

- `mem_probe.cpp` `#include`s both collector sources and opens the process collector's private map,
  so the create time it prints is the key the collector read. `once` prints the memory collector's
  fields, then every process row after a priming call 200 ms earlier. Names outside a list of
  Windows' own and the VM's processes print as `other`.
- `windows.ps1`: a round of `Get-Counter` on `\Memory` (c1), `Get-Process` (p1), `mem_probe once`,
  `Get-Process` (p2) and `Get-Counter` (c2), in that order, with UTC times. It also starts the pings.
  `StartTime` is read where Windows allows it, non-elevated.
- `guest_sample.py`: every 0.5 s, `/proc/meminfo`, every `/proc/pid/stat` and `/proc/pid/status`
  `VmRSS`, `ps -eo pid=,rss=,cputimes=` and `free -b`, each timed on `CLOCK_MONOTONIC`, the clock the
  collector's `mono_s` uses.
- `run.sh`: the static facts, then the guest sampler, `windows.ps1` (25 rounds) and
  `winwsltop --record 60` together.
- `slim_record.py`: `results/run1-record-*.txt` keeps `memory`, `vm`, the timing keys, the guest
  `memory`, and pid, sizes and cores of every process on both sides. Other programs' names become
  `other`, as in the probe and sampler, because this repo is public. The full lines were not kept.
- `analyze.py`: the comparisons below, from the files alone.

Each comparison is a bracket. The collector's value counts as `inside` when it lies between an
independent reading taken just before it and one taken just after it. `equal` means both readings agree
and the collector matches them. `outside` is printed with its distance. Guest readings are bracketed on
`mono_s` and the collector's `cost_ms`. Windows readings are bracketed by round order. `Get-Counter`
takes about 1.5 s per call and does not say when inside that call it sampled, so its bracket is loose.
A value that never moved in the run is counted apart, because agreement on it proves bindings at that
one value only.

How independent each comparison is:

| comparison | independent of | shows |
|---|---|---|
| `GlobalMemoryStatusEx` and `GetPerformanceInfo` against `Get-Counter` `\Memory` | nothing: the same kernel counts | bindings, units, which counter a field matches |
| process rows against `Get-Process` sizes | nothing: .NET reads the same NT call | the hand-written struct's offsets and units |
| create time against `Get-Process` `StartTime` | the NT call: `StartTime` comes from `GetProcessTimes` on an opened handle | the field and its unit, where the handle opens |
| guest RSS and cores against `/proc/pid/stat`, `status` and `ps` | nothing: the same guest kernel counts | bindings, units, which kernel figure the collector shows |
| guest swap against `free -b` | nothing, and it never moved | not checked |
| guest memory beside `vmmemWSL` working set | yes: the hypervisor host counts one side | one state only, with nothing changing either side on purpose |

Nothing here reads memory outside each OS's own accounting. Task Manager was not read: its window
has no command-line reading, and opening it would start a process that does work.

**Result (2026-11-02):** see `docs/measurable.md`, section "Memory and process collectors checked".
