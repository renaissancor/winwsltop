# Panels

**PROPOSED, 2026-11-02.** Nothing here is decided until `contracts/ui.md` records it in its decision log.
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
  guest's memory is inside `vmmemWSL`, its network traffic crosses both `vEthernet` and the NIC, its GPU
  work is inside `vmwp.exe`. Where machtop's cards name the app using most of a resource, winwsltop's name
  the side, then the process on that side where one is measured.
- **A Windows figure about the VM and the guest's own figure are two readings, both shown.** Where they
  disagree, the gap is the finding (the CPU panel's `lost`), named as inferred with its formula.
- **WSL has no per-process GPU use.** `/dev/dxg` holders are identity only (ledger, Experiment 01 result
  and WSL collector checked). A holder can be selected, and its history then says `no usage measured`
  instead of drawing a graph.

## Shared grammar

As in machtop, with two changes marked:

- A panel is a list on top and the selected row's history below.
- `1`-`8` switch panels; `↑`/`↓` select a row; `←`/`→` change the time range (1, 5, 10 minutes); `esc`
  returns to 1; `q` quits. Keys typed with Korean input on (ㅂ for q, ㄴ for s) count as their Latin
  letters, and a bare `esc` is told from an arrow key's escape sequence by a short delay.
- **Added, needs agreement with sshvllmtop and machtop:** `tab` moves the selection between the Windows
  list and the WSL list on a panel that has both. One list per side is the rule here, not an exception,
  so a second key is cheaper than merging the lists, which `contracts/ui.md` rule 3 forbids.
- **Changed for winwsltop:** `←`/`→` replace `t`. The history keeps 15 minutes, so the range could go to
  15; it stops at 10 to match the other tools. See open questions.
- The footer lists the panels with the current one lit. A panel that is not built yet is listed dim.
- Graphs: a measured 0 is the lowest braille dot row (⣀) in steel; no sample inside the watched span is
  blank; the part of the window from before winwsltop started watching is a faint `·` every other cell.
  Graphs are labelled with the window's average and maximum, never the minimum.

## 1 Overview: is either side being held back by the other right now?

- **Core detail:** one card per resource (GPU, CPU, memory, disk, network), each with how loaded it is,
  the Windows / WSL split where one is measured, the busiest process on each side where one is measured,
  and its history. GPU first: it is why winwsltop exists.
- **Above the cards, PROPOSED (`ROADMAP.md`, contention verdict):** one line when both sides are busy
  and the GPU is near its limit, with the moment it began. Experiment 01 showed utilization alone hides
  the slowdown (ledger, Experiment 01 result, "Contention slows WSL" and "Contention slows Windows"). Waits
  for a rule with a ledger row behind its threshold.
- **Supporting:** collector state (`collector_status`, restarts, data age), which the large CPU layout
  does not show (open question in `contracts/ui.md`).
- **Not here:** per-thread cells, per-process rows beyond the busiest one per side.

## 2 GPU: which side is using the GPU, and which process?

- **Core detail:** the device (utilization, memory, power, clock: ledger, GPU device and memory
  collectors checked), then the VM-level split, `VM level, not exact shares` (ledger, Experiment 01
  result, "Shares add to 100%": no), with `unattributed` between device and split (PROPOSED,
  `ROADMAP.md` 1). Then two lists side by side: Windows GPU users by engine percent with `Local Usage`
  memory, holders first; WSL `/dev/dxg` holders, `no usage measured`.
- **History:** the selected Windows process's GPU percent, or for a WSL holder, `no usage measured`.
- **Caveats on the panel:** `dxg_denied` processes could not be checked (ledger, WSL collector checked);
  `Dedicated Usage` is not VRAM in use and is never shown as if it were (ledger, GPU device and memory
  collectors checked).

## 3 CPU: how hard are the threads working, and how much CPU is the VM not getting?

- **Core detail:** the large layout of `design/cpu-panel.md` as built: what each side says, the heat
  lanes, `lost` (inferred, its formula on the panel), then the Windows and WSL process lists by cores.
- **History:** the selected process's cores. Needs a per-process ring (PROPOSED, `ROADMAP.md` 2); until
  then the panel keeps its own graphs and selection only highlights.
- **Moves out:** RAM, to panel 4.
- **Not here:** which LP a thread ran on, which VP ran which guest thread (ledger, CPU contention
  checked: no VP stayed put).

## 4 Memory: where is physical memory going, and is the guest holding host memory it does not use?

- **Core detail:** Windows memory (total, available, commit, cache) beside the VM as Windows sees it
  (`vmmemWSL` working set) and the guest's own view (`/proc/meminfo`: total, available, cached, swap
  used, and PSI memory). The guest's page cache is held as `vmmemWSL` working set on Windows (ledger, WSL
  collector checked, "Guest memory vs host", read minutes apart), which is the question this panel
  answers. Then both process lists by memory: Windows working set and private bytes, WSL RSS.
- **History:** the selected process's memory.
- **Waits for:** a ledger row checking Windows available, commit and cache against Windows' own tools
  (only total is checked: ledger, Windows collector checked), and the guest-versus-`vmmemWSL` row read
  in one sample rather than minutes apart.
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
  speed where known. Not added up: the guest's traffic crosses `vEthernet (WSL)` and the NIC.
- **History:** the selected interface's in + out.
- **Not yet:** per-process network on either side; the guest's own `eth0` (used once as an independent
  check, ledger "vEthernet octets match a count outside Windows", but not in the collector's line).

## 7 Processes: every process, every measure

- **Core detail:** two tables side by side. Windows: cores, working set, private bytes, threads,
  handles, GPU. WSL: cores, RSS, state, threads, `/dev/dxg` holder. `s` cycles the sort column of the
  selected table. Never merged or sorted together (`contracts/ui.md` rule 3).
- **History:** the selected process's cores.
- **Caveat on the panel:** the WSL table is the collector's top 30 by cores then RSS plus every holder
  (`wsl-collector/PROTOCOL.md`, `processes`), not every guest process; the panel shows
  `process_count` beside it.

## 8 VM cost: what has each side cost the other, and when?

winwsltop's own panel, and what `ROADMAP.md` calls the centrepiece: the record btop does not keep.

- **Core detail:** over the selected range, side by side: the GPU split (Windows, WSL, unattributed),
  `lost` CPU (inferred), and memory held by `vmmemWSL` against the guest's used. Below it, the moments
  contention began and ended, once the verdict in panel 1 exists.
- **History:** the whole panel is history; the selection picks which of the three rows is drawn large.
- **Later, PROPOSED (`ROADMAP.md` 2):** workload truth, such as vLLM `/metrics` from inside WSL, so a
  slowdown is shown in the work done and not only in utilization.
- **Not here:** anything per process. This panel is about the two sides.

## From today's screen

| today | proposed |
|---|---|
| `1` Overall | 1 Overview, cards and the verdict line |
| `2` GPU | 2 GPU, with selection and holders first |
| `3` CPU and RAM | 3 CPU, and RAM moves to 4 Memory |
| `t` span 60 s, 5 min, 15 min | `←`/`→` 1, 5, 10 minutes, every panel |
| none | 4 to 8, each built only once its ledger rows exist |

## Open questions

- `design/cpu-panel.md` section 6 refuses a sort key, scrolling and list focus on panel 3 ("Each costs
  a mode"). The shared grammar needs selection on every panel. Adopting this file supersedes that line,
  and the decision must be recorded there and in `contracts/ui.md`.
- `tab` between the two lists changes the shared grammar. Agree it with sshvllmtop and machtop first, or
  find a way that needs no new key.
- The range: 1, 5, 10 like the others, or 1, 5, 15 to use the 15 minutes `history` already keeps.
- The contention verdict needs a threshold with evidence behind it: how near the limit, for how long.
- sshvllmtop does not yet follow the shared numbering (its keys are `p`, `a`, `w`, `j`/`k`).
