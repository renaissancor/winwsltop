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
| [`measurable.md`](measurable.md) | What this machine can measure, with the raw file for every number. The evidence ledger | everyone | before building on any counter |
| [`TODO.md`](TODO.md) | Known defects and their status, with the decision on each | core, the owner of each item | before fixing anything |
| [`ROADMAP.md`](ROADMAP.md) | What is proposed next and why. Everything in it is PROPOSED until a contract records a decision | orchestrator, the user | planning |
| [`../wsl-collector/PROTOCOL.md`](../wsl-collector/PROTOCOL.md) | The collector's line, field by field, and the clock-alignment pings | collect, ui, core | before changing the collector or the parser |
| [`../experiments/01-gpu-attribution/README.md`](../experiments/01-gpu-attribution/README.md) | Can Windows tell WSL's GPU work from its own? Procedure and verdict; numbers are in the ledger | evidence | repeating or extending experiment 01 |
| [`../experiments/02-gpu-device/README.md`](../experiments/02-gpu-device/README.md) | Do the GPU device and memory collectors read true? Procedure and verdict | evidence | repeating or extending experiment 02 |
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
