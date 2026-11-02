# Panel grammar, slice 1

Status: PROPOSED, 2026-11-02, for review. Nothing here is decided until `contracts/ui.md` records it.

The shared grammar is machtop's `docs/panels.md` (outside this repository), which the owner made
authoritative on 2026-11-02: where it is clear, winwsltop follows it, and `design/cpu-panel.md`'s
refusals of list focus, the `s` sort and per-process history are superseded. [`../panels.md`](../panels.md)
is winwsltop's reading of it and is being corrected in parallel, so every claim below was checked
against [`../measurable.md`](../measurable.md) and the code, not taken from it. Rows are cited by claim.

## 0. The slice, and what it cuts

| part | in this slice |
|---|---|
| 1. Keys and footer | Yes, on every panel |
| 2. Selection, focus and the selected row's history | Panel 3 only, both layouts, with a per-process ring |
| 3. Panel 4 Memory | Yes, from the rows that exist, with what it waits for named on the panel |
| 4. Graph labels, average and maximum | Yes, every braille graph, and rule 8 changes |

Cut from the brief, and why:

- **Panel 1 gets no selection.** machtop's overview is a summary, not a list to select in
  (`list_rows` returns nothing for it, and its footer omits `↑↓` there), and winwsltop's target panel 1
  has no process lists at all ([`../panels.md`](../panels.md), 1, "Not here"). Adding focus to two lists
  that the next slice removes is wasted work.
- **Panel 2 gets no selection yet.** Its lists sit under five graphs and are 3 and 2 rows long in the
  `busy` fixture. Making it list-on-top means re-laying the panel, and ROADMAP 1 (Windows holders first,
  `unattributed` between device and split) re-lays it too. Doing both at once, in slice 2, lays it out
  once. `←`/`→` and the labels of part 4 do reach panel 2 now.
- **`s` sorts nothing yet.** In machtop `s` acts on panel 7 only; panels 3 and 4 sort on the one figure
  their question is about. Panel 7 is not built, so `s` and `ㄴ` are decoded and tested but act nowhere,
  and no footer offers them.
- **Panels 5 to 8 are not built.** Their keys switch to a page that says so (part 1).
- **The contention verdict is left out**, as the owner asked: its threshold is open.

## 1. Keys and footer

**Question:** what can I do on this panel, and where else can I go?

### Keys

| key | does | where |
|---|---|---|
| `1`-`8`, numpad 1-8 | Switch panel. 5-8 show a "not built yet" page | everywhere |
| `↑` `↓` | Move the selection one row in the focused list. Clamped at the first and last row shown, no wrap, no scrolling | panels 3, 4 |
| `←` | Range further back: 1 → 5 → 10 minutes, clamped | panels with a graph: 2, 3, 4 |
| `→` | Range nearer: 10 → 5 → 1 minute, clamped | same |
| `tab` | Focus the next selectable list on the panel, wrapping. With one selectable list, nothing | panel 3, and only while both sides' lists are selectable |
| `s`, `ㄴ` | Nothing in this slice (panel 7) | none |
| `esc` | Panel 1. Each panel keeps its focus and its selections; the range is kept | everywhere |
| `q`, `ㅂ` | Quit. Ctrl+C still quits | everywhere |
| `t` | Nothing: the range replaces it | none |
| Shift+Tab | Nothing, as in machtop, whose parser drops `ESC [ Z` | none |

The range is one setting for the whole screen, as machtop's `view_state.range` is. Copied from machtop:
`←` is further back, `→` nearer, both clamped (machtop `apply`).

**Decoding.** winwsltop reads key records from `CONIN$` with VT input off (`console.cpp`), so `←` arrives
as `VK_LEFT` and `esc` as `VK_ESCAPE`, never as bytes. A bare `esc` cannot be mistaken for the start of
an arrow's sequence, so the shared grammar's short delay is met by construction and no timer is added.
Where bytes do arrive, over ConPTY or OpenSSH, the console host's own input parser turns them into key
records before winwsltop reads them; that path is not checked (section 7).

A key is decided by its virtual key code first, as today. If that gives nothing, as under an IME that
reports `VK_PROCESSKEY`, the character decides, through machtop's 2-set Korean table (`keys.cpp`
`kJamo`): U+3142 `ㅂ` is `q`, U+3134 `ㄴ` is `s`. Ctrl or Alt held means no key, as today. Shift is
allowed, because some layouts need it for digits, except with Tab.

The decoder becomes a pure function in a new `src/ui/keys.h`, with no Windows header, so `render_check`
can run it on Linux too: `key_event key_for(uint16_t vk, char32_t ch, uint32_t modifiers)`. Applying a
key also becomes pure, `apply_key(view_state&, key_event, snapshot, history, width, height)` in
`render.h`, so the behaviour `handle_keys` holds today, which has no automated check
(`contracts/ui.md`, Current state), is checked. `main.cpp` keeps the polling, the redraw on a key and
the quit.

### Footer

machtop's `footer()`: the panels on the left, the keys that act on this panel on the right. When both
do not fit, every panel but the current one shrinks to its number. Labels are machtop's short ones,
with winwsltop's own 8: `overview gpu cpu mem disk net procs` and `vm cost`. Three tones: the current
panel lit (`title`, bold), a built panel `dim`, a panel not built yet `faint`. The right side lists
only keys that act here, with the range's current value.

Footers generated from those rules, in order: panel 1 at 131 and 100 columns, panel 3 at 131 and 100,
panel 4 at 100, panel 2 at 80:

```
1 overview  2 gpu  3 cpu  4 mem  5 disk  6 net  7 procs  8 vm cost                                                           q quit
1 overview  2 gpu  3 cpu  4 mem  5 disk  6 net  7 procs  8 vm cost                            q quit
1 overview  2 gpu  3 cpu  4 mem  5 disk  6 net  7 procs  8 vm cost                            ↑↓ select  tab list  ←→ 1 min  q quit
1 2 3 cpu 4 5 6 7 8                                            ↑↓ select  tab list  ←→ 1 min  q quit
1 overview  2 gpu  3 cpu  4 mem  5 disk  6 net  7 procs  8 vm cost       ↑↓ select  ←→ 1 min  q quit
1 2 gpu 3 4 5 6 7 8                                             ←→ 1 min  q quit
```

Tones the text cannot show, on panel 3's lines: `3 cpu` is lit; `1`, `2` and `4` are `dim`; `5` to `8` are `faint`.

A panel that is not built, at 100 by 34 (title, then this, then the footer):

```
DISK ──────────────────────────────────────────────────────────────────────────────── not built yet

Which disk is busy, and is it the WSL disk? The disk rows are on panel 1 until this panel is built.
```

Each of 5 to 8 carries its question from [`../panels.md`](../panels.md) and where its figures are today:
5 and 6 point to panel 1, 7 to panels 1 and 3, 8 to panels 2 and 3.

### State and cost

`view_state` in `render.h`, replacing `view` and `render_options`: the panel, the range index, and per
panel the focused list and one selection key per list (part 2). Under 1 KiB, nothing per second. A key
redraws the last snapshot at once, as `1`-`3` do today. Frame cost: none beyond the footer's own text.

### render_check must check

- `key_for`: every row of the key table, numpad digits, `9` and `0` give nothing, `T` gives nothing,
  Ctrl+Q and Alt+Q give nothing, Shift+1 gives `1`, Shift+Tab gives nothing, `VK_PROCESSKEY` with U+3142
  gives `q` and with U+3134 gives `s`, and every other jamo maps as machtop's table does.
- `apply_key` sequences: `3 tab tab` returns focus to Windows; `3 tab esc 3` keeps WSL focused; `↓↓↓ esc 3`
  keeps the third row; `←←←` stops at 10 minutes and `→→→` at 1; `tab` on panels 1, 2 and 4 changes
  nothing; `tab` on panel 3 with WSL unavailable changes nothing; `↑` on the first row stays.
- Footer: at widths 1 to 300 on every panel, never wider than the screen, the current panel always
  named, `tab list` only on panel 3 with two selectable lists, `←→` only where a graph is drawn, no `[t]`,
  and `s` nowhere.
- `--text` is unchanged: no history, no footer.

### Out of scope

Mouse, scrolling a list past the rows shown, a help overlay, saving the range between runs, Shift+Tab.

## 2. Selection, focus and the selected row's history

**Question:** what has this one process been doing over the last minutes, on its own side?

Panel 3 is where both sides' process lists already sit side by side (`contracts/ui.md` rule 3), so it
takes the grammar first.

### Behaviour

- Each list keeps one selection, as a key: pid and name. machtop keys by name because its rows are apps
  grouped by name; winwsltop's rows are processes, and the `contended` fixture alone has 41 rows named
  `svchost.exe`. The name stops a reused pid from inheriting a selection.
- With no key, or a key whose row is not among the rows shown, the selection is the first row shown, as
  in machtop. Before any `↑`/`↓` the history therefore follows the busiest process. `↑`/`↓` set the key
  from the row the selection is on.
- Focus starts on Windows. The focused list's heading is lit and its selected row is marked `▶` on the
  selection background (machtop's `44;62;76`). The other list's heading is `dim`, and its selected row
  has no `▶` and no background.
- **Each column draws its own list's selected row** under its list. The focused column's history heading
  is lit and the other's `dim`. This is the one place the slice reads the grammar rather than copies it:
  machtop has one list per panel, so "the history below follows focus" has no two-column case there.
  Drawing one history across both columns would break `design/cpu-panel.md`'s rule that a column shares
  one time axis and Windows and WSL never share a graph. Open question 1 in section 7.
- Under the lists, per column: a heading, then a 2-row braille plot on the column's time axis, fixed 0 to
  100 percent of that side's threads (rule 8). The heading gives the name, pid and `avg` and `max` in
  cores over the range (part 4), and what 100 is: `of 20 threads` on Windows, `of 20 vCPUs` in the guest,
  shortened to `of 20` below 131 columns.
- Every list row gains a 2-column lead, `▶ ` or blank, taken from the name column, as machtop's
  `table_row` does. A `cores` of `100.00` still fits.

Windows per-process cores are a measurement (ledger, CPU contention checked, "Windows loses work to the
VM": the burner read 19.320 cores alone and 13.253 beside WSL), and they do not add up to busy (ledger,
CPU counters checked, "Process cores account for Windows busy": No), so a process's history is never
summed with another's or drawn against the side's total. A guest process's cores are what the guest
counted, not what it got (ledger, CPU contention checked, "Guest busy stays high when the host has taken
the vCPU": the burner's threads counted 19.718 cores while `Hv VP` ran 5.735), which is why its heading
says `vCPUs` and the head row above already says `WSL short ≈14`.

### The per-process ring: added in this slice

Panel 3's history and panel 4's both need it, and without it selection is decoration. Cost at 1 s × 10
minutes:

| item | size |
|---|---|
| One process slot: 601 samples × (cores, memory) as `float` | 4808 bytes |
| This machine: 360 Windows processes (ledger, "Windows collector checked against Windows") and at most 30 guest processes plus holders (`PROTOCOL.md`, `processes`) | about 400 slots, 1.9 MB |
| Cap | 1024 slots, 4.9 MB |
| The existing ring after the range change: 601 × 608 bytes (`sizeof(history_sample)` with g++) | 365 KB, down from 548 KB at 901 |

- The slots live beside the existing ring in `history`, indexed by the same ring position, so a stall, a
  clock step or a clear moves them with it at no cost. `kKeepMs` becomes 600000 and `kCapacity` 601,
  since the range now stops at 10 minutes.
- A slot is keyed by side, pid and a hash of the name; one hash lookup per process per sample. A slot no
  process has filled for `kKeepMs` is freed. Slots come in chunks of 64, allocated when needed and never
  moved, so there is no allocation per second.
- Windows: every row but pid 0, cores and working set. Guest: every row in the collector's line, cores
  and RSS, with the large layout's repeat rule (a repeated line carries its value once, a second repeat is
  a gap; ui rule 8's exception), and a gap from `kStaleS`. RSS is stored but not drawn until it has a
  ledger row (part 3).

### When the selected row has no history yet

| case | the history shows |
|---|---|
| Just launched | The plot's unwatched cells are faint `·`, as every graph's are; `avg` and `max` over what exists |
| Process first seen 40 s ago, range 5 minutes | Blank before it was first seen, which is a gap: watched, nothing to measure. Drawn from then on |
| Guest process outside the collector's top 30 for a while | Blank for those samples |
| `vmmemWSL` cores unknown (`vmmem_cores_known` false) | Blank, never 0 |
| 1024 slots in use | One `dim` line in place of the plot: `history not kept: over 1024 processes`. Never a flat line |
| WSL unavailable or stale | The WSL list says why, as today, nothing is selectable there, and `tab` does nothing |
| Window too small for the history | machtop's rule: the history is dropped before the lists go under 3 rows each; selection still shows |

### Mockups

From `render_check`'s `contended` fixture, as `render_check contended 131 40` and `100 34` print it
today, with RAM removed and the history added. Every plot is drawn by `render.cpp`'s own `make_axis`,
`bucket_cells` (mean, part 4) and `plot_block` on the fixture's history; the per-process values are what
`history::push` would store from the fixture's rows; widths and heights were checked by script. The
fixture's process rows do not change at the onset (`quiet_moment` leaves them), so both histories are
flat; `render_check` should give `cpu_burn.exe` and `cpu_burn` 0 cores in `quiet_moment`, and then they
show the step. Focus is on Windows.

131 by 40:

```
winwsltop  21:43:51  data 0.4 s old  every 1.0 s
CPU ─────────────────────────────────────────────────────────────────────── two systems' own views of overlapping work, never added
100%  WINDOWS  user 65 · kernel 6 · VM 29                        │ 100%  WSL  user 99 · sys 1 · iowait 0, as idle
      whole machine, includes the WSL VM                         │       guest view, inside the VM · Ubuntu-24.04
VM ran 5.76 of 20 threads · ≈41 ready, 13 VM                     │ WSL short ≈14 of 20 vCPUs, inferred · load 19.8
busy  avg 60%  max 100%                                          │ busy  avg 60%  max 100%
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
     -60s ──────────────── each cell 1.0 s ───────────────── now │      -60s ──────────────── each cell 1.0 s ───────────────── now
Windows  top 6 of 46 by cores                                    │ WSL  top 6 of 64 by cores, dxg: no usage measured
   cores     pid name                          thr WS GiB        │    cores     pid name                     thr RSS GiB st dxg
▶  13.25   31000 cpu_burn.exe                   21    0.1        │    19.72    4121 cpu_burn                  21     0.1  R
    5.76    7120 vmmemWSL                       22   10.1 WSL VM │     0.10    4388 python3                   12     2.0  S holder
    0.42       4 System                        310    0.0        │     0.04    2000 node                      11     0.4  S
    0.31   11736 vmwp.exe                       40    0.1 WSL VM │     0.03    2001 sshd                       1     0.0  S
    0.22    9001 카카오톡.exe                   60    0.4        │     0.02    2002 systemd                    1     0.0  S
    0.20   10000 svchost.exe                    12    0.0        │     0.01    2003 dbus-daemon                1     0.0  S
cpu_burn.exe 31000 · cores avg 13.25  max 13.25 of 20 threads    │ cpu_burn 4121 · cores avg 19.72  max 19.72 of 20 vCPUs
100 │⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀ │ 100 │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
  0 │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿ │   0 │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
1 overview  2 gpu  3 cpu  4 mem  5 disk  6 net  7 procs  8 vm cost                            ↑↓ select  tab list  ←→ 1 min  q quit
```

100 by 34:

```
winwsltop  21:43:51  data 0.4 s old  every 1.0 s
CPU ──────────────────────────────────────── two systems' own views of overlapping work, never added
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
     -60s ──────── each cell 1.4 s ───────── now │      -60s ──────── each cell 1.4 s ───────── now
Windows  top 6 of 46 by cores                    │ WSL  top 6 of 64 by cores, dxg: no usage measured
   cores     pid name          thr WS GiB        │    cores     pid name      thr RSS GiB st dxg
▶  13.25   31000 cpu_burn.exe   21    0.1        │    19.72    4121 cpu_burn   21     0.1  R
    5.76    7120 vmmemWSL       22   10.1 WSL VM │     0.10    4388 python3    12     2.0  S holder
    0.42       4 System        310    0.0        │     0.04    2000 node       11     0.4  S
    0.31   11736 vmwp.exe       40    0.1 WSL VM │     0.03    2001 sshd        1     0.0  S
    0.22    9001 카카오톡.exe   60    0.4        │     0.02    2002 systemd     1     0.0  S
    0.20   10000 svchost.exe    12    0.0        │     0.01    2003 dbus-dae…   1     0.0  S
cpu_burn.exe 31000 · avg 13.25 max 13.25 of 20   │ cpu_burn 4121 · avg 19.72 max 19.72 of 20
100 │⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀ │ 100 │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
  0 │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿ │   0 │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
1 2 3 cpu 4 5 6 7 8                                            ↑↓ select  tab list  ←→ 1 min  q quit
```

Against today's frames: head row 4 (`RAM 41.2/63.7 GiB  free 22.5 GiB`) and the 2-row RAM plots are gone,
3 rows. The history takes 3 rows under the lists. At 40 rows head row 4 becomes the busy plots' label row
(part 4), and the busy plots give one row, 9 to 8. At 34 the label row does not fit and is the first thing
dropped, so the lists keep their 6 rows, which the design guarantees. At 100 the onset cell reads `⣤`, the
mean of a quiet and a busy second, where today's peak drew `⣿` (part 4).

Tones: `Windows` and the left history heading lit, `WSL` and the right one `dim`; the `▶` row on the
selection background.

### State and cost

| | |
|---|---|
| Memory | the ring above, about 1.9 MB here, at most 4.9 MB |
| Push | about 400 hash lookups and 800 float writes a second. Budget: under 0.5 ms with MSVC `/O2`, measured by `render_check measure` |
| Frame | two `bucket_cells` passes over at most 601 samples. Budget: the 5 ms frame build, now 0.32 to 0.69 ms |
| Bytes | same rows as the RAM plots it replaces, plus one background run. Under 48 KiB on the noisy fixture |

### render_check must check

- The ring: a process seen, then gone for `kKeepMs`, frees its slot; 1025 processes give the
  `history not kept` line and no plot; a stall of `kKeepMs` clears slots with the ring; a backward clock
  step keeps a slot's values in their samples; a pid reused under another name starts a new slot; a
  repeated guest line carries once and the second repeat is a gap; a stale line is a gap; pid 0 never
  gets a slot.
- Selection: default is the first row; a key whose row leaves the shown rows falls back to the first and
  comes back when it returns; `▶` appears exactly once on the panel, in the focused list; the unfocused
  list has no `▶`; the history heading names the selected row's pid.
- The history: at 100 by 34 and 131 by 40 for all four fixtures; never a 0 where the slot is NaN; the
  unwatched `·` on a fresh start; `vCPUs` in the right heading and `threads` in the left at 131.
- Rows: at 34 the lists keep 6 rows; at heights 17 to 66 nothing overlaps; hostile names in the
  heading are made safe; frame bytes under budget.
- Nothing on panel 3 says `RAM`.

### Out of scope

Selection on panels 1 and 2, scrolling, per-thread rows, a history of GPU percent per process (slice 2,
with panel 2), and opening a process into its threads.

## 3. Panel 4 Memory

**Question:** where is physical memory going, on each side, and how much of the machine does the VM hold?

### What has a row, and what waits

| figure | shown | ledger |
|---|---|---|
| Windows total | yes | "Windows collector checked against Windows": total memory 63.75 GiB both |
| `vmmemWSL` working set | yes | same section: 10.15 GiB both |
| Per-process working set, Windows | yes, the list | the same read: `processes.cpp` fills every row's working set and `vm.vmmem_working_set` from one field, so the `vmmemWSL` check is a check of one row, not of many |
| Guest total, used (total less available), cache | yes | "WSL collector checked", "Guest memory vs host": read live, not checked against a second tool |
| Guest pressure, memory | yes | "CPU contention checked", "`/proc/pressure` memory and io": observed, 0 in every case |
| Windows available, commit, cache | **waits** | no row: only total is checked |
| Guest swap used | **waits** | no row |
| Guest per-process RSS | **waits** | no row |
| Windows private bytes | **waits** | no row, and not on screen today |
| Whether the guest's cache is held as `vmmemWSL` working set | **not shown** | the one reading read the two minutes apart ("Guest memory vs host") |

**How a waiting figure is shown:** its place holds `waits for a ledger row`, `dim`, never a number. The
WSL list is that sentence until RSS has a row, so panel 4 has one selectable list and no `tab`. The guest
and `vmmemWSL` sit in their own columns and are never compared on screen, so nothing needs to stand in
for the hypothesis.

Panel 1 shows Windows available, commit and cache, guest swap and guest RSS today, and panel 3 shows
Windows used and free, all without a row, against `CLAUDE.md` rule 2, as disk and network were before
`TODO.md` item 20. Section 8 asks for the item and the experiment that closes both. The experiment reads
only and starts no load. When its rows land, each `waits` turns into its number in the same place, and
the WSL list appears in the right column with the geometry of the left.

### Layout

Two columns, as panel 3: heads, then each side's list by memory, then the selected row's history,
as machtop's `list_body`: the history takes `clamp((height - head) / 3, 3, 12)` rows, here
`(height - 8)`, and none when the list would go under 5 rows. The default selection is the first row,
which is `vmmemWSL` whenever the VM is the largest working set, so the panel opens on the VM's
history. The plot is 0 to 100 percent of the side's total; its heading gives `avg` and `max` in GiB.

### Mockups

From `render_check busy` with `demo_minute`, the fixture with Windows memory and a guest line. Its 5 s
stall shows as the gap in the history. The 40 `svchost.exe` rows are the fixture's own.

131 by 40:

```
winwsltop  21:43:50  data 1.2 s old  every 1.0 s
MEMORY ────────────────────────────────────────────────────────────────── two systems' own views of overlapping memory, never added
63.7 GiB  WINDOWS  total                                         │ 7.3/31.2 GiB  WSL  used · cache 7.0 GiB
          whole machine, includes the WSL VM                     │               guest view, inside the VM · Ubuntu-24.04
VM  vmmemWSL working set 10.1 GiB, Windows' view                 │ pressure mem 0 · swap waits for a ledger row
available, commit, cache wait for a ledger row                   │
                                                                 │
Windows  top 18 of 46 by working set                             │ WSL  processes by RSS wait for a ledger row
   WS GiB     pid name                              cores        │
▶   10.15    7120 vmmemWSL                           3.10 WSL VM │
     7.00    9002 Game.Shipping-Win64-Test-with-a-…  2.84        │
     0.38    9001 카카오톡.exe                       0.22        │
     0.08   11736 vmwp.exe                           0.31 WSL VM │
     0.04   10000 svchost.exe                        0.20        │
     0.04   10004 svchost.exe                        0.20        │
     0.04   10008 svchost.exe                        0.19        │
     0.03   10012 svchost.exe                        0.19        │
     0.03   10016 svchost.exe                        0.18        │
     0.03   10020 svchost.exe                        0.18        │
     0.03   10024 svchost.exe                        0.18        │
     0.03   10028 svchost.exe                        0.17        │
     0.03   10032 svchost.exe                        0.17        │
     0.03   10036 svchost.exe                        0.16        │
     0.03   10040 svchost.exe                        0.16        │
     0.03   10044 svchost.exe                        0.16        │
     0.03   10048 svchost.exe                        0.15        │
     0.03   10052 svchost.exe                        0.15        │
                                                                 │
vmmemWSL 7120 · working set avg 10.15  max 10.15 GiB of 63.7     │
100 │                                                            │
    │                                                            │
    │                                                            │
    │                                                            │
    │                                                            │
    │                                                            │
    │                                                            │
    │                                                            │
    │⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤     ⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤⣤ │
  0 │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿     ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿ │
1 overview  2 gpu  3 cpu  4 mem  5 disk  6 net  7 procs  8 vm cost                                      ↑↓ select  ←→ 1 min  q quit
```

100 by 34:

```
winwsltop  21:43:50  data 1.2 s old  every 1.0 s
MEMORY ─────────────────────────────────── two systems' own views of overlapping memory, never added
63.7 GiB  WINDOWS  total                         │ 7.3/31.2 GiB  WSL  used · cache 7.0 GiB
          whole machine, includes the WSL VM     │       guest view, inside the VM · Ubuntu-24.04
VM  vmmemWSL working set 10.1 GiB                │ pressure mem 0 · swap waits for a ledger row
available, commit, cache wait for a ledger row   │
                                                 │
Windows  top 14 of 46 by working set             │ WSL  processes by RSS wait for a ledger row
   WS GiB     pid name              cores        │
▶   10.15    7120 vmmemWSL           3.10 WSL VM │
     7.00    9002 Game.Shipping-Wi…  2.84        │
     0.38    9001 카카오톡.exe       0.22        │
     0.08   11736 vmwp.exe           0.31 WSL VM │
     0.04   10000 svchost.exe        0.20        │
     0.04   10004 svchost.exe        0.20        │
     0.04   10008 svchost.exe        0.19        │
     0.03   10012 svchost.exe        0.19        │
     0.03   10016 svchost.exe        0.18        │
     0.03   10020 svchost.exe        0.18        │
     0.03   10024 svchost.exe        0.18        │
     0.03   10028 svchost.exe        0.17        │
     0.03   10032 svchost.exe        0.17        │
     0.03   10036 svchost.exe        0.16        │
                                                 │
vmmemWSL 7120 · avg 10.15 max 10.15 GiB of 63.7  │
100 │                                            │
    │                                            │
    │                                            │
    │                                            │
    │                                            │
    │                                            │
    │⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀   ⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀ │
  0 │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿   ⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿ │
1 overview  2 gpu  3 cpu  4 mem  5 disk  6 net  7 procs  8 vm cost       ↑↓ select  ←→ 1 min  q quit
```

At `←` to 5 minutes the same plot at 100 columns reads, from the same fixture, with the unwatched `·`
before the 61 s it holds:

```
100 │
  0 │· · · · · · · · · · · · · · · · · ⣀⣀⣀⣀⣀⣀⣀⣀⣀
```

### State and cost

The per-process ring of part 2, no more. The panel draws one history; its frame is smaller than panel 3's.

### render_check must check

- The labels `whole machine, includes the WSL VM` and `guest view, inside the VM`, and `waits for a
  ledger row` for each waiting figure at every size; no number for available, commit, cache, swap or RSS
  anywhere on panel 4; never one line holding both a Windows and a guest GiB figure.
- The Windows list sorted by working set, then pid; pid 0 left out; `WSL VM` on `vmmemWSL` and `vmwp.exe`.
- The default selection is the first row, and its history heading names it.
- WSL stopped: the right column says why; panel 4's footer never offers `tab`.
- Sizes 1 to 300 by 1 to 66 for `busy`, `bare` and hostile names; frame bytes under budget.

### Out of scope

The WSL list until RSS has a row; private bytes and commit per process; growth over the window
(machtop's panel 4 has it, and it needs a decision on how growth is measured); a sum of Windows used and
guest used, which counts the guest twice.

## 4. Graph labels: average and maximum

**Question:** over the range I chose, how loaded was this, typically and at worst?

### Decision

machtop's rule, copied:

1. **Every braille graph's cell, or dot column, is the mean of the samples in it.** NaN never counts.
   This replaces the highest sample on the 60 s panels and the peak on the large layout's busy plots.
   The heat block and the lanes already take the mean and are unchanged.
2. **Every braille graph is labelled `avg A max M`** over the selected range, worked out from the
   samples, not the cells, so a one-second spike the mean flattens still shows in `max`. Never the
   minimum. In the graph's own unit: percent, GiB, W, cores. Over 100 is printed as measured (rule 5):
   the `busy` fixture's VM split reads `max 112%`. With no sample in the range, no label, as machtop.
3. The axis caption `peak over mean · 20 s` becomes `each cell 20 s`, and `collecting: N s of M s` goes:
   the `·` says it, as in machtop. That also closes the `contracts/ui.md` open question on that caption
   after a stall.

Why mean: at 1 minute a cell holds about one sample, so nothing changes; at 10 minutes a 43-cell plot holds
14 samples a cell, where peak paints any one-second burst as 14 seconds of it, the reason the heat block
already takes the mean (`design/cpu-panel.md` section 5). `max` keeps the burst's height in words.

Where the label sits:

| graph | label |
|---|---|
| 60 s-style graphs: panel 2, the small panel 3 | the middle of its own axis row, which costs no row |
| The large layout's busy plots | the busy label row, head row 4's old place, from 35 rows |
| Histories of a selected row (parts 2 and 3) | its heading |
| Heat block, lanes | none: they are colour, and their heads give the instant figure |

### Mockup

Panel 2 is otherwise unchanged, so only its axis rows are shown, from `render_check busy 131 40 gpu` and
`100 34 gpu`, with `avg` and `max` from `demo_minute`'s 56 samples:

131 by 40, rows 18 and 32:

```
    -60s ────── avg 60% max 95% ────── now      -60s ───── avg 37% max 112% ────── now      -60s ────── avg 16% max 38% ────── now
    -60s ─────────────── avg 8.4 max 9.8 GiB ─────────────── now      -60s ──────────────── avg 129 max 182 W ──────────────── now
```

100 by 34, rows 11, 19 and 26:

```
    -60s ───────── avg 60% max 95% ────────── now      -60s ───────── avg 37% max 112% ───────── now
    -60s ───────── avg 16% max 38% ────────── now
    -60s ─────── avg 8.4 max 9.8 GiB ──────── now      -60s ──────── avg 129 max 182 W ───────── now
```

Panel 3's busy label row and history headings are in part 2's mockups.

### Rule 8, proposed text

> Graphs use a fixed 0-100 scale with time on the x axis. A cell is the mean of the samples in it; a
> measured 0 is the bottom dot row (⣀), a missing sample or a stall is a blank gap, never a zero, and time
> from before watching began is a faint `·` in every other cell of the bottom row. Each graph is labelled
> with the average and maximum of its samples over the range, never the minimum. Panels are never stacked
> or summed, and each graph title names its source.

The large-layout exception keeps its repeat rule and drops "the plots a peak".

### render_check must check

- A 10-minute range with one 100 among 0s: the cell is under 100 and the label says `max 100`.
- `avg` and `max` of a fixture against values worked out by hand, including over 100.
- No label when every sample in the range is a gap; no `min` anywhere; no `peak over mean` and no
  `collecting:` in any frame.
- The labelled axis rows keep `-60s`, `-5m`, `-10m` and `now`, and drop the label before either end.

### Out of scope

Labels on the heat block and the lanes; a scale other than 0-100 (open question 3).

## 5. Costs against the budgets

| budget (`contracts/ui.md`, `design/cpu-panel.md` section 8) | after this slice |
|---|---|
| Frame bytes, 131 by 40, noisy fixture: under 48 KiB, now 33392 | about the same: the history replaces the RAM plots row for row. Asserted |
| Frame build: under 5 ms, now 0.32 to 0.69 ms | plus two passes over 601 samples. Measured |
| History: one flat ring, no per-second allocation | 601 samples, 365 KB, plus about 1.9 MB of process slots here, 4.9 MB at most, allocated in chunks |
| Own CPU: no more than 0.5% of a core above the old build | the push's hash lookups. Measured live as before, two runs |
| New PDH counters, collector line | none, none |

## 6. Build plan

Small commits, each with `scripts/check.sh` and `--windows`. A commit that changes a signature both
sides use goes in one commit, owned by ui with core's file changed by request, as `design/cpu-panel.md`
commit 7 was.

| # | owner | change |
|---|---|---|
| 0 | evidence | Memory experiment, read only, no load: Windows available, commit and cache against `Get-Counter` and Task Manager's own figures, several processes' working set and private bytes against `Get-Process`, guest `/proc/meminfo` and swap against `free -b`, guest RSS against `/proc/pid/status`, and guest used plus cache beside `vmmemWSL` working set in one sample. Ledger rows. May run beside 1 to 5 |
| 1 | ui | Range: `kKeepMs` 600000, `kCapacity` 601, ranges 1, 5 and 10 minutes, the range passed to `plot_columns` and the 60 s `unwatched_cells`. Part 4: mean cells, `avg` and `max` labels, the captions. `t` steps through the new ranges until commit 4. `render_check` |
| 2 | ui | `src/ui/keys.h` and `.cpp`, `key_for` and the jamo table; `console::poll()` returning a `key_event`, beside `poll_key` until commit 4. `msvc.cmd` `SOURCES` by request to core. `render_check` key table |
| 3 | ui | `view_state`, `apply_key`, the footer, the unbuilt panels, `render()` taking `view_state`, with the old overload kept forwarding. `render_check` sequences and footers |
| 4 | ui, core | `main.cpp`: `handle_keys` on `console::poll()` and `apply_key`, help text, `t` gone. The old overload, `poll_key`, `next_span`, `span_key_acts` and `render_options` removed. A fresh auditor reads 1 to 4 |
| 5 | ui | The per-process ring; panel 3 selection, focus, the history, RAM out. `render_check` |
| 6 | ui | Panel 4 from the rows that exist when it is built. `render_check` |
| 7 | core | `README.md` "Use"; `contracts/ui.md` rule 8, decisions and state; `design/cpu-panel.md` superseded notes; `panels.md`; `ROADMAP.md` 2; `TODO.md`. A fresh auditor reads the whole |

`history.h` is ui's (`ARCHITECTURE.md`, `src/ui/`); nothing in this slice touches a seam, `snapshot.h`,
`PROTOCOL.md` or the record, and `--once`, `--record` and `--text` are unchanged.

## 7. Open questions

For machtop:

1. Two lists side by side: does each keep its own history, the unfocused one `dim` (this design), or
   does one history follow focus?
2. Selection key: name for apps, pid and name for processes. Worth one line in the shared grammar?
3. Scale: winwsltop's rule 8 is fixed 0-100; machtop's `sample_series` scales each series to a nice
   ceiling and labels it `scale`. The shared text says neither. A process at 0.2 of 20 cores is one dot
   row here.
4. A cell is the mean of its samples in machtop's code but not its text. Add it?
5. machtop's text has `tab`; its code does not yet (`apply` has no case and `key_parser` drops 0x09).
   Shift+Tab is dropped as `ESC [ Z`; intended?
6. Footer tones: winwsltop adds a third, `faint` for a panel not built. machtop shows its unbuilt GPU panel
   like any other.

For the owner:

7. Panel 1 gets no selection and panel 2 waits for slice 2 (section 0).
8. Panel 4 shows no Windows available, commit or cache and no WSL list until experiment 0's rows land,
   while panel 1 keeps showing them without a row. Close that gap with the same experiment, or hide them
   on panel 1 too?
9. The busy plots' label row exists only from 35 rows, to keep 6 list rows at 34.
10. `collecting: N s of M s` is removed (part 4.3).
11. Unchecked: the IME fallback with a real Korean IME, and `esc` and the arrows over ConPTY and OpenSSH.
    `render_check` covers the decoder, not the console; a person runs each key live at 100 by 34 and 131
    by 40.

## 8. Changes needed in other files

Written as requests; this file changes nothing else.

| file | change |
|---|---|
| `docs/README.md` | Index this file, or `scripts/check.sh` fails once it is tracked |
| `docs/contracts/ui.md` | Decision entries for: the range replacing `t`; `tab`, focus and per-column histories; the selection key; the per-process ring and its cap; mean cells and `avg`/`max` labels with rule 8's new text; the caption removed; panel 4 from existing rows. Supersede the 2026-11-01 entries "Where several samples share a column the highest is drawn" and the keys entry for `1`, `2`, `3`, `q`. Close the open question on the `collecting` caption |
| `docs/design/cpu-panel.md` | A note at sections 4 ("`[s]` sort ... Cut"), 6 (`t`; "No sort key, text entry, scrolling or list focus") and 11 (per-process history) that this file supersedes them; RAM moved to panel 4 |
| `docs/panels.md` | `tab` agreed (machtop 3e0f34b); range 1, 5, 10 decided; `avg`/`max` decided; panel 4's waits as in part 3 |
| `docs/ROADMAP.md` | Section 2: per-process history built for cores and memory |
| `docs/TODO.md` | New item: Windows available, commit, cache, used and free, guest swap and guest RSS on panels 1 and 3 have no ledger row (`CLAUDE.md` rule 2), as item 20 was for disk and network. Owner collect, evidence |
| `docs/contracts/evidence.md` | The memory experiment of commit 0 as a request |
| `README.md` | "Use": keys `1`-`8`, `←`/`→`, `↑`/`↓`, `tab`, `esc`, `q`; `t` gone; panel 4 |
| `scripts/msvc.cmd` | `src/ui/keys.cpp` in `SOURCES` (commit 2) |
