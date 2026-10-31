# What can actually be measured

One row per claim, only from observation on this machine. Unknown stays unknown.

Machine: Windows build 26200, WSL2 kernel 6.6.114.1, 12 GiB NVIDIA GPU (200 W limit), 20 threads.

| claim | status | evidence |
|---|---|---|
| Whole-GPU util, memory, power, temperature, SM clock and PCIe are visible inside WSL | Observed 2026-10-31; accuracy against Windows not checked, source (NVML or nvidia-smi) not checked | `sshvllmtop --once` in WSL |
| `vmmemWSL` is visible from Windows | Observed 2026-10-31 | `Get-Process *vmmem*` |
| The PDH `GPU Engine` counter is readable without elevation | Observed 2026-10-31 | PDH probe, non-elevated |
| WSL GPU work appears under the `vmmemWSL` PID in `GPU Engine` | **False here.** `vmmemWSL` stayed at 0 under load | `experiments/01-gpu-attribution/results/02-wsl-cuda-only-2026-10-31.txt` |
| WSL GPU work appears under `vmwp.exe`, the parent of `vmmemWSL` | Observed once, 2026-10-31: PID 11736, present only t=6-20, the 15 s load, peak 104.52%. Must hold under runs 3 and 4 | same |
| WSL CUDA compute shows as a Compute engine | **False here.** It appeared as `engtype_3D` | same |
| Per-process GPU use is listed by `nvidia-smi` inside WSL | Not listed: `--query-compute-apps` and `pmon` both empty under load | run 2, live query |
| `/dev/dxg` holders identify the Linux process | Observed: the load's PID was the only holder under load. Identity only, no usage | run 2, `/proc/*/fd` scan |
| At idle, `vmmemWSL` shows no non-zero GPU engine | Observed 2026-10-31, 3 s, 485 instances, no WSL GPU load | `counter_probe.exe 3` |
| WSL whole-GPU util agrees with Windows engine counters | Under load close (99% vs about 104%). Not at idle: `nvidia-smi` read 16-21% while Windows engines summed well under that. Different definitions or a bad reading; unresolved | run 2 |

## Windows collector checked against Windows (2026-10-31)

Read a few seconds apart on a live machine, so small differences are expected.

| value | `winwsltop` | Windows |
|---|---|---|
| total memory | 63.75 GiB | 63.75 GiB |
| processes | 360 | 360 |
| threads | 6516 | 6553 |
| CPU busy | 6.44% | 5.62% (PDH, later window) |
| `vmmemWSL` working set | 10.15 GiB | 10.15 GiB |

Per-process CPU adds up: 0.96 cores across processes against 1.09 from the CPU counters. A sample
cost about 9 ms of collection; `GPU Engine` took 0.53 ms over its 485 instances, far below what was
feared, and `NtQuerySystemInformation` took 6.42 ms.

## WSL collector checked (2026-10-31)

| check | result |
|---|---|
| Live line reaches `winwsltop` | Yes, 0.08-0.17 s old each sample; collector cost 3.46 ms |
| A process with `/dev/dxg` open is found | Yes, a test holder doing no GPU work was the only entry |
| `/dev/dxg` scan is complete without root | **No.** 36 of about 64 processes could not be read (`dxg_denied`), so a root GPU process would be missed |
| Killed collector recovers | Yes, within about 3 s, with `restarts` counted |
| Collector ends with `winwsltop` | Yes, none left after a normal exit |
| Guest memory vs host | Guest limit 31.22 GiB of 63.75 GiB host. Guest used 2 GiB and cache 7 GiB against about 10 GiB `vmmemWSL`, read minutes apart |

The -612 ms between WSL and Windows timestamps in one sample is mostly sample age, not clock offset.
Clock alignment was added afterwards; see the WSL clock rate section below and `wsl-collector/PROTOCOL.md`.

## Experiment 01 result (2026-10-31)

Each case ran once on an RTX 4070 with both loads at full rate. Raw files are in
`experiments/01-gpu-attribution/results/`.

| case | `vmwp.exe` 3D | load process 3D | other |
|---|---|---|---|
| WSL only | 95-105% | - | Windows desktop about 1% |
| Windows only | absent | 80-97% | |
| both | 49-54% | 35-37% | |

| claim | status |
|---|---|
| Windows can separate WSL's GPU work from its own at VM level | Observed: the two moved together in step, in both directions |
| Shares add to 100% | **No.** Both together summed to about 88%; peaks reached 112%, so these are not exact shares |
| Contention slows WSL | Observed: 306 to about 164 matmuls/s (-46%), worst case 4.7 to 9.1 ms |
| Contention slows Windows | Observed: about 59% less work per second. Dispatch time alone hides this, because `d3d_load` calibrated smaller under contention (421 vs 659 groups, same 30 ms) |
| Which Linux process used how much | Not measurable from this: all WSL work is one PID on the Windows side |

Anomaly: the last WSL second printed 645 matmuls, double the rate. `wsl_load.py` has used
`time.monotonic()` since its first commit, so a wall-clock step does not explain it. The WSL
`CLOCK_MONOTONIC` rate (below) is a candidate; the cause is unconfirmed.

## GPU device and memory collectors checked (2026-10-31)

RTX 4070, driver 610.88, idle desktop, no GPU load started. Raw output is in
`experiments/02-gpu-device/results/`. `nvidia-smi` is an NVML client, so the first table proves the
bindings and units, not the hardware.

Five rounds, `nvidia-smi` read 100-200 ms after the collector:

| value | collector | `nvidia-smi` | result |
|---|---|---|---|
| power | 11.30-27.69 W | same | equal in 5 of 5 |
| power limit | 200.00 W | 200.00 W | equal in 5 of 5 |
| temperature | 41-42 C | same | equal in 5 of 5 |
| SM clock | 210, one 2520 MHz | same | equal in 5 of 5, including the jump |
| utilization | 4-22% | same | equal in 5 of 5 |
| memory total | 12282 MiB | 12282 MiB | equal in 5 of 5 |
| memory used | 1469-1513 MiB | 1470-1511 MiB | within 2 MiB in 4 of 5; 22 MiB apart in one, on a changing value |

| claim | status | evidence |
|---|---|---|
| NVML loads without an SDK, without elevation, from `System32` | Observed | `gpu_probe.exe`. `nvmlInit_v2` makes construction take 626-685 ms; a sample takes about 1 ms |
| A missing or wrong `nvml.dll` leaves `present` false without a crash | Observed, with the load redirected | `nvml_absent.exe missing` and `wrong` |
| The `NVSMI` fallback path works | **Not checked.** That directory does not exist here | |
| A failing `nvmlInit_v2` or device handle is handled | **Not checked.** Those branches have not run | |
| NVML v1 `used` includes driver-reserved memory | Observed: 1744 vs 1472 MiB, difference 272 MiB, `memory.reserved` 272 MiB. v2 is what `nvidia-smi` reports, so the collector uses v2 | `probe-idle-2026-10-31.txt` |
| The memory collector reads what `Get-Counter` reads | Observed: 77 of 78 pid readings fell between two probe reads, over 3 rounds with 26 pids. The other was Chrome, moving non-monotonically (318, 326, 302 MiB) | `compare-idle-2026-10-31.txt` |
| `GPU Process Memory` is readable on first collection, non-elevated | Observed: one-round probes returned 26 pids | same |
| `Dedicated Usage` measures VRAM in use | **False here.** Summed over processes 4476-4569 MiB while NVML used about 1480 MiB and `GPU Adapter Memory` dedicated about 1400 MiB. One process, `dwm`, alone showed about 3.5 GiB dedicated with 266 MiB `Local Usage`. Cause not established | same |
| Any of the memory figures sum to one another | **No.** In one round, process `Local Usage` summed to 939 MiB, adapter local 769 MiB, adapter dedicated 1398 MiB, NVML used about 1480 MiB | same |
| WSL GPU memory shows under `vmwp.exe` | Only seen at idle, 15.1 MiB (15810560 bytes) dedicated, equal to its `Local Usage`. Not checked with a WSL allocation | `compare-idle-2026-10-31.txt`, first `Get-Counter` read |
| One GPU has one LUID | **No.** Two appear. The second had 9 process instances, all zero, and `Win32_VideoController` lists only the RTX 4070; what it is remains unknown. `gpu_memory_row` sums across LUIDs, which changes nothing while the second stays zero | same |

## WSL clock rate (2026-10-31)

| check | result |
|---|---|
| WSL `CLOCK_MONOTONIC` against Windows wall clock | **3.68% fast** over a 40 s `sleep`: 40.24 s in WSL, 38.81 s on Windows. `wt-wsl`'s ping fit agrees at about 3.7% |
| WSL wall clock against Windows wall clock | -2.21% over the same window, because it is stepped back: a +1.1 s offset jump was seen once |
| Cause | Unknown. Which clock is right was not checked against an outside reference; this is one machine, one load state |

Consequences: WSL durations are not interchangeable with Windows durations, WSL wall time can jump,
and the rate may explain the 645-matmul second in experiment 01. Align through `clock_offset` only.
