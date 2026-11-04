# Contract: ui

Owner: the `ui` role. Paths: `src/ui/` (the `ui` row of [`../ARCHITECTURE.md`](../ARCHITECTURE.md)).

## Purpose

Turn snapshots into a terminal screen that never claims more than was measured. `render()` is a pure
function of one snapshot, an optional in-memory `history` and a `view_state`; with no history it is one snapshot
to lines. `keys.h` turns a key record into a key and `apply_key` applies it to the `view_state`, both pure;
`console.cpp` puts lines on a Windows console and reads key records; `wsl_view.h`, which core owns, reads schema 1
of the collector's line.

## What belongs here

- `render.h`, `render.cpp`: layout, colour tones, number formatting, UTF-8 width, sanitising names,
  and `history::push`.
- `history.h`: the last 10 minutes of screen values, in memory, in one flat ring of 601 samples, and beside it
  the per-process ring, cores and memory per process, keyed by side, pid, start and name.
- `keys.h`: `key_for`, header-only and pure, and the 2-set Korean table.
- `console.h`, `console.cpp`: VT mode, alternate screen, Ctrl+C, key polling from `CONIN$` with echo
  off, plain text when redirected.
- `render_check.cpp`: the renderer's checks, built on hand-made snapshots.

## What does not belong here

- Reading a counter, a file or a process. The screen reads the snapshot and its own history of earlier
  snapshots, and nothing else.
- Deriving a number that is not in the snapshot. The history holds only earlier snapshots' values and
  is never written to disk. Summing engines, adding Windows to WSL, or guessing
  a per-process GPU share are all forbidden. Rule 9's ratio is display, not derivation.
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
8. Graphs have time on the x axis, over 1, 5 or 10 minutes. A measure with a real ceiling (a percentage of
   one core, a device or the whole machine) is drawn 0 to 100; a process's cores from 0 to the window's
   maximum rounded up to 1, 2 or 5 times a power of ten, never below 0.05, printing `scale`. A process's memory
   history draws from 0 to its window's maximum rounded up the same way (never under 4 MiB, in MiB below 1 GiB)
   and prints `scale`. Panel 4's used and cache plots zoom and print `range`. History graphs have no gutter or
   time axis: `avg`, `max`, `scale` and `last N min` sit in the heading, figures dropped from the left when
   narrow, the range kept. Device, busy, heat and lane plots keep their gutter and axis. A history or busy plot
   has two time columns per braille cell, and every axis's cell is an even number of milliseconds, so a column
   is exactly half a cell. A column is the mean of the samples in its slice of the window, each clamped to the
   scale first. The column holding the window's maximum, the latest if several do, also marks it with one dot at
   its height, when that column holds more than one sample and the dot falls above the column's mean. A value
   above a 0-100 graph's ceiling is drawn at the ceiling and labelled as measured, in the watch tone. A measured
   0 is the bottom dot row (⣀), a missing sample or a stall is a blank gap, never a zero, and time from before
   watching began is a faint `·` in every other cell of the bottom row, only where both of the cell's columns are
   unwatched. Each graph is labelled with the average and maximum of its raw samples over the window, never the
   minimum. Panels are never stacked or summed, and each graph title names its source.
   Exception, wherever a guest figure is kept over time from the large layout's rule: the large CPU panel,
   panel 4's guest used and cache, and the per-process histories of panels 3 and 4. There a repeated guest
   line carries the previous value for one sample, and two in a row are a gap (`design/cpu-panel.md` section
   5). On the large CPU panel the `·` is placed by bucket, on the bottom row of the busy plots; the heat block
   and the lanes have no bottom dot row, so they stay blank before watching began.
   A Windows process's history on panels 3 and 4 is two layers on one scale from 0: all Windows processes'
   sum (`process_totals`, the track tone) and the selected process's part of it (the bar tone), a braille cell
   one colour, with a dim legend naming both. It draws as one series, no grey, no legend, no `now`, when the
   whole is unknown, the list has one row or the part equals the whole at every sample on screen. A guest
   process's history is always one series: the guest has no whole (ledger, "Guest process walk checked"). Panel 2's
   GPU history, busiest engine, is always one series, 0 to 100: it does not add across processes.
   (Slice 2 steps C, F1 and G, `design/panel-grammar-slice2.md` sections 3 to 6; decisions of 2026-11-04.)
9. A ratio of two values already in the snapshot or the history, for the selected row, is display, not
   derivation: the selected process's value over its side's whole at the latest sample where both are known,
   `now N% of all`, on the part-of-whole history. Both values come from one side and one reading; nothing else
   the snapshot does not hold is worked out here (user's decision, 2026-11-04, `design/panel-grammar-slice2.md`
   Q7).

## How to validate

- `scripts/check.sh --windows` builds `render_check.exe` and runs it. Zero failures is the bar.
- `render_check busy 131 40` or `render_check bare 80 24` prints a frame to inspect by eye;
  `render_check busy W H <overall|gpu|cpu|1-8>` prints a panel, and `render_check busy live` runs the
  panels with live keys.
- `render_check busy` draws on the live console until Ctrl+C: colours, braille and Korean text need a
  person's eyes in Windows Terminal. Legacy conhost is unchecked.
- `render_check <contended|idle|stale|noisy> W H [span_s]` prints the large layout of panel 3,
  `render_check vt <fixture> W H FILE [span_s]` writes one frame's VT bytes to a file, `render_check measure`
  prints frame bytes and build time, and `render_check quick` skips the size sweeps, for mutating the renderer.

## Current state (2026-11-05, slice 2 built, after its audit)

| item | value |
|---|---|
| source | `src/ui` 9094 lines, of which `render.cpp` 3456, `render_check.cpp` 5126, `history.h` 175, `keys.h` 89 |
| checks | `render_check` 187351, 0 failed, and `lost_check` 154, 0 failed, under `scripts/check.sh --windows` after 64b395b. `apply_key` and `key_for` are checked there; `main.cpp`'s polling and redraw are not |
| sizes checked | widths 1 to 300, heights 1 to 66, every panel, hostile numbers at every size and the other cases at every third. Large layout: widths 100 to 300 every 7, heights 17 to 66, four fixtures, ranges 1 and 10 minutes; 4, 8, 14, 20, 21, 40, 64 and 65 threads |
| frame bytes, 131 by 40 | noisy 32842 (23764 at 100 by 34), noisy at 64 threads 38789 and at 40 threads 39905, panel 2 13004, panel 4 11754 (8363 at 100 by 34), contended 15471 (11827 at 100 by 34). Budget 48 KiB |
| frame build, `render()` and `vt_frame()`, MSVC `/O2` | 0.314 to 0.706 ms over the fixtures, ranges 1 and 10 minutes, panels 2 and 4, across four runs (core's and the re-audit's three); every figure varies run to run. Budget 5 ms |
| history push, MSVC | about 0.03 ms with 390 processes (0.031 and 0.032 in two runs), the per-process ring included. 2000 processes with 50 new a second, `selected_processes` included, across four runs (core's of 2026-11-05 and the re-audit's three): 0.159 to 0.228 ms mean, 0.315 to 0.754 ms worst, and the first push at 2000, once, 2.881 to 4.402 ms. Every figure varies run to run. `render_check measure` |
| per-process slots | never shrink: a spike to 2000 processes leaves about 4100 slots, about 20 MB, until exit |
| history | 10 minutes in 601 samples, five memory floats a sample for panel 4, the Windows whole and whether GPU engines were known; the per-process ring keyed by side, pid, start and name, a GPU series and last GPU use only for processes that used an engine, a first reading stored as a gap, every slot written every push, and past max(1024, twice the rows of the newest sample) the slot least recently seen reused, never a selected one; nothing is refused |
| builds from recorded JSONL | no. TARGET in `ARCHITECTURE.md`; snapshots are hand-built |
| console | Windows Terminal checked by eye once for the older panels; conhost not checked. The live key checks of `design/panel-grammar.md` section 8 (Korean IME, ConPTY and OpenSSH, numpad with NumLock off) are not run |

## Open questions

- A step back of `kStepMs` (1 s) or less during a stall, with the new sample still after the newest one, is
  not corrected: those samples sit up to a second too close to now. Below that it cannot be told from jitter.
- PROPOSED (`ROADMAP.md`): Windows GPU holders are listed on panel 2 since slice 2, after the processes with
  engine use; still proposed: an `unattributed` line between the
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
- `read_wsl` runs about 7 times a sample on panels 3 and 4, and allocates each time: once each in `lost_tracker`,
  `history::push` and `render`, and in `selectable_lists` every time it is called, which is through `selection_of`
  for each panel body and footer that draws a selection, and from `apply_key` on `tab` and the arrows. Parse the line once a sample and hand the
  view to each reader? Not urgent at the measured frame times; left for later. The ring itself allocates nothing after
  its slots are made.

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
  Superseded in part by slice 1 (2026-11-02): `history` keeps 10 minutes in 601 samples, so the clearing stall is
  600 s, not 900 s, and there is no 15-minute span.
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
  Alt+key decodes as `esc` then the key, so Alt+Q quits, under the owner's decision that keys follow the shared
  grammar: the source is machtop's key parser (`src/keys.cpp` at 246c2cb, `key_parser::next`: an ESC followed by any byte but `[` or `O` is `esc` on its own, so Alt+key, which a terminal sends as ESC then the key, reads as `esc` then the key); the grammar's text does not state it yet. Still open: the live key checks the design lists in section 8.
- 2026-11-02. Slice 1 as built, where it departs from `docs/design/panel-grammar.md`, commits 2 to 4 (dc33e18,
  345e358, 8f9d7d8, 64019d6, fixed by ee42854). `↑`/`↓` and `tab` are offered, and act, only on a panel that draws a
  selection, so they came with panel 3 and panel 4, not before. Key `4` opened a not-built page until panel 4 landed
  (01384a4). `apply_key` takes the view state, the key and the snapshot, not the history, width or height.
  Alt with an arrow is the arrow: Alt+key reads as `esc` then the key only for a key that types a character, and
  Alt with a numpad digit is Windows' Alt code entry and nothing. Each sample
  is clamped to the graph's scale before a cell's mean, so one sample over 100 cannot lift its neighbours, while
  `avg` and `max` are of the raw samples. `each cell N s` has one decimal below 10 s.
- 2026-11-02. Slice 1 as built, commit 6 (ac703b5, 9af1ac4). `--text` shows the `?` marks, `stat rss` and `n/a`
  for an unrated process's cores too, since it is the same frame. Panel 1 has no rule line of its own, so
  `? not yet checked` sits on its title row. The over-100 `*` on a label and on the caption `VM level, not exact
  shares` are drawn together or not at all. At 100 columns guest names on panel 3 are cut to 5 columns. A narrow
  head with WSL down still reports the collector's restarts. The large layout's floor is 29 rows at 20 threads,
  two above `design/cpu-panel.md`'s 27, for the `N of M` line and the history. A process whose first reading has
  no earlier one to rate against reads `n/a` cores, never 0.
- 2026-11-02. Slice 1 as built, commit 7 (01384a4), the zoom rule the design's mockups lacked: spread is
  max(max - min, floor), bottom max(0, min - 0.1 spread), top bottom + 1.2 spread, as machtop's `sample_series` at
  246c2cb. Floors: 64 MiB for whole-machine memory, 4 MiB per process. Labels are GiB or MiB with the fewest decimals
  that still tell bottom from top. Below 80 columns panel 4 stacks its two sides. The design's check "never a line
  with a Windows and a guest GiB figure" holds as "guest figures only right of the divider", since the stacked and
  side-by-side layouts share rows differently.
- 2026-11-02. Slice 1 as built, the final audit's fixes (679fc5f).
  - `selected_processes` returns the keys remembered, pid, name and the start last seen, not the rows they resolve to,
    so a selected process that has left the list is still named and its slot kept. A key with no start is matched by
    pid and name. A list never moved in names nothing: its first row is in the sample.
  - Eviction. `push` marks the whole current sample seen before placing any row, so a new process never takes the slot
    of one in the same sample. Slots let go go on a free list, used first. At the cap it gathers, once a push, the slots
    that are used, not in the sample and not kept, least recently seen first. A backward clock step moves each process's
    last-seen time with its samples, so the slots do not grow. Slots never shrink.
  - The zoom label (supersedes the label part of the commit 7 entry above). A printed range spans at least 4 units of
    its last printed digit: GiB with 1 to 3 decimals while the top is at least 1 GiB, else MiB with 0 to 3. The bottom
    is rounded down and the top up, so every sample drawn lies inside the printed range. `avg` and `max` take one
    decimal fewer than the range, never under one. A gutter prints the two ends only if both fit 4 columns whole, else
    it is blank. The cores history's gutter prints its floor as `0.05` whole.
  - `?` marks added: the guest's `load?`, `steal?` and process count (`guest count? 64`, `top 8 of 64?`), on panels 1,
    3 and 4, and panel 3's rule line says `? not yet checked` whenever the guest's line is read.
- 2026-11-04. Rule 8 as slice 2 steps B and C built it (9b6b174, 002c084), in the text the ui agent proposed, adopted
  by core with its wording adjusted. It supersedes the slice 1 text's zoomed memory history and its doubled cells.
  Two departures from `design/panel-grammar-slice2.md`, accepted:
  - The newest cell, whose second time column has not started yet, is drawn from its first column, so the right edge
    is not half empty for most of each bucket.
  - A memory scale below 1 GiB is rounded up in MiB, 1, 2 or 5 times a power of ten, not as a fraction of a GiB.
- 2026-11-04, the user. Rule 9: a ratio of two values already in the snapshot or the history, for the selected row,
  is display, not derivation, so the part-of-whole history shows `now N% of all` (slice 2 step F1). It answers Q7 of
  `design/panel-grammar-slice2.md`, raised by the audit: the Windows whole had moved into the snapshot because the screen
  may not derive, and the share, which depends on the selection, cannot.
- 2026-11-04. Slice 2 as built, steps F1 (a4ae1e5) and G (4f9e546), where the design left the detail to ui.
  - The heading's figures drop from the left when narrow; then `now` goes before the whole's name, because with no
    legend the name is the only thing naming the grey. The range stays last.
  - On the large CPU layout the history gets its legend row once the lists reach 6 rows, so at 131 by 40 the busy
    plot has 7 rows, not 8.
  - Panel 2's row budget: the history takes rows only while the Windows list keeps 3; it gets
    clamp((room - 2) / 2, 2, 6) plot rows, and the lists' rule line comes after it.
  - Panel 2's rows: an engine row whose pid is in neither `processes` nor the ring is not listed. A process that has
    exited reads `n/a` for GPU %. A reused pid's old process stays listed, and keeps the selection, while it used an
    engine inside the range; otherwise the selection falls to the first row.
  - A WSL history, and a Windows history drawn as one series, do not show `Σ working sets`: there is no whole drawn
    to name.
  - Panel 2's WSL `/dev/dxg` holder list is a reference list, not selectable (machtop, 2026-11-04), so panel 2 has
    one selectable list and no `tab`.
- 2026-11-05. The slice 2 audit's fixes, as built.
  - Finding 10 (d0f0b97): a part-of-whole heading with no legend keeps the grey named. Short of room it shortens the
    whole's name to `all Windows` (`Σ all Windows` on panel 4), then drops `scale`, then `last`, so the name survives
    while the short form fits beside the title. With a legend, which names the grey, the scale-only fallback stays.
  - Finding 3 (af6d422): panel 2's history reserves and draws its rows only while the split `gpu_lists` makes
    (`gpu_windows_rows`) leaves the Windows list `kLeastListRows`, or every row it has; stacked under 64 columns with two
    or more holders it no longer leaves the Windows list 2 rows.
  - Finding 6 (1a7faf3): a stall of what is kept clamps every slot's times: `seen_ms` to at most `t - kKeepMs`, and
    `gpu_seen_ms` cleared, so a clock step back during a stall cannot bring a process gone before it back into panel 2's
    list.
  - Finding 5 (2c2ee2b, and core's 64b395b): panel 2 counts a GPU use from the history's first column, the axis's left
    edge at the history's width, not the start of the range; `apply_key` takes the same width, which `main.cpp` passes.
  - Finding 4 (87b1169): a share of exactly 0 prints `0%`; `<1%` only above 0 and under half a percent, as machtop's.

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
  Superseded in part: the keep span is 600 s since slice 1, and a cell is the mean of its samples, not the highest.
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
