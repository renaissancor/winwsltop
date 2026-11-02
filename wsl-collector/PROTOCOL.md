# Collector protocol

`winwsltop.exe` runs `wsl.exe [-d <distro>] -e python3 -I -u -c <bootstrap>`, by wsl.exe's full System32 path. `-I` keeps
the current folder off `sys.path`, so a `json.py` beside a download cannot run. The bootstrap is one line of
single-quoted Python that reads a decimal length line from stdin, then that many bytes of `collector.py`,
and executes them. Windows keeps stdin open afterwards for pings. The collector prints one JSON object
per line, about once a second, until its stdout closes or stdin reaches EOF (it then exits at once, so no
collector outlives `winwsltop.exe`). Nothing is written inside WSL, and it reads no command lines.
A normal build reads `collector.py` from `..\wsl-collector\` relative to the executable and falls back to
the copy embedded in `src/wsl/collector_embed.cpp`; regenerate that with `python3 wsl-collector/embed.py`.
A release build only ever uses the embedded copy.

## Clock alignment

Windows QPC and Linux `CLOCK_MONOTONIC` share no base, and the two wall clocks run at different rates
(observed: WSL fast by about 3.7 percent, for minutes; `docs/measurable.md`, WSL clock rate) and can step (observed: about 1 s).
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
| `schema` | 1; a change of meaning raises it, a new key does not |
| `distro` | `WSL_DISTRO_NAME`. One collector sees one distro's processes |
| `kernel` | `uname -r` of the guest, informational |
| `unix_s`, `mono_s` | wall clock and `CLOCK_MONOTONIC`, seconds, kept for clock alignment |
| `busy_pct`, `core_pct` | CPU from `/proc/stat`; idle includes iowait. This is the guest's view |
| `cpu_split` | the whole guest, from the same `/proc/stat` delta as `busy_pct`, each a percent of all ticks: `user` is user plus nice, `sys` is system plus irq plus softirq plus steal, and `iowait`. `user` plus `sys` is `busy_pct`, within rounding, and `iowait` is outside busy. Each is `null` when no ticks passed |
| `load` | three numbers from `/proc/loadavg`, the 1, 5 and 15 minute averages; `null` when it cannot be read |
| `steal_pct` | from `/proc/stat`; zero does not prove the host took nothing |
| `memory` | bytes from `/proc/meminfo`: `total`, `available`, `cached`, `swap_used` |
| `pressure` | PSI `some avg10` for `cpu`, `memory`, `io`; `null` when absent |
| `process_count` | every process seen |
| `processes` | the top 30 by cores then RSS, plus every `/dev/dxg` holder. `pid`, `name`, `cores`, `rss`, `state`, `threads` (`num_threads` from `/proc/pid/stat`, `null` when it cannot be read), `rated`, `start`, and `on_gpu`, which means it holds `/dev/dxg`, not that it uses the GPU. `rated` is a boolean, false on the collector's first reading of a process, when there is no earlier time to subtract and `cores` is an unmeasured 0. `start` is `starttime`, `/proc/pid/stat` field 22, in clock ticks since the guest booted: an identity, not a counter, so `pid` and `start` together tell a reused pid apart. A row without `rated`, from an older collector, is read as not rated; without `start`, as no identity beyond `pid` |
| `dxg_pids` | PIDs with `/dev/dxg` open: identity, not usage |
| `dxg_denied` | processes whose fds could not be read, usually root's |
| `cost_ms` | time this sample took |

A reader takes a key it does not know as nothing, and a new key that is absent or `null` as unknown, never
0, so an older collector still reads. A value that cannot be measured is `null`: `NaN` is not JSON, and
the strict reader refuses the whole line for it.

Replacing the collector, in Rust for example, needs only this format.
