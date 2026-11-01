# Contract: core

Owner: the orchestrator, or a fresh agent given the `core` role. Paths: see the `core` row of
[`../ARCHITECTURE.md`](../ARCHITECTURE.md).

## Purpose

Hold the two seams, `src/core/snapshot.h` and `wsl-collector/PROTOCOL.md`, so that collectors and
readers never need to know about each other. Run the one-second loop, write the JSON record, work out
the derived values and the one inferred value, and keep the build, the checks and the ownership
documents true.

## What belongs here

- The snapshot model, the collector interfaces and the parser of the collector's line (`src/core/`).
- The loop and the command line (`src/main.cpp`).
- The JSONL recorder (`src/record/`): one line per snapshot, every value with its source.
- The Windows-versus-WSL GPU split, `lost` and the CPU figures derived beside it (`src/analysis/`). It is the
  only place inference happens.
- The protocol document, the build and check scripts, and every document about ownership.

## What does not belong here

- Reading a counter or a `/proc` file. That is collect.
- Drawing. That is ui.
- A claim about what a counter means. That is a ledger row, owned by evidence.
- Anything that stops, signals or configures a process on either side.

## Rules

1. A seam changes only here, and only when a request names the field, its unit, its source and the
   ledger row that proves it is measured.
2. The snapshot holds measured values and what `src/analysis` writes, and nothing else. `derived` is
   arithmetic on one side's own figures, so it says nothing that side did not: `gpu_split`, `cpu_derived`.
   `inferred` is a quantity no counter on either side reads, reached across the two: `lost`, the only one.
   The record gives each its word as `source`, and an inferred value also carries it in the key,
   `lost_cpu_inferred`, so a reader who takes one key cannot mistake it for a measurement.
3. The record never adds a Windows figure to a WSL figure. The two describe overlapping resources. `lost`
   subtracts one from the other on purpose: see the decision log.
4. A change of meaning in the collector's line raises `schema` in `PROTOCOL.md`, in `collector.py`
   and in `wsl_view.cpp`, in one commit.
5. `scripts/check.sh` passes before every commit. `scripts/check.sh --windows` passes before a commit
   that touches `src/`.
6. Facts live in one place (`docs/README.md`, "Where a fact lives"). A code comment may name a ledger
   section; it does not restate a number.

## How to validate

- `scripts/check.sh`: embedded collector in sync, every tracked path owned by exactly one area, every
  `.md` indexed, every relative link resolves, every result file the ledger cites exists.
- `scripts/check.sh --windows`: `winwsltop.exe` and `render_check.exe` build with `/W4 /WX`;
  `render_check` reports zero failures.
- `winwsltop --once` prints one line that a JSON parser accepts, and every key is in
  `docs/json-output.md` (`scripts/check.sh --windows`).
- `scripts/lost_check.cpp`, built and run by `scripts/check.sh --windows`: each clause of the `lost` gate by
  hand, `cpu_derived`, the record's labels, and a replay through `lost_tracker` of the fixture
  `experiments/05-cpu-contention/results/run1-record-2026-11-01.txt`, with
  `experiments/05-cpu-contention/results/run1-marks-2026-11-01.txt` to place each case. It starts no load.

## Current state (2026-11-01)

| item | value |
|---|---|
| source under core | `src/core` 619 lines, `src/main.cpp` 201, `src/record` 164, `src/analysis` 169, `scripts/lost_check.cpp` 448 |
| JSON schema | 1 |
| collection cost per sample | about 9 ms on this machine, ledger "Windows collector checked" |
| checks that exist | the two `check.sh` modes above, `scripts/lost_check.cpp` under the second, and `scripts/release.sh` |
| checks that are targets | CI, recorded fixtures (`ARCHITECTURE.md`, Targets) |

## Open questions

- PROPOSED: a per-collector status block in the snapshot and the record, so the screen can say which
  reader failed. Today a failed read leaves an empty vector (collect, `TODO.md` item 1) and only the
  engine counters carry a flag, `gpu_engines_known`, which the record now emits: `gpu_split.valid`
  is false for it, but also when `vmwp` is unknown, so the record cannot tell the two apart.
- PROPOSED: record the adapter LUID in `gpu_engine_row` and `gpu_memory_row` so a second adapter
  can be told apart (`TODO.md` item 4). Needs a ledger row first: two LUIDs are seen, the second unexplained.
- Who owns the GUI if one is built as a separate process over the JSON stream (`ROADMAP.md`)? Not decided.
- `docs/design/cpu-panel.md` section 7 wants `64 of 128` past 64 logical CPUs. `cpu_layout_view` holds one
  processor group only, so that head needs a machine-wide count: a field with no ledger row, since this
  machine has one group. Not added.
- Should `vm.vmmem_cores` read `null` instead of 0 when `vmmem_cores_known` is false? It would match
  "unknown is null", but it changes an existing key, so it would raise `schema`. Left at 0.

## Decision log

- 2026-10-31. The split counts the `3D` and `Compute` engines and leaves `Copy` and video out, because
  WSL CUDA appears as `3D` (ledger, experiment 01). Percentages are labelled not exact shares.
- 2026-10-31. Per-process GPU memory is shown as `local`, not `dedicated`, because dedicated summed to
  three times NVML's used figure (ledger, experiment 02).
- 2026-10-31. The loop uses a fixed deadline rather than summed sleeps, because Windows timers overshoot.
- 2026-11-01. Eight workspace rows became four areas; `src/record/` and `src/analysis/` joined core;
  the `verify` row was dropped and each area owns its own checks. Approved by the user.
- 2026-11-01. `CLAUDE.md` carries the rules; `AGENTS.md` is a pointer, kept because Codex reads it.
- 2026-11-01. `snapshot.gpu_engines_known` added; the split is invalid without it. Not written to the
  record at first, since `gpu_split.valid` already said so; added later, see below. The deadline clamps to now after a stall.
- 2026-11-01. The old rule "work from one company is reviewed by the other" became "authors never
  audit their own work; a fresh auditor reads each stage". The user confirmed on 2026-11-01 that a
  fresh Claude auditor is enough for now; cross-company review (Codex) is no longer required.
- 2026-11-01. Rule 1's one exception, ending the collector child on exit, confirmed by the user.
- 2026-11-01. `TODO.md` item 20 (disk and network counters without a ledger row) is the next
  session's first step: evidence records the rows, then collect cites them.
- 2026-11-01. Item 20 closed on 622f874, 74a5c32, 36ed08b and 0cd336d, audited by a fresh auditor against the
  raw files. It closes as a partial check, not a full one: the task's independent source was met
  only for disk 0 bytes and `vEthernet` octets, and what remains is in `contracts/evidence.md`.
- 2026-11-01. Releases are built by `scripts/release.sh`, never from the normal build: `/Zi` embeds the
  absolute `.pdb` path, which holds the builder's user name, so the release build has no debug
  information. The script publishes nothing. A person merges, then runs `gh release create`.
- 2026-11-01. The JSON output is a public interface for scripts and agents, documented in
  `docs/json-output.md`. Adding a key keeps `schema` at 1; a change of meaning raises it. `gpu_engines_known`
  was added to it, because an empty `gpu_engines` could mean idle or a failed read. A bad `--record` is now
  exit 2: `atoi` made a typo print nothing and exit 0.
- 2026-11-01. `--record 0`, a value out of range, and combining `--text`, `--once` and `--record` are exit 2.
  A closed stdout ends the run with exit 1, when launched natively. A release build never reads a collector from disk
  (`WWT_RELEASE`), and `scripts/release.sh` proves that with a planted collector and `wsl.exe`.
- 2026-11-01. The DLL search is limited to System32 first thing in `main`, a release build delay-loads
  `pdh` and `iphlpapi` and embeds a manifest, which stops a sidecar manifest redirecting a DLL. `/DEPENDENTLOAD:SYSTEM32` was tried and does not cover an `.exe`'s own static
  imports: the planted DLLs in `scripts/release.sh` still stopped it loading.
- 2026-11-01. A `winwsltop.exe.local` beside the exe was still honoured on the build machine, which has
  `DevOverrideEnable=1`, a developer setting. It is not tested on a default machine, and is accepted as
  outside what a release can close. Publishing order: merge, then `scripts/release.sh`, then
  `gh release create vX.Y.Z dist/winwsltop.exe dist/winwsltop.exe.sha256 --notes-file docs/releases/vX.Y.Z.md`.
- 2026-11-01. CPU panel data seam, `docs/design/cpu-panel.md` commit 2, all additive, schema stays 1.
  Snapshot: `cpu.user_pct`, `cpu.kernel_pct` (percent of the machine, the NT call, ledger "User and kernel
  time come from the call"), `cpu_layout` (ledger "CPU layout"), `ready_threads`, `vm_ready_threads` (threads,
  the `SystemProcessInformation` call, ledger "A Windows `Ready` count is a free "short of CPU" figure" and
  "The count of `vmmemWSL` threads in `Ready` tracks lost work"), `vm.vmmem_cores_known`. Protocol:
  `cpu_split` (ledger "`/proc/stat` fields move"), `load` (ledger "`/proc/loadavg`"), per-process `threads`
  (ledger, "`num_threads`" under "A process's `/proc/pid/stat` `processor` is where it works").
- 2026-11-01. `vmmem_cores` keeps its value and meaning, 0 when unmeasured, and `vmmem_cores_known` tells the two
  apart, as `gpu_engines_known` does for its list. The design's NaN belongs to the reader of the figure, not the
  field: `render.cpp` prints `vmmem_cores` with `%.2f` on the overall panel, so a NaN in the field would change
  that screen, and in the record it would turn a number into `null` on an existing key.
- 2026-11-01. The layout sits in the snapshot beside `cpu_view`, set once by `main.cpp`, and its rows go
  in every record line, so each line can still be read alone. `ready_threads` and `vm_ready_threads` are
  `std::optional`, so an unknown cannot be read as a count. New `double` figures default to NaN, so a reset
  view gives `null` in the record.
- 2026-11-01. `lost` subtracts `vm.vmmem_cores` from guest busy times guest threads. That is arithmetic across the
  two sides, and it is deliberate: decided by the orchestrator under the design the user approved,
  `docs/design/cpu-panel.md` section 4. It adds no overlapping resources together, so rule 3 stands, and it
  is labelled `inferred` wherever it appears. Guest busy is what the guest wanted, not what it got, which is
  why the gap means something. It is the only figure that crosses, and a second needs its own decision here.
- 2026-11-01. Rule 2 and the design used `derived` and `inferred` for the same thing. They now differ: `derived` is
  arithmetic on one side's own figures, `inferred` a quantity no counter reads. `gpu_split` is Windows' engine
  counters grouped by pid, so it is derived, which is what its `source` already says, and `ARCHITECTURE.md` no longer
  calls it the only inference.
- 2026-11-01. CPU panel analysis seam, `docs/design/cpu-panel.md` commit 3, additive, schema stays 1. All from
  `src/analysis/lost.cpp`. `snapshot.lost` and the record's `lost_cpu_inferred`: `state`, and `vcpus` in vCPUs, source
  guest busy, `core_pct` count and `vm.vmmem_cores`, ledger "Guest busy times threads less `Hv VP` `% Total Run Time`
  tracks lost work", and "`vm.vmmem_cores` is linear with the guest up to saturation". `snapshot.cpu_derived` and
  the record's `cpu_derived`: `vm_cpu_pct` and `own_kernel_pct`, percent of the machine, source `vm.vmmem_cores`
  and `cpu.kernel_pct`, ledger "The VM's time is Windows kernel time"; `ready_threads_mean` and
  `vm_ready_threads_mean`, threads, ledger "A Windows `Ready` count is a free "short of CPU" figure" and "The
  count of `vmmemWSL` threads in `Ready` tracks lost work". Two structs, not one, so each comment can
  say exactly which word applies.
- 2026-11-01. `wsl_view` moved from `src/ui/` to `src/core/` so analysis can read the guest line. `kStaleS` and `kGapMs`
  moved from `render.cpp` to `snapshot.h`, and the repeat test to `wsl_line_repeats`, so there is one source of each.
- 2026-11-01. Gate details the design left open. The window holds consecutive usable samples only, so a failed
  Windows read or an unknown `vmmem_cores` empties it too, and shows as `no_data`. The sample after a gap is the first of
  the new run, since a gap lies between samples, as on the graph. A repeat with nothing to carry, right after a reset, is not a
  sample. Under 5 samples reads `collecting` whatever the guest is doing, as the design's states say. The tracker runs on
  the priming pass too, so a first real sample can be known to repeat. `ready_threads_mean` does not wait on the VM's
  count, because the machine's figure stands without it; each mean needs its own 5 samples in a row.
- 2026-11-01. `lost_check` is `scripts/lost_check.cpp`, not a `src/analysis/` file, because `msvc.cmd` builds that folder into
  the program. The slim record has no `vmmem_cores_known`, `schema`, `memory`, `kernel_pct` or ready counts: the first
  is rebuilt from the pid, the next two put back empty, and the rest are checked by hand-built cases only. The
  10 s rule is asserted for the stops of `wsl-20`, `win-20` and `both-after`; `both-win-20`'s stop is pinned, see open questions.
- 2026-11-01. `lost` after a Windows load stops while the guest stays full fades over about 7 samples:
  13.534, 11.066, 8.708, 5.870, 3.219, 1.502, 1.211, 0.874 in `both-after`. Accepted as the lag of a 5-sample
  mean, stated in the README, with no further clause: the guest really is full there, and a clause tuned to
  one trial would not be evidence. `lost_check` pins these values.
- 2026-11-01. `lost` tolerates an interval up to 2500 ms, wider than the graph's `kGapMs`. A loop short of CPU
  runs late in exactly the case the figure exists for: the 1.503 s interval in `both-win-20` cost 4 samples
  mid-contention.
