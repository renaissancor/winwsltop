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
8. Graphs use a fixed 0-100 scale with time on the x axis. A measured 0 is the bottom dot row (⣀), a
   missing sample or a stall is a blank gap, never a zero, and time from before watching began is a
   faint `·` in every other cell of the bottom row. Panels are never stacked or summed, and each graph
   title names its source.
   Exception, the large CPU panel only: a repeated guest line carries the previous value for one sample,
   and two in a row are a gap (design section 5). Heat and lanes there show a mean, the plots a peak.
   There the `·` is placed by bucket, on the bottom row of the busy and RAM plots; the heat block and the
   lanes have no bottom dot row, so they stay blank before watching began.

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
| source | `src/ui` 5719 lines, of which `render_check.cpp` 3215 |
| checks | 120649, 0 failed, run on Windows 2026-11-02 after merging main's graph rule (`scripts/check.sh --windows`, which also ran `lost_check`, 143, 0 failed). Rerun on Linux with g++ as `render_check quick`: 5293, 0 failed; 11 mutants of the merged rule (bucket placement, the `·` in the plots, the stall at `kKeepMs`, `kStepMs`, when watching began) were built and run that way, and all are caught. The 97 mutants of the ring, buckets, colours and large layout from before the merge were not rerun, and main's own mutant list was not kept. `handle_keys` in `main.cpp` has no automated check |
| sizes checked | widths 1 to 300, heights 1 to 66, all three panels: hostile numbers at every size, the other cases at every third size including 300 and 66. Large layout: widths 100 to 300 every 7, heights 17 to 66, four fixtures, spans 60 s and 15 min; 4, 8, 14, 20, 21, 40 and 64 threads, normal and hostile, at five widths. The graph rule: 60 s panels at 131 by 40, 80 by 24 and 200 by 60; the large layout at 100 by 34, 131 by 40 and 200 by 60, both spans |
| frame bytes, 131 by 40 | noisy 33392, contended 16176, idle 14107, stale 15232; noisy at 40 threads 44542 and at 64 threads 43388, rerun 2026-11-02 after the merge and unchanged. Budget 48 KiB |
| frame build, `render()` and `vt_frame()`, MSVC `/O2` | 0.32 to 0.69 ms, worst noisy at 131 by 40, rerun 2026-11-02 after the merge. Budget 5 ms |
| builds from recorded JSONL | no. TARGET in `ARCHITECTURE.md`; snapshots are hand-built |
| console | Windows Terminal checked by eye once for the older panels; conhost not checked. The large layout and `t` ran live through a pseudo-terminal, and its colours were read from rendered images, not seen in Windows Terminal |

## Open questions

- A step back of `kStepMs` (1 s) or less during a stall, with the new sample still after the newest one, is
  not corrected: those samples sit up to a second too close to now. Below that it cannot be told from jitter.
- Closes with slice 1's commit 2, which removes the caption (decision of 2026-11-02, slice 1). The large layout's axis says `collecting: N s of M s` from the oldest kept sample, not from when watching
  began. After a stall that clears the ring it says collecting while the cells are a gap, not dotted. Whether
  it should count from `watched_from_ms()` instead is open.
- PROPOSED (`ROADMAP.md`): GPU holders first in the Windows list, an `unattributed` line between the
  device total and the split, about 10 Windows rows, idle virtual NICs hidden.
- PROPOSED: `--replay FILE` through the same `render()`, which would also give the recorded fixtures
  the target asks for.
- Does the screen need the clock offset? `wsl_clock_known` and `wsl_clock_offset_s` are in the
  snapshot and the record but not drawn.
- `docs/design/cpu-panel.md` section 7 has `64 of 128` for a machine past one processor group. Nothing in the
  snapshot holds a machine-wide count and the ledger has no row for one, so the heads say `of 64` and no more.
  Needs a snapshot field and a ledger row, or that row of the design changed.
- A collector that restarted while its line still reads is not named in the large layout: `collector_status`
  has no row there. The overall panel keeps it.
- `read_wsl` still runs three times a sample and allocates each time, in `lost_tracker`, `history::push` and `render`. The ring
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
- 2026-11-02. Graphs follow the rule shared with sshvllmtop and machtop: ⣀ measured 0, blank gap, faint ·
  every other cell before watching began. The `·` is on the bottom row only, in blank cells, counted from
  the graph's left edge. `history` keeps when watching began apart from its samples, so the 600-sample cap
  does not move it. A backward clock step moves it with the samples. A stall of the span or longer,
  whichever way the wall clock moved, clears the samples and makes the whole window a gap, not unwatched.
  Before anything is pushed the graph is all unwatched; a pushed sample whose time cannot be placed counts
  as watching, so the span before the first placed sample is a gap. Audited 2026-11-02 (verdict ship);
  its findings D1-D3 fixed the same day.
- 2026-11-02. Merging main's graph rule into the ring. `history` keeps 15 minutes and is told the span only
  when drawing, so main's stall at `kSpanMs` became one at `kKeepMs`: a stall of 900 s or more, either way the
  wall clock moved, clears the ring and puts when watching began 900 s before the sample, so every span is a
  gap. Clearing at 60 s would have thrown away 14 minutes the 15-minute span still draws correctly. To keep
  main's guarantee that a 120 s stall reads as a gap on the 60 s span, the stored samples and when watching
  began now shift whenever the wall clock moved more than `kStepMs` (1 s, in `history.h`) less than
  `interval_s` says, not only when it went behind the newest sample. That closes main's open question on a
  partial step back during a stall. On the large layout the unwatched cells are the buckets before the one
  watching began in, `floor(t / bucket)` as `make_axis` places a sample; the 60 s panels keep main's placement,
  as `plot_columns` places one. `render_check` reads both gutters, `  0│` and `  0 │`.
- 2026-11-02, the owner, recorded by core. The shared panel grammar from machtop (`docs/panels.md`, "Shared
  grammar" and "Decisions") overrules winwsltop's existing panel design. It supersedes three refusals in
  `docs/design/cpu-panel.md`: list focus and the sort key (section 6), the `[s]` sort (section 4, "Refused, and
  why") and per-process history (section 11). The range on `←`/`→` is 1, 5, 10 minutes, replacing `t`'s 60 s,
  5 min, 15 min. Keys follow machtop and sshvllmtop. `tab` moves between a panel's selectable lists as machtop
  decided 2026-11-02, the details in `docs/panels.md`. Nothing is built yet; rule 3 stands, since `tab` moves
  between the lists and never merges them. Graph labels of average and maximum would change rule 8 and are
  not yet adopted here.
- 2026-11-02, the owner, recorded by core. Slice 1 of the shared grammar, `docs/design/panel-grammar.md`, is
  approved to build after experiment 0. Its decisions, each built by the commit of that design's section 7 named:
  - Keys (commits 3 to 5): `1`-`8`, `↑`/`↓`, `←`/`→` over 1, 5, 10 minutes, one range for the screen, `tab`,
    `esc` to panel 1 keeping every panel's focus and selections, `q`; `t` goes. Keys are decided by a pure
    `key_for` in a header-only `src/ui/keys.h` and applied by a pure `apply_key`, both checked by `render_check`.
    The footer lists the eight panels, current lit, built `dim`, not built `faint`, and offers only keys that act.
    This supersedes the 2026-11-01 entry on panels `1`, `2`, `3` and `q`, and the 2026-11-02 entry on
    `render_options` and `[t]`.
  - Selection and scrolling (commit 6), as machtop's "Selection": a process is remembered by pid and name and
    falls to the first row when it leaves the list; `↑`/`↓` stop at the ends and do not wrap; a list longer
    than its space follows the selection, which stays on the last row shown once it passes the bottom, with no
    paging and no stored offset; each list ends with a `dim` `N of M` line, and the list heading drops
    `top N of M`. Every list scrolls by its own selection.
  - One history, full width, below the lists, following focus and titled with its list, never one per list,
    so rule 3 stands. Panel 3 at 34 rows shows 5 list rows, the owner's choice over a 2-row busy plot; a list
    keeps at least 3 rows and the history gives way first. Panel 1 gets no selection; panel 2 waits for slice 2.
  - The per-process ring (commit 6): 601 samples of cores and memory per process, keyed by side, pid and
    start time; a first reading stored as a gap; every slot written every push; at the cap of 1024 slots, or
    twice the processes in the newest sample, the slot least recently seen is reused, never a selected one,
    and nothing is refused. `kKeepMs` becomes 600000 and the ring 601 samples.
  - Graphs (commit 2): a cell is the mean of its samples, replacing the highest sample of the 60 s panels
    (2026-11-01 entry on samples sharing a column) and the large layout's peak; the cell holding the window's
    maximum carries one extra dot; every graph is labelled `avg` and `max` over the window, never the minimum;
    scale by measure, 0-100 for measures with a ceiling, else `scale` to a rounded maximum with a floor or a
    zoomed `range`; an over-100 split value is printed with `*` beside the caption `VM level, not exact
    shares*`. The captions `peak over mean` and `collecting: N s of M s` go, which closes the open question on
    the latter. Rule 8 takes the design's proposed text (part 5) in that commit.
  - Panel 4 Memory (commit 7) as in the design's part 4, and RAM leaves panel 3.
  - Figures the screen already shows without a ledger row are shown marked `?`, with `? not yet checked` on the
    panel's rule line, the owner's choice over hiding them; a figure new to the screen with no row is not
    shown. Experiment 0 (`contracts/evidence.md`) is to clear them.
  - The seam change of commit 1 is additive flags, `schema` stays 1 (`contracts/core.md`).
  Still open: Alt+key decoded as `esc` then the key for OpenSSH, so Alt+Q quits; and the live key checks the
  design lists in section 8.

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
  them at a stall of the keep span, 900 s since the large CPU panel; a forward step reads as a stall.
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
- 2026-11-01. The large CPU panel's heat block and thin lanes replace part of the btop-sized graphs
  decision above, on windows under about 40 rows: the user chose it, given the 100 by 34 allocation.
  Taller windows give the height back to the busy graph.
