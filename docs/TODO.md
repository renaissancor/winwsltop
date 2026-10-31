# Known defects

Found in the 2026-10-31 audit (one author's read, one independent read, both against the code).
Status is one of `open`, `fixed <commit>`, `deferred` with the reason. An item is fixed only by its
area's owner, and the fix is reviewed by someone else. Fixing is not auditing: the audit of a fix
re-reads the code.

| # | sev | area | file | defect | status |
|---|---|---|---|---|---|
| 1 | medium | collect, core | `src/host/*.cpp`, `src/gpu/pdh_*.cpp`, `src/main.cpp` | `sample()` results are ignored and most collectors return before clearing their output, so a failed PDH or `NtQuerySystemInformation` read leaves the previous second's rows in the snapshot, unflagged. Only `nvml.cpp` clears first | fixed, collect: every `sample()` resets its output before any early return; core half in e68e7b4 |
| 2 | medium | core | `src/main.cpp` | The deadline is never clamped to now. After a suspend or a stall longer than a second, the loop samples and draws with no sleep until it catches up; `--record` then emits many lines a second | open |
| 3 | medium | collect | `src/wsl/bridge.cpp` | The read loop ends only when the child's stdout closes, and only the job object forces that. If the job is null or `AssignProcessToJobObject` fails, the destructor's `join()` never returns | open |
| 4 | medium | collect, core | `src/gpu/pdh_engine.cpp`, `src/gpu/pdh_memory.cpp` | Engine and memory rows are keyed by PID only. On a machine with a second adapter its use is counted on the Windows side next to NVIDIA-only NVML figures. Not an issue on this machine, where the second LUID stays at zero | deferred: needs a LUID field in the snapshot (seam) and a ledger row on what the second LUID is |
| 5 | low | core | `src/analysis/attribution.cpp`, `src/core/snapshot.h`, `src/ui/render.cpp` | `gpu_split.valid` is set whenever `vmwp` is known, regardless of engine data. The `snapshot.h` comment says otherwise and the `no GPU engine data` branch in `render.cpp` is unreachable | open |
| 6 | low | collect | `src/host/network.cpp` | Unsigned octet delta: a counter reset produces a rate near 1.8e19 in the screen and the record | fixed, collect: a counter below its previous value leaves that rate at 0 for the sample |
| 7 | low | collect | `src/wsl/bridge.cpp` | The clock window is cleared only when the next collector starts, so during the 2 to 30 s backoff `clock_known` stays true and the offset keeps extrapolating from a dead collector | open |
| 8 | low | collect | `src/wsl/bridge.cpp` | An invalid `--distro` or a missing `wsl.exe` never increments `restarts`; the screen says only that the collector has sent nothing. Collector stderr goes to `NUL` | deferred: a message needs a snapshot field (seam); see `contracts/collect.md` |
| 9 | low | core | `scripts/build.sh` | The Windows path is passed to `cmd.exe` unquoted, so a space in the profile path breaks the build. The no-rsync fallback deletes `build/` and aborts if the exe is running | deferred: no path with a space on this machine to verify a fix against; rsync is present |
| 10 | low | collect | `wsl-collector/embed.py` | Bytes are emitted into a `const char[]`; a byte of 128 or more in `collector.py` would be a narrowing conversion MSVC rejects. Today there are none | open |
| 11 | low | collect | `src/host/cpu.cpp` | More than 64 logical CPUs: one processor group is reported and nothing says so | deferred: not this hardware; needs a flag in the snapshot |
| 12 | low | core, ui | `src/ui/render.cpp`, `src/analysis/attribution.cpp` | GPU rows are joined to processes by PID with no create time; a PID reused within a second shows another process's GPU figures | deferred: PDH instance names carry no create time; documented here |
| 13 | nit | collect | `src/host/processes.cpp` | Any process with a null image name is called `Idle`, not only PID 0 | fixed, collect: `Idle` only for PID 0, any other nameless row is `?` |
| 14 | nit | core | `src/main.cpp` | If `GetModuleFileNameW` fails, `find_last_of` returns `npos` and `resize` throws | open |
| 15 | nit | core | `src/main.cpp` | `--record 0` prints nothing and exits 0; `--text --record N` depends on argument order | open, question in `contracts/core.md` |
| 16 | nit | ui | `src/ui/console.cpp` | Keystrokes echo into the alternate screen until the next redraw | open |
| 17 | nit | collect | `src/wsl/bridge.cpp` | `pending` grows without bound if the child never writes a newline | open |
| 18 | nit | collect | `src/wsl/bridge.cpp` | A failed `CreateFileW("NUL")` hands `INVALID_HANDLE_VALUE` to the child as stderr | open |
| 19 | nit | core | `src/record/jsonl.cpp` | `%.3f` prints `nan` or `inf` as bare words; no producer makes one today, so the record is valid by construction only | open |

Checked and found correct in the same audit: the `nt_process` layout, the `/proc/<pid>/stat` field
indices, the NVML v2 struct version, the clock-offset sign and midpoint maths, the JSON parser's
limits, the renderer's UTF-8 handling, the bootstrap quoting, and every handle close path in
`run_once`.
