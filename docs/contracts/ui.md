# Contract: ui

Owner: the `ui` role. Paths: `src/ui/` (the `ui` row of [`../ARCHITECTURE.md`](../ARCHITECTURE.md)).

## Purpose

Turn snapshots into a terminal screen that never claims more than was measured. `render()` is a pure
function of one snapshot and an optional in-memory `history`; with no history it is one snapshot to lines; `console.cpp` puts lines on a Windows console; `wsl_view.h`,
which core owns, reads schema 1 of the collector's line.

## What belongs here

- `render.h`, `render.cpp`: layout, colour tones, number formatting, UTF-8 width, sanitising names,
  and `history::push`.
- `history.h`: the last 15 minutes of screen values, in memory, in one flat ring of 901 samples.
- `console.h`, `console.cpp`: VT mode, alternate screen, Ctrl+C, key polling from `CONIN$` with echo
  off, plain text when redirected.
- `render_check.cpp`: the renderer's checks, built on hand-made snapshots.

## What does not belong here

- Reading a counter, a file or a process. The screen reads the snapshot and its own history of earlier
  snapshots, and nothing else.
- Deriving a number that is not in the snapshot. The history holds only earlier snapshots' values and
  is never written to disk. Summing engines, adding Windows to WSL, or guessing
  a per-process GPU share are all forbidden.
- Changing what the collector sends. Request it from core.
- The strict parser for the collector's line, `src/core/wsl_view.h` and `.cpp`, which `src/analysis` reads too.

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
7. Thresholds for amber and red are judgement calls and live in one block at the top of `render.cpp`,
   except `kStaleS`, in `snapshot.h` because `lost` stops on it too.
8. Graphs use a fixed 0-100 scale with time on the x axis. A missing sample or a stall is a blank gap,
   never a zero. Panels are never stacked or summed, and each graph title names its source.

## How to validate

- `scripts/check.sh --windows` builds `render_check.exe` and runs it. Zero failures is the bar.
- `render_check busy 131 40` or `render_check bare 80 24` prints a frame to inspect by eye;
  `render_check busy W H <overall|gpu|cpu>` prints a panel, and `render_check busy live` runs the
  panels with live keys.
- `render_check busy` draws on the live console until Ctrl+C: colours, braille and Korean text need a
  person's eyes in Windows Terminal. Legacy conhost is unchecked.
- `render_check <contended|idle|stale|noisy> W H [span_s]` prints the large layout of panel 3,
  `render_check vt <fixture> W H FILE [span_s]` writes one frame's VT bytes to a file, `render_check measure`
  prints frame bytes and build time, and `render_check quick` skips the size sweeps, for mutating the renderer.

## Current state (2026-11-02)

| item | value |
|---|---|
| source | `src/ui` 5236 lines, of which `render_check.cpp` 2801 |
| checks | 120083, 0 failed, run on Windows 2026-11-02. 97 mutants of the ring, buckets, colours and large layout were built and run through `render_check quick`, on Linux with g++; all are caught now, 13 only after a check was added for them. It was not exhaustive. `handle_keys` in `main.cpp` has no automated check |
| sizes checked | widths 1 to 300, heights 1 to 66, all three panels: hostile numbers at every size, the other cases at every third size including 300 and 66. Large layout: widths 100 to 300 every 7, heights 17 to 66, four fixtures, spans 60 s and 15 min; 4, 8, 14, 20, 21, 40 and 64 threads, normal and hostile, at five widths |
| frame bytes, 131 by 40 | noisy 33392, contended 16176, idle 14107, stale 15232; noisy at 40 threads 44542 and at 64 threads 43388. Budget 48 KiB |
| frame build, `render()` and `vt_frame()`, MSVC `/O2` | 0.28 to 0.64 ms, worst noisy at 131 by 40. Budget 5 ms |
| builds from recorded JSONL | no. TARGET in `ARCHITECTURE.md`; snapshots are hand-built |
| console | Windows Terminal checked by eye once; conhost not checked. The large layout has not been seen on a terminal: its colours are unchecked, and `t` is not wired in `main.cpp` |

## Open questions

- PROPOSED (`ROADMAP.md`): GPU holders first in the Windows list, an `unattributed` line between the
  device total and the split, about 10 Windows rows, idle virtual NICs hidden.
- PROPOSED: `--replay FILE` through the same `render()`, which would also give the recorded fixtures
  the target asks for.
- Does the screen need the clock offset? `wsl_clock_known` and `wsl_clock_offset_s` are in the
  snapshot and the record but not drawn.
- `docs/design/cpu-panel.md` section 7 has `64 of 128` for a machine past one processor group. Nothing in the
  snapshot holds a machine-wide count and the ledger has no row for one, so the heads say `of 64` and no more.
  Needs a snapshot field and a ledger row, or that row of the design changed.
- The lost state `vcpus_differ` was not committed when the large layout was, so `lost CPU n/a, vCPUs differ`
  is chosen by comparing the two thread counts. Switch to the state once `snapshot.h` has it on the branch.
- A collector that restarted while its line still reads is not named in the large layout: `collector_status`
  has no row there. The overall panel keeps it.
- `vm_row` on the overall screen prints `vmmem_cores` without reading `vmmem_cores_known`, so a new VM's first
  sample reads `0.00 cores`. The large layout does not. Not changed here: outside this work, and it changes a checked screen.
- `read_wsl` still runs twice a sample and allocates each time, in `history::push` and in `render`. The ring
  itself allocates nothing after the first push.

## Decision log

- 2026-11-02. The large layout of panel 3 takes its row order from `docs/design/cpu-panel.md`, with one reading:
  the heat block gets up to 10 rows at step 2, and more only after the lists have 6, so `6 rows guaranteed before
  the graph grows` holds for 40 and 64 threads too.
- 2026-11-02. The `lost` lane has three looks: blank, quiet under 1 vCPU, amber from 1 vCPU. The percent of guest
  threads the design names has no visible effect once 1 vCPU turns it amber, so it is not stored.
- 2026-11-02. A row's pixels: past the natural rows, threads are shared evenly over the block's lines of pixels
  and each pixel shows the highest thread's mean, so a row of a 64-thread machine is still two lines.
- 2026-11-02. `highest of each row` outranks the count in a caption, because it says what a cell is.
- 2026-11-02. `render()` takes `render_options`: a span and whether the caller acts on `t`. The footer offers `[t]`
  only when both the option and the layout say so, so no dead key ships before `main.cpp` handles it.

- 2026-10-31. `render()` does no I/O and takes the snapshot only, so it can be checked anywhere and a
  GUI can later draw from the same data.
  Superseded 2026-11-01: it also takes the optional history, and still does no I/O.
- 2026-10-31. The parser is hand-written and strict (depth 16, `from_chars`, surrogate handling)
  rather than a library, to keep the build at one `cl` call with no dependencies.
- 2026-10-31. `Local Usage` is the per-process GPU memory column, labelled `LOCAL` (core decision log).
- 2026-11-01. The `no GPU engine data` branch is reachable: core sets `gpu_engines_known` and the
  split is invalid without it (`TODO.md` item 5). No screen change was needed.
- 2026-10-31. The refresh is held at 1 s with a fixed deadline; the console survives a rebuild
  because `build.sh` renames the running exe.
- 2026-11-01. The user chose panels switched by key (`1` overall, `2` GPU, `3` CPU, `q` quit) with
  btop-sized graphs of the last minute, over graphs taking rows from one screen. The overall panel
  is the old screen plus a key footer. `--text` passes no history, so it is byte-identical, except a
  failed CPU read (below).
- 2026-11-01. Panels keep at least 36 columns: three to a row from 112 columns, two from 74. On a short
  window the lists go first, then the device line, then a panel's plot, keeping its value and label;
  the footer says when something is hidden, and a cut holder list counts itself (`top N of M`).
  `dxg_denied` has a reserved row whenever the holders block is drawn.
- 2026-11-01. Samples more than 1.5 s apart are a gap. Where several samples share a column the highest
  is drawn, and a measured 0 draws its bottom dot, so it differs from a gap. A WSL line 5 s or older
  and an empty `core_pct` are gaps, and so is a repeated line, which also happens with a healthy
  collector when the two 1 Hz loops slip, and blanks about 2 s. A WSL value is plotted at the
  Windows sample time. A backward clock step shifts the stored samples using `interval_s`, and clears
  them at a stall of 60 s or more; a forward step reads as a stall.
- 2026-11-01. Keys are decided on the virtual key code, so Delete does nothing and `q` should work
  under Hangul input. Not tried with a real IME: if it delivers `VK_PROCESSKEY`, `q` does not quit in
  that mode. Ctrl+C always does.
- 2026-11-01. A failed CPU read prints `n/a` on the overall screen too (rule 2). This is the one case
  where the frame without a history differs from before.
- 2026-11-01. The size sweep covers every third size for four cases, because all sizes for all cases
  took about 2 minutes on Windows; layout depends on size and number width, not on the history's shape.
- 2026-11-01. The `dxg_denied` warning has two wordings, chosen by the room it has, because no single
  honest string fits every width. It is whole in both panels from 37 columns up, and neither wording says
  a root process holds the GPU.
- 2026-11-01. `/dev/dxg` holders read `dxg` and `holder` on screen, because `on GPU` read like usage; the
  `on_gpu` field stays in the data. The WSL list head has three wordings chosen by room, and the distro
  gives way before the label. The overall row carries the label even when WSL is unavailable, in a short form
  when narrow.
- 2026-11-01. `wsl_view` moved to `src/core/`, so `src/analysis/lost.cpp` can read the guest line, and
  `kStaleS` and `kGapMs` moved to `snapshot.h`, with the line-repeat test to `wsl_line_repeats`, so the screen
  and `lost` stop trusting a sample at the same point. Nothing drawn changed (core decision log).
- 2026-11-01. `console::write_failed()` is set when a write fails in the plain branch, including with no
  stdout at all, and never for a console. A reader that stays open but stops reading still blocks the
  write; that is not covered.
