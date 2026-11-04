# Documentation index

Every document in this repository, what it answers, who reads it and when. A fact lives in exactly
one of these files; the others link to it. `scripts/check.sh` fails if a tracked `.md` file is
missing from this list or a link below does not resolve.

| file | answers | reader | when |
|---|---|---|---|
| [`../CLAUDE.md`](../CLAUDE.md) | What this project is, the rules every agent follows, where to go next | every agent, first | start of every task |
| [`../AGENTS.md`](../AGENTS.md) | Pointer to `CLAUDE.md` for tools that read `AGENTS.md` | Codex and other non-Claude tools | start of every task |
| [`../README.md`](../README.md) | What winwsltop is, how to build and run it | people | first contact |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | Data flow, the four areas, who owns which path, the handoffs, which checks exist and which are targets | every agent | before editing anything |
| [`contracts/core.md`](contracts/core.md) | The seams, the loop, the recorder, the split, the scripts: what belongs, the rules, how to validate, state, open questions, decisions | core (orchestrator) | before changing a seam or a script |
| [`contracts/collect.md`](contracts/collect.md) | Windows and WSL collectors: same sections | collect agent | before touching `src/host`, `src/gpu`, `src/wsl`, the collector |
| [`contracts/ui.md`](contracts/ui.md) | The screen and the line parser: same sections | ui agent | before touching `src/ui` |
| [`contracts/evidence.md`](contracts/evidence.md) | How an experiment becomes a ledger row; what the ledger may and may not claim | evidence agent | before running or recording an experiment |
| [`design/cpu-panel.md`](design/cpu-panel.md) | The CPU panel: the job, layout, evidence for each figure, the `lost` inference, states, costs, build order | ui, core and collect agents | before changing the CPU panel |
| [`design/panel-grammar.md`](design/panel-grammar.md) | Slice 1 of the shared panel grammar: keys and footer, selection and per-process history on panel 3, panel 4 Memory, `avg`/`max` graph labels. PROPOSED, under review | ui and core agents | before changing keys, selection, panel 4 or graph labels |
| [`design/panel-grammar-slice2.md`](design/panel-grammar-slice2.md) | Slice 2 of the shared panel grammar: selection by pid and start, a whole for WSL in the protocol, part-of-whole process histories, two time columns per braille cell, panel 2 selectable, the range in the history heading; build order, and machtop's answers to its questions. Built, the guest whole dropped on experiment 07 | ui, core, collect and evidence agents | before building slice 2 |
| [`panels.md`](panels.md) | Which panel answers which question, the keys shared with sshvllmtop and machtop, and what each panel waits for. PROPOSED until `contracts/ui.md` records it | ui and core agents, the user | before adding or changing a panel or a key |
| [`measurable.md`](measurable.md) | What this machine can measure, with the raw file for every number. The evidence ledger | everyone | before building on any counter |
| [`TODO.md`](TODO.md) | Known defects and their status, with the decision on each | core, the owner of each item | before fixing anything |
| [`ROADMAP.md`](ROADMAP.md) | What is proposed next and why. Everything in it is PROPOSED until a contract records a decision | orchestrator, the user | planning |
| [`../wsl-collector/PROTOCOL.md`](../wsl-collector/PROTOCOL.md) | The collector's line, field by field, and the clock-alignment pings | collect, ui, core | before changing the collector or the parser |
| [`json-output.md`](json-output.md) | Every field of `--once` and `--record`, exit codes, and how not to misread them | scripts and agents using the tool, core | before parsing the output or changing `src/record` |
| [`releases/v0.1.0.md`](releases/v0.1.0.md) | Release notes for v0.1.0, used by `gh release create` | whoever publishes it | when releasing |
| [`../experiments/01-gpu-attribution/README.md`](../experiments/01-gpu-attribution/README.md) | Can Windows tell WSL's GPU work from its own? Procedure and verdict; numbers are in the ledger | evidence | repeating or extending experiment 01 |
| [`../experiments/02-gpu-device/README.md`](../experiments/02-gpu-device/README.md) | Do the GPU device and memory collectors read true? Procedure and verdict | evidence | repeating or extending experiment 02 |
| [`../experiments/03-host-counters/README.md`](../experiments/03-host-counters/README.md) | Do the disk and network collectors read true? Procedure, how independent each comparison is, and verdict | evidence | repeating or extending experiment 03 |
| [`../experiments/04-cpu-counters/README.md`](../experiments/04-cpu-counters/README.md) | Which CPU counters are readable non-elevated, which move, and does Windows' figure for the VM follow the guest? Procedure and verdict, no load started | evidence |
| [`../experiments/05-cpu-contention/README.md`](../experiments/05-cpu-contention/README.md) | When Windows and WSL both want the CPU, what shows it? Linearity of the VM figures, whether Windows busy holds the VM, what reveals lost CPU. Procedure and verdict; one approved CPU-only load | evidence | repeating or extending experiment 05 |
| [`../experiments/06-memory-process/README.md`](../experiments/06-memory-process/README.md) | Do the memory and process figures read true? Windows available, commit, cache and per-process memory, guest swap, RSS and per-process cores, vmmemWSL beside guest memory, and create times. Procedure, how independent each comparison is, and verdict; no load started | evidence | repeating or extending experiment 06 |
| [`../experiments/07-guest-walk/README.md`](../experiments/07-guest-walk/README.md) | Does the collector's walk of `/proc` reach every guest process, so that a sum over it is the guest's whole? Procedure and verdict, one run; one 20 s CPU load, no GPU load | evidence | repeating or extending experiment 07 |
| [`../.claude/agents/core.md`](../.claude/agents/core.md) | The core role: what it may read and write, its return format | a fresh core agent | when spawned |
| [`../.claude/agents/collect.md`](../.claude/agents/collect.md) | The collect role | a fresh collect agent | when spawned |
| [`../.claude/agents/ui.md`](../.claude/agents/ui.md) | The ui role | a fresh ui agent | when spawned |
| [`../.claude/agents/evidence.md`](../.claude/agents/evidence.md) | The evidence role | a fresh evidence agent | when spawned |
| [`../.claude/agents/auditor.md`](../.claude/agents/auditor.md) | The read-only auditor: tries to prove a stage's docs and numbers wrong | a fresh auditor, never the author | after every stage |

## Where a fact lives

| kind of fact | lives in |
|---|---|
| what a counter measures on this machine, with its number | `measurable.md` |
| who owns a path, what reads and writes what | `ARCHITECTURE.md` |
| what an area must and must not do | `contracts/<area>.md` |
| a defect and whether it is fixed | `TODO.md` |
| the collector's fields | `wsl-collector/PROTOCOL.md` |
| a rule every agent follows | `CLAUDE.md` |
| what we might do next | `ROADMAP.md`, always PROPOSED |

Code comments may name a ledger section; they do not repeat its numbers.
