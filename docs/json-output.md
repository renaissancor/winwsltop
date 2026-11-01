# JSON output

`winwsltop --once` prints one JSON object on one line. `--record N` prints N of them, one a second,
each flushed as it is taken. Nothing else goes to stdout, and normal runs write nothing to stderr. Text
is UTF-8 whatever the console's code page, and lines end in CRLF, so a shell `read` loop keeps a `\r`.

`--once` takes about 3 s, because the first pass only primes the rates, and about 8 s when WSL doesn't
answer. A stopped distro gets started, which can take longer, and the run may then report no WSL data.
`--help` and `--version` return at once.

## Exit codes

| code | meaning |
|---|---|
| 0 | It ran. This includes runs with no WSL data or no GPU, so read the fields below |
| 1 | Stdout closed before the run finished, for example the reader of a pipe went away. From a WSL shell, WSL's own pipe relay decides instead: the exit code can be 141 or 0, and with `>&-` a run goes to its end, which a live run with no mode flag never reaches |
| 2 | Bad arguments: an unknown option, a `--record` value that isn't a whole number from 1 to 2147483647, a `--distro` name that is empty, starts with `-`, or holds spaces or double quotes, or more than one of `--text`, `--once` and `--record`. Usage goes to stderr |

## Reading it safely

- **Check before you trust a figure.** Use `gpu_split` only when `valid` is true. An empty `gpu_engines`
  means idle only when `gpu_engines_known` is true. `wsl.data` is `null` and `wsl.age_s` is -1 when the
  collector has sent nothing, and `gpu_split` can still be valid then. A dead collector's last line stays
  in `wsl.data`, so treat it as stale when `wsl.age_s` is above 5, which is when the screen stops using it.
  `gpu_device` leaves out readings the driver didn't answer, `present` is false with no NVML, and
  `power_limit_w` can read 0 when only the limit failed. `cpu_layout` is empty when `cpu_layout_known` is
  false. `ready_threads`, `vm_ready_threads`, `user_pct` and `kernel_pct` are `null` when unknown, never 0.
- **Never add Windows and WSL figures.** They describe overlapping resources.
- **`vm_pct` is the whole WSL VM.** No field says how much GPU one Linux process used.
  `wsl.data.dxg_pids` lists who holds the GPU, and `on_gpu` on a WSL process means the same. Both are
  identity, not usage.
- **The split is not an exact share.** `vm_pct` and `windows_pct` have summed to 88% and peaked at 112%
  here, and they can differ from `gpu_device.util_pct`. The difference is unexplained.
- **A zero can be a failed read.** Disk fields, CPU busy, `memory` fields, network rates after a counter
  reset, and `vm_local` and `windows_local` can read 0 instead of being absent, even with `valid` true.
  `vmmem_cores` reads 0 when it was not measured, so use it only when `vmmem_cores_known` is true;
  [`TODO.md`](TODO.md) tracks most of these. Numbers that aren't finite print as `null`.

`schema` is 1. A change of meaning raises it, and new keys can appear without raising it, so ignore keys
you don't know. Sizes are bytes, and `cores` of 1.0 is one logical CPU busy for the whole interval.
Percentages are 0 to 100, except `pct`, `vm_pct` and `windows_pct`, which can pass 100.

## Fields

| key | meaning |
|---|---|
| `schema`, `unix_ms`, `interval_s` | Format version, Windows wall clock, seconds since the last sample |
| `cpu` | Windows CPU: `busy_pct`, `user_pct`, `kernel_pct`, and `core_pct` per logical CPU. `user_pct` plus `kernel_pct` is `busy_pct`. `kernel_pct` has idle taken out and still holds the WSL VM's time, because Windows counts the VM as kernel time, even when Linux runs user code |
| `cpu_layout_known` | Whether the CPU layout could be read at start |
| `cpu_layout` | One row per logical CPU, in `core_pct` order, read once at start: `core`, `thread`, `efficiency`. Rows with the same `core` are sibling threads, and `thread` is the place within that core. `efficiency` is Windows' class, higher is faster |
| `memory` | Windows memory: `total`, `available`, `cache`, `commit_used`, `commit_limit` |
| `vm` | The WSL VM as Windows sees it: `vmmem_pid`, `vmwp_pid`, `vmmem_working_set`, `vmmem_cores`, `vmmem_cores_known`. The PIDs are 0 when not found, and `vmmem_cores_known` is false then and on a new `vmmemWSL`'s first sample |
| `processes` | Windows processes: `pid`, `ppid`, `name`, `cores`, `working_set`, `private`, `threads`, `handles` |
| `ready_threads`, `vm_ready_threads` | Threads ready to run and not running, at one instant, and `vmmemWSL`'s among them, which are `null` when it isn't found. Noisy, so average several samples. `ready_threads` shows a shortage but not whose; only `vm_ready_threads` names a side. Neither is a count of blocked threads |
| `disks` | `name`, `read_bytes_s`, `write_bytes_s`, `busy_pct` |
| `nets` | `name`, `in_bytes_s`, `out_bytes_s`, `link_bits_s`, and `hardware` for a physical adapter |
| `gpu_engines_known` | Whether the engine counters could be read this sample |
| `gpu_engines` | One row per process and engine: `pid`, `engine`, `pct`, `source`. On the tested machine, WSL's CUDA work appeared under `vm.vmwp_pid`, on the 3D engine |
| `gpu_device` | NVML: `present`, `name`, `mem_used`, `mem_total`, `util_pct`, `power_w`, `power_limit_w`, `temp_c`, `sm_mhz` |
| `gpu_memory` | Per-process WDDM accounting: `pid`, `dedicated`, `shared`, `local`. `local` is the closest per-process figure, and `dedicated` is not VRAM in use |
| `gpu_split` | Derived from the engine counters: `valid`, `vm_pct`, `windows_pct`, `vm_local`, `windows_local`, `source` |
| `wsl` | `age_s`, `restarts`, `clock_known`, `clock_offset_s` (Windows unix time minus WSL's), and `data` |
| `wsl.data` | The collector's line, or `null`. Its fields are in [`PROTOCOL.md`](../wsl-collector/PROTOCOL.md) |
| `cost_ms` | Time each collector took this sample: `cpu`, `memory`, `processes`, `disk`, `net`, `gpu`, `gpu_device`, `gpu_memory` |

Which of these were checked against an independent source is in [`measurable.md`](measurable.md).
