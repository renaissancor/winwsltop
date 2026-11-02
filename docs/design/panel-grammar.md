# Panel grammar, slice 1

Status: built, 2026-11-02, commits e37993d to 01384a4 on `cpu-panel` (section 7). Approved to build after
experiment 0 the same day, after a UX review and a systems review ("fix first", both at 326a2e1) and machtop's
answers; the owner's decisions are in section 8. Where the build departs from this file, `contracts/ui.md`'s
decision log says so and rules; the mockups are as designed, not printed from the build.

The shared grammar is machtop's `docs/panels.md` (246c2cb on machtop main), section "Shared
grammar" (outside this repository). The owner made it authoritative on 2026-11-02: where it is clear,
winwsltop follows it, and `design/cpu-panel.md`'s refusals of list focus, the `s` sort, scrolling and
per-process history are superseded. [`../panels.md`](../panels.md) records winwsltop's decisions on it.
Every claim about a counter below was checked against [`../measurable.md`](../measurable.md) and the code,
and rows are cited by claim.

## 0. The slice, and what it cuts

| part | in this slice |
|---|---|
| 1. Figures without a ledger row | A mark, `?`, on every figure the screen shows without a row, and experiment 0 to clear them |
| 2. Keys and footer | Every panel |
| 3. Selection, scrolling and the selected row's history | Panel 3, both layouts, with a per-process ring and the seam change it needs |
| 4. Panel 4 Memory | Built |
| 5. Graphs: scale, mean cells, `avg` and `max` | Every braille graph; rule 8 changes |

Cut from the brief, and why:

- **Panel 1 gets no selection.** machtop's overview is a summary, not a list to select in (its
  `list_rows` returns nothing there), and winwsltop's target panel 1 has no process lists
  ([`../panels.md`](../panels.md), 1, "Not here").
- **Panel 2 gets no selection yet.** Making it list-on-top re-lays the panel, and ROADMAP 1 (Windows holders
  first, `unattributed`) re-lays it too; slice 2 does both at once. `←`/`→` and part 5 reach panel 2 now.
- **`s` sorts nothing yet.** In machtop it acts on panel 7, which is not built. `s` and `ㄴ` are decoded and
  tested, act nowhere and are in no footer.
- **Panels 5 to 8 are not built.** Their keys open a page that says so.
- **The contention verdict is left out**, as the owner asked.

## 1. Figures without a ledger row

`CLAUDE.md` rule 2: nothing is built on a counter before the ledger records it. The reviews found that
today's screen already shows several figures with no row, and that this design leaned on one:

| figure | on screen today | why it has no row |
|---|---|---|
| Windows available, and used and free from it | panel 1 `MEM`, panel 3 `RAM` | "Windows collector checked against Windows" checks total only |
| Windows commit, cache | panel 1 `MEM` | same |
| Windows per-process working set | panels 1 and 3, `WS GiB` | the `vmmemWSL` row checks one process, not the read for every process |
| Guest per-process cores | panels 1 and 3, the WSL lists, and their sort | no row reads `/proc/pid/stat` against another clock. The 19.718 in "Guest busy stays high when the host has taken the vCPU" is the burner's own count, not `/proc` |
| Guest per-process RSS | panels 1 and 3, `RSS GiB` (`render.cpp` lines 1765, 1839, 1847) | no row |
| Guest swap used | panel 1 `MEM` | no row |

**Decision: these stay where they are, and on any panel that shows them their label carries `?`**, in the
`dim` tone, and the panel's rule line says once `? not yet checked`. A figure that is not on screen today
and has no row, Windows private bytes, is not shown. The alternative, hiding them all until experiment 0,
empties panel 4 and the WSL list of panel 3; it is the owner's choice (section 8).

**Experiment 0, read only, no load, no GPU** (evidence; the owner decides whether it runs): Windows
available, commit and cache against `Get-Counter` and Task Manager; several processes' working set and
private bytes against `Get-Process`; guest swap against `free -b`; guest RSS against `/proc/pid/status`
`VmRSS` and `ps`; guest per-process cores against `ps` `TIME` over the same window; guest used and cache
beside `vmmemWSL` working set read in one sample; and that a process's create time (Windows) and
`starttime` (guest) are constant while it lives and differ after a pid is reused. Each row that lands
removes its `?`; nothing moves.

**Experiment 06 is experiment 0** (run 2026-11-02; ledger, "Memory and process collectors checked"; one idle
state, and on both sides the second reading comes from the same OS's own counts, so most rows show bindings
and units only). What it does to each `?`:

| figure | after experiment 06 | ledger claim |
|---|---|---|
| Windows available, and used and free from it | `?` cleared | "Windows `available` is `\Memory\Available Bytes`": observed, bindings only |
| Windows commit | `?` cleared | "Windows `commit_used` and `commit_limit` are `\Memory\Committed Bytes` and `Commit Limit`" |
| Windows cache | `?` stays | "Windows `cache` ... is a known `\Memory` figure": Unresolved, about 161 MiB above standby plus `Cache Bytes` |
| Windows per-process working set | `?` cleared | "Windows per-process working set and private bytes are `Get-Process` ...": observed, bindings only |
| Guest per-process cores | `?` cleared | "Guest per-process cores are `/proc/pid/stat` utime + stime ...": observed, bindings only |
| Guest swap used | `?` stays | "Guest swap used is `free -b` Swap used": `Not checked`, it read 0 throughout |
| Guest per-process RSS | relabelled, no `?` | "Guest per-process RSS is `/proc/pid/status` `VmRSS` and `ps` RSS": No. The collector shows `/proc/pid/stat` field 24 times the page size and matches it, which reads up to 2568192 B below `VmRSS` |

Guest RSS keeps its source and is labelled as what it reads: the column heading becomes `stat rss GiB` on
panels 1, 3 and 4, and a history titled with it says `stat rss`, not `RSS`. The lead's call, 2026-11-02:
it reads its own kernel figure exactly, so the label, not a `?`, is the honest fix. Whether to move to
`VmRSS` or `statm` waits for the `statm` check to be rerun with its output saved (`contracts/evidence.md`).
Windows private bytes now has a row too, but stays off the screen in this slice. The mockups in parts 3
and 4 predate experiment 06 and show the old marks; this table rules where they differ.

Rows the slice does build on: Windows total and `vmmemWSL` working set ("Windows collector checked against
Windows"); guest total, used and cache ("WSL collector checked", "Guest memory vs host", read live);
guest memory pressure ("CPU contention checked", "`/proc/pressure` memory and io", 0 in every case);
Windows per-process cores ("CPU contention checked", "Windows loses work to the VM": the burner's process
time read 19.320 cores alone and 13.253 beside WSL), which do not add up to busy ("CPU counters checked",
"Process cores account for Windows busy": No), so no process's history is summed or drawn against busy.

## 2. Keys and footer

**Question:** what can I do on this panel, and where else can I go?

### Keys

| key | does | panels | in the footer |
|---|---|---|---|
| `1`-`8`, numpad 1-8 with NumLock on | Switch panel; 5-8 open a "not built yet" page | all | the panel list |
| `↑` `↓` | Move the selection in the focused list; stop at its ends, no wrap; a long list scrolls (part 3) | 3, 4 | there only |
| `←` | Range further back: 1 → 5 → 10 minutes, clamped | 2, 3, 4 | there only, with the range |
| `→` | Range nearer: 10 → 5 → 1 minute, clamped | 2, 3, 4 | there only |
| `←` `→` on 1 and 5-8 | Nothing: no graph to change. The range is kept | | no |
| `↑` `↓` on 1, 2 and 5-8 | Nothing: no list to select in | | no |
| `tab` | Focus the next selectable list, wrapping; nothing with one | 3, 4, when both lists are selectable | then only |
| `s`, `ㄴ` | Nothing in this slice | | no |
| `esc` | Panel 1. Every panel keeps its focus and selections; the range is kept | all | no |
| `q`, `ㅂ` | Quit. Ctrl+C still quits | all | yes |
| `t` | Nothing: the range replaces it | | no |
| Shift+Tab | Nothing (machtop 246c2cb: there is no Shift+Tab; its parser drops `ESC [ Z` whole) | | no |

The range is one setting for the screen, as machtop's `view_state.range`; `←` further back and `→` nearer,
clamped, as machtop's `apply`.

### Decoding

winwsltop reads key records from `CONIN$` with VT input off (`console.cpp`), so `←` arrives as `VK_LEFT`
and `esc` as `VK_ESCAPE`, never as bytes: a bare `esc` cannot be the start of an arrow's sequence, and the
grammar's short delay is met by construction. A key is decided by its virtual key code first, as today.
If that gives nothing, as under an IME reporting `VK_PROCESSKEY`, the character decides through machtop's
2-set Korean table: U+3142 `ㅂ` is `q`, U+3134 `ㄴ` is `s`. Ctrl held means no key. Shift is allowed,
because some layouts need it for digits, except with Tab.

Alt+key is `esc` then the key, with one exception: **Alt+arrow is the arrow alone**. A terminal sends it as
one CSI sequence (`ESC [ 1 ; 3 D`), and machtop's parser reads it as the arrow, so the grammar has no `esc`
in it. Keys that arrive as `VK_PACKET`, or as plain ASCII with no virtual key, through the character path
only, are out of scope for slice 1: the character path reads the jamo table and nothing else.

Three cases only a person can check live, at 100 by 34 and 131 by 40:

| case | what happens | mitigation in this slice |
|---|---|---|
| OpenSSH into Windows: `esc` then a key, typed fast, reaches the console host as one Alt+key | Today Alt+key is dropped, so both keys are lost | **Taken:** Alt+key without Ctrl decodes as `esc` then the key. winwsltop gives Alt no meaning, so nothing else changes. AltGr arrives as Ctrl+Alt and stays dropped |
| Korean IME holding a lone `ㅂ` in composition | If the IME reports only `VK_PROCESSKEY` and commits nothing, `q` waits for the next key | None cheap: the virtual key is tried first, which is `Q` under most IMEs. Ctrl+C always quits |
| Numpad with NumLock off | The arrows arrive as `VK_UP` and the others without the enhanced flag, the same codes, so they work | None needed. With NumLock on, numpad 2, 4, 6 and 8 switch panels, as digits do |

The decoder is a header-only pure function, `src/ui/keys.h`, `inline key_event key_for(uint16_t vk,
char32_t ch, uint32_t modifiers)` with the jamo table `inline constexpr`, so `msvc.cmd` needs no change
and `render_check` includes it and runs it on Linux too. Applying a key is pure as well,
`apply_key(view_state&, key_event, const snapshot&, const history&, int width, int height)` in
`render.cpp`, so what `handle_keys` does today without a check (`contracts/ui.md`, Current state) is
checked. `main.cpp` keeps the polling, the redraw on a key and the quit.

### Footer

machtop's `footer()`: the panels on the left, the keys that act on this panel on the right; when both do
not fit, every panel but the current one shrinks to its number. Labels are machtop's short ones with
winwsltop's 8: `overview gpu cpu mem disk net procs vm cost`. The current panel is lit (`title`, bold), a
built one `dim`, one not built `faint`, which 246c2cb made shared grammar.

The footer offers `←→` only when the frame draws a graph over the range. A panel that has a range but is too
short to draw any graph shows no `←→`, since the key would change nothing on screen; the range is still
kept and still applied by `apply_key`.

Footers, in order: panel 1 at 131 and 100 columns, panel 3 at 131 and 100, panel 4 at 100, panel 2 at 80:

```
1 overview  2 gpu  3 cpu  4 mem  5 disk  6 net  7 procs  8 vm cost                                                           q quit
1 overview  2 gpu  3 cpu  4 mem  5 disk  6 net  7 procs  8 vm cost                            q quit
1 overview  2 gpu  3 cpu  4 mem  5 disk  6 net  7 procs  8 vm cost                            ↑↓ select  tab list  ←→ 1 min  q quit
1 2 3 cpu 4 5 6 7 8                                            ↑↓ select  tab list  ←→ 1 min  q quit
1 2 3 4 mem 5 6 7 8                                            ↑↓ select  tab list  ←→ 1 min  q quit
1 2 gpu 3 4 5 6 7 8                                             ←→ 1 min  q quit
```

On panel 3's lines, `3 cpu` is lit, `1`, `2` and `4` are `dim`, `5` to `8` `faint`.

A panel not built, at 100 by 34: the title, then this, then the footer.

```
DISK ───────────────────────────────────────────────────────────────────────────────── not built yet

Which disk is busy, and is it the WSL disk? The disk rows are on panel 1 until this panel is built.
```

Each of 5 to 8 carries its question from [`../panels.md`](../panels.md) and where its figures are today:
5 and 6 point to panel 1, 7 to panels 1 and 3, 8 to panels 2 and 3.

### State and cost

`view_state` in `render.h` replaces `view` and `render_options`: the panel, the range, and per panel the
focused list and a selection key per list. Under 1 KiB, nothing per second. A key redraws the last
snapshot at once, as `1`-`3` do today.

### render_check must check

- `key_for`: every row of the key table; numpad digits; `9`, `0` and `T` give nothing; Ctrl+Q gives
  nothing; Alt+Q gives `esc` then `q`; Ctrl+Alt+Q gives nothing; Shift+1 gives `1`; Shift+Tab gives
  nothing; `VK_PROCESSKEY` with U+3142 gives `q` and with U+3134 `s`; every other jamo maps as machtop's
  table does.
- `apply_key`: `3 tab tab` returns focus to Windows; `3 tab esc 3` keeps WSL focused; `↓↓↓ esc 3` keeps the
  third row; `←←←` stops at 10 minutes, `→→→` at 1; `←` on panel 1 leaves the range; `tab` on 1 and 2
  changes nothing; `tab` on 3 with WSL unavailable changes nothing; `↑` on the first row stays.
- Footer at widths 1 to 300 on every panel: never wider than the screen, the current panel always named,
  `tab list` only where two lists are selectable, `↑↓` only on 3 and 4, `←→` only on 2, 3 and 4 and only when a graph is drawn, no `[t]`,
  no `s`.
- `--text` is unchanged.

### Out of scope

Mouse, paging (`PgUp`, `PgDn`, `Home`, `End`), a help overlay, saving the range between runs.

## 3. Selection, scrolling and the selected row's history

**Question:** what has this one process been doing over the last minutes?

Panel 3 is where both sides' process lists already sit side by side (`contracts/ui.md` rule 3).

### Behaviour

From machtop 246c2cb, "Selection" and "tab":

- Each list remembers its selected row by identity: a process by pid and name. If that row leaves the list
  the selection falls to the first row; the key is kept, so the row is selected again if it returns. Before
  any `↑`/`↓` the history follows the busiest process.
- `↑`/`↓` move through every row of the list and stop at its ends. With `n` rows of space the first row
  drawn is 0 while the selection is in the first `n`, else `selection - n + 1`, so past the bottom the
  selection stays on the last row shown. The offset is worked out from the selection each frame, as
  machtop's `app_table` does; no paging. Each list scrolls by its own selection, focused or not.
- Each list ends with a `dim` `N of M` line, rows shown of rows in the list. The guest's adds its own count,
  `6 of 6 · 64 in the guest`, because the collector's line holds the top 30 by cores then RSS plus every
  holder (`PROTOCOL.md`, `processes`). The list headings drop `top N of M`, which scrolling makes untrue.
- Focus starts on Windows. The focused list's heading is lit and its selected row marked `▶` on the
  selection background (machtop's `44;62;76`); the other's heading is `dim` and shows no `▶`. Every list row
  has a 2-column lead for the `▶`, taken from the name column.
- **One history, full width, below both lists**, following focus and titled with its list:
  `Windows · cpu_burn.exe 31000 · cores` (246c2cb: "never one per list (two side by side invite comparing
  rows the panel keeps apart)", which is `contracts/ui.md` rule 3). Cores are drawn from 0 to the window's
  maximum rounded up to 1, 2 or 5 times a power of ten, never below 0.05 cores (machtop's per-app CPU
  floor), and the title's right side reads `avg A  max M  scale S` (part 5). A guest process's history is
  titled `cores?, guest's count`: what the guest counted, not what it got ("Guest busy stays high when the
  host has taken the vCPU"), and with no row of its own yet (part 1).

### Rows

Head row 4 (`RAM 41.2/63.7 GiB  free 22.5 GiB`) and the 2-row RAM plots go to panel 4: 3 rows. The
`N of M` line, the history's title and its 2 rows take 4. Rows are given in this order: title, rule, 3
heads, lanes, caption, heat block and axis, list headings, `N of M`, 3 list rows, the history, the footer;
then the busy plots 3 rows; then list rows up to 6; then the busy plots up to 12; then the history up to 6;
then the lists. **A list keeps at least 3 rows**: below that the history goes first, and selection still
shows. So at 34 the busy plots have 3 rows and the lists 5; at 40, 8 and 6.

The small layout, under 100 columns or too short for the large one, stacks the two lists as today and puts
the one history under both, by the same 3-row rule.

The busy plots' `avg` and `max` sit on the column's axis row, in place of `each cell N s` where that does
not fit (part 5), so they show at 34 rows too.

### The per-process ring

Panel 3's history and panel 4's need it. Cost at 1 s × 10 minutes:

| item | size |
|---|---|
| A slot: 601 samples × (cores, memory) as `float` | 4808 bytes |
| This machine: 360 Windows processes ("Windows collector checked against Windows") and the guest's 30 plus holders | about 400 slots, 1.9 MB |
| Slots kept | 1024, or twice the rows of the newest sample if that is more; 4.9 MB at 1024 |
| The existing ring after the range change: 601 × 608 bytes (`sizeof(history_sample)` with g++), plus 5 floats for panel 4 | 377 KB, down from 548 KB at 901 |

- Slots sit beside the existing ring, indexed by the same ring position, so a stall, a clock step or a clear
  moves them with it. `kKeepMs` becomes 600000 and `kCapacity` 601, since the range stops at 10 minutes.
- **Identity.** A slot is keyed by side, pid and start: Windows' create time, which `processes.cpp` already
  reads for its own key, and the guest's `starttime`, which `collector.py` already reads for its own. A reused
  pid, with the same name or not, gets a new slot. Both need the seam change below.
- **A first reading is not a measurement.** A process's first sample reads 0 cores because there is no
  earlier time to subtract (`src/host/processes.cpp` lines 104-111, `rated`; `collector.py` line 145, `if old
  else 0.0`). The ring stores it as NaN. This needs the seam change below.
- **Every push writes every slot.** A slot whose process did not report writes NaN at the new position;
  otherwise the value from 10 minutes earlier would show when the ring wraps. 1024 slots × 2 floats is
  8 KB of writes a second.
- **At the cap the slot least recently seen is cleared and reused; nothing is refused.** A guest parallel
  build that churns the top 30 fills slots with short-lived compilers, and they are the first reused. The
  selected rows' slots are never taken. An evicted process that returns starts again, with a gap before.
- Windows: every row but pid 0, cores and working set. Guest: every row in the line, cores and RSS, with the
  large layout's repeat rule (a repeated line carries its value once, a second repeat is a gap) and a gap
  from `kStaleS`.

### The seam change it needs

Both are additive keys, so `schema` stays 1, as `vm.vmmem_cores_known` did (`contracts/core.md`, decision of
2026-11-01: a flag beside the value, not a changed value):

| where | field | unit and source |
|---|---|---|
| `snapshot.h` `process_row`, the record | `cores_known` | bool: the row had an earlier reading, `processes.cpp`'s `rated` |
| same | `create_time` | 100 ns since 1601, `SystemProcessInformation` `CreateTime`, already read |
| `PROTOCOL.md` `processes`, `wsl_view` | `rated` | bool: false on a process's first reading |
| same | `start` | clock ticks since boot, `/proc/pid/stat` field 22, already read |

If the owner prefers `cores` to read `null` on a first reading instead, that changes the meaning of a key
and raises `schema` in `PROTOCOL.md`, `collector.py` and `wsl_view.cpp` in one commit (`contracts/core.md`
rule 4). The ledger rows these identity fields want are in experiment 0.

### When the selected row has no history yet

| case | the history shows |
|---|---|
| Just launched | Faint `·` in the unwatched cells, as every graph; `avg` and `max` over what exists |
| A process's first reading | Blank for that sample, never 0 |
| First seen 40 s ago, range 5 minutes | Blank before then: watched, nothing to measure |
| Guest process outside the top 30 for a while | Blank for those samples |
| `vmmemWSL` cores unknown | Blank, never 0 |
| WSL unavailable or stale | The WSL list says why, as today; nothing to select there; `tab` does nothing |
| Window too small | The history goes before a list falls under 3 rows |

### Mockups

From `render_check contended 131 40` and `100 34` as they print today, with RAM removed and the rest
added. **The fixture is amended first, as `render_check` must be:** in `quiet_moment` both burners read 0
cores, so the histories step where the busy plots do. Every plot is drawn by `render.cpp`'s own
`make_axis`, `bucket_cells` (mean) and `plot_block` on that history; the values are what `history::push`
would store from the fixture's rows; widths and heights were checked by script.

131 by 40, focus on Windows:

```
winwsltop  21:43:51  data 0.4 s old  every 1.0 s
CPU ─────────────────────────────────────────────────── ? not yet checked · two systems' own views of overlapping work, never added
100%  WINDOWS  user 65 · kernel 6 · VM 29                        │ 100%  WSL  user 99 · sys 1 · iowait 0, as idle
      whole machine, includes the WSL VM                         │       guest view, inside the VM · Ubuntu-24.04
VM ran 5.76 of 20 threads · ≈41 ready, 13 VM                     │ WSL short ≈14 of 20 vCPUs, inferred · load 19.8
100 │                        ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿ │ 100 │                        ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
    │                        ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿ │     │                        ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
    │                        ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿ │     │                        ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
    │                        ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿ │     │                        ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
    │                        ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿ │     │                        ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
    │                        ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿ │     │                        ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
    │                        ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿ │     │                        ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
  0 │⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿ │   0 │⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
kern│███████████████████████████████████████████████████████████ │ iowt│███████████████████████████████████████████████████████████
VM  │███████████████████████████████████████████████████████████ │ lost≈                        ███████████████████████████████████
threads · 20 of 20 ≥90%     idle █████ ≥90%                      │ vCPUs, not Windows cores · 20 of 20 ≥90%
P0  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v0  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
P1  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v2  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
P2  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v4  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
P3  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v6  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
P4  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v8  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
P5  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v10 │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
E0  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v12 │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
E2  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v14 │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
E4  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v16 │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
E6  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v18 │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
     -60s ──── busy avg 60% max 100% · each cell 1.0 s ───── now │      -60s ──── busy avg 60% max 100% · each cell 1.0 s ───── now
Windows  by cores                                                │ WSL  by cores?, guest's count · dxg: no usage measured
   cores     pid name                         thr WS GiB?        │   cores?     pid name                     thr RSS GiB? st dxg
▶  13.25   31000 cpu_burn.exe                  21     0.1        │    19.72    4121 cpu_burn                  21      0.1  R
    5.76    7120 vmmemWSL                      22    10.1 WSL VM │     0.10    4388 python3                   12      2.0  S holder
    0.42       4 System                       310     0.0        │     0.04    2000 node                      11      0.4  S
    0.31   11736 vmwp.exe                      40     0.1 WSL VM │     0.03    2001 sshd                       1      0.0  S
    0.22    9001 카카오톡.exe                  60     0.4        │     0.02    2002 systemd                    1      0.0  S
    0.20   10000 svchost.exe                   12     0.0        │     0.01    2003 dbus-daemon                1      0.0  S
6 of 46                                                          │ 6 of 6 · 64 in the guest
Windows · cpu_burn.exe 31000 · cores                                                                  avg 7.73  max 13.25  scale 20
 20 │                                                      ⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀
  0 │⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
1 overview  2 gpu  3 cpu  4 mem  5 disk  6 net  7 procs  8 vm cost                            ↑↓ select  tab list  ←→ 1 min  q quit
```

100 by 34, after `tab`: focus on WSL, so its heading is lit and `▶` is in its list:

```
winwsltop  21:43:51  data 0.4 s old  every 1.0 s
CPU ──────────────────── ? not yet checked · two systems' own views of overlapping work, never added
100%  WINDOWS  user 65 · kernel 6 · VM 29        │ 100%  WSL  user 99 · sys 1 · iowait 0, as idle
      whole machine, includes the WSL VM         │       guest view, inside the VM · Ubuntu-24.04
VM ran 5.76 of 20 threads · ≈41 ready, 13 VM     │ WSL short ≈14 of 20 vCPUs, inferred · load 19.8
100 │                  ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿ │ 100 │                  ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
    │                 ⣤⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿ │     │                 ⣤⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
  0 │⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿ │   0 │⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
kern│███████████████████████████████████████████ │ iowt│███████████████████████████████████████████
VM  │███████████████████████████████████████████ │ lost≈                 ██████████████████████████
threads · 20 of 20 ≥90%     idle █████ ≥90%      │ vCPUs, not Windows cores · 20 of 20 ≥90%
P0  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v0  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
P1  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v2  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
P2  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v4  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
P3  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v6  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
P4  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v8  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
P5  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v10 │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
E0  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v12 │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
E2  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v14 │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
E4  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v16 │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
E6  │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀ │ v18 │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀
     -60s ───── busy avg 60% max 100% ────── now │      -60s ────── busy avg 60% max 100% ────── now
Windows  by cores                                │ WSL  by cores? · dxg: no usage measured
   cores     pid name         thr WS GiB?        │   cores?     pid name      thr RSS GiB? st dxg
   13.25   31000 cpu_burn.exe  21     0.1        │ ▶  19.72    4121 cpu_burn   21      0.1  R
    5.76    7120 vmmemWSL      22    10.1 WSL VM │     0.10    4388 python3    12      2.0  S holder
    0.42       4 System       310     0.0        │     0.04    2000 node       11      0.4  S
    0.31   11736 vmwp.exe      40     0.1 WSL VM │     0.03    2001 sshd        1      0.0  S
    0.22    9001 카카오톡.exe  60     0.4        │     0.02    2002 systemd     1      0.0  S
5 of 46                                          │ 5 of 6 · 64 in the guest
WSL · cpu_burn 4121 · cores?, guest's count                           avg 11.50  max 19.72  scale 20
 20 │                                        ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
  0 │⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
1 2 3 cpu 4 5 6 7 8                                            ↑↓ select  tab list  ←→ 1 min  q quit
```

`avg 7.73`: `cpu_burn.exe` read 13.25 cores in the 35 samples since the onset and 0 in the 25 before. At 100
the onset cell of the busy plots reads `⣤`, the mean of a quiet and a busy second (part 5).

### State and cost

| | |
|---|---|
| Memory | the ring above, about 1.9 MB here, 4.9 MB at 1024 slots |
| Push | about 400 lookups, 800 value writes and the NaN writes, a second. Budget under 0.5 ms with MSVC `/O2`, measured by `render_check measure` |
| Frame | one history over at most 601 samples. Budget 5 ms, now 0.32 to 0.69 ms |
| Bytes | about the RAM plots' it replaces, plus one background run; under 48 KiB on the noisy fixture |

### render_check must check

- The ring: a process gone for `kKeepMs` frees its slot; at the cap the least recently seen slot is reused,
  a selected one never, and no process is refused; a slot that missed a sample reads NaN there after the
  ring wraps, not its old value; a stall of `kKeepMs` clears the slots; a backward clock step keeps a slot's
  values in their samples; a reused pid with the same name and another start gets a new slot; a first
  reading (`cores_known` or `rated` false) is NaN; a repeated guest line carries once and a second repeat is
  a gap; a stale line is a gap; pid 0 gets no slot.
- Selection: default first row; a row that leaves falls back to the first and comes back when it returns;
  exactly one `▶` on the panel, in the focused list; the history's title starts with the focused list's
  name and names the selected pid.
- Scrolling on 46 rows in 5: `↓` 4 times keeps row 1 drawn first; the fifth draws rows 2 to 6 with the
  selection last; `↓` at 46 stays; `↑` from 46 keeps the selection on the bottom row until row 5, then the
  list is drawn from row 1; `5 of 46` throughout; `· 64 in the guest`; no heading says `top`.
- The history at 100 by 34 and 131 by 40 for all four fixtures: `scale 20` for the burner, `scale 0.05` for
  an idle process, never a 0 where the slot is NaN; `guest's count` in a WSL title.
- Rows: 5 list rows at 34 and 6 at 40; at heights 17 to 66 nothing overlaps; hostile names in the title are
  made safe; frame bytes under budget; nothing on panel 3 says `RAM`; every `?` column carries `?`.

### Out of scope

Selection on panels 1 and 2, paging, per-thread rows, per-process GPU history (slice 2), opening a process
into its threads.

## 4. Panel 4 Memory

**Question:** where is physical memory going on each side, and how much of the machine does the VM hold?

### Layout

- **Heads**, one format in both columns: `SIDE  used U of T GiB · cache C`, then the side's label, then
  its other figures: commit on Windows, swap and `PSI mem 0.00% avg10` in the guest.
- **The VM on its own row**, full width, away from the guest's figures: `VM  vmmemWSL working set 10.1 GiB
  · Windows' view of the VM, included in WINDOWS`. Never compared with the guest's used plus cache: whether
  the guest's cache is held in that working set is unchecked ("Guest memory vs host" read the two minutes
  apart), and experiment 0 reads them in one sample.
- **Each side's used and cache over the range**, a 2-row plot each with a title giving `avg`, `max` and
  `range`. Memory zooms, as machtop's `sample_series` does at 246c2cb: spread is max(max - min, floor), the
  bottom max(0, min - 0.1 spread) and the top the bottom plus 1.2 spread, with a floor of 64 MiB here, so a few
  pages of noise do not fill the plot. Labels are GiB or MiB with the fewest decimals that tell bottom from top.
  This rule, as built in 01384a4, rules over the ranges in the mockups below. Those were computed by the same
  formula in a scratch script, but printed with decimals picked by hand, which no rule stated: the build prints
  `range 24.7–24.8 GiB` where the mockup has `24.75–24.82`. The guest's are
  from rows; Windows' carry `?`.
- **Each side's processes by memory**, Windows by working set and the guest by RSS, both `?`, with
  `N of M`, scrolling and `tab` as panel 3.
- **One history, full width**, following focus, titled `Windows · vmmemWSL 7120 · working set?`. Zooming,
  never narrower than 4 MiB, machtop's per-app floor. The default selection is the first row, which is
  `vmmemWSL` while the VM holds the largest working set, so the panel opens on the VM's history.

Rows: title, rule, 3 heads, the VM row, 2 used-and-cache blocks of 3 rows, list headings 2, `N of M`, the
history's title, the footer: 17 fixed. The history takes `clamp((height - 16) / 3, 2, 12)` plot rows and
the lists the rest, but a list keeps at least 3 rows and the history gives way first. At 40: 8 and 15; at
34: 6 and 11.

### Mockups

From `render_check busy` with `demo_minute`, the fixture with Windows memory and a guest line; its 5 s stall
shows as the gap in every history, and the 40 `svchost.exe` rows are the fixture's own. Its memory figures
do not change over the minute, so each zoomed plot is a flat line near its floor with the range printed.

131 by 40, default selection:

```
winwsltop  21:43:50  data 1.2 s old  every 1.0 s
MEMORY ────────────────────────────────────────────── ? not yet checked · two systems' own views of overlapping memory, never added
WINDOWS  used? 24.7 of 63.7 GiB · cache? 12.0                    │ WSL  used 7.3 of 31.2 GiB · cache 7.0
         whole machine, includes the WSL VM                      │      guest view, inside the VM · Ubuntu-24.04
         commit? 31.0 of 80.0 GiB                                │      swap? 0.0 GiB · PSI mem 0.00% avg10
VM  vmmemWSL working set 10.1 GiB · Windows' view of the VM, included in WINDOWS
used? avg 24.8 max 24.8 · range 24.75–24.82 GiB                  │ used avg 7.3 max 7.3 · range 7.28–7.35 GiB
    │                                                            │     │
    │⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀     ⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀ │     │⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀     ⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀
cache? avg 12.0 max 12.0 · range 11.99–12.07 GiB                 │ cache avg 7.0 max 7.0 · range 6.99–7.07 GiB
    │                                                            │     │
    │⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀     ⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀ │     │⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀     ⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀
Windows  by working set?                                         │ WSL  by RSS?
   WS GiB?     pid name                             cores        │   RSS GiB?     pid name                                       st
▶    10.15    7120 vmmemWSL                          3.10 WSL VM │       6.00    4121 python3                                    R
      7.00    9002 Game.Shipping-Win64-Test-with-a…  2.84        │       2.26    2000 python3                                    S
      0.38    9001 카카오톡.exe                      0.22        │       2.18    2001 ollama                                     S
      0.08   11736 vmwp.exe                          0.31 WSL VM │       2.10    2002 node                                       S
      0.04   10000 svchost.exe                       0.20        │       2.01    2003 cargo                                      S
      0.04   10004 svchost.exe                       0.20        │       2.00    4388 한글추론기                                 S
      0.04   10008 svchost.exe                       0.19        │       1.93    2004 clang++                                    S
      0.03   10012 svchost.exe                       0.19        │       1.84    2005 rustc                                      S
      0.03   10016 svchost.exe                       0.18        │       1.76    2006 ld.lld                                     S
      0.03   10020 svchost.exe                       0.18        │       1.68    2007 sshd                                       D
      0.03   10024 svchost.exe                       0.18        │       1.59    2008 systemd                                    S
      0.03   10028 svchost.exe                       0.17        │       1.51    2009 dbus-daemon                                S
      0.03   10032 svchost.exe                       0.17        │       1.42    2010 python3                                    S
      0.03   10036 svchost.exe                       0.16        │       1.34    2011 ollama                                     S
      0.03   10040 svchost.exe                       0.16        │       1.26    2012 node                                       S
15 of 46                                                         │ 15 of 29 · 64 in the guest
Windows · vmmemWSL 7120 · working set?                                                  avg 10.15  max 10.15  range 10.14–10.22 GiB
10.2│
    │
    │
    │
    │
    │
    │
10.1│⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶           ⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶⣶
1 overview  2 gpu  3 cpu  4 mem  5 disk  6 net  7 procs  8 vm cost                            ↑↓ select  tab list  ←→ 1 min  q quit
```

100 by 34, after 16 presses of `↓`: the 17th row is selected, the list has scrolled and the selection sits on
its last row shown:

```
winwsltop  21:43:50  data 1.2 s old  every 1.0 s
MEMORY ─────────────── ? not yet checked · two systems' own views of overlapping memory, never added
WINDOWS  used? 24.7 of 63.7 GiB · cache? 12.0    │ WSL  used 7.3 of 31.2 GiB · cache 7.0
         whole machine, includes the WSL VM      │      guest view, inside the VM · Ubuntu-24.04
         commit? 31.0 of 80.0 GiB                │      swap? 0.0 GiB · PSI mem 0.00% avg10
VM  vmmemWSL working set 10.1 GiB · Windows' view of the VM, included in WINDOWS
used? avg 24.8 max 24.8 · range 24.75–24.82 GiB  │ used avg 7.3 max 7.3 · range 7.28–7.35 GiB
    │                                            │     │
    │⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀   ⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀ │     │⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀   ⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀
cache? avg 12.0 max 12.0 · range 11.99–12.07 GiB │ cache avg 7.0 max 7.0 · range 6.99–7.07 GiB
    │                                            │     │
    │⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀   ⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀ │     │⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀   ⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀
Windows  by working set?                         │ WSL  by RSS?
   WS GiB?     pid name             cores        │   RSS GiB?     pid name                        st
      0.04   10008 svchost.exe       0.19        │       6.00    4121 python3                     R
      0.03   10012 svchost.exe       0.19        │       2.26    2000 python3                     S
      0.03   10016 svchost.exe       0.18        │       2.18    2001 ollama                      S
      0.03   10020 svchost.exe       0.18        │       2.10    2002 node                        S
      0.03   10024 svchost.exe       0.18        │       2.01    2003 cargo                       S
      0.03   10028 svchost.exe       0.17        │       2.00    4388 한글추론기                  S
      0.03   10032 svchost.exe       0.17        │       1.93    2004 clang++                     S
      0.03   10036 svchost.exe       0.16        │       1.84    2005 rustc                       S
      0.03   10040 svchost.exe       0.16        │       1.76    2006 ld.lld                      S
      0.03   10044 svchost.exe       0.16        │       1.68    2007 sshd                        D
▶     0.03   10048 svchost.exe       0.15        │       1.59    2008 systemd                     S
11 of 46                                         │ 11 of 29 · 64 in the guest
Windows · svchost.exe 10048 · working set?                   avg 26.7  max 26.7  range 26.3–31.1 MiB
 31 │
    │
    │
    │
    │
 26 │⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤        ⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤
1 2 3 4 mem 5 6 7 8                                            ↑↓ select  tab list  ←→ 1 min  q quit
```

The same history after `←`, at 5 minutes, all its rows: the unwatched `·` before the 61 s the fixture holds,
and the bottom row the same as above:

```
 31 │
    │
    │
    │
    │
 26 │· · · · · · · · · · · · · · · · · · · · · · · · · · · · · · · · · · · · · ·⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤ ⣤⣤⣤⣤⣤⣤⣤
```

### State and cost

The ring of part 3, plus 5 floats a sample for the side figures (Windows used and cache, `vmmemWSL` working
set, guest used and cache): 12 KB. Four 2-row plots and one history a frame.

### render_check must check

- The labels `whole machine, includes the WSL VM` and `guest view, inside the VM`; `?` on Windows used,
  cache, commit, working set, guest swap and RSS, and on nothing else; never a line holding a Windows and a
  guest GiB figure; the VM row on its own line.
- Lists sorted by memory then pid; pid 0 left out; `WSL VM` on `vmmemWSL` and `vmwp.exe`.
- After 16 `↓` at 100 by 34 the list is drawn from row 7 with the 17th last and marked, `11 of 46` under it,
  and the history's title names pid 10048.
- A zoomed plot prints `range` and never draws under its floor; a flat series draws one line, not blank.
- WSL stopped: the right column says why, and the footer drops `tab`.
- Sizes 1 to 300 by 1 to 66 for `busy`, `bare` and hostile names; frame bytes under budget.

### Out of scope

Private bytes and commit per process; growth over the window (machtop's panel 4 has it, and how growth is
measured is undecided); a sum of Windows used and guest used, which counts the guest twice.

## 5. Graphs: scale, mean cells, `avg` and `max`

**Question:** over the range I chose, how loaded was this, typically and at worst?

### Decision, from machtop 246c2cb

1. **A cell is the mean of the samples in its slice of the window; a sample stands for the second before
   it.** NaN never counts. This replaces the highest sample on the 60 s panels and the peak on the large
   layout's busy plots; the heat block and the lanes already take the mean.
2. **`avg` and `max` cover the whole window**, worked out from the samples, not the cells, so a max can sit
   above every cell. Never the minimum. In the graph's unit. With no sample in the range, no label.
3. **Scale by measure.** A measure with a real ceiling is drawn 0 to 100%. Any other is drawn from 0 to the
   window's maximum rounded up to 1, 2 or 5 times a power of ten, never below its floor, and prints
   `scale` and that top; a graph that zooms prints `range` and its bottom and top.

| measure | scale | floor |
|---|---|---|
| Windows busy, guest busy, GPU utilization, the VM and Windows split, GPU memory of the device, power of its limit | 0 to 100% | |
| A process's cores, either side | 0 to `scale` | 0.05 cores, machtop's per-app CPU |
| A process's working set or RSS | zoom, `range` | 4 MiB, machtop's per-app memory |
| `vmmemWSL` working set; Windows and guest used and cache | zoom, `range` | 64 MiB: whole-machine figures move by tens of MiB, and 4 MiB would draw that as a cliff |
| Heat block, lanes | the ramp, unchanged | |

4. **The cell holding the window's maximum** gets one more dot at the maximum's height, in the plot's own
   tone, costing no row (machtop `docs/panels.md` (246c2cb), "Shared grammar", the graph-cell
   bullet; proposed here, winwsltop builds it first). Only when that cell holds more than one sample, and
   only when the dot falls above the dot row the cell's mean reaches; if several cells hold the maximum, the
   latest one. No other cell gets a dot. At 10 minutes on a 43-cell plot a cell holds 14 samples, so a
   one-second spike to 100% has a mean of about 7, the lowest dot row, and would read as idle; the `max`
   label says how high, the dot says when.
5. **Over 100.** Shared rule (246c2cb, the over-100 bullet): on a 0-100% graph a value above the ceiling is
   drawn at the ceiling, and its label prints the measured number in `watch`; the graph clamps, the number
   does not, as `contracts/ui.md` rule 5 already says. The split is engine time grouped by side and can pass
   100 ("Shares add to 100%": no, in "Experiment 01 result"). **winwsltop's own:** the label also carries a
   `*`, and the same frame puts `*` after the caption `VM level, not exact shares`, which is why. A window
   with nothing over 100 shows neither.
6. The axis caption `peak over mean · 20 s` becomes `each cell 20 s`, and `collecting: N s of M s` goes: the
   `·` says it. That closes the `contracts/ui.md` open question on that caption.

`plot_columns` (`render.cpp` lines 811-834) keeps no count: it takes `std::max` into each dot column. It
gains a sum and a count per column; a sample adds its value to every column from the one after the previous
joined sample up to its own, which is the span it stands for, and each column is its sum over its count.
It also returns the column holding the window's maximum, for item 4. `bucket_cells` already takes the mean.

**The edge of the window.** A sample stands for the second before it, so a sample at exactly `-span` lies
outside the window: a sample at `t` is in when `end - t < span`. `plot_series`, `series_stats` and the large
layout's plots all follow this rule, so `avg`, `max` and the cells cover the same samples. For the same
reason **a joined empty cell takes the later sample** everywhere, as `plot_series` does: the later sample
covers the second the cell stands for. Both follow machtop.

**The caption's `*`** appears only when the label carrying `max …*` is drawn in the same frame. A label
dropped for width takes the caption's `*` with it, so a `*` never points at nothing.

### Where the label sits

| graph | label |
|---|---|
| 60 s panels: panel 2, the small panel 3 | its own axis row, between `-60s` and `now`, which costs no row |
| The large layout's busy plots | the column's axis row, with `each cell N s` after it where it fits |
| A history and panel 4's used and cache | its title |
| Heat block, lanes | none: their heads give the instant figure |

### Mockup

Panel 2's axis rows, from `render_check busy 131 40 gpu` and `100 34 gpu`, with `avg` and `max` over
`demo_minute`'s 55 samples in the window (61 seconds less the 5 s stall, less the sample at exactly
`-60s`, which lies outside: part 5, "The edge of the window"); its VM split peaks at 112:

131 by 40, rows 18 and 32:

```
    -60s ────── avg 59% max 95% ────── now      -60s ───── avg 37% max 112%* ───── now      -60s ────── avg 16% max 38% ────── now
    -60s ─────────────── avg 8.4 max 9.8 GiB ─────────────── now      -60s ──────────────── avg 129 max 182 W ──────────────── now
```

100 by 34, the WSL VM title's second row (row 4), then rows 11, 19 and 26:

```
                                                         VM level, not exact shares*
    -60s ───────── avg 60% max 95% ────────── now      -60s ──────── avg 37% max 112%* ───────── now
    -60s ───────── avg 16% max 38% ────────── now
    -60s ─────── avg 8.4 max 9.8 GiB ──────── now      -60s ──────── avg 129 max 182 W ───────── now
```

### Rule 8, proposed text

> Graphs have time on the x axis. A measure with a real ceiling (a percentage of one core, a device or the
> whole machine) is drawn 0 to 100; any other is drawn from 0 to the window's maximum rounded up to 1, 2 or
> 5 times a power of ten, never below its floor, and prints `scale` and that top, or zooms and prints
> `range`. A cell is the mean of the samples in its slice of the window. The cell holding the window's
> maximum, the latest if several do, also marks it with one dot at its height, when that cell holds more
> than one sample and the dot falls above the cell's mean. A value above a 0-100 graph's ceiling is drawn at
> the ceiling and labelled as measured, in the watch tone. A measured 0 is the bottom dot row (⣀), a missing sample or a stall is
> a blank gap, never a zero, and time from before watching began is a faint `·` in every other cell of the
> bottom row. Each graph is labelled with the average and maximum of its samples over the window, never the
> minimum. Panels are never stacked or summed, and each graph title names its source.

The large-layout exception keeps its repeat rule and drops "the plots a peak".

### render_check must check

- 10 minutes with one 100 among 0s: the cell is under 100, the label says `max 100`, and that cell has the
  extra dot; at 1 minute no extra dot appears; two cells holding the maximum put it in the later one; a
  cell whose mean already reaches the maximum's dot row gets none; no other cell ever has one.
- `plot_columns`: two samples in one column give their mean; a joined gap of two columns takes the later
  sample in both.
- `avg` and `max` against values worked out by hand, including over 100; with `demo_minute` the cell at 112
  is drawn at the ceiling, `max 112%*` is in `watch` and the caption has its `*`; neither with every split
  value at or under 100.
- `scale 20` for 13.25 cores, `scale 0.05` for 0.01; `range` on every zoomed plot.
- No label when every sample in the range is a gap; no `min`, `peak over mean` or `collecting:` anywhere.
- Labelled axis rows keep `-60s`, `-5m`, `-10m` and `now`, and drop the label before either end.

### Out of scope

Labels on the heat block and the lanes.

## 6. Costs against the budgets

| budget (`contracts/ui.md`, `design/cpu-panel.md` section 8) | after this slice |
|---|---|
| Frame bytes, 131 by 40, noisy fixture: under 48 KiB, now 33392 | about the same: the history replaces the RAM plots. Asserted |
| Frame build: under 5 ms, now 0.32 to 0.69 ms | plus one history and, on panel 4, four small plots. Measured |
| History: one flat ring, no per-second allocation | 601 samples, 377 KB, plus process slots, about 1.9 MB here, allocated in chunks of 64 |
| Own CPU: no more than 0.5% of a core above the old build | the push's lookups and writes. Measured live, two runs |
| New PDH counters | none |
| Collector line: under 8 KiB, see `contracts/collect.md` | two keys a process, `rated` and `start`: 796 bytes more at 30 rows, 4205 to 4213 bytes in all, as built (e37993d) |

## 7. Build plan

Small commits, each with `scripts/check.sh` and `--windows`. A commit that changes something both sides use
is one commit, as `design/cpu-panel.md` commit 2 was. `render_check.cpp` includes `render.cpp`, `history.h`
and `keys.h`, so every new piece of ui code is in what it checks.

| # | owner | change |
|---|---|---|
| 0 | evidence | Experiment 0 (part 1). The owner decides whether it runs. It may run beside the rest, and each row it lands is a one-line ui change |
| 1 | core, collect | **The seam** (part 3): `process_row` `cores_known` and `create_time`; `PROTOCOL.md` `rated` and `start`; `collector.py` and its embedded copy; `processes.cpp`; `wsl_view`; `jsonl.cpp`, `json-output.md`, `check_json.py`. Additive, `schema` stays 1. Before the ring |
| 2 | ui | Range: `kKeepMs` 600000, `kCapacity` 601, ranges 1, 5 and 10 minutes, passed to `plot_columns` and the 60 s `unwatched_cells`. Part 5: mean cells and the sum and count in `plot_columns`, the maximum's dot, scale by measure, `avg` and `max`, the `*`, the captions. `t` steps the new ranges until commit 5 |
| 3 | ui | `src/ui/keys.h`, header-only; `console::poll()` returning a `key_event`, beside `poll_key` until commit 5. Key table checks |
| 4 | ui | `view_state`, `apply_key`, the footer, the unbuilt pages, `render()` taking `view_state` with the old overload kept forwarding |
| 5 | ui, core | `main.cpp` on `console::poll()` and `apply_key`, help text, `t` gone; the old overload, `poll_key`, `next_span`, `span_key_acts` and `render_options` removed. A fresh auditor reads 1 to 5 |
| 6 | ui | The `?` marks on panels 1 and 3; the fixture amendment (burners 0 in `quiet_moment`); the per-process ring; panel 3's selection, scrolling, one history; RAM out |
| 7 | ui | Panel 4 |
| 8 | core | `README.md` "Use"; `contracts/ui.md` rule 8, decisions and state; `design/cpu-panel.md` notes; `panels.md`; `ROADMAP.md` 2; `TODO.md`. A fresh auditor reads the whole |

This slice touches both seams, `snapshot.h` and `PROTOCOL.md`, in commit 1, and the record gains two keys.
`--text` is unchanged.

## 8. Open questions

**Answered by machtop (246c2cb), now in the design:** one history, full width, following focus (part 3);
a process keyed by pid and name; scale by measure (part 5); mean cells and window-wide `avg` and `max`;
Shift+Tab does nothing; the `faint` tone for panels not built.

Answered by machtop (246c2cb), 2026-11-02:

1. The dot at the window's maximum is shared grammar, with two additions now in part 5, item 4: the latest
   cell on a tie, and no dot at or under the mean's dot row.
2. Over 100 is shared: drawn at the ceiling, labelled as measured in `watch`. The `*` stays winwsltop's own.

Decided by the owner, 2026-11-02:

3. Figures already on screen without a ledger row are shown, marked `?` (part 1), not hidden.
4. Experiment 0 runs first, before any commit of section 7 that builds on its rows.
5. The seam change is additive flags; `schema` stays 1 (part 3).
6. Panel 1 gets no selection and panel 2 waits for slice 2 (section 0). Accepted as proposed.
7. Panel 3 at 34 rows shows 5 list rows (part 3).

8. Alt+key decodes as `esc` then the key (part 2), so Alt+Q quits. Closed under the owner's standing decision
   that keys follow machtop and sshvllmtop's shared grammar (`panels.md`, Decisions): the source is machtop's key parser (`src/keys.cpp` at 246c2cb, `key_parser::next`: an ESC followed by any byte but `[` or `O` is `esc` on its own, so Alt+key, which a terminal sends as ESC then the key, reads as `esc` then the key); the grammar's text does not state it yet.

Decided by the orchestrator, 2026-11-02, after the audit of commits 1 to 5, following machtop:

10. A sample stands for the second before it: one at exactly `-span` is outside (`end - t < span`), in
    `plot_series`, `series_stats` and the large layout alike, and a joined empty cell takes the later sample
    (part 5).
11. Alt+arrow is the arrow alone, the exception to item 8, because a terminal sends it as one CSI sequence
    and machtop's parser reads it so. `VK_PACKET` and ASCII through the character path are out of scope for
    slice 1 (part 2, Decoding).
12. The footer offers `←→` only when a graph is drawn, and the caption's `*` appears only when its label is
    drawn (parts 2 and 5).

Still open:

9. Unchecked live: the IME holding a lone `ㅂ`, `esc` and the arrows over ConPTY and OpenSSH, and the
   numpad with NumLock off. A person runs each key at 100 by 34 and 131 by 40.

## 9. Changes needed in other files

Written as requests; this file changes nothing else.

| file | change |
|---|---|
| `docs/contracts/ui.md` | Decision entries: the range replacing `t`; `tab`, focus, scrolling and the one history; the selection key; the ring, its identity, its cap and eviction; scale by measure, mean cells, the maximum's dot, `avg` and `max`, the `*`, with rule 8's new text; the caption removed; the `?` mark; panel 4. Supersede the 2026-11-01 entries "Where several samples share a column the highest is drawn" and the keys entry for `1`, `2`, `3`, `q`. Close the open question on the `collecting` caption |
| `docs/contracts/core.md` | Decision entry for the seam change of commit 1 |
| `docs/contracts/collect.md` | `rated` and `start` in the collector; `cores_known` and `create_time` in `processes.cpp` |
| `docs/design/cpu-panel.md` | Notes at sections 4, 6 and 11 that this file supersedes them; RAM moved to panel 4; 5 list rows at 34 |
| `docs/panels.md` | The one history, scale by measure, the `?` mark, panel 4 as in part 4 |
| `docs/ROADMAP.md` | Section 2: per-process history built for cores and memory |
| `docs/TODO.md` | New item: the figures of part 1's table are on screen with no ledger row (`CLAUDE.md` rule 2), as item 20 was for disk and network; owner collect and evidence; closed by experiment 0 |
| `docs/contracts/evidence.md` | Experiment 0 as a request |
| `README.md` | "Use": keys `1`-`8`, `←`/`→`, `↑`/`↓`, `tab`, `esc`, `q`; `t` gone; panel 4; what `?` means |
