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
- `winwsltop --once` prints one line that a JSON parser accepts, and every key is in
  `docs/json-output.md` (`scripts/check.sh --windows`).

## Current state (2026-11-01)

| item | value |
|---|---|
| source under core | `src/core` 243 lines, `src/main.cpp` 188, `src/record` 130, `src/analysis` 33 |
| JSON schema | 1 |
| collection cost per sample | about 9 ms on this machine, ledger "Windows collector checked" |
| checks that exist | the two `check.sh` modes above |
| checks that are targets | CI, recorded fixtures (`ARCHITECTURE.md`, Targets) |

## Open questions

- PROPOSED: a per-collector status block in the snapshot and the record, so the screen can say which
  reader failed. Today a failed read leaves an empty vector (collect, `TODO.md` item 1) and only the
  engine counters carry a flag, `gpu_engines_known`, which the record now emits: `gpu_split.valid`
  is false for it, but also when `vmwp` is unknown, so the record cannot tell the two apart.
- PROPOSED: record the adapter LUID in `gpu_engine_row` and `gpu_memory_row` so a second adapter
  can be told apart (`TODO.md` item 4). Needs a ledger row first: two LUIDs are seen, the second unexplained.
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
  A closed stdout ends the run with exit 1. A release build never reads a collector from disk
  (`WWT_RELEASE`), and `scripts/release.sh` proves that with a planted collector and `wsl.exe`.
