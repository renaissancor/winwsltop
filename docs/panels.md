# Panels

**PROPOSED, 2026-11-02**, except what "Decisions" at the end records, which `contracts/ui.md` also
records. The rest is decided only once `contracts/ui.md` records it in its decision log.
Today's screen is in `README.md` ("Use") and, for panel 3, `design/cpu-panel.md`.

Each panel answers one question. Its core detail is what answers that question; everything else on it
supports that answer or is left to another panel. A value appears on a panel only once its ledger row
exists in `measurable.md`; a panel whose rows are missing says what it waits for and keeps its number.

The numbering and the grammar are shared with sshvllmtop and machtop (machtop's `docs/panels.md`,
agreed 2026-11-02), so keys learnt in one tool work in the others: 1 overview, 2 GPU, 3 CPU, 4 memory,
5 disk, 6 network, 7 processes, 8 and up for each tool's own panels. winwsltop's own is 8 VM cost.

## What is different here

winwsltop watches two systems that share one machine. So on every panel:

- **Windows and WSL sit side by side, never added together.** They describe overlapping resources: the
  guest's memory is inside `vmmemWSL` and its GPU work is inside `vmwp.exe`. Its network traffic may be
  counted twice, on `vEthernet` and again on the NIC: whether it is, is `Unresolved` (ledger, Disk and
  network collectors checked, "Traffic on `vEthernet` is also counted on the NIC"), so the two are not
  added up either. Where machtop's cards name the app using most of a resource, winwsltop's name the
  side, then the process on that side where one is measured.
- **A Windows figure about the VM and the guest's own figure are two readings, both shown.** Where they
  disagree, the gap is the finding (the CPU panel's `lost`), named as inferred with its formula.
- **WSL has no per-process GPU use.** `/dev/dxg` holders are identity only (ledger, the table at the
  top, "`/dev/dxg` holders identify the Linux process", and WSL collector checked). A holder can be
  selected, and its history then says `no usage measured` instead of drawing a graph.

## Shared grammar

As in machtop. Where winwsltop's screen differs today, the line says so.

- A panel is a list on top and the selected row's history below.
- `1`-`8` switch panels; `↑`/`↓` select a row; `←`/`→` change the time range (1, 5, 10 minutes); `esc`
  returns to 1; `q` quits. Keys typed with Korean input on (ㅂ for q, ㄴ for s) count as their Latin
  letters, and a bare `esc` is told from an arrow key's escape sequence by a short delay.
- `tab` moves the selection to the next selectable list on the current panel and wraps round. There is
  no Shift+Tab. On a panel with one selectable list it does nothing and the footer does not show it.
  Decided by machtop 2026-11-02, added to the shared grammar in machtop's `docs/panels.md`. Here it is
  how a panel moves between its Windows list and its WSL list, which `contracts/ui.md` rule 3 forbids
  merging.
  - Each list keeps its own selected row, a process by pid and name (machtop f7e24f2), so the selection
    survives a reorder. Switching lists resets neither.
  - The list holding the selection has its heading lit; the other list's heading is dim and its rows
    show no ▶.
  - `s` sorts the list holding the selection, and the one history below the lists, full width and titled
    with its list, shows that list's selected row. Never one history per list (machtop f7e24f2).
  - `esc` returns to panel 1 and does not reset focus. Each panel keeps its focus when you switch away
    and back, as it keeps its selection.
- **Changed for winwsltop:** `←`/`→` replace `t`, and the range is 1, 5, 10 minutes, the same as
  machtop's (decided 2026-11-02, below). From slice 1 the history keeps 10 minutes.
- The footer lists the panels with the current one lit and a panel not built yet in the faint tone, which
  machtop f7e24f2 made shared grammar.
- Graphs: a measured 0 is the lowest braille dot row (⣀) in steel; no sample inside the watched span is
  blank; the part of the window from before winwsltop started watching is a faint `·` every other cell.
  This is already winwsltop's rule, `contracts/ui.md` rule 8, from 6ef13ef. The large layout's
  `collecting: N s of M s` caption goes in slice 1.
- Graphs are labelled with the window's average and maximum, never the minimum; a cell is the mean of
  its samples; a measure with a real ceiling is drawn 0-100, any other to a rounded `scale` or a zoomed
  `range` (machtop f7e24f2). Built in slice 1, which changes rule 8 (`design/panel-grammar.md`, part 5).
  **Added for winwsltop, put to machtop:** the cell holding the window's maximum carries one extra dot, and
  a split value over 100 is printed with `*` beside `VM level, not exact shares*`.
- **Added for winwsltop:** a figure the screen shows without a ledger row is marked `?`, and the panel's
  rule line says `? not yet checked` (`design/panel-grammar.md`, part 1).

## 1 Overview: is either side being held back by the other right now?

- **Core detail:** one card per resource (GPU, CPU, memory, disk, network), each with how loaded it is,
  the Windows / WSL split where one is measured, the busiest process on each side where one is measured,
  and its history. GPU first: it is why winwsltop exists.
- **Above the cards, PROPOSED (`ROADMAP.md`, contention verdict):** one line when both sides are busy
  and the GPU is near its limit, with the moment it began. Experiment 01 measured the slowdown in work
  done on both sides (ledger, Experiment 01 result, "Contention slows WSL" and "Contention slows
  Windows"); what that row says hid the Windows loss is dispatch time, not utilization. "Utilization
  alone hid" it is `ROADMAP.md`'s wording (section 2, contention verdict), with no ledger row of its own.
  Waits for a rule with a ledger row behind its threshold.
- **Supporting:** collector state (`collector_status`, restarts, data age), which the large CPU layout
  does not show (open question in `contracts/ui.md`).
- **Not here:** per-thread cells, per-process rows beyond the busiest one per side.

## 2 GPU: which side is using the GPU, and which process?

- **Core detail:** the device (utilization, memory, power, clock: ledger, GPU device and memory
  collectors checked), then the VM-level split, `VM level, not exact shares` (ledger, Experiment 01
  result, "Shares add to 100%": no), with `unattributed` between device and split (PROPOSED,
  `ROADMAP.md` 1). Then two lists side by side: Windows GPU users by engine percent with `Local Usage`
  memory, holders first (PROPOSED, `ROADMAP.md` 1 and `contracts/ui.md` open questions: today the list is
  by engine percent only); WSL `/dev/dxg` holders, `no usage measured`, which already lists holders first
  (`src/ui/render.cpp`, the WSL process list's head).
- **History:** the selected Windows process's GPU percent, or for a WSL holder, `no usage measured`.
- **Caveats on the panel:** `dxg_denied` processes could not be checked (ledger, WSL collector checked);
  `Dedicated Usage` is not VRAM in use and is never shown as if it were (ledger, GPU device and memory
  collectors checked).

## 3 CPU: how hard are the threads working, and how much CPU is the VM not getting?

- **Core detail:** the large layout of `design/cpu-panel.md` as built: what each side says, the heat
  lanes, `lost` (inferred, its formula on the panel), then the Windows and WSL process lists by cores.
- **History:** the selected process's cores, one history following focus, from the per-process ring of
  slice 1 (`design/panel-grammar.md`, part 3). A guest process's cores are the guest's own count, marked
  `?` until experiment 0 checks them.
- **Moves out:** RAM, to panel 4.
- **Not here:** which Windows LP ran a vCPU, and so which LP ran a guest thread. No counter read says
  which LP ran a VP, and no VP stayed put (ledger, CPU counters checked, "A guest vCPU maps to a Windows
  core"). The steps either side of that are measured: a guest thread's vCPU from
  `/proc/pid/task/*/stat` (ledger, CPU contention checked, "A process's `/proc/pid/stat` `processor` is
  where it works") and a vCPU's `vmmemWSL` thread (same section, "A `vmmemWSL` thread carries one
  vCPU"). Neither is collected today, in the snapshot or the collector's line.

## 4 Memory: where is physical memory going, and is the guest holding host memory it does not use?

- **Core detail:** as built in slice 1 (`design/panel-grammar.md`, part 4): each side's used and cache,
  with Windows commit and the guest's swap and PSI memory; the VM on its own row (`vmmemWSL` working set);
  each side's used and cache over the range; both process lists by memory, Windows working set and WSL
  RSS. Private bytes are not shown: no row and not on screen today.
- **Hypothesis behind the question, not a finding:** that the guest's page cache is held as `vmmemWSL`
  working set on Windows. The one reading near it (ledger, WSL collector checked, "Guest memory vs
  host") has guest used plus cache close to `vmmemWSL`, but read minutes apart, so the fit may be
  coincidence. It needs a row with the two read in one sample before the panel states it.
- **History:** the selected process's memory.
- **Marked `?` until experiment 0:** Windows available, used, commit and cache, Windows per-process
  working set, guest swap and guest RSS (only Windows total and `vmmemWSL` working set are checked: ledger,
  Windows collector checked). The guest-versus-`vmmemWSL` reading in one sample is in the same experiment.
- **Not here:** a sum of Windows used and guest used. The guest's memory is inside `vmmemWSL`.

## 5 Disk: which disk is busy, and is it the WSL disk?

- **Core detail:** one row per physical disk: read and write bytes per second and busy, with the
  checks behind each (ledger, Disk and network collectors checked: disk 0 checked against the drive,
  disk 1 not, busy is bindings only, `% Idle Time` above 100 is clamped).
- **History:** the selected disk's bytes.
- **Not yet:** per-process disk on either side (no collector, no ledger row); the guest's own disk view
  (`/proc/diskstats` is not in the collector's line); which disk holds the WSL VHDX.

## 6 Network: which interface is busy?

- **Core detail:** interfaces that are up and moved in the window, hardware NICs first, idle virtual NICs
  hidden (PROPOSED, `ROADMAP.md` 1), filter rows dropped (ledger, Disk and network collectors checked:
  they would double-count). In and out bytes per second, a trend of in + out and its peak, and link
  speed where known. Not added up: the guest's traffic may cross both `vEthernet (WSL)` and the NIC,
  which is `Unresolved` (ledger, Disk and network collectors checked, "Traffic on `vEthernet` is also
  counted on the NIC"), so a total could count it twice.
- **History:** the selected interface's in + out.
- **Not yet:** per-process network on either side; the guest's own `eth0` (used once as an independent
  check, ledger, Disk and network collectors checked, "`vEthernet` octets match a count outside
  Windows", but not in the collector's line).

## 7 Processes: every process, every measure

- **Core detail:** two tables side by side. Windows: cores, working set, private bytes, threads,
  handles, GPU. WSL: cores, RSS, state, threads, `/dev/dxg` holder. `s` cycles the sort column of the
  selected table. Never merged or sorted together (`contracts/ui.md` rule 3).
- **History:** the selected process's cores.
- **Caveat on the panel:** the WSL table is the collector's top 30 by cores then RSS plus every holder
  (`wsl-collector/PROTOCOL.md`, `processes`), not every guest process; the panel shows
  `process_count` beside it.

## 8 VM cost: what has each side cost the other, and when?

winwsltop's own panel, the record btop does not keep. It is where `ROADMAP.md`'s centrepiece would
live: "Braille chart of WSL vs Windows over 1, 10 and 60 minutes" (section 2, History). The range here
is the shared one (Shared grammar).

- **Core detail:** over the selected range, side by side: the GPU split (Windows, WSL, unattributed),
  `lost` CPU (inferred), and memory held by `vmmemWSL` against the guest's used. The guest's used is not
  collected: it is `memory.total` less `memory.available` from the collector's line, derived, on the
  guest's side only. Below it, the moments contention began and ended, once the verdict in panel 1
  exists.
- **History:** the whole panel is history; the selection picks which of the three rows is drawn large.
- **Later, PROPOSED (`ROADMAP.md` 2):** workload truth, such as vLLM `/metrics` from inside WSL, so a
  slowdown is shown in the work done and not only in utilization.
- **Not here:** anything per process. This panel is about the two sides.

## From today's screen

| today | proposed |
|---|---|
| `1` Overall | 1 Overview, cards and the verdict line |
| `2` GPU | 2 GPU, with selection; Windows holders first is PROPOSED (`ROADMAP.md` 1), WSL's already are |
| `3` CPU and RAM | 3 CPU, and RAM moves to 4 Memory |
| `t` span 60 s, 5 min, 15 min | `←`/`→` 1, 5, 10 minutes, every panel |
| none | 4 to 8, each built only once its ledger rows exist |

## Open questions

- The contention verdict needs a threshold with evidence behind it: how near the limit, for how long. Open,
  confirmed by the owner 2026-11-02: it waits for a ledger row.
- Put to machtop 2026-11-02: the dot on the cell holding the window's maximum, and the over-100 `*`.

## Decisions

- 2026-11-02, the owner. The shared grammar from machtop's `docs/panels.md` overrules winwsltop's existing
  panel design. The three refusals in `design/cpu-panel.md` are superseded:
  - list focus and the sort key: section 6, "No sort key, text entry, scrolling or list focus. Each
    costs a mode", against selection on every panel and `tab` between lists;
  - the `[s]` sort: section 4, "Refused, and why", "`[s]` sort by memory ... Cut", against `s` on
    panel 7;
  - per-process history: section 11, "Out of this design", against the selected process's history on
    panels 3 and 4.
  Recorded in `design/cpu-panel.md` and `contracts/ui.md`.
- 2026-11-02, the owner. The range on `←`/`→` is 1, 5, 10 minutes, the same as machtop and sshvllmtop's
  shared grammar. It replaces the 1, 5, 15 option once open here, and supersedes the "1, 10 and 60 minutes"
  of `ROADMAP.md` section 2 for the centrepiece chart on panel 8.
- 2026-11-02, the owner. Keys follow machtop and sshvllmtop's shared grammar.
- 2026-11-02, machtop. `tab` as in Shared grammar: next selectable list, wrapping, no Shift+Tab, nothing on
  a panel with one list and then not in the footer; each list keeps its selected row by name; the lit
  heading marks the list holding the selection, the other's is dim with no ▶; `s` and the history follow
  that list; `esc` does not reset focus, and a panel keeps its focus across a switch. Source: machtop
  2026-11-02, added to the shared grammar in machtop's `docs/panels.md`. Closes the open question on `tab`.
- 2026-11-02, machtop. Lists scroll as in machtop's `docs/panels.md`, Shared grammar, "Selection" (branch
  `grammar-tab`, read from machtop's `move_selection` and `app_table`): `↑`/`↓` stop at the ends and do
  not wrap; a list longer than its space follows the selection, which stays on the last row shown once it
  passes the bottom; no paging; the list ends with a dim `N of M` line; a selected name that leaves the
  list drops the selection to the first row. Supersedes the refusal of scrolling in `design/cpu-panel.md`
  section 6. The `tab` details above are in the same file, bullet "tab".
- 2026-11-02. sshvllmtop does not follow the shared grammar yet: its range is 1 m, 10 m, 1 h and it moves
  with `j`/`k`/`g`/`G`. That is sshvllmtop's own work, not a winwsltop task.
- 2026-11-02, machtop f7e24f2. One history, full width, below the lists, following focus and titled with its
  list, never one per list; a process remembered by pid and name; scale by measure; a cell is the mean of its
  slice of the window and `avg` and `max` cover the whole window; no Shift+Tab; panels not built listed faint.
- 2026-11-02, the owner. Slice 1, `design/panel-grammar.md`, is approved to build after experiment 0. Figures
  shown without a ledger row are marked `?`, not hidden; the seam change is additive flags, `schema` stays 1;
  panel 3 at 34 rows shows 5 list rows; panel 1 gets no selection and panel 2 waits for slice 2. Recorded in
  `contracts/ui.md`.
