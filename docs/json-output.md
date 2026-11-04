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
- **`lost_cpu_inferred` is an inference, not a measurement.** No counter on either side reads it. It is
  guest busy times guest threads, less `vm.vmmem_cores`, each a mean of 5 samples, clamped at 0: what the guest
  counted, less what Windows charged the VM. The answer is in vCPUs, which are not Windows cores. Use `vcpus`
  only when `state` is `known`. It is `null` otherwise, and `null` says nothing about whether WSL lost CPU.
  See "The lost CPU figure" below for the gate, the lag and what it was not tested on.
- **A zero can be a failed read.** Disk fields, CPU busy, `memory` fields, network rates after a counter
  reset, and `vm_local` and `windows_local` can read 0 instead of being absent, even with `valid` true.
  `vmmem_cores` reads 0 when it was not measured, so use it only when `vmmem_cores_known` is true;
  [`TODO.md`](TODO.md) tracks most of these. Numbers that aren't finite print as `null`.

`schema` is 1. A change of meaning raises it, and new keys can appear without raising it, so ignore keys
you don't know. Sizes are bytes, and `cores` of 1.0 is one logical CPU busy for the whole interval.
Percentages are 0 to 100, except `pct`, `vm_pct` and `windows_pct`, which can pass 100, and `vm_cpu_pct`, which
is not clamped.

## The lost CPU figure

When Windows and WSL both want the CPU the guest cannot see what it lost: it keeps reading busy, and `steal`
stays 0. `lost_cpu_inferred` subtracts across the two sides to show it, on purpose, and only inside what
[`measurable.md`](measurable.md) shows, "Guest busy times threads less `Hv VP` `% Total Run Time` tracks lost
work". That is one trial per case.

`state` says why `vcpus` is or isn't a number:

| state | `vcpus` | meaning |
|---|---|---|
| `known` | a figure, 0 or more | Every clause of the gate holds |
| `collecting` | `null` | Fewer than 5 usable samples since the last reset. `--once` takes one sample, so it is never `known` |
| `guest_under_99` | `null` | 5 samples, and at least one guest busy under 99%. This is the usual state of a machine that isn't saturated |
| `windows_under_95` | `null` | 5 samples, and the newest Windows busy under 95% |
| `vcpus_differ` | `null` | The guest doesn't have one vCPU per Windows logical CPU, as a `.wslconfig` limit gives. Outside the evidence, and it restarts the count |
| `no_data` | `null` | This sample can't count, and the count restarts: no guest line under 5 s old that parses, the same line a third time in a row, or `vm.vmmem_cores` or Windows CPU unread |

The gate, all of it on every sample: 5 finite samples since the last reset, all 5 guest busy at 99% or more,
the newest Windows busy at 95% or more, `vmmem_cores_known` true on all 5, a guest line under 5 s old, and no
more than one repeated guest line in a row. A repeated line counts as a sample carrying the guest's last
value, because under contention the two 1 Hz loops slip. A reset is more than 2.5 s between samples, a new
`vmmem_pid`, a stale or unreadable line, a vCPU count that differs from Windows', or anything else `no_data` lists.

**It lags.** A 5-sample mean takes about 5 s to rise. A Windows load that stops
while WSL stays at 100% still reads as lost for about 7 s, because Windows stays busy with the VM and the
gate holds.

**Below 1 is noise.** A full guest with nothing competing reads above 0: Windows' own work shares the same
processors.

**Outside the evidence**, so don't read a `known` figure as proven here or take its absence as no loss: a
partly busy guest, which the gate refuses, `.wslconfig` limits on vCPUs, and memory, disk or GPU load. Each
contended case had WSL at full load on a machine with no such limit, under CPU load alone.

## Fields

| key | meaning |
|---|---|
| `schema`, `unix_ms`, `interval_s` | Format version, Windows wall clock, seconds since the last sample |
| `cpu` | Windows CPU: `busy_pct`, `user_pct`, `kernel_pct`, and `core_pct` per logical CPU. `user_pct` plus `kernel_pct` is `busy_pct`. `kernel_pct` has idle taken out and still holds the WSL VM's time, because Windows counts the VM as kernel time, even when Linux runs user code |
| `cpu_layout_known` | Whether the CPU layout could be read at start |
| `cpu_layout` | One row per logical CPU, in `core_pct` order, read once at start: `core`, `thread`, `efficiency`. Rows with the same `core` are sibling threads, and `thread` is the place within that core. `efficiency` is Windows' class, higher is faster |
| `memory` | Windows memory: `total`, `available`, `cache`, `commit_used`, `commit_limit` |
| `vm` | The WSL VM as Windows sees it: `vmmem_pid`, `vmwp_pid`, `vmmem_working_set`, `vmmem_cores`, `vmmem_cores_known`. The PIDs are 0 when not found, and `vmmem_cores_known` is false then and on a new `vmmemWSL`'s first sample |
| `processes` | Windows processes: `pid`, `ppid`, `name`, `cores`, `working_set`, `private`, `threads`, `handles`, `cores_known` and `create_time`. `cores_known` is false on a process's first reading, when there is no earlier time to subtract and `cores` is an unmeasured 0. `create_time` is the process's creation time in 100 ns units since 1601-01-01 UTC, the Windows `FILETIME` epoch: an identity, not a counter, so `pid` and `create_time` together tell a reused pid apart |
| `ready_threads`, `vm_ready_threads` | Threads ready to run and not running, at one instant, and `vmmemWSL`'s among them, which are `null` when it isn't found. Noisy, so average several samples. `ready_threads` shows a shortage but not whose; only `vm_ready_threads` names a side. Neither is a count of blocked threads |
| `disks` | `name`, `read_bytes_s`, `write_bytes_s`, `busy_pct` |
| `nets` | `name`, `in_bytes_s`, `out_bytes_s`, `link_bits_s`, and `hardware` for a physical adapter |
| `gpu_engines_known` | Whether the engine counters could be read this sample |
| `gpu_engines` | One row per process and engine: `pid`, `engine`, `pct`, `source`. On the tested machine, WSL's CUDA work appeared under `vm.vmwp_pid`, on the 3D engine |
| `gpu_device` | NVML: `present`, `name`, `mem_used`, `mem_total`, `util_pct`, `power_w`, `power_limit_w`, `temp_c`, `sm_mhz` |
| `gpu_memory` | Per-process WDDM accounting: `pid`, `dedicated`, `shared`, `local`. `local` is the closest per-process figure, and `dedicated` is not VRAM in use |
| `gpu_split` | Derived from the engine counters: `valid`, `vm_pct`, `windows_pct`, `vm_local`, `windows_local`, `source` |
| `lost_cpu_inferred` | Inferred, see "The lost CPU figure": `state`, `vcpus`, `source`. `source` is always `inferred` |
| `cpu_derived` | Worked out from Windows' own figures, `source` always `derived`, each value `null` when unknown. `vm_cpu_pct` is `vmmem_cores` as percent of the logical CPUs. `own_kernel_pct` is `cpu.kernel_pct` less that, clamped at 0, so `user_pct`, `own_kernel_pct` and `vm_cpu_pct` add to `busy_pct` unless the clamp acts. `ready_threads_mean` and `vm_ready_threads_mean` are means of the last 5 samples, `null` until 5 known samples in a row exist, and after more than 2.5 s between samples. `vm_ready_threads_mean` also starts again for a new `vmmem_pid` |
| `process_totals` | The Windows whole the `processes` rows are part of, worked out from those rows in the same sample, `source` always `derived`. `cores` is the sum of `cores` over every row but pid 0 whose `cores_known` is true, `null` when no row is. `working_set` is the sum of `working_set` over every row but pid 0, `null` when `processes` is empty, a failed read. A shared page counts once per process, so `working_set` is not memory in use, and `cores` is not `cpu.busy_pct` as cores: [`measurable.md`](measurable.md), "Process cores account for Windows busy". It is never a WSL figure and never to be added to one |
| `wsl` | `age_s`, `restarts`, `clock_known`, `clock_offset_s` (Windows unix time minus WSL's), and `data` |
| `wsl.data` | The collector's line, or `null`. Its fields are in [`PROTOCOL.md`](../wsl-collector/PROTOCOL.md) |
| `cost_ms` | Time each collector took this sample: `cpu`, `memory`, `processes`, `disk`, `net`, `gpu`, `gpu_device`, `gpu_memory` |

Which of these were checked against an independent source is in [`measurable.md`](measurable.md).
