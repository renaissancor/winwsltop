# Contract: ui

Owner: the `ui` role. Paths: `src/ui/` (the `ui` row of [`../ARCHITECTURE.md`](../ARCHITECTURE.md)).

## Purpose

Turn one snapshot into a terminal screen that never claims more than was measured. `render()` is a
pure function from snapshot to lines; `console.cpp` puts lines on a Windows console; `wsl_view.cpp`
reads schema 1 of the collector's line.

## What belongs here

- `render.h`, `render.cpp`: layout, colour tones, number formatting, UTF-8 width, sanitising names.
- `wsl_view.h`, `wsl_view.cpp`: a strict parser for the collector's line. Private to `src/ui`.
- `console.h`, `console.cpp`: VT mode, alternate screen, Ctrl+C, plain text when redirected.
- `render_check.cpp`: the renderer's checks, built on hand-made snapshots.

## What does not belong here

- Reading a counter, a file or a process. The screen reads the snapshot and nothing else.
- Deriving a number that is not in the snapshot. Summing engines, adding Windows to WSL, or guessing
  a per-process GPU share are all forbidden.
- Changing what the collector sends. Request it from core.

## Rules

1. Every value is shown with what it is: `VM level, not exact shares`, `/dev/dxg holders, no usage
   measured`, `guest view, inside the VM`, `whole machine, includes the WSL VM`. These labels are
   checked by `render_check`.
2. A missing value is `n/a` or `unavailable`, never `0`.
3. Windows and WSL process lists sit side by side and are never merged, sorted together or compared.
4. Names from either system are arbitrary bytes. Control characters, bidi overrides and malformed
   UTF-8 never reach the terminal; `render_check` feeds hostile names.
5. Over 100 percent is printed as measured; bars clamp, numbers do not.
6. A line with a schema other than 1, or one that does not parse to the end, is reported as
   unreadable, not guessed at.
7. Thresholds for amber and red are judgement calls and live in one block at the top of `render.cpp`.

## How to validate

- `scripts/check.sh --windows` builds `render_check.exe` and runs it. Zero failures is the bar.
- `render_check busy 131 40` or `render_check bare 80 24` prints a frame to inspect by eye.
- `render_check busy` draws on the live console until Ctrl+C: colours, braille and Korean text need a
  person's eyes in Windows Terminal. Legacy conhost is unchecked.

## Current state (2026-11-01)

| item | value |
|---|---|
| source | `src/ui` 1444 lines, of which `render_check.cpp` 280 |
| checks | 4555, 0 failed, run on Windows 2026-10-31 |
| sizes checked | widths 1 to 300, heights 1 to 66, two snapshots plus hostile numbers |
| builds from recorded JSONL | no. TARGET in `ARCHITECTURE.md`; snapshots are hand-built |
| console | Windows Terminal checked by eye once; conhost not checked |

## Open questions

- PROPOSED (`ROADMAP.md`): GPU holders first in the Windows list, an `unattributed` line between the
  device total and the split, about 10 Windows rows, idle virtual NICs hidden.
- PROPOSED: `--replay FILE` through the same `render()`, which would also give the recorded fixtures
  the target asks for.
- Does the screen need the clock offset? `wsl_clock_known` and `wsl_clock_offset_s` are in the
  snapshot and the record but not drawn.

## Decision log

- 2026-10-31. `render()` does no I/O and takes the snapshot only, so it can be checked anywhere and a
  GUI can later draw from the same data.
- 2026-10-31. The parser is hand-written and strict (depth 16, `from_chars`, surrogate handling)
  rather than a library, to keep the build at one `cl` call with no dependencies.
- 2026-10-31. `Local Usage` is the per-process GPU memory column, labelled `LOCAL` (core decision log).
- 2026-11-01. The `no GPU engine data` branch is reachable: core sets `gpu_engines_known` and the
  split is invalid without it (`TODO.md` item 5). No screen change was needed.
- 2026-10-31. The refresh is held at 1 s with a fixed deadline; the console survives a rebuild
  because `build.sh` renames the running exe.
