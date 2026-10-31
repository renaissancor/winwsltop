# winwsltop — agent rules

A Windows-native monitor for Windows 11 + WSL2 on one machine, above all for the GPU they share.

- **Read-only.** It observes. It never stops, signals or reconfigures a process, Windows or Linux.
- **Windows + WSL2 only.** No macOS, no standalone Linux build, no remote or fleet monitoring.
- **Evidence first.** Nothing is built on a claim about a counter until `docs/measurable.md` records
  it as observed on real hardware. Cite the experiment. The Windows-versus-WSL split is proven at VM level
  only (experiment 01). Per-Linux-process GPU use is not measured and must never be shown as if it were.
- **Measured is never mixed with inferred.** Every value carries its source. Windows and WSL
  numbers describe overlapping resources and are never added together.
- **Source lives in WSL, builds run on Windows.** `scripts/build.sh` mirrors the tree to
  `%LOCALAPPDATA%\winwsltop\<tree name>` and runs MSVC there. Never build MSVC from a `\\wsl$` path.
- **GPU workloads are short and deliberate.** Start one only when asked, and stop it afterwards.

## Workspaces

Each row is one agent's write set. An agent edits only its row; an edit needed elsewhere is written
as a request to the orchestrator. Agents with different rows can run at the same time.

| workspace | owns | depends on | status | default company |
|---|---|---|---|---|
| contract | `src/core/`, `src/main.cpp`, `wsl-collector/PROTOCOL.md`, this file | nothing | built | Anthropic (orchestrator) |
| host | `src/host/` | contract | built, needs tests | OpenAI (Codex) |
| gpu | `src/gpu/` | contract | built; memory under load unverified | Anthropic |
| wsl | `src/wsl/`, `wsl-collector/` | contract | built; clock alignment, embedding, distros next | OpenAI (Codex) |
| analysis | `src/analysis/` | contract, gpu, wsl | VM-level split built | Anthropic |
| ui | `src/ui/`, later the GUI | contract only, or recorded JSONL | not started | Anthropic |
| verify | `tests/`, `.github/`, `scripts/` | recorded fixtures | not started | OpenAI (Codex) |
| evidence | `experiments/`, `docs/` | anything | in progress | Anthropic |

Rules that make the split work:

- **`snapshot.h` and `PROTOCOL.md` are the seams.** Only the orchestrator changes them. Collectors
  write the snapshot, everything else reads it, so a screen never needs to know how data was collected.
- **`ui` builds from recorded JSONL**, so it needs neither a GPU nor a running collector.
- **Design and review stay with Anthropic; well-specified mechanical work goes to Codex.** Work from
  one company is reviewed by the other before merging.
- **GPU load and anything using the live GPU runs one agent at a time**, because agents would
  otherwise disturb each other's numbers. Builds may run in parallel: each tree has its own mirror.
