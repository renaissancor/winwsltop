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
Clock alignment is not implemented.

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

Anomaly: the last WSL second printed 645 matmuls, double the rate. The loop uses `time.time()`, so a
backward wall-clock step of about 1 s would explain it; that is unconfirmed. Use `CLOCK_MONOTONIC`.
