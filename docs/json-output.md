# JSON output

`winwsltop --once` prints one JSON object on one line. `--record N` prints N of them, one a second,
each flushed as it is taken. Nothing else goes to stdout, and normal runs write nothing to stderr.

`--once` takes about 3 s, because the first pass only primes the rates. `--help` and `--version` return
at once.

## Exit codes

| code | meaning |
|---|---|
| 0 | It ran. This includes runs with no WSL data or no GPU, so read the fields below |
| 2 | Bad arguments; usage goes to stderr |

## Reading it safely

- **Check before you trust a figure.** Use `gpu_split` only when `valid` is true. An empty `gpu_engines`
  means idle only when `gpu_engines_known` is true. `wsl.data` is `null` and `wsl.age_s` is -1 when the
  collector has sent nothing, and `gpu_split` can still be valid then. `gpu_device` leaves out any field the driver didn't answer, and `present` is false with no NVML.
- **Never add Windows and WSL figures.** They describe overlapping resources.
- **`vm_pct` is the whole WSL VM.** No field says how much GPU one Linux process used.
  `wsl.data.dxg_pids` lists who holds the GPU, which is identity, not usage.
- **The split is not an exact share.** `vm_pct` and `windows_pct` have summed to 88% and peaked at 112%
  here, and they can differ from `gpu_device.util_pct`. The difference is unexplained.
- **A zero can be a failed read.** Disk fields and CPU busy can read 0 instead of being absent:
  [`TODO.md`](TODO.md) items 22 and 24. Numbers that aren't finite print as `null`.

`schema` is 1. A change of meaning raises it, and new keys can appear without raising it, so ignore keys
you don't know. Sizes are bytes, percentages run 0 to 100 unless noted, and `cores` of 1.0 is one
logical CPU busy for the whole interval.

## Fields

| key | meaning |
|---|---|
| `schema`, `unix_ms`, `interval_s` | Format version, Windows wall clock, seconds since the last sample |
| `cpu` | Windows CPU: `busy_pct`, and `core_pct` per logical CPU |
| `memory` | Windows memory: `total`, `available`, `cache`, `commit_used`, `commit_limit` |
| `vm` | The WSL VM as Windows sees it: `vmmem_pid`, `vmwp_pid`, `vmmem_working_set`, `vmmem_cores`. The PIDs are 0 when not found |
| `processes` | Windows processes: `pid`, `ppid`, `name`, `cores`, `working_set`, `private`, `threads`, `handles` |
| `disks` | `name`, `read_bytes_s`, `write_bytes_s`, `busy_pct` |
| `nets` | `name`, `in_bytes_s`, `out_bytes_s`, `link_bits_s`, and `hardware` for a physical adapter |
| `gpu_engines_known` | Whether the engine counters could be read this sample |
| `gpu_engines` | One row per process and engine: `pid`, `engine`, `pct`, `source`. WSL's work appears under `vm.vmwp_pid`, on the 3D engine |
| `gpu_device` | NVML: `present`, `name`, `mem_used`, `mem_total`, `util_pct`, `power_w`, `power_limit_w`, `temp_c`, `sm_mhz` |
| `gpu_memory` | Per-process WDDM accounting: `pid`, `dedicated`, `shared`, `local`. `local` is the closest per-process figure, and `dedicated` is not VRAM in use |
| `gpu_split` | Derived from the engine counters: `valid`, `vm_pct`, `windows_pct`, `vm_local`, `windows_local`, `source` |
| `wsl` | `age_s`, `restarts`, `clock_known`, `clock_offset_s` (Windows unix time minus WSL's), and `data` |
| `wsl.data` | The collector's line, or `null`. Its fields are in [`PROTOCOL.md`](../wsl-collector/PROTOCOL.md) |
| `cost_ms` | Time each collector took this sample: `cpu`, `memory`, `processes`, `disk`, `net`, `gpu`, `gpu_device`, `gpu_memory` |

Which of these were checked against an independent source is in [`measurable.md`](measurable.md).
