# winwsltop

A Windows-native, read-only monitor for one machine running Windows 11 and WSL2, above all for the
GPU they share: which side is using it, what that costs the other side, and a record of it. Not a
general btop clone, not remote or fleet monitoring, not macOS or standalone Linux.

## Rules for every agent

1. **Read-only.** It observes. It never stops, signals or reconfigures a process, Windows or Linux.
   The one exception is its own collector child, which it ends when it exits.
2. **Evidence first.** No counter is built on until `docs/measurable.md` records it as observed on this
   machine. Cite the row; do not restate the number. The Windows-versus-WSL split is proven at VM level
   only. Per-Linux-process GPU use is not measured and is never shown as if it were.
3. **Measured is never mixed with inferred.** Every value carries its source. Windows and WSL numbers
   describe overlapping resources and are never added together.
4. **One owner per file.** `docs/ARCHITECTURE.md` says who. An edit outside your row is a request to
   the orchestrator, written in your report, not made.
5. **Source lives in WSL, builds run on Windows.** `scripts/build.sh` mirrors the tree and runs MSVC
   there. Never build from a `\\wsl$` path. `scripts/check.sh` passes before every commit.
6. **GPU loads are short and deliberate.** Start one only when the user asked, one agent at a time,
   and stop it afterwards.
7. **Authors never audit their own work.** A fresh `auditor` reads each stage against the code.
8. **Never push or merge.** Work on a branch in small commits.

## Where to go next

- `docs/README.md`: the index of every document, and where each kind of fact lives.
- `docs/ARCHITECTURE.md`: data flow, the four areas, ownership, handoffs, and which checks exist.
- `docs/contracts/<area>.md`: your area's purpose, rules, validation, state, open questions, decisions.
- `.claude/agents/<role>.md`: your role's read and write set and return format.
