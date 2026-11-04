# Panel grammar, slice 2

Status: built, 2026-11-04, on `cpu-panel`: steps A, B and C (422f050, 9b6b174, 002c084), E1 (8881344, 9494198),
D (afa7b2d), F1 (a4ae1e5), G (4f9e546 and core's d9a699f) and H (c4de089, f09e170 and the commit that marks it built). D's verdict is No (section 2),
so steps E2 and F2 are dropped and the guest's histories stay one series. Where the build departs from this file,
`contracts/ui.md`'s decision log says so and rules. Step I, the fresh audit, is still to come. The user approved the
scope in section 0.
machtop answered section 10's questions the same day; machtop leads on panel design, so they are decided and
the sections below follow them.
Slice 1 is [`panel-grammar.md`](panel-grammar.md), built; where this file changes something there, this file
says so. The spec is machtop's `docs/panels.md`, section "Shared grammar" (machtop dc3a241, outside this
repository), which leads on panel design; machtop's gap list for winwsltop, `docs/winwsltop-gaps.md` there,
read `cpu-panel` at 8cbf51e and is cited below as "gap N". The drawing machtop already has is in its
`src/render.cpp` at dc3a241: `sample_part`, `part_equals_whole`, `draw_part_of_whole`, `share_text`, and
the history heading in its app panels. Ledger rows are cited by claim from [`../measurable.md`](../measurable.md).

## 0. The slice

| # | item, as approved | gap |
|---|---|---|
| 1 | Selection identity is pid plus start time, falling back to pid plus name when start is 0 | 1 |
| 2 | A whole for WSL: `cores_total` and `rss_total` over every guest process, in the protocol, after a ledger row. Until it lands, WSL histories draw as one series | 3 |
| 3 | Part-of-whole on the panel 3 and panel 4 process histories, one whole per side, never one across sides | 2, 4 |
| 4 | `plot_block`: two time columns per braille cell | 5 |
| 5 | Panel 2's list selectable, with a history below; GPU % is busiest-engine and always one series | 6 |
| 6 | The range in the history heading; the graph gutter and axis | 8, 10 |
| 7 | A fresh auditor checks the result | |

Out of scope: panels 5 to 8 (gap 7), the overview's history and top consumer (gap 9), app grouping (gap 11),
`s` sorting, and ROADMAP 1's `unattributed` line. ROADMAP 1's Windows holders come in through Q4: panel 2's
Windows list holds every process with GPU `local` memory (section 5).

## 1. Selection identity

**Owner:** ui, the `ui` agent.

**Today.** `selected_row` in `src/ui/render.cpp` matches `r.pid == k.pid && r.name == k.name`. `row_key.start`
(`src/ui/render.h`) is stored "not what selects it". The per-process ring keys by side, pid and start and checks
the name (`history::slot_of`, `take_slot`, `kept_id`), so a reused pid under the same name, which `svchost.exe`
makes likely on Windows, keeps the old selection but gets a new history slot: selection and history disagree.

**Change.** A row matches its key when the pids are equal, the names are equal, and, where both starts are
non-zero, the starts are equal. Where either start is 0 (a Windows row with no create time, a guest row from a
collector without `start`, PROTOCOL.md `processes`), pid and name decide, as today.

**Why the name stays.** The approved rule is pid plus start. The name is kept beside it as a backstop, because
the ledger has not seen a pid reused on either side: "Windows create time differs after a pid is reused" and
"Guest `starttime` differs after a pid is reused" both read `Not checked` (section "Memory and process
collectors checked"). Slice 1 kept the name for the same reason (`panel-grammar.md` part 3, "Identity"). The
rule is strictly tighter than today's, so it can be built now. machtop agreed, 2026-11-04 (section 10, b).
**The name goes once the ledger has rows on pid reuse** (Q1, decided): identity is then pid and start alone,
as machtop's, so a guest process that `exec`s and changes its `comm` keeps its selection, and the ring's
`slot_of`, `take_slot` and `kept_id` drop their name check in the same commit so history and selection still
agree. That is a later ui commit, not part of this slice's steps.

**Files and functions.** `src/ui/render.cpp` `selected_row`; the comment on `row_key::start` in
`src/ui/render.h`. `history::slot_of`, `take_slot` and `kept_id` already compare start and name, so they do
not change.

**Protocol.** None. **Ledger.** Cites "Windows create time is constant while a process lives" and "Guest
`starttime` is constant while a process lives" (Observed). Requests to evidence the two reuse rows (section 9).

**render_check.**
- Panel 3, Windows list: select `svchost.exe` pid 10000 with create time A; the next snapshot has pid 10000,
  `svchost.exe`, create time B. The selection falls to the first row, and the history title does not name 10000.
- The same with B equal to A: the selection stays.
- Start 0 on either side (a guest row with no `start`, a Windows row with create time 0): pid and name decide.
- After `↓` moves to a row, `row_key.start` is that row's start, and `selected_processes` passes it on.

## 2. A whole for WSL

**Dropped, 2026-11-04, on experiment 07's verdict.** The walk reads every process in this distro's PID namespace
and nothing outside it: it misses most thread groups in the VM and all of WSLg, and the CPU summed over it falls
short of `/proc/stat` busy, far short when processes exit between walks (ledger, "Guest process walk checked",
"The collector's walk reaches every process in the WSL VM", "WSLg's processes are in the walk" and "Process cores
summed over the walk are the guest's CPU", all **No**; one run). A sum over the walk is not the guest's whole, so
`cores_total` and `rss_total` are not added, steps E2 and F2 are dropped, and the guest's process histories on
panels 3 and 4 stay one series, which step F1 draws. The protocol and `wsl_view` do not change. If a guest whole
ever returns, its label is `processes in Ubuntu (this distro's namespace)`, the distro named as `wsl_view.distro`
gives it; whether the root cgroup's `cpu.stat` could be the CPU whole is a future evidence question
(`contracts/evidence.md`, open questions), not part of this slice. The rest of this section is the design as it
stood before the verdict, kept for that case.

**Owner:** evidence first (the `evidence` agent), then collect and core in one commit (the `collect` agent for
`collector.py`, the `core` agent for the seam).

**Why.** The collector sends the top 30 rows by cores then RSS, plus every `/dev/dxg` holder (`collector.py`
`TOP = 30`; PROTOCOL.md `processes`). A sum of the rows received is not every process, so the guest's
selected row would be drawn against a whole that leaves processes out, and its share would be too high.

### The ledger row, first

The seam changes only with a ledger row (`contracts/core.md` rule 1). No row today says that the collector
reads every guest process: the `guest count?` mark on panel 3 exists for that reason (`panel-grammar.md`
part 1, "Guest process count", no row). Experiment 07, read only, no load and no GPU, run by evidence
with its own probe, not the collector:

| claim to record | how |
|---|---|
| The collector's walk of `/proc` reaches every guest process | Count the numeric entries of `/proc` beside the walk in the same second, over the run; note any entry the walk could not read and why. This also decides the `guest count?` mark |
| The gap between the sum over every process of `/proc/pid/stat` utime + stime, over the probe's own interval, and `/proc/stat` busy times vCPUs over the same interval | Expected below busy (a process that exits between readings, and time not charged to any process, are missing from the sum), as "Process cores account for Windows busy" found on Windows. Record the gap, not a verdict on it |
| The gap between the sum of `/proc/pid/stat` field 24 times the page size over every process and `/proc/meminfo` used | Expected to differ, since shared pages count once per process. Record the gap |
| What "every guest process" covers | The walk sees this distro's PID namespace only, not the whole VM's (audit finding 3). The experiment says what is outside it and proposes the label the heading uses for the WSL whole |
| Cost | The walk is already made each second; the probe times the sum alone |

Each row carries its raw file under `experiments/07-*/results/`. A `No` on the first stops item 2 and the guest
stays one series. The audit of f270ef8 dropped two earlier rows, that each process's figure is at or below the
sum: that holds by arithmetic and proves nothing, so the gaps against `/proc/stat` and `/proc/meminfo` replace
them (finding 11). That a part never exceeds its whole is asserted by `check_json.py` and `render_check` instead.

### The protocol change

After the row lands. Both keys are additive, so `schema` stays 1 (PROTOCOL.md: "a change of meaning raises
it, a new key does not"), as `rated` and `start` were in slice 1.

| key | unit | source |
|---|---|---|
| `cores_total` | cores: CPU-seconds per second, 3 decimals | the sum of utime + stime deltas over every process the collector read in this sample and the one before, over the collector's own interval, rounded once after summing (rounding is monotonic, so a row's rounded `cores` never exceeds it); a process on its first reading adds nothing, as its `cores` is an unmeasured 0 |
| `rss_total` | bytes | the sum of `/proc/pid/stat` field 24 times the page size over every process read in this sample |

Both sit at the top level beside `process_count`, which counts the same processes. `null` when the walk read
no process.

**An old collector.** A normal build can read a `collector.py` from disk beside the executable; a release
build never does (PROTOCOL.md). A collector without the keys leaves them absent, which `wsl_view` reads as
unknown, and the guest's history draws as one series, as it does until this lands. A new collector with an
old `winwsltop.exe` is read as before: a reader takes a key it does not know as nothing.

**Files and functions.**

| file | change | owner |
|---|---|---|
| `wsl-collector/collector.py` `main()` | sum over `rows` before `keep` is cut, and emit the two keys | collect |
| `src/wsl/collector_embed.cpp` | `python3 wsl-collector/embed.py`, same commit | collect |
| `wsl-collector/PROTOCOL.md` | two rows in the field table, beside `process_count` | core |
| `src/core/wsl_view.h`, `wsl_view.cpp` `read_wsl` | `std::optional<double> cores_total`, `std::optional<uint64_t> rss_total`, read by a new `total_or_null`, not `identity_or_null` (which drops a non-integer, and `cores_total` is one) and not `number` (which refuses the whole line on a value that is not a number). `total_or_null`: `null` leaves it empty; a finite number at or above 0 is kept (for `rss_total`, only a whole number up to 2^63); a negative number leaves it empty; any other JSON value, a string, a boolean, an array or an object, is consumed by `skip()` and leaves it empty. The line is read either way (audit finding 6) | core |
| `scripts/check_json.py` | `WSL_DATA` gains both keys (it is an exact set, so it must change in the same commit). Each is `null` or a number; when a number, `cores_total` is at or above every row's `cores` (rounding is monotonic, so exactly) and at or above the sum of the rows sent less N × 0.0005, N the rows sent, since each row is rounded to 3 decimals on its own; `rss_total` is at or above every row's `rss` and their sum, exactly, being whole bytes (audit finding 7) | core |
| `docs/json-output.md` | `wsl.data` names both | core |
| `docs/contracts/collect.md`, `core.md` | decision entries | core |

The record carries them with no change to `src/record/jsonl.cpp`: `wsl.data` is the collector's line as sent.

**Cost.** Two keys, about 40 bytes, against the 8 KiB line budget (`contracts/collect.md`, 2026-11-02). One
addition per process the walk already reads.

**render_check.** `read_wsl` on a line with both keys, with neither, with `null`, with a negative number, with
`cores_total` 1.5 (kept), `rss_total` 1.5 (empty), and with a string, `true`, `[]` and `{}` for each (empty, and
the rest of the line still reads). The draw cases are in section 3.

## 3. Part-of-whole on the panel 3 and 4 histories

**Owner:** core for the Windows whole (the `core` agent), ui for the drawing (the `ui` agent).

### The two wholes

One whole per side, never one across them (`CLAUDE.md` rule 3; gap list section 3):

| side | measure | whole | source |
|---|---|---|---|
| Windows | cores | Σ `cores` over every row of `snapshot.processes` but pid 0 whose `cores_known` is true | `SystemProcessInformation`, the same reading as each row. Not PDH busy: "Process cores account for Windows busy" is **No**, so the part could exceed busy and the share would be meaningless |
| Windows | working set | Σ `working_set` over every row but pid 0 | same |
| WSL | cores | none: section 2 is dropped | the guest's history is one series |
| WSL | stat rss | none | same |

The Windows list holds every process: "Windows collector checked against Windows", processes 360 against 360.
Each term rests on "Windows per-process working set ... are `Get-Process` ..." (bindings only) and, for cores,
"CPU contention checked", "Windows loses work to the VM". `vmmemWSL` and `vmwp.exe` are rows of the Windows
whole, so `now N% of all Windows processes` on `vmmemWSL` is the VM's share as Windows counts it, which rule 3
allows: it is one side's figure over the same side's.

**Same set, same reading** (machtop, 2026-11-04, section 10, a). Each whole is summed over exactly the process
set its list's rows come from, in the same reading: Windows over `snapshot.processes` with pid 0 left out, as
the lists leave it out; the guest over the rows the collector built in that sample, before `TOP` cuts them. No
whole is carried from another sample or read from another source.

Σ working sets and Σ stat rss count a shared page once per process, so the whole is never called `used`; it is
named for what it sums. The part never exceeds it, which is what the graph needs.

### The Windows whole is a snapshot field, not the screen's sum

`contracts/ui.md`, "What does not belong here": "Deriving a number that is not in the snapshot" is not the
screen's. So the Windows whole is computed in `src/analysis`, as `cpu_derived` is, and the screen reads it.
This departs from gap 3's "summed in `history::push`". It is arithmetic on one side's own figures, so its
word is `derived` (`contracts/core.md` rule 2). Additive; `schema` stays 1.

| where | field | unit | source |
|---|---|---|---|
| `snapshot.h`, new `process_totals_view process_totals` | `known` | bool | false when `processes` is empty (a failed read) |
| same | `cores` | cores, NaN when not known | Σ over rows with `cores_known`, pid 0 left out |
| same | `working_set` | bytes, 0 when not known | Σ over rows, pid 0 left out |
| record, `src/record/jsonl.cpp` | `process_totals`: `cores`, `working_set`, `source: "derived"` | as above, `null` when not known | |

Written by a header-only `inline process_totals_view process_totals_of(const snapshot&)` in a new
`src/analysis/process_totals.h`, called by `src/main.cpp` once a sample after the processes are read, where
the loop hands the snapshot to `lost_tracker`. Header-only so that `render_check.cpp`, built alone by
`msvc.cmd`, can fill its hand-built fixtures with it. `docs/json-output.md` and `scripts/check_json.py` (the
key, its `source`, `cores` at or above every known row's, `working_set` at or above every row's) change in the
same commit. A decision entry in `contracts/core.md`.

### What is drawn

machtop's form, from `panels.md` "Part of the whole" exactly, with winwsltop's names:

- **Two layers on one scale from 0.** The whole in the track tone (`tone::track`, 58;58;58, in `kToneColor`
  already); the selected row's part in the bar tone (`tone::bar`). The scale is the whole's window maximum
  rounded up by `scale_top`, never below the measure's floor: 0.05 cores (`kCoresFloor`), 4 MiB
  (`kProcessFloorGiB`).
- **Memory's history draws from 0 in both forms**, one series and part-of-whole (Q3, decided by machtop): it
  does not zoom. Panel 4's used and cache plots and any list trend keep their zoom; only the history changes.
  This replaces `panel-grammar.md` part 4's zoomed history. As one series it is drawn to `scale_top` of its own
  maximum, never below 4 MiB; under part-of-whole, to the whole's.
- **A braille cell takes one colour**: a cell holding the part's top level, or below it, is the part's; above
  it, up to the whole's top, the rest's. The boundary is exact to the cell row. A time column, two to a braille
  cell (section 4), is the rule's "cell".
- **The part's level** in a column is the mean of the part's samples in it, over the samples where both part and
  whole are known, held at or under the whole's level there (machtop `sample_part`).
- **A measured 0** of the whole is `⣀` in the bar tone, winwsltop's steel for a measured 0 (`contracts/ui.md`
  rule 8); a part that measured 0 holds no cell. Gaps and the unwatched `·` are as rule 8.
- **The max dot** goes on each layer by its own samples: the whole's in the track tone, the part's in the bar
  tone, compared with the part's own mean before it is held inside the whole. A braille cell holding the part's
  dot is the part's.
- **The heading** keeps winwsltop's left side, which names the list, the process and the measure; the right
  side is machtop's, its parts dropped from the left when narrow, the tail kept. At about 160 columns, the
  figures illustrative, not from a fixture:

  ```
  Windows · cpu_burn.exe 31000 · cores        process avg 7.73  max 13.25 · all Windows processes avg 9.10  max 15.40 · now 86% of all   scale 20   last 1 min
  ```

  `now N%` is the part over the whole at the latest sample where both are known, by machtop's `share_text`
  (`<1%` under half a percent), and blank when the whole is 0 or unknown. It is computed in `render.cpp` from two
  values the snapshot holds, which Q7 allows: a ratio of two values already in the snapshot or history, for the
  selected row, is display, not derivation. **The guest has no whole**: step D found the walk covers this
  distro's PID namespace only (audit finding 3; section 2), so no `all WSL processes` form is drawn; were a guest
  whole to return, its label is `processes in Ubuntu (this distro's namespace)`. On panel 4 the Windows whole is
  named for its sum: `Σ working sets, all Windows processes avg … max …`; never
  `used`. Units as the measure prints them: cores with 2 decimals, memory in GiB or MiB by the scale.
- **The legend**, one dim row under the heading, its swatches in the two tones:
  `█ cpu_burn.exe  █ all other Windows processes` (`… all other WSL processes`). On panel 4 the second reads
  `all other Windows processes' working sets` / `all other WSL processes' stat rss`, cut from the right if it
  does not fit. The legend takes one of the history's rows; drawn as one series, that row goes back to the
  plot, as machtop's panels do, so the history's height does not change between the two forms.
- **Giving way when short** (Q6, decided by machtop): a blank row above the heading, where the layout has one,
  goes first; then the legend is kept and the plot keeps at least 2 rows; only if heading, legend and 2 plot rows
  still do not fit does the legend go, and the heading's `all Windows processes` (`all WSL processes`) then names
  the whole. So a part-of-whole history wants one row more than slice 1's minimum, and a list still keeps
  `kLeastListRows` before the history takes it.

### When it draws as one series

The one-row rule applies per side, over the samples in the window on screen:

- the side has no whole: the guest, always, since section 2 is dropped; Windows at any sample where
  `process_totals.known` is false;
- the side's list has one row;
- the part equals the whole at every sample where both are known, within 1e-6 relative (machtop
  `part_equals_whole`); a second row appearing inside the window turns it back into part-of-whole;
- the measure does not add across rows: panel 2's busiest-engine GPU % (section 5), always.

One series is slice 1's history plus section 6's heading: no share, no grey, no legend. A guest process's
cores keep `guest's count` in the title in both forms.

### The history ring

As built (`src/ui/history.h`), `history_sample` gains two floats, `windows_cores_all` and `windows_ws_all`
(GiB), NaN where `process_totals` was not known, which `history::push` copies from the snapshot, and the bool
`gpu_engines_known` of section 5. About 9 bytes a sample before padding, about 5.4 KB over 601. The two guest
floats this section first planned, `wsl_cores_all` and `wsl_rss_all`, were not added: the guest has no whole
(section 2).

### Files and functions (ui)

`src/ui/history.h` `history_sample`; `src/ui/render.cpp` `history::push`, `process_history`,
`memory_history`, and two new functions beside `plot_block`: `part_columns` (machtop's `sample_part` on a
`time_axis`: the whole's columns and stats, the part's levels, its own stats and share) and `plot_part_block`
(machtop's `draw_part_of_whole`, emitting `tone::bar` and `tone::track` spans), plus `part_is_whole` for the
one-row rule.

### render_check

- Windows, panel 3, `contended`: the burner's history is two layers; every lit cell is `bar` or `track`;
  no `track` cell lies at or below the part's top in its column; the heading holds `all Windows processes`
  and `now`, the legend `all other Windows processes`; the scale is the whole's.
- The part never draws above the whole in any column, with a fixture where the selected row is the largest.
- A fixture with one Windows process (pid 0 and one row): one series, no legend, no `now`, and the plot has the
  legend's row.
- A part equal to the whole for the first half of the window and below it after: part-of-whole.
- WSL, panels 3 and 4: one series, `guest's count` in panel 3's title, no legend, no `now`, no `all WSL processes`.
- Panel 4: the history's plot starts at 0 (the bottom row is never blank where a sample exists), no `range` in
  its heading, `Σ working sets` in it and no `used`, in both forms (one series too); panel 4's used and cache
  plots still print `range`.
- Giving way: at the height where heading, legend and 2 plot rows just fit, the legend is drawn; one row less,
  no legend, 2 plot rows, and the heading still names `all Windows processes`.
- `now` blank when the whole's latest sample is 0 or NaN; `<1%` for a share under 0.5%.
- The max dot: the whole's in `track`, the part's in `bar`; a part's dot at or under its own mean's level is not
  drawn.
- Heading at widths 40 to 300: never wider than the screen; parts go from the left; `last` always present while
  the left side fits.
- `process_totals_of` on a fixture: pid 0 and rows with `cores_known` false are left out of `cores`; `known`
  false for an empty list. In `scripts/lost_check.cpp`, as E1 built it (audit finding 2).
- Frame bytes at 131 by 40 under budget with two-tone histories.

## 4. Two time columns per braille cell

**Owner:** ui, the `ui` agent.

**Today.** `plot_block` doubles each bucket into both dot columns ("both dots of a braille cell show the same
value"), and puts the maximum's dot in the right half. Its callers: the large layout's busy plots
(`cpu_frame`), `process_history`, and `zoom_plot` (panel 4's used and cache plots and `memory_history`). The
60 s graphs of panel 2 and the small panel 3 already sample two columns per cell (`panel_graph`,
`plot_series(..., 2 * cells, ...)`), so they do not change.

**Change.** An axis can be built with two columns per cell: `make_axis(h, end_ms, span_ms, cells, per_cell)`.
With `per_cell` 2 the cell's bucket is rounded up to an even number of milliseconds and each column is half of
it, so column `2c` and `2c + 1` lie exactly inside cell `c`. That keeps the large layout's rule that every row of
a column shares one axis (`cpu-panel.md` section 5): its heat block and lanes stay on the one-column axis, its
busy plots move to the two-column axis over the same cells. `bucket_cells` and `bucket_stats` work unchanged on
either axis, so `max_at` becomes a column. `plot_block` takes columns, stops doubling, and passes `max_mark`
straight through. `unwatched_cells(h, axis)` counts columns; `plot_line` takes the cells wholly unwatched, the
column count halved and rounded down, as machtop draws `·` only where both columns are unwatched.

At 1 minute a cell is about a second and a column half of one, so the joined-gap rule ("a joined empty cell
takes the later sample", `panel-grammar.md` part 5) fills the empty half: nothing changes there. At 5 and 10
minutes the resolution doubles.

**As sent to ui for step B (audit finding 1).** Every axis's bucket is rounded up to an even number of
milliseconds, the one-column axis too, so the two never round differently; and the two-column axis is anchored on
the cell axis, not worked out from `end_ms` again: its last column is `2 × last_cell + 1`, so column `2c` and
`2c + 1` are cell `c` at every range and end time.

**Files and functions.** `src/ui/render.cpp`: `make_axis`, `plot_block`, `zoom_plot`, `cpu_frame`'s busy
plots, `process_history`, `memory_history`; `unwatched_cells(const history&, const time_axis&)`.

**Protocol, ledger.** None.

**render_check.**
- At 10 minutes on a 43-cell plot, two samples one bucket apart with different values give a braille cell
  whose two columns differ.
- On the large layout at 1, 5 and 10 minutes, every busy-plot cell covers the same milliseconds as the heat cell
  under it.
- The max dot lands in the column of the latest maximum, left or right.
- `·` only in cells with both columns unwatched.
- Every existing expected plot string that relied on doubling is recomputed, not loosened.

## 5. Panel 2: a selectable list and a history

**Owner:** ui, the `ui` agent, and core for one call site in `src/main.cpp` (below).

**Layout.** Panel 2 keeps the device and split graphs on top, then its two lists, then one history below them,
full width, titled with the Windows list, as panels 3 and 4. When rows run short, the device graphs give way to
`kMinGraph`, then the history (section 3's order of giving way), before the Windows list falls under
`kLeastListRows`; the ui agent sets the numbers and states them in `contracts/ui.md`.

**The Windows list** (`gpu_users_list`) becomes selectable: the selection, `▶`, the selection background, its
heading lit, scrolling and `N of M` as on panel 3. Which rows it holds (Q4, decided by machtop 2026-11-04):

- every Windows process with any engine above 0 at some sample inside the range on screen, not only this second,
  so a selected process that idles for a moment stays listed and selected;
- and every process holding GPU `local` memory now (`snapshot.gpu_memory`, ledger "The memory collector reads
  what `Get-Counter` reads"), which is ROADMAP 1's Windows holders;
- ordered by GPU % now, then by most recent use (the latest sample inside the range with an engine above 0),
  then by pid. A holder that never used an engine in the range sorts last, by `local` memory.

Its rows' keys are pid, the `process_row` name and create time joined by pid. A row that comes from the ring and
is no longer in `processes` (it used an engine inside the range and has since exited) takes its name and start from
its slot's `proc_id`, never `-` and 0: `history::slot_of` and `kept_id` match on start and name, so a made-up key
would never find its slot or keep it at the cap (audit finding 4). The history exposes the slots that have a `gpu`
series, with their `proc_id` and `gpu_seen_ms`, for this. A GPU engine row whose pid is in neither `processes` nor
a slot is not listed. The heading drops `top N of M`, which scrolling makes untrue.

**The WSL list** (`dxg_holder_list`) stays a reference list, not selectable (Q5, decided by machtop, which
overrules the line in its own `panels.md` and the one in winwsltop's `docs/panels.md` section 2): no `▶`, no
selection background, no history. Panel 2 has one selectable list, so `tab` does nothing there and its footer
does not offer it. The list keeps `/dev/dxg holders, no usage measured`.

**The history.** The selected Windows process's GPU %, busiest engine, the figure the list shows
(`busiest_engine`), titled `Windows · <name> <pid> · GPU %, busiest engine`. One series always: the busiest
engine of each process does not add across processes, so there is no whole. Drawn 0 to 100; above 100 it is
drawn at the ceiling and its `max` prints the number as measured in `watch` (`contracts/ui.md` rule 5; engine
time can pass 100, Experiment 01 result, `vmwp.exe` "peak 104.52%"). No `*`: winwsltop's `*` points at the
split's caption, which is about shares, not one process. A sample where `gpu_engines_known` is false is a gap;
a process present with no engine row reads 0, measured, while engines are known. A holder with no engine use
draws `⣀` across the range.

**Q4 needs history data, not a seam change.** Every figure it uses is in the snapshot today: engine rows by pid,
`gpu_memory` `local` by pid, `gpu_engines_known`. What is new is that list membership and the "most recent use"
order read the ring, not only the newest snapshot:

- The per-process slot gains a third series, `gpu`, allocated only when the process first has a non-zero engine
  reading, so the 2.4 KB a slot costs is paid by the few processes that use the GPU, not the 360. When it is
  allocated, its earlier positions are filled from what is already known: 0 where the slot's memory value shows
  the process was present and that sample's engines were known, NaN elsewhere. `history_sample` gains
  `bool gpu_engines_known` for that. `history::gpu_at(slot, position)` reads 0 or NaN by the same rule for a slot
  with no `gpu` series. `write_processes` writes it from `busiest_engine(s)`, Windows side only.
- The slot also keeps `gpu_seen_ms`, the newest sample with an engine above 0, so the list need not scan 601
  positions per row each frame; a row is in the list while `gpu_seen_ms` lies inside the range on screen.
- `take_slot` clears the `gpu` series (empties it, so it is allocated again on first use) and `gpu_seen_ms` when
  it reuses a slot, as it clears `cores` and `memory`; `push`'s clock-step shift moves `gpu_seen_ms` with
  `seen_ms` (audit finding 5).
- Because the list now depends on the history, `selectable_lists` and `apply_key` take `const history&` beside
  the snapshot, the range from `view_state`, and `now_ms`. They place the range's end with
  `axis_end(h, now_ms)`, as the frame does, so the list a key moves through is the list drawn (audit finding 8).
  `apply_key` is declared in `src/ui/render.h` and called from `src/main.cpp`, so the call site's change, passing
  the history and the same `now_ms` the next `render` gets, is core's. As built it landed separately: ui's
  4f9e546 kept the three-argument overload, and core's d9a699f moved `main.cpp` to the new one, so between the
  two the keys moved through this sample's list alone.

**Files and functions.** `src/ui/render.cpp`: `gpu_frame`, `gpu_lists`, `gpu_users_list` (membership, order,
selection), `dxg_holder_list` (unchanged but for the footer's `tab`), `selectable_lists` (panel 2's one list),
`selected_processes` (panel 2's Windows selection added to 3 and 4), `apply_key`, the footer's `↑↓` for panel 2,
a new `gpu_history`; `history::push`, `write_processes`. `src/ui/render.h`: `apply_key`'s signature.
`src/ui/history.h`: the slot's `gpu` and `gpu_seen_ms`, `gpu_at`, `history_sample::gpu_engines_known`.
`src/main.cpp` (core): the `apply_key` call, with the history and `now_ms`.

**Protocol.** None. **Ledger.** Cites Experiment 01 result (per-process `3D` engine time, the load process
71-97%, `vmwp.exe` for WSL's work), "The memory collector reads what `Get-Counter` reads" and the core decision
of 2026-10-31 that per-process GPU memory is shown as `local`, and "`/dev/dxg` holders identify the Linux
process" (identity only, no usage). Per-Linux-process GPU use stays unmeasured and is never drawn (`CLAUDE.md`
rule 2).

**render_check.**
- Panel 2 `busy` at 80 by 24, 100 by 34, 131 by 40: one `▶` on the panel, in the Windows list; `↑↓` in the
  footer and no `tab`; `tab` changes nothing on panel 2; the WSL list has no `▶` and no selection background;
  `top N of M` gone; `N of M` under the Windows list.
- Membership: a process with engine use 30 s ago and none now is listed at 1 minute and still selected; at the
  next sample after its last use leaves the range, it is gone and the selection falls to the first row. A process
  with `local` memory and no engine use is listed, after those with use.
- Order: GPU % now, then most recent use, then holders by `local` memory.
- The history: `GPU %, busiest engine` in the title, `last 1 min` in the heading, no `scale` (a real ceiling),
  no legend and no `all Windows processes`, ever.
- `demo_minute`'s 112: drawn at the ceiling, `max 112%` in `watch`, no `*` on the history.
- `gpu_engines_known` false for a stretch: a gap, not a 0; a known sample with no row for the pid: `⣀`.
- A `gpu` series allocated late is 0 where the process was present before, NaN where it was not.
- A pid reused under the same name (section 1) does not keep the selection.
- `selected_processes` names panel 2's selection, and its slot survives the cap.
- A process that used an engine 30 s ago and has exited is listed under its own name and pid, its key carries its
  slot's start, and selecting it keeps its slot at the cap.
- A slot reused by another process has no `gpu` series and no `gpu_seen_ms` from the last occupant; after a clock
  step back, a process's `gpu_seen_ms` still lies in the range it did before.
- `apply_key` with `now_ms` moves through exactly the rows `render` draws for the same `now_ms`, including at the
  moment a row's last use leaves the range.
- Sizes 1 to 300 by 1 to 66 on panel 2 for `busy`, `bare` and hostile names; frame bytes under budget.

## 6. The range in the heading; gutter and axis

**Owner:** ui, the `ui` agent.

**Range.** Every history heading ends with `last 1 min`, `last 5 min` or `last 10 min` (gap 8; machtop's
`"last " + range_text`), after `scale` where there is one, and it is the last part dropped for width. Applies to
`process_history`, `memory_history` and the new `gpu_history`.

**Gutter and axis (gap 10; Q2, decided by machtop 2026-11-04).** History graphs, and only they, lose the
gutter and the time axis: the plot is full width, and `avg`, `max`, `scale` and `last N min` sit in the heading.
winwsltop's histories have a gutter today (`100 │`, `  0 │`, or a zoomed range's ends) and no axis row, so in
practice the gutter goes. The device graphs of panel 2, the large layout's busy plots, the heat block and lanes,
and panel 4's used and cache plots keep their gutter and axis, where several plots sit side by side and the axis
carries `avg` and `max` (`panel-grammar.md` part 5, "Where the label sits").

**Memory's history from 0 (Q3).** Step C also takes `memory_history` off the zoom in its one-series form, which
needs no whole: it draws from 0 to `scale_top` of its own maximum, never below 4 MiB, and prints `scale`, not
`range`. Section 3's part-of-whole form then only changes which maximum sets the scale.

**Files and functions.** `src/ui/render.cpp`: `process_history`, `memory_history`, `gpu_history` build their
axis over `width`, not `width - kCpuGutter`, and their plot without `gutter`; `plot_block` gains a no-gutter
form; `memory_history` stops calling `zoom_plot`, which keeps serving panel 4's used and cache plots unchanged.

**render_check.**
- Each history's heading ends with `last` and the range after `←` and `→`; at every width the heading is never
  cut inside `last N min`.
- A history's plot rows start with braille, `·` or a blank at column 0: no `│`, and no `-60s`/`now` row under a
  history.
- Panel 4's history, one series: `scale` in the heading, no `range`, the bottom row drawn where a sample exists.
- Panel 2's device graphs and panel 3's busy plots still print `-60s` and `now` and their gutter.

## 7. Costs against the budgets

| budget (`contracts/ui.md`) | after this slice |
|---|---|
| Frame bytes, 131 by 40, under 48 KiB | about the same: a two-tone history adds a colour change per run of cells. Asserted |
| Frame build under 5 ms | one more series per history, a few hundred columns. Measured |
| History | two floats and a bool a sample (about 5.4 KB), and 2.4 KB per process that used the GPU |
| Push | the two Windows totals copied, and `busiest_engine` once a sample, already computed for the list. Measured by `render_check measure` |
| Collector line, under 8 KiB | unchanged: the guest totals were dropped with step E2 |
| New PDH counters | none |

## 8. Build order

Small commits, each with `scripts/check.sh` and `--windows` where it touches `src/`. All ui work is in
`src/ui/render.cpp` and `render_check.cpp`, so the ui items run one after another, by one ui agent; the
tracks below run in parallel because they share no file.

| step | owner | items | files | needs |
|---|---|---|---|---|
| A | ui | 1, selection identity | `render.cpp` `selected_row`, `render.h` | nothing |
| B | ui | 4, two columns per cell | `render.cpp` axis and plot functions | A (same file) |
| C | ui | 6, Q2 and Q3: range, avg, max and scale in the history heading; histories lose gutter and axis; memory's history from 0 as one series; `contracts/ui.md` rule 8 for these, in the same commit, so the rule is never stale (audit finding 9) | `render.cpp` `process_history`, `memory_history`, `plot_block`; `contracts/ui.md` rule 8 | B |
| D | evidence | 2, experiment 07 and its ledger rows; the two pid-reuse rows if they can be had read only (section 9) | `experiments/07-*`, `docs/measurable.md` | nothing |
| E1 | core | 3, the Windows whole | `snapshot.h`, `src/analysis/process_totals.h`, `main.cpp`, `jsonl.cpp`, `json-output.md`, `check_json.py`, `contracts/core.md` | nothing |
| E2 | dropped | ~~2, `cores_total` and `rss_total`~~ D's verdict is No (section 2) | `collector.py`, `collector_embed.cpp`, `PROTOCOL.md`, `wsl_view.h`, `wsl_view.cpp`, `check_json.py`, `json-output.md`, `contracts/collect.md`, `contracts/core.md` | D's rows; after E1 (both touch `check_json.py` and `json-output.md`) |
| F1 | ui | 3, part-of-whole, Windows side, with `now N% of all` (Q7 decided); the guest one series | `history.h`, `render.cpp` push, histories, new plot functions | C, E1 |
| G | ui (4f9e546), then core for one line (d9a699f) | 5, Q4, Q5: panel 2's Windows list selectable, its rows from the range on screen and the `local` holders, ordered by GPU % then most recent use; the WSL list a reference list, no `tab`; the GPU history. History data, no seam change: the slot's `gpu` series and `gpu_seen_ms`; `apply_key` and `selectable_lists` take the history | `history.h`, `render.h` (`apply_key`), `render.cpp` panel 2 and the ring; `src/main.cpp`'s `apply_key` call (core) | F1 (same files) |
| F2 | dropped | ~~3, the guest side lit from `cores_total` and `rss_total`~~ D's verdict is No | | |
| H | core | `contracts/ui.md` decisions and rule 8's part-of-whole and panel 2 text (step C's part already done), `panels.md` (panel 2's holder list not selectable, against its section 2 line), `panel-grammar.md` notes, `ROADMAP.md` 1 and 2, `README.md` keys for panel 2 | docs | G |
| I | auditor | a fresh auditor reads A to H against the code and the ledger | none | H |

In parallel: the ui chain A, B, C; D; E1, all built. D returned `No`, so E2 and F2 are dropped and the guest's
histories stay one series, which F1 draws. What remains: F1, then G, then H and I.

## 9. Requests to other files

Written as requests; this design changes no other file.

| file | owner | change |
|---|---|---|
| `experiments/07-*`, `docs/measurable.md` | evidence | Experiment 07 (section 2). Beside it, if it can be done read only with idle processes as experiment 06 did: the two pid-reuse rows, by starting short idle processes until a pid recurs (Windows reuses pids quickly; in the guest `pid_max` makes it slow, so the guest row may stay `Not checked`) |
| `docs/contracts/evidence.md` | core, for evidence | experiment 07 as a request |
| `docs/contracts/ui.md` | at C, then core at H | at step C: rule 8's histories with no gutter or axis, their labels and `last` in the heading, memory's history from 0 (the coordinator asked ui to make this change in step C; `docs/contracts/` is core's row in `ARCHITECTURE.md`). At H: two-layer histories in rule 8; decisions; panel 2's Windows list selectable, its membership and order; Q7's answer |
| `docs/panels.md` | core, at H | the selection by pid and start with the name backstop, the name to go once pid reuse is in the ledger; panel 2 as built, the WSL holder list a reference list, replacing "A holder can be selected"; part-of-whole's wholes per side |
| `src/main.cpp` | core, in step G's commit | the `apply_key` call passes the history and `now_ms` |
| `docs/ROADMAP.md` | core, at H | section 1: Windows holders listed on panel 2 |
| `docs/design/panel-grammar.md` | core, at H | notes at part 3 "Identity" and part 4 "One history" that this file supersedes them |
| `docs/ROADMAP.md` | core, at H | section 2: per-process GPU history built |
| machtop `docs/panels.md` | machtop's owner | the three amendments marked "winwsltop not yet asked" can read "agreed by winwsltop" once this is built |

## 10. Questions, decided by machtop, 2026-11-04

machtop leads on panel design (the user's standing decision, `docs/panels.md`, Decisions), so these are decided.

- **a. The Windows whole as a snapshot field:** agreed. Each whole is summed over the same process set, in the
  same reading, as its rows (section 3, "Same set, same reading").
- **b. Identity keeps the name beside pid and start, for now:** agreed (section 1).
- **Q1. Dropping the name:** once the ledger has rows on pid reuse, identity is pid and start only, as machtop's,
  so a process that `exec`s keeps its selection (section 1). Not a step of this slice.
- **Q2. Gutter and axis:** history graphs only lose the gutter and the time axis; `avg`, `max`, `scale` and
  `last N min` move into the heading. Device and panel-level plots keep both (section 6, step C).
- **Q3. Memory's history:** from 0 in both forms, one series and part-of-whole; the list's own trends may still
  zoom (sections 3 and 6, step C).
- **Q4. Panel 2's Windows list:** a process stays while any of its engines read above 0 at some point in the range
  on screen, and any process holding GPU `local` memory is listed too; ordered by GPU % now, then by most recent
  use. It needs history data (a per-slot `gpu` series and `gpu_seen_ms`) and `apply_key` reading the history; no
  seam change: every figure is in the snapshot today (section 5, step G).
- **Q5. Panel 2's WSL holder list:** not selectable; no `tab` on panel 2, no `▶`, a reference list only. machtop
  overrules its own `panels.md` line on this, and winwsltop's `docs/panels.md` section 2 changes at step H.
- **Q6. Giving way:** the blank row goes first; the legend is kept with at least 2 plot rows; the legend goes only
  if that still does not fit, and the heading's `all Windows processes` then names the whole (section 3).

## 11. After the audit of f270ef8

A fresh auditor read this design at f270ef8. Findings 1 (even buckets, the two-column axis anchored on the cell
axis), 2 (E1's checks in `lost_check`), 3 (the WSL whole is this distro's namespace; D then dropped it) and 11
(experiment 07's rows) were handled elsewhere and are recorded in sections 2, 3 and 4. Findings 4, 5 and 8 changed
step G (section 5), 6 and 7 step E2 (section 2, since dropped), 9 steps C and H (section 8). Finding 10 is Q7.

- **Q7. May the screen compute `now N%`? Decided by the user, 2026-11-04: yes.** `contracts/ui.md` now says that a
  ratio of two values already in the snapshot or the history, for the selected row, is display, not derivation, so
  step F1 shows `now N% of all`. The question as put: Section 3 moved the Windows whole into the snapshot because
  `contracts/ui.md` keeps the screen from "deriving a number that is not in the snapshot". The share is such a
  number: the selected row's value over the whole, computed in `render.cpp`. It cannot move into the snapshot,
  because it depends on the selection. `contracts/ui.md` has no rule that a ratio of two snapshot values, or a
  statistic over the window, is display rather than derivation, though the screen already prints `avg` and `max`
  over the window and keeps Windows used as total less available (`history_sample.windows_used`). Options: amend
  `contracts/ui.md` to say that one snapshot value over another of the same side and the same reading, and
  statistics over the window, are display, not derivation; or leave `now N%` out of winwsltop's heading. For the
  user.
