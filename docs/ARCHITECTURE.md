# Architecture and ownership

How winwsltop is built, who owns each part, and what each part may read and write.
`docs/README.md` indexes every document. `CLAUDE.md` holds the rules every agent follows.

## Data flow

```
experiments/  ──measured facts──▶  docs/measurable.md  (the evidence ledger)
                                          │ cited before any counter is used
                                          ▼
 src/host  src/gpu  src/wsl + wsl-collector/collector.py        (collect: writes the snapshot)
        └──────┬──────┘        │  JSON line over a pipe, wsl-collector/PROTOCOL.md
               ▼               ▼
         src/core/snapshot.h  ◀──  src/main.cpp samples once a second
               │
               ├──▶ src/analysis/attribution.cpp   derives gpu_split (the only inference)
               ├──▶ src/record/jsonl.cpp           --once, --record N: one JSON line per snapshot
               └──▶ src/ui/render.cpp              live screen and --text; reads the snapshot
                        └── src/ui/wsl_view.cpp    parses the collector's line (PROTOCOL schema 1)
```

Two seams carry everything: `src/core/snapshot.h` between collectors and readers, and
`wsl-collector/PROTOCOL.md` between the Linux collector and both its readers (the bridge and the
screen). Only core changes a seam.

## Areas

One owner per file. The owner of a path is the first row whose `owns` column matches it. An edit
needed in another row is written as a request to the orchestrator, not made.

| area | owns | reads | writes | contract |
|---|---|---|---|---|
| core | `src/core/`, `src/main.cpp`, `src/record/`, `src/analysis/`, `wsl-collector/PROTOCOL.md`, `scripts/`, `docs/README.md`, `docs/ARCHITECTURE.md`, `docs/ROADMAP.md`, `docs/TODO.md`, `docs/contracts/`, `CLAUDE.md`, `AGENTS.md`, `README.md`, `.claude/`, `.gitignore`, `.gitattributes` | everything | the seams, the loop, the recorder, the split, the build and check scripts, all ownership docs | [contracts/core.md](contracts/core.md) |
| collect | `src/host/`, `src/gpu/`, `src/wsl/`, `wsl-collector/collector.py`, `wsl-collector/embed.py`, `wsl-collector/clock_probe.cpp` | `snapshot.h`, `collectors.h`, `PROTOCOL.md`, `measurable.md` | snapshot fields, the collector and its embedded copy | [contracts/collect.md](contracts/collect.md) |
| ui | `src/ui/` | `snapshot.h`, `PROTOCOL.md`, `measurable.md` | screen, console, the line parser, `render_check.cpp` | [contracts/ui.md](contracts/ui.md) |
| evidence | `experiments/`, `docs/measurable.md` | anything | probes, loads, raw results, ledger rows | [contracts/evidence.md](contracts/evidence.md) |

The `core` row is the orchestrator's. A fresh agent given the `core` role acts for the
orchestrator and reads [contracts/core.md](contracts/core.md) first.

## Handoffs, one way

1. **evidence → collect.** A counter is used only after `docs/measurable.md` records it as observed on
   this machine. The collector comment cites the ledger row; it does not restate the number.
2. **collect → core.** A new measured value needs a snapshot field. collect requests it; core adds it
   to `snapshot.h`, the recorder and, if the collector's line changes, `PROTOCOL.md` with a schema bump.
3. **core → ui.** The screen reads the snapshot and schema 1 of the protocol and nothing else. A field
   the screen wants is requested from core.
4. **any → evidence.** A claim about what a counter means is a request for an experiment, not a
   comment in code.

## Targets

A target is a check that does not exist yet. Nothing may assume it runs.

| check | state |
|---|---|
| `scripts/check.sh` passes on the Linux side: embedded collector in sync, ownership complete, docs indexed, links resolve | **exists**, run before every commit |
| `scripts/check.sh --windows` builds `winwsltop.exe` and `render_check.exe` and runs the render checks | **exists**, needs the Windows toolchain |
| CI runs `scripts/check.sh` on push | TARGET: no `.github/` exists |
| Recorded JSONL fixtures under `tests/` that the screen is rendered from | TARGET: `render_check.cpp` builds its snapshots by hand |
| Collector checks replayable without a live machine | TARGET: `clock_probe.cpp` and the ledger checks are manual |
| Experiment 01 repeated more than once per case | TARGET: one trial per case (`docs/measurable.md`) |
| GPU memory split checked under a WSL allocation | TARGET: idle only |

## Build

Source is edited in WSL; MSVC runs on Windows. `scripts/build.sh` mirrors the tree to
`%LOCALAPPDATA%\winwsltop\<tree name>` and compiles there, so each worktree has its own mirror and
builds may run in parallel. MSVC is never run on a `\\wsl$` path. A GPU load, by contrast, runs for
one agent at a time, because a second load changes the first one's numbers.
