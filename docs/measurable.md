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
| WSL CUDA compute shows as a Compute engine | **False here.** It appeared as `3D` | same |
| Per-process GPU use is listed by `nvidia-smi` inside WSL | Not listed: `--query-compute-apps` and `pmon` both empty under load | run 2, live query, no raw file |
| `/dev/dxg` holders identify the Linux process | Observed: the load's PID was the only holder under load. Identity only, no usage | run 2, `/proc/*/fd` scan, no raw file |
| At idle, `vmmemWSL` shows no non-zero GPU engine | Observed 2026-10-31, 3 s, 485 instances, no WSL GPU load | `counter_probe.exe 3` |
| WSL whole-GPU util agrees with Windows engine counters | Under load close (99% vs about 104%). Not at idle: `nvidia-smi` read 16-21% while Windows engines summed well under that. Different definitions or a bad reading; unresolved | run 2; the `nvidia-smi` side has no raw file |

## Windows collector checked against Windows (2026-10-31)

Read a few seconds apart on a live machine, so small differences are expected. No raw file was
saved for this section; the numbers were read live.

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

No raw file was saved for this section; the numbers were read live.

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
| WSL only | 91-105% (t=7-20) | - | Windows desktop about 1% |
| Windows only | absent | 71-97% | |
| both | 49-54% | 34-35% | |

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
| NVML loads without an SDK, without elevation, from `System32` | Observed | `gpu_probe.exe`. `nvmlInit_v2` makes construction take 626 ms in `probe-idle-2026-10-31.txt`; a 685 ms figure quoted earlier has no raw file. A sample takes about 1 ms |
| A missing or wrong `nvml.dll` leaves `present` false without a crash | Observed, with the load redirected | `nvml_absent.exe missing` and `wrong` |
| The `NVSMI` fallback path works | **Not checked.** That directory does not exist here | |
| A failing `nvmlInit_v2` or device handle is handled | **Not checked.** Those branches have not run | |
| NVML v1 `used` includes driver-reserved memory | Observed: 1744 vs 1472 MiB, difference 272 MiB, `memory.reserved` 272 MiB. v2 is what `nvidia-smi` reports, so the collector uses v2 | `probe-idle-2026-10-31.txt` |
| The memory collector reads what `Get-Counter` reads | Observed: 77 of 78 pid readings fell between two probe reads, over 3 rounds with 26 pids. The other was Chrome, moving non-monotonically (318, 302, 326 MB over the three reads) | `compare-idle-2026-10-31.txt` |
| `GPU Process Memory` is readable on first collection, non-elevated | Observed: one-round probes returned 26 pids | same |
| `Dedicated Usage` measures VRAM in use | **False here.** Summed over processes 4476-4564 MiB while NVML used about 1480 MiB and `GPU Adapter Memory` dedicated about 1400 MiB. One process, `dwm`, alone showed about 3.3 GiB dedicated with 243-271 MiB `Local Usage`. Cause not established | same |
| Any of the memory figures sum to one another | **No.** In one round, process `Local Usage` summed to 939 MiB, adapter local 769 MiB, adapter dedicated 1398 MiB, NVML used about 1480 MiB | same |
| WSL GPU memory shows under `vmwp.exe` | Only seen at idle, 15.1 MiB (15810560 bytes) dedicated, equal to its `Local Usage`. Not checked with a WSL allocation | `compare-idle-2026-10-31.txt`, first `Get-Counter` read |
| One GPU has one LUID | **No.** Two appear. The second's instances read zero; how many it had is not recoverable from the file. `Win32_VideoController` lists only the RTX 4070; what it is remains unknown. `gpu_memory_row` sums across LUIDs, which changes nothing while the second stays zero | same |

## Disk and network collectors checked

Run 2026-11-01, non-elevated, on ambient traffic only: this experiment started nothing and ran no GPU
load. Disk 0 is a Samsung SSD 980 PRO 2TB on NVMe (instance `0 C:`), disk 1 a SanDisk Portable SSD on
USB (`1 D:`), the NIC an Intel I225-V linked at 100 Mbps. One session: 3 concurrent windows of 60
rounds, 10 network brackets, so a figure here is one state of the machine, not a range of states. Raw
output is in `experiments/03-host-counters/results/`; `analysis-2026-11-01.txt` is computed from the
others by `analyze.py`. Ambient traffic in the three windows was 738844721, 738995985 and 740052050
bytes in on the NIC and 1438.13, 1397.67 and 1223.15 MiB written to disk 0. Its source was not
identified.

Disk, `src/host/disk.cpp`:

| claim | status | evidence |
|---|---|---|
| Disk bytes match a count outside Windows | Observed on disk 0, 3 windows, a reading independent of `\PhysicalDisk`: the NVMe health log's Data Units, kept by the drive, readable non-elevated. Collector over drive: read 1.0002, 1.0038, 0.9982; write 1.0001, 0.9998, 0.9980. Totals read 303.35 against 303.22 MiB, write 4058.94 against 4061.52 MiB. This also fixes the unit as bytes per second | `analysis-2026-11-01.txt`, `window1-probe-2026-11-01.txt`, `window2-probe-2026-11-01.txt`, `window3-probe-2026-11-01.txt` |
| The difference from the drive stays inside one Data Unit (512000 bytes) | **No.** Five of six comparisons were at or under 0.58 units; window 3 write was 5.00. Cause `Unresolved` | `analysis-2026-11-01.txt` |
| The collector reads what `Get-Counter` reads | Observed, 3 windows, overlap 60.562-60.764 s: its raw delta lay between the inner and outer `Get-Counter` deltas, and likewise for WMI, 12 of 12 on disk 0. Rate times time was within 105.6 ppm of that raw delta. `Get-Counter` reads the same provider, so this proves bindings, units and instance handling, not the disk | `analysis-2026-11-01.txt`, `window1-counters-2026-11-01.txt`, `window2-counters-2026-11-01.txt`, `window3-counters-2026-11-01.txt` |
| WMI `MSDiskDriver_Performance` is a second measurement | **No.** It is readable non-elevated, but its `BytesRead` equalled the nearest `Get-Counter` raw value in 71, 66 and 62 of 75 samples on disk 0 while both moved. A second path to one count | `analysis-2026-11-01.txt`, `window1-counters-2026-11-01.txt` |
| Busy (100 minus `% Idle Time`) reads true | Bindings only, 3 windows: mean busy on disk 0 was 4.166, 3.263 and 2.580 in the collector, within 0.102 points of `Get-Counter` and WMI, peak 24.954. Against the disk itself `Not checked`: no reading outside that count was found | `analysis-2026-11-01.txt` |
| Instance names and `_Total` handling | Observed, 3 windows: the collector returned `0 C:` and `1 D:`, never `_Total`, while the raw array held `_Total` in every round. `_Total` equalled the sum of the two in all readings (0 of 122 differ, each window). `Get-Counter` prints the same names in lower case | `analysis-2026-11-01.txt`, `identity-2026-11-01.txt` |
| `% Idle Time` exceeds 100 and the clamp fires | Observed on disk 1 only: above 100 in 103 of 180 rounds, largest 100.080000, always with no traffic on that disk (clamp rounds with traffic: 0). `Get-Counter` also read above 100, in 120 of 225 samples. Disk 0 never did, largest 99.351811. Busy matched `v > 100 ? 0 : 100 - v` in every round. Why it exceeds 100 is `Unresolved` | `analysis-2026-11-01.txt` |
| Busy on a disk with no traffic | Observed: up to 0.073 points on disk 1 with no traffic in any window, so a reading that small is noise | `analysis-2026-11-01.txt` |
| Disk 1 bytes read true | `Not checked`. Read and write stayed at 0 in all 3 windows, so agreement at zero proves nothing, and the NVMe query fails on it with error 1 | `analysis-2026-11-01.txt`, `identity-2026-11-01.txt` |
| A disk sample is cheap | Observed: mean 0.226-0.292 ms a window, largest 1.952 ms. The priming call returned false with no rows, each window | `analysis-2026-11-01.txt` |

Network, `src/host/network.cpp`:

| claim | status | evidence |
|---|---|---|
| What the row filter keeps and drops | Observed: of 64 rows it keeps 6 and drops 24 as filters, 33 as not up, 1 as loopback. The rows returned equalled that kept set in 60 of 60 rounds, each window | `identity-2026-11-01.txt`, `analysis-2026-11-01.txt` |
| Dropped rows would double-count | Observed: 44 rows have `FilterInterface` set and 10 carry octets, 5 on the NIC and 5 on `vEthernet`, repeating their adapter's traffic (`vEthernet` and its five all read 2047992023 in). Dropped loopback and not-up rows held zero octets | `identity-2026-11-01.txt` |
| Rows kept but always zero | Three WAN Miniports (link 0) and `vSwitch (WSL (Hyper-V firewall))` (10000000000 bit/s) stay in the output. All are `Hidden` in `Get-NetAdapter`; the filter does not look at that. Their octets stayed at 0 in all 3 windows, so whether they read true is `Not checked` | `identity-2026-11-01.txt`, `analysis-2026-11-01.txt` |
| Rates are octet delta over elapsed time | Observed, 3 windows: octets the collector read lay outside the raw reads either side of its sample in 0 of 720 checks, and its rate differed from delta over elapsed in 0 of 720, each window | `analysis-2026-11-01.txt`, `window1-probe-2026-11-01.txt`, `window2-probe-2026-11-01.txt`, `window3-probe-2026-11-01.txt` |
| Raw octets are what `Get-NetAdapterStatistics` reads | Observed: inside the raw bracket in 10 of 10 rounds for the NIC and `vEthernet`, received and sent, with raw moving in all 10. Bindings and units; whether the two share a source was not established. It has no row for the three WAN Miniports or `vSwitch` | `net-bracket-2026-11-01.txt`, `analysis-2026-11-01.txt` |
| `vEthernet` octets match a count outside Windows | Observed, 3 windows, a reading independent of Windows: guest `eth0` tx fell inside `vEthernet` in, and rx inside out, 6 of 6. Window 2: in 749737 to 750067 against tx 749935. Volumes were small, 262256 to 750067 bytes in and 182548 to 373756 out | `window1-edges-2026-11-01.txt`, `window2-edges-2026-11-01.txt`, `window3-edges-2026-11-01.txt`, `analysis-2026-11-01.txt` |
| Physical NIC octets match a count outside Windows | `Not checked`: no reading outside Windows was available. The peak, 98.92% of the 100000000 bit/s link, fits the link and proves nothing further | `analysis-2026-11-01.txt` |
| Link speed and hardware flag | Observed: `TransmitLinkSpeed` and `HardwareInterface` agree with `Get-NetAdapter` on all 19 joined rows, 0 differ; its speed is empty for the WAN Miniports. Receive and transmit speed differ on 0 of 64 rows, so the collector loses nothing by reading one | `identity-2026-11-01.txt`, `analysis-2026-11-01.txt` |
| How WSL's adapter appears | Observed: `vEthernet (WSL (Hyper-V firewall))`, `Hyper-V Virtual Ethernet Adapter`, 10000000000 bit/s, `HardwareInterface` False, `Virtual` True, `Hidden` False. Only `Wi-Fi` and the wired NIC have the hardware flag | `identity-2026-11-01.txt` |
| Traffic on `vEthernet` is also counted on the NIC | `Unresolved`. `vEthernet` in never exceeded NIC out, nor `vEthernet` out NIC in, in 180 seconds. Per-second correlation of `vEthernet` in with NIC out was 0.068, 0.738, 0.355; of `vEthernet` out with NIC in 0.013, -0.049, 0.053. That fits overlap and does not show it | `analysis-2026-11-01.txt` |
| Wi-Fi reads true | `Not checked`: `Disconnected`, dropped as not up, zero octets. It still reported 287000000 bit/s | `identity-2026-11-01.txt` |
| A network sample is cheap | Observed: mean 1.020-1.374 ms a window, largest 12.006 ms, over 64 rows. The priming call returned false with no rows, each window | `analysis-2026-11-01.txt` |

## WSL clock rate (2026-10-31)

No raw file was saved for this section; the numbers were read live.

| check | result |
|---|---|
| WSL `CLOCK_MONOTONIC` against Windows wall clock | **3.68% fast** over a 40 s `sleep`: 40.24 s in WSL, 38.81 s on Windows. `wt-wsl`'s ping fit agrees at about 3.7% |
| WSL wall clock against Windows wall clock | -2.21% over the same window, because it is stepped back: a +1.1 s offset jump was seen once |
| Cause | Unknown. Which clock is right was not checked against an outside reference; this is one machine, one load state |

Consequences: WSL durations are not interchangeable with Windows durations, WSL wall time can jump,
and the rate may explain the 645-matmul second in experiment 01. Align through `clock_offset` only.
