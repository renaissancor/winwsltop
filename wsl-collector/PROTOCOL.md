# Collector protocol

`winwsltop.exe` runs `wsl.exe -e python3 -u -`, writes `collector.py` to its stdin and closes it.
The collector prints one JSON object per line, about once a second, until its stdout closes.
Nothing is written inside WSL, and it reads no command lines.

| field | meaning |
|---|---|
| `schema` | 1; a change of meaning raises it |
| `distro` | `WSL_DISTRO_NAME`. One collector sees one distro's processes |
| `unix_s`, `mono_s` | wall clock and `CLOCK_MONOTONIC`, seconds, kept for clock alignment |
| `busy_pct`, `core_pct` | CPU from `/proc/stat`; idle includes iowait. This is the guest's view |
| `steal_pct` | from `/proc/stat`; zero does not prove the host took nothing |
| `memory` | bytes from `/proc/meminfo`: `total`, `available`, `cached`, `swap_used` |
| `pressure` | PSI `some avg10` for `cpu`, `memory`, `io`; `null` when absent |
| `process_count` | every process seen |
| `processes` | the top 30 by cores then RSS, plus every `/dev/dxg` holder. `cores`, `rss`, `state`, `on_gpu` |
| `dxg_pids` | PIDs with `/dev/dxg` open: identity, not usage |
| `dxg_denied` | processes whose fds could not be read, usually root's |
| `cost_ms` | time this sample took |

Replacing the collector, in Rust for example, needs only this format.
