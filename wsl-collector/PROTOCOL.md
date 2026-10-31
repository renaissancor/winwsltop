# Collector protocol

`winwsltop.exe` runs `wsl.exe [-d <distro>] -e python3 -u -c <bootstrap>`. The bootstrap is one line of
single-quoted Python that reads a decimal length line from stdin, then that many bytes of `collector.py`,
and executes them. Windows keeps stdin open afterwards for pings. The collector prints one JSON object
per line, about once a second, until its stdout closes or stdin reaches EOF (it then exits at once, so no
collector outlives `winwsltop.exe`). Nothing is written inside WSL, and it reads no command lines.
The bridge reads `collector.py` from `wsl-collector/` beside the executable and falls back to the copy
embedded in `src/wsl/collector_embed.cpp`; regenerate that with `python3 wsl-collector/embed.py`.

## Clock alignment

Windows QPC and Linux `CLOCK_MONOTONIC` share no base, and the two wall clocks run at different rates
(observed: WSL fast by about 3.7 percent, 35 ms per second, for minutes) and can step (observed: about 1 s).
So the bridge sends `P <seq>` lines on stdin: five 0.2 s apart, then one a second. For each the collector
replies at once with a pong, a line that is not a sample:

| pong field | meaning |
|---|---|
| `pong` | the `seq` being answered |
| `u2` | `time.time()` when the ping was read |
| `d` | hold time from reading to replying, on `CLOCK_MONOTONIC` |
| `wm` | `time.time() - time.monotonic()`; a change of more than 50 ms between pongs means the wall clock stepped |

The bridge records Windows unix time and QPC at send and receipt. Round trip is the QPC span minus `d`;
the offset (Windows unix minus WSL unix) is the midpoint of the Windows pair minus `u2 + d/2`, which assumes
the two directions take equally long. It keeps the last 30 samples, takes the offset of the lowest round
trip, and carries it forward at the drift rate fitted over the window. A step empties the window. Lines
starting with `{"pong":` are consumed by the bridge and never reach the snapshot.

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
