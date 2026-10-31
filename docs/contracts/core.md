# Contract: core

Owner: the orchestrator, or a fresh agent given the `core` role. Paths: see the `core` row of
[`../ARCHITECTURE.md`](../ARCHITECTURE.md).

## Purpose

Hold the two seams, `src/core/snapshot.h` and `wsl-collector/PROTOCOL.md`, so that collectors and
readers never need to know about each other. Run the one-second loop, write the JSON record, derive
the one inferred value, and keep the build, the checks and the ownership documents true.

## What belongs here

- The snapshot model and the collector interfaces (`src/core/`).
- The loop and the command line (`src/main.cpp`).
- The JSONL recorder (`src/record/`): one line per snapshot, every value with its source.
- The Windows-versus-WSL GPU split (`src/analysis/`). It is the only place inference happens.
- The protocol document, the build and check scripts, and every document about ownership.

## What does not belong here

- Reading a counter or a `/proc` file. That is collect.
- Drawing. That is ui.
- A claim about what a counter means. That is a ledger row, owned by evidence.
- Anything that stops, signals or configures a process on either side.

## Rules

1. A seam changes only here, and only when a request names the field, its unit, its source and the
   ledger row that proves it is measured.
2. The snapshot holds measured values and `gpu_split`. Nothing else in it is derived. A derived value
   is marked `derived` in the record, as `gpu_split.source` is today.
3. The record never adds a Windows figure to a WSL figure. The two describe overlapping resources.
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
- `winwsltop --once` prints one line that a JSON parser accepts (manual; a check is a TARGET).

## Current state (2026-11-01)

| item | value |
|---|---|
| source under core | `src/core` 242 lines, `src/main.cpp` 116, `src/record` 127, `src/analysis` 33 |
| JSON schema | 1 |
| collection cost per sample | about 9 ms on this machine, ledger "Windows collector checked" |
| checks that exist | the two `check.sh` modes above |
| checks that are targets | CI, recorded fixtures, a JSON validity check (`ARCHITECTURE.md`, Targets) |

## Open questions

- PROPOSED: a `gpu_engines_known` flag, or a per-collector status block, so a failed read is shown
  as unknown instead of as the previous second's values (`TODO.md` item 1). This is a seam change.
- PROPOSED: record the adapter LUID in `gpu_engine_row` and `gpu_memory_row` so a second adapter
  can be told apart (`TODO.md` item 4). Needs a ledger row first: two LUIDs are seen, the second unexplained.
- Should `--record 0` be an error? Today it prints nothing and exits 0.
- Who owns the GUI if one is built as a separate process over the JSON stream (`ROADMAP.md`)? Not decided.

## Decision log

- 2026-10-31. The split counts the `3D` and `Compute` engines and leaves `Copy` and video out, because
  WSL CUDA appears as `3D` (ledger, experiment 01). Percentages are labelled not exact shares.
- 2026-10-31. Per-process GPU memory is shown as `local`, not `dedicated`, because dedicated summed to
  three times NVML's used figure (ledger, experiment 02).
- 2026-10-31. The loop uses a fixed deadline rather than summed sleeps, because Windows timers overshoot.
- 2026-11-01. Eight workspace rows became four areas; `src/record/` and `src/analysis/` joined core;
  the `verify` row was dropped and each area owns its own checks. Approved by the user.
- 2026-11-01. `CLAUDE.md` carries the rules; `AGENTS.md` is a pointer, kept because Codex reads it.
