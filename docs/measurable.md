# What can actually be measured

One row per claim, only from observation on this machine. Unknown stays unknown.

Machine: Windows build 26200.9711, WSL2 kernel 6.6.114.1, RTX 4070 with 12282 MiB (200 W limit), driver 610.88,
i5-13600K with 6 P-cores and 8 E-cores, 20 threads.

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
| Guest memory vs host | Guest limit 31.22 GiB of 63.75 GiB host. Guest used 2 GiB and cache 7 GiB against about 10 GiB `vmmemWSL`, read minutes apart. Read in one sample on 2026-11-02: section "Memory and process collectors checked" |

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
others by `analyze.py`. Ambient traffic in the three windows, as deltas of the counters the
collector read, was 738844711, 738995994 and 740052056 bytes in on the NIC and 1507995136, 1465581568
and 1282579968 bytes written to disk 0. Its source was not identified.

Disk, `src/host/disk.cpp`:

| claim | status | evidence |
|---|---|---|
| Disk bytes match a count outside Windows | Observed on disk 0, 3 windows, a reading independent of `\PhysicalDisk`: the NVMe health log's Data Units, kept by the drive, readable non-elevated. Collector rate times time over drive: read 1.0002, 1.0038, 0.9982; write 1.0001, 0.9998, 0.9980, a worst error of 0.38% read, 0.23 units on a 61-unit count and inside the drive's resolution, and 0.20% write, 5.00 units and outside it (next row). Totals read 303.35 against 303.22 MiB, write 4058.94 against 4061.52 MiB. This also fixes the unit as bytes per second | `analysis-2026-11-01.txt`, `window1-probe-2026-11-01.txt`, `window2-probe-2026-11-01.txt`, `window3-probe-2026-11-01.txt` |
| The difference from the drive stays inside one Data Unit (512000 bytes) | **No.** Five of six comparisons were at or under 0.58 units; window 3 write was 5.00. Cause `Unresolved` | `analysis-2026-11-01.txt` |
| The collector reads what `Get-Counter` reads | Observed, 3 windows, overlap 60.562-60.764 s: its raw delta lay between the inner and outer `Get-Counter` deltas, and likewise for WMI, 12 of 12 on disk 0. The bracket is one sample wide at each end, 0.59% to 48.83% of the value for `Get-Counter` and 0.59% to 50.25% for WMI, so it is a coarse test. Tighter: each `Get-Counter` and WMI cumulative value lay between the collector's two adjacent reads, 356 of 356 for each source. Rate times time was within 105.6 ppm of that raw delta. `Get-Counter` reads the same provider, so this proves bindings, units and instance handling, not the disk | `analysis-2026-11-01.txt`, `window1-counters-2026-11-01.txt`, `window2-counters-2026-11-01.txt`, `window3-counters-2026-11-01.txt` |
| WMI `MSDiskDriver_Performance` is a second measurement | **No.** It is readable non-elevated, but its `BytesRead` equalled the nearest `Get-Counter` raw value in 71, 66 and 62 of 75 samples on disk 0 while both moved. A second path to one count | `analysis-2026-11-01.txt`, `window1-counters-2026-11-01.txt`, `window2-counters-2026-11-01.txt`, `window3-counters-2026-11-01.txt` |
| Busy (100 minus `% Idle Time`) reads true | Bindings only, 3 windows: mean busy on disk 0 was 4.166, 3.263 and 2.580 in the collector, within 0.102 points of `Get-Counter` and WMI, peak 24.954. Against the disk itself `Not checked`: no reading outside that count was found | `analysis-2026-11-01.txt` |
| Instance names and `_Total` handling | Observed, 3 windows: the collector returned `0 C:` and `1 D:`, never `_Total`, while the raw array held `_Total` in every round. `_Total` equalled the sum of the two in all readings (0 of 122 differ, each window). `Get-Counter` prints the same names in lower case | `analysis-2026-11-01.txt`, `identity-2026-11-01.txt`, `window1-counters-2026-11-01.txt` |
| `% Idle Time` exceeds 100 and the clamp fires | Observed on disk 1 only: above 100 in 103 of 180 rounds, largest 100.080000, on a disk with no traffic in any of the 180 rounds, so whether the clamp fires under traffic is `Not checked`. `Get-Counter` also read above 100, in 120 of 225 samples. Disk 0 never did, largest 99.351811. Busy matched `v > 100 ? 0 : 100 - v` in every round. Why it exceeds 100 is `Unresolved` | `analysis-2026-11-01.txt` |
| Busy on a disk with no traffic | Observed: up to 0.073 points on disk 1 with no traffic in any window, so a reading that small is noise | `analysis-2026-11-01.txt` |
| Disk 1 bytes read true | `Not checked`. Read and write stayed at 0 in all 3 windows, so agreement at zero proves nothing, and the NVMe query fails on it with error 1 | `analysis-2026-11-01.txt`, `identity-2026-11-01.txt` |
| A disk sample is cheap | Observed: mean 0.226-0.292 ms a window, largest 1.952 ms. The priming call returned false with no rows, each window | `analysis-2026-11-01.txt` |

Network, `src/host/network.cpp`:

| claim | status | evidence |
|---|---|---|
| What the row filter keeps and drops | Observed: of 64 rows it keeps 6 and drops 24 as filters, 33 as not up, 1 as loopback. The rows returned equalled that kept set in 60 of 60 rounds, each window | `identity-2026-11-01.txt`, `analysis-2026-11-01.txt` |
| Dropped rows would double-count | Observed: 44 rows have `FilterInterface` set and 10 carry octets, 5 on the NIC and 5 on `vEthernet`, repeating their adapter's traffic (`vEthernet` and its five all read 2047992023 in). Dropped loopback and not-up rows held zero octets | `identity-2026-11-01.txt`, `analysis-2026-11-01.txt` |
| Rows kept but always zero | Three WAN Miniports (link 0) and `vSwitch (WSL (Hyper-V firewall))` (10000000000 bit/s) stay in the output. All are `Hidden` in `Get-NetAdapter`; the filter does not look at that. Their octets stayed at 0 in all 3 windows, so whether they read true is `Not checked` | `identity-2026-11-01.txt`, `analysis-2026-11-01.txt` |
| Rates are octet delta over elapsed time | Observed, 3 windows, on the NIC and `vEthernet`, the two rows that moved: octets the collector read lay outside the raw reads either side of its sample in 0 of 240 checks, and its rate differed from delta over elapsed in 0 of 240, each window. Octets moved in all 240. The four always-zero rows add 480 checks at zero, which prove nothing | `analysis-2026-11-01.txt`, `window1-probe-2026-11-01.txt`, `window2-probe-2026-11-01.txt`, `window3-probe-2026-11-01.txt` |
| Raw octets are what `Get-NetAdapterStatistics` reads | Observed: inside the raw bracket in 10 of 10 rounds for the NIC and `vEthernet`, received and sent, with raw moving in all 10. Bindings and units; whether the two share a source was not established. It has no row for the three WAN Miniports or `vSwitch` | `net-bracket-2026-11-01.txt`, `analysis-2026-11-01.txt` |
| `vEthernet` octets match a count outside Windows | Observed, 3 windows, a reading independent of Windows: guest `eth0` tx fell inside `vEthernet` in, and rx inside out, 6 of 6. Window 2: in 749737 to 750067 against tx 749935. Volumes were small, 262256 to 750067 bytes in and 182548 to 373756 out | `window1-edges-2026-11-01.txt`, `window2-edges-2026-11-01.txt`, `window3-edges-2026-11-01.txt`, `analysis-2026-11-01.txt` |
| Physical NIC octets match a count outside Windows | `Not checked`: no reading outside Windows was available. The peak, 98.92% of the 100000000 bit/s link, fits the link and proves nothing further | `analysis-2026-11-01.txt` |
| Link speed and hardware flag | Bindings only: `HardwareInterface` agrees with `Get-NetAdapter` on all 19 joined rows, 0 differ. `TransmitLinkSpeed` agrees on the 9 rows where `Get-NetAdapter` gives a speed, 0 differ, though 4 of those 9 are at speed 0; it gives none on the other 10, 8 WAN Miniports and 2 Wi-Fi Direct adapters. Both are Windows' reading of the same adapter, so this proves bindings and units, not the link. Receive and transmit speed differ on 0 of 64 rows, so the collector loses nothing by reading one | `identity-2026-11-01.txt`, `analysis-2026-11-01.txt` |
| How WSL's adapter appears | Observed: `vEthernet (WSL (Hyper-V firewall))`, `Hyper-V Virtual Ethernet Adapter`, 10000000000 bit/s, `HardwareInterface` False, `Virtual` True, `Hidden` False. Only `Wi-Fi` and the wired NIC have the hardware flag | `identity-2026-11-01.txt` |
| Traffic on `vEthernet` is also counted on the NIC | `Unresolved`. `vEthernet` in never exceeded NIC out, nor `vEthernet` out NIC in, in 180 seconds. Per-second correlation of `vEthernet` in with NIC out was 0.068, 0.738, 0.355; of `vEthernet` out with NIC in 0.013, -0.049, 0.053. That fits overlap and does not show it | `analysis-2026-11-01.txt` |
| Wi-Fi reads true | `Not checked`: `Disconnected`, dropped as not up, zero octets. It still reported 287000000 bit/s | `identity-2026-11-01.txt` |
| A network sample is cheap | Observed: mean 1.020-1.374 ms a window, largest 12.006 ms, over 64 rows. The priming call returned false with no rows, each window | `analysis-2026-11-01.txt` |

## CPU counters checked

Run 2026-11-01, non-elevated, on whatever the machine was doing: this experiment started no CPU or GPU
load. The token was not elevated and in none of Administrators, Performance Monitor Users or Hyper-V
Administrators; it was in Performance Log Users, and reading without that group is `Not checked`
(`experiments/04-cpu-counters/results/topology-2026-11-01.txt`). One session: 3 concurrent windows of 60 `--record` lines and 70 probe rounds, then 60 cost
rounds, so a figure here is one session, not a range of states. A rate at round r covers rounds r-1 to
r, about 1 s. `_Total` `% Processor Time` ran 2.609-81.450, 0.800-85.323 and 2.061-15.935 in the three
windows, with short bursts in the first two; their source was not identified. The experiment's
`README.md` says how independent each comparison is. Record files are cut down from the full
`--record` lines, which were not kept.

Windows, what the existing NT call already holds, `src/host/cpu.cpp`:

| claim | status | evidence |
|---|---|---|
| User and kernel time come from the call `cpu.cpp` already makes | Observed, 3 windows, and the VM's time lands in kernel (section "CPU contention checked"): `SystemProcessorPerformanceInformation` returns `UserTime`, `KernelTime` and `IdleTime` per LP. `KernelTime` includes idle: user plus kernel less idle equalled that formula's busy, largest gap 0.000000000. Against `_Total` `% User Time` and `% Privileged Time` the mean gap was at most 0.020 points, the largest 0.872. Same kernel counts, so bindings only | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt`, `experiments/04-cpu-counters/results/window1-probe-2026-11-01.txt` |
| DPC and interrupt time come from the same call | Observed, 3 windows: `Reserved1[0]` and `Reserved1[1]` of the `winternl.h` struct, largest gap 0.077 points from `% DPC Time` and 0.088 from `% Interrupt Time`. Those names rest on this agreement. `% Privileged Time` includes both: taking them out of kernel leaves a mean -0.723, -0.649, -0.547 points | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| The times have a tick | Observed, 3 windows: every round delta of idle, kernel, user, DPC and interrupt, per LP, and of `vmmemWSL` and `vmwp` process times, is a multiple of 156250 (100 ns units), 15.6250 ms. That is 1.5625 points on one LP in a 1.0 s interval and 0.078125 on the 20 LP total | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| One second of per-LP busy is coarse | Observed, 3 windows: LP `% Processor Time` was exactly 0 in 192 of 1377, 248 of 1379 and 214 of 1376 readings while `% Processor Utility` on those rounds averaged 0.953, 0.842, 0.815. LP `% Idle Time` passed 100 in 195, 247 and 221 of 1380, largest 118.702. Kernel plus user over all LPs ran 96.880 to 102.756 percent of 20 LPs times the round length, so `_Total` busy from the NT call and from PDH differed by up to 2.869 points in a round, mean gap at most 0.017 | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| Per-LP `% Processor Time` always returns a value | **No.** `PDH_CSTATUS_INVALID_DATA` (0xc0000bba) on 3, 1 and 4 of 1518 instance readings. Whole-array reads also failed: 2, 2 and 0 in the windows, and in `experiments/04-cpu-counters/results/cost-2026-11-01.txt` one array short (22 instances) in one round of 60 of each group that holds all ten counters, always `\Processor Information`. Their status was not printed, so why is `Unresolved`. A run under load printed the status of its own failures: section "CPU contention checked" | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt`, `experiments/04-cpu-counters/results/window1-probe-2026-11-01.txt` |

Windows, `\Processor Information`:

| claim | status | evidence |
|---|---|---|
| Instance naming | Observed: 22 instances, `0,0` to `0,19` as group,LP, then `0,_Total` and `_Total`. The two totals never differed. Arrays arrive in string order, `0,0 0,1 0,10 0,11`, not LP order, and the list did not change in any window | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt`, `experiments/04-cpu-counters/results/pdh-names-2026-11-01.txt` |
| `% Processor Utility` can exceed 100 | Observed: an LP above 100 in 27, 26 and 0 readings, largest 134.394; `_Total` largest 116.438. It has no 15.6250 ms step: 1360 distinct LP values in 1360 readings in window 1, against 650 for `% Processor Time`. `_Total` over `_Total` `% Processor Time` averaged 0.982, 0.955, 0.909 and ran 0.329 to 2.833. Against the hardware `Not checked`: nothing outside Windows' account was available | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| `PDH_FMT_NOCAP100` matters to `% Processor Utility` and `% Processor Performance` | **No**, here: leaving it out changed 0 values in any window, and capped reads still reached 134.394 and 145.747 | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| `% Processor Performance` is live | Changing: LP range 24.041 to 145.747, an instance above 100 in 326, 173 and 166 readings. Against the hardware `Not checked` | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| `Processor Frequency` is a live clock | **False here.** 3500 on every LP, E-cores included, in every round. `CallNtPowerInformation` `current_mhz`, `max_mhz` and `limit_mhz` also read only 3500 | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| `Actual Frequency` is a live clock | Changing, `Not checked` as true: P LPs 1034.6 to 5101.1, E LPs 841.4 to 3900.9, within 103.688 of `Processor Frequency` times `% Processor Performance`, so it is not a second reading. No frequency outside Windows' account exists here: the guest's MHz never moves | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| `% of Maximum Frequency` agrees with `% Processor Performance` | **No.** 22 to 100, with r +0.466, +0.455, +0.401 against it, pooled over LP readings. Which one reflects the clock is `Unresolved`; at all-core load they fit together (section "CPU contention checked") | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| `% Performance Limit` and `Performance Limit Flags` report anything | `Not checked`: 100 and 0 in every reading, at all-core load too (section "CPU contention checked"), so nothing here shows they can move | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| `Parking Status` means an LP is unused | **False here.** 0 or 1, changing 201, 120 and 127 times a window, and parked LPs ran: above 0 in 792 of 972, 779 of 1010, 781 of 984 parked readings, mean 5.329, 4.688, 3.808, largest 81.307. `_Total` stays 0. The CPU set Parked flag exists too, read once at another moment, so the two were not compared | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt`, `experiments/04-cpu-counters/results/topology-2026-11-01.txt` |

Windows, the rest:

| claim | status | evidence |
|---|---|---|
| `\System` counters are readable | Observed, 3 windows. Bindings not checked, no second reading was taken. `Processor Queue Length` 0 to 18, above 0 in 2, 11 and 7 of 69 rounds, r with busy +0.491, +0.390, -0.121. `Context Switches/sec` 11355.373 to 325447.617. `System Calls/sec` 17400.550 to 9452541.628 | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| The largest `System Calls/sec` reading is real | `Unresolved`: 9452541.6 in window 3, round 11, at `% Processor Time` 10.314 and 53530.0 context switches, nearly eight times the other windows' peaks of 1199615.3 and 1172613.1, which came at 81.450 and 85.323 | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| A usable CPU temperature exists, non-elevated | **False here**, for every source tried. `\Thermal Zone Information(\_TZ.TZ00)\Temperature` read 301 and `High Precision Temperature` 3010 in all 207 rounds, through bursts to 85.323, with `% Passive Limit` 100 and `Throttle Reasons` 0; 27.85 C if the unit is Kelvin. It looks like a fixed ACPI value, and it did not move under sustained load either (section "CPU contention checked"). `MSAcpi_ThermalZoneTemperature` returned access denied. `Win32_TemperatureProbe` has 2 rows with no `CurrentReading`. The guest has no thermal zones | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt`, `experiments/04-cpu-counters/results/windows-identity-2026-11-01.txt`, `experiments/04-cpu-counters/results/wsl-static-2026-11-01.txt` |
| CPU layout | Observed: i5-13600K, 1 package, 1 NUMA node, 1 processor group, 14 cores, 20 LPs. LPs 0-11 are 6 P-cores with two threads each, efficiency class 1, 49152 byte L1D and 2097152 byte L2 each. LPs 12-19 are 8 E-cores with one thread each, efficiency class 0, in two groups of 4 sharing 4194304 bytes of L2. L3 is 25165824 bytes. `Win32_Processor` agrees on 14 and 20 | `experiments/04-cpu-counters/results/topology-2026-11-01.txt`, `experiments/04-cpu-counters/results/windows-identity-2026-11-01.txt` |
| Topology and identity reads run non-elevated | Observed. CPUID `hypervisor_bit` reads 1 on Windows, and leaf 16 reads 0 | `experiments/04-cpu-counters/results/topology-2026-11-01.txt` |

The VM, as Windows and the hypervisor see it. Guest figures are the guest kernel's own count, so these
comparisons are independent of Windows. All of it is VM level, or vCPU level for the hypervisor's
counters. None of it says which Linux process used what:

| claim | status | evidence |
|---|---|---|
| `\Hyper-V Hypervisor` per-processor counters are readable non-elevated | Observed: 20 `Hv VP` instances of one VM, 20 `Hv LP`, 20 `Root VP`, each with a `_Total`. `_Total` is the mean of its instances, not the sum: largest gap 0.001876 on `Hv VP` | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt`, `experiments/04-cpu-counters/results/pdh-names-2026-11-01.txt` |
| `vm.vmmem_cores` is non-zero | Observed: zero in 0 of 60 records in each window. Range 0.0310 to 2.2320 | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt`, `experiments/04-cpu-counters/results/window1-record-2026-11-01.txt` |
| `vm.vmmem_cores` tracks the guest | Observed over a small natural range, 3 windows: guest busy times 20 ran 0.0600-1.8620, 0.0700-1.4760 and 0.0600-0.4960 cores, a range of 9.010, 7.030 and 2.180 points of 20. Best r +0.820, +0.936, +0.748, at lag +1, 0, +1 records, chance level 0.236, 0.411, 0.221; rank r 0.483, 0.438, 0.665. The record pairs Windows with a guest line up to about 1 s old, hence the lag: pooled at lag 0, r is only +0.639. Totals 17.891 against 14.702, 12.170 against 11.061, 11.941 against 8.868 core-seconds. Bursts drive the first two windows. Above 2 cores: section "CPU contention checked" | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| `vmwp.exe` CPU tracks the guest | **No.** Mean 0.0016-0.0045 cores, zero in 54, 43 and 54 of 60 records, best r +0.301 in the records and +0.308 from the raw counters. Experiment 01's finding about GPU work does not carry over to CPU | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| `Hv VP` `% Guest Run Time` tracks the guest | Observed, 3 windows: summed over VPs against `/proc/stat` busy, r +0.823, +0.964, +0.928 at lag 0, rank r +0.825, +0.819, +0.883, and +0.648, +0.812, +0.898 with rounds at or over 0.5 guest cores left out. Mean over guest 0.924, 0.941, 1.002, closer than process time's 1.182, 1.089, 1.318. Summed range 0.0681 to 1.2193 cores | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| `vmmemWSL` `CycleTime` follows `Hv VP` run time | Observed: r +0.998, +0.999, +0.997 at lag 0. Its round deltas have a gcd of 1 where process time has 156250, and it comes in the `SystemProcessInformation` call `processes.cpp` already makes. About 4.115-4.282e9 per core-second of VP time, with the VM near idle. This session did not establish the unit; section "CPU contention checked" does | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| `Hv LP` `% Guest Run Time` is WSL's share | **False here.** Windows runs as a guest too: `Hv LP` less `Root VP` less `Hv VP` guest time averaged -0.012, -0.003, -0.001 points, largest -0.611. `_Total` read 4.226 to 84.468 while `Hv VP` read 0.340 to 6.097 | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| Windows busy includes the VM's time | Yes under load: section "CPU contention checked". In this session, on a signal of about 1 point, `Unresolved`: `% Processor Time` less `Root VP` guest time, against `Hv VP` guest time: slope 1.061, 1.394, 1.432, r +0.596, +0.598, +0.451, rank r +0.310, +0.320, +0.473. That leans towards yes on a signal of about 1 point. `vmmemWSL` is in the Windows process list either way, so Windows and WSL figures are still never added | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| Process cores account for Windows busy | **No.** Mean 1.402, 1.202, 1.106 cores over processes against 1.992, 1.768, 1.311 from busy, in the same records. The gap was not attributed | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| `vmmemWSL` threads | Observed: 57 threads, 21, 22 and 21 moved in a window, and their times summed to the process time exactly. Which thread runs which vCPU: section "CPU contention checked" | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| Guest `cpuN` is `Hv VP n` | Observed, one session: r +0.825 on the same number on average, +0.299 off it, the largest in 20 of 20 rows, with per-vCPU means close. `cpu0` is the exception, 2.260 percent in the guest against 1.021 on `Hv VP 0`; why is `Unresolved`. Numbering only | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| `Hv LP n` and `Root VP n` are Windows LP `n` | Observed, one session: r 0.975 and 0.973 on the same number on average, 0.824 and 0.833 off it, the largest in 20 of 20 rows of both | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| A guest vCPU maps to a Windows core | **Unproven, and the evidence is against a fixed mapping**, under load too (section "CPU contention checked"). `Hv VP n` tops its row at `Hv LP n` in 1 of 20 rows, r 0.491 on the number against 0.486 off it, and guest `cpuN` at Windows LP `n` in 3 of 20. The counters report 0.537, 0.532 and 0.484 migrations per dispatch: per VP a mean of 40.1 to 594.5 a second, and no VP stayed put in any window. No counter read here says which LP ran a VP. A high r is no mapping: `Hv VP 0` reached +0.939 with LP 7 and +0.928 with LP 9 through shared bursts. The meaning of migrations rests on the counter's name | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |

Cost, on one sample, wall time of collect and format. Each group has its own query, and the groups run
back to back, so "first" is the group that led its round:

| group | mean ms | median | p95 | max | mean when first |
|---|---|---|---|---|---|
| NT call `cpu.cpp` already makes, no new cost | 0.058 | 0.018 | 0.299 | 0.486 | 0.279 |
| 3 of `\Processor Information`: performance, utility, frequency | 0.254 | 0.160 | 0.792 | 1.432 | 0.700 |
| all 10 candidates of `\Processor Information` | 0.505 | 0.309 | 1.800 | 2.141 | 1.112 |
| 3 `\System` counters | 5.512 | 4.783 | 9.476 | 11.445 | 5.926 |
| `Temperature` | 0.093 | 0.073 | 0.251 | 0.288 | 0.172 |
| all 14 in one query | 4.944 | 4.326 | 9.951 | 10.633 | 7.457 |
| `Hv VP` `% Guest Run Time` | 1.521 | 0.812 | 4.654 | 6.113 | 3.833 |

Evidence: `experiments/04-cpu-counters/results/analysis-2026-11-01.txt`, `experiments/04-cpu-counters/results/cost-2026-11-01.txt`. Setup: the first query in the process took 628.531 ms to open and add, and adding
the `\System` counters took 27.110 ms. That first figure was the first query in the process; whether a process already holding a PDH query pays it again is `Not checked`.

WSL guest, all from `/proc` and `/sys`, in the same session:

| claim | status | evidence |
|---|---|---|
| `/proc/stat` fields move | Observed: `user`, `system`, `idle`, `iowait` and `softirq` moved. `nice`, `irq`, `steal`, `guest` and `guest_nice` were 0 ticks in all 3 windows, so they are `Not checked`. All fields summed to 1998.2-1998.9 ticks per guest second, for 20 vCPUs at 100 ticks each | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt`, `experiments/04-cpu-counters/results/window1-guest-2026-11-01.txt` |
| `steal` shows the host taking CPU | **False here**, from experiment 05: section "CPU contention checked". This session it was 0 ticks throughout, including the windows where Windows reached 81.450 and 85.323 percent busy, and the guest was nowhere near needing that CPU | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| `ctxt`, `procs_running`, `procs_blocked` | Observed: `ctxt` 848.3 to 25761.9 a second. `procs_running` 1 to 4, and the sampler counts itself. `procs_blocked` 0 or 1 | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| `/proc/loadavg` | Observed, changing: 1 min 0.09 to 0.42 | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| `/proc/cpuinfo` MHz is live | **False here.** 3494.272 on all 20 vCPUs in every round of 3 windows, through the bursts | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| `cpufreq` exists | **No.** `cpu0/cpufreq` is absent and `/sys/devices/system/cpu/cpufreq` is empty. `cpuidle` driver is `none` | `experiments/04-cpu-counters/results/wsl-static-2026-11-01.txt` |
| `/sys/class/thermal` holds a temperature | **No.** 0 thermal zones and 0 `hwmon` entries. 20 `Processor` cooling devices, all state 0 of 0 | `experiments/04-cpu-counters/results/wsl-static-2026-11-01.txt` |
| `/proc/pressure/cpu` | Observed: `some avg10` 0.00 to 0.18, `some` total moved 56795 to 103761 us a window. `full` stayed 0 | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |
| cgroup `cpu.max` limits the guest | **No.** Absent at the root, `max 100000` on all 15 cgroups below it that have the file, and the root `cpu.stat` shows `nr_throttled` 0 | `experiments/04-cpu-counters/results/wsl-static-2026-11-01.txt` |
| Guest thread count matches Windows | Observed: `nproc` 20 and Windows 20, with no `.wslconfig` in any profile | `experiments/04-cpu-counters/results/wsl-static-2026-11-01.txt`, `experiments/04-cpu-counters/results/topology-2026-11-01.txt` |
| The guest shows the host's layout | **False here.** It reports 10 cores of 2 threads, every LP with 48K L1D and 2048K L2, where the host has 6 P-cores and 8 single-thread E-cores. P and E cannot be told apart from inside the guest | `experiments/04-cpu-counters/results/wsl-static-2026-11-01.txt`, `experiments/04-cpu-counters/results/topology-2026-11-01.txt` |
| WSL `CLOCK_MONOTONIC` against Windows in this session | Slope of `mono_s` on Windows time 1.00009, 0.99978, 0.99916 over 58.3 s, 3 windows. This does not repeat the 3.68% of the section below, and it stands beside it, not in place of it. Why they differ is `Unresolved` | `experiments/04-cpu-counters/results/analysis-2026-11-01.txt` |

## CPU contention checked

Experiment 05, run 2026-11-01, non-elevated, one CPU-only load approved by the user: burners ran from
1793540254.027 to 1793540501.090 unix s, 247.1 s with 217.0 s of it under a burner. No GPU, memory or disk
load was started, nothing else was stopped, and no burner, probe or recorder remained afterwards. **One
trial of each case**, so every figure here is one trial. The cases are listed in the experiment's
`README.md`: `wsl-N` is the WSL burner alone at N threads, `win-20` the Windows burner alone, and
`both-win-N` WSL at 20 threads with Windows at N, between `both-before` and `both-after`, WSL 20 alone.
Loss is the WSL burner's units per second against 44621.6, the mean of `both-before` and `both-after`,
from 11 burner rows. A case mean leaves out its first 2 s and last 1 s. Windows times are moved
onto the guest clock by +0.031 s, from pings of +0.057 to +0.003 s (each +-0.026 s at worst). Other Windows work
was not stopped: 0.313 to 0.762 cores on average in the contended cases, and 1.892 in `both-win-10`,
where a program outside this experiment burst to 3.760; no game ran. All of this is VM level, or vCPU
level for the hypervisor's counters. None of it says which Linux process used what. Record files are
cut down as in experiment 04, and the names of other programs are masked as `other-N`.

The VM's figures against the guest and the work, with no contention started:

| claim | status | evidence |
|---|---|---|
| `vm.vmmem_cores` is linear with the guest up to saturation | Observed, one trial: 1.073, 2.159, 5.061, 10.059, 19.430 cores at 1, 2, 5, 10, 20 threads, guest busy times threads 1.087, 2.202, 5.066, 10.133, 20.000. r +0.9996 over 71 records. A line through 0 to 10 threads has slope 0.99261 and predicts 19.859 at 20. The gap at 20 fits the guest overstating, not the figure bending: Windows' own work shared the LPs then, and `Hv LP` `% Total Run Time` read 100.000 | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-record-2026-11-01.txt` |
| `Hv VP` `% Guest Run Time`, summed, is linear too | Observed, one trial: 1.062, 2.069, 5.026, 9.995, 19.302 cores, r +0.9999 over 66 rounds. `% Total Run Time` adds 0.007 to 0.063 of a core | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-probe-2026-11-01.txt` |
| `vmmemWSL` `CycleTime` calibrates to cores | Observed, one trial: it runs at a constant reference, not the core's clock. With load it read 3.4801 to 3.5016e9 per process core-second and 3.5082 to 3.5191e9 per `Hv VP` core-second, and `cpu_burn.exe` 3.4782 to 3.4993e9, while `Actual Frequency` said P 5100 and E 3900. Divided by 3.494272e9, the guest's `cpu MHz`: 1.075, 2.092, 5.062, 10.077, 19.459 cores at 1 to 20 threads, and 14.471, 9.717, 5.766 under contention, within 0.073 of process time and with no tick. The divisor rests on that guest figure, read once a second and never moving. With the VM idle the ratio is coarse, 3.0602 to 3.6772, and 4.0754 to 4.1832 per `Hv VP` guest core-second, which is experiment 04's 4.115-4.282e9: an idle-VM figure | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-probe-2026-11-01.txt` |
| Which VM figure follows a change within a second | `Hv VP` and `CycleTime`. Against loss, over all rounds of the contended cases: `% Total Run Time` r -0.999, `CycleTime` -0.999, `vm.vmmem_cores` -0.997, process time -0.995. Within `both-win-20` alone, where the loss moves only a little: -0.895, -0.844, -0.367, -0.299. The two that fall are the two that step in the tick of section "CPU counters checked". Means agree: all four are within 0.1 of each other in `both-win-20`. One trial | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| Windows busy includes the VM's time | **Observed, one trial.** With no Windows burner, Windows busy rose to 100.000 at 20 guest threads, `Hv LP` `% Idle Time` fell to 0.027, and `cpu_burn.exe` read 0.000. Windows busy cores against `Hv VP` total, six cases: slope 1.0007, intercept 0.6819, r +0.9982 over 70 rounds. Windows busy and WSL busy therefore describe the same time, and are never added | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-probe-2026-11-01.txt` |
| The VM's time is Windows kernel time | Observed, one trial: `vmmemWSL` user time 0.000 at every thread count, kernel 19.443 cores at `wsl-20`. Windows `kernel` read 99.457% and `user` 0.543% while the guest spent 99.932% of its ticks in `user`. So on Windows kernel time holds the guest's user code | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| `Hv LP` `% Guest Run Time` is Root VP plus `Hv VP` | Observed, one trial: 99.766 at `win-20` with the VM idle, 19.953 cores against 19.956 for the two summed, and 19.917 against 19.913 at `both-win-20` | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |

What shows that WSL lost CPU. The work: 45090.0 units/s in `both-before`, 33007.7, 23026.2, 12876.6 at `both-win-5`, `both-win-10`, `both-win-20`, 44153.2 in `both-after`, a loss of 26.028, 48.397, 71.143 percent:

| claim | status | evidence |
|---|---|---|
| Guest busy times threads less `Hv VP` `% Total Run Time` tracks lost work | Observed, one trial, the best found. In cores of the 20, work lost 5.359, 9.787, 14.288, the difference 5.615, 10.351, 14.265, and with `vm.vmmem_cores` instead 5.521, 10.372, 14.244. r +0.999 over 81 rounds, and +0.896 inside `both-win-20`. It is 0.105 or less up to 10 threads and 0.015 at `win-20`. It is 0.635 at `wsl-20`, range 0.348 to 1.334, where no Windows burner ran but Windows' own work shared the LPs. A raw difference of two clocks about a second apart. Only tested with the guest at 100%: under a partly busy guest it is `Not checked` | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| The count of `vmmemWSL` threads in `Ready` tracks lost work | Observed, one trial, at case level: 6.00, 9.00, 13.11 mean against 5.359, 9.787, 14.288 cores lost, 1.60 and 1.75 in `both-before` and `both-after`, 1.18 at `wsl-20`, 0.00 below saturation and 0.06 at `win-20`. It names the side, and comes from the call `processes.cpp` already makes. It is one snapshot a second, range 6-18 in `both-win-20`, so r is only +0.826 over rounds and +0.284 inside `both-win-20` | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-probe-2026-11-01.txt` |
| The sum of `/proc/stat` fields per wall second shows it from inside the guest | Observed, one trial, partly: 2000.00 in `both-before`, 1926.19, 1517.33, 1075.72, then 1998.56, and 1995.78 to 2006.60 with no contention. r -0.976 over rounds, -0.880 inside `both-win-20`. It understates the loss, 0.738, 4.827, 9.243 cores missing against 5.359, 9.787, 14.288 lost. A busy percent taken over the same ticks hides it | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-guest-2026-11-01.txt` |
| `steal` shows lost CPU | **False here.** 0 ticks in all 299 rounds, at a loss of 71.143 percent | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| PSI cpu shows lost CPU | **No**, at any useful scale. `some` total ran 15389.8 us/s in `both-before`, 17619.7, 50960.0, 56632.8, and 19551.9 after, against a 71.143 percent loss, and it read 7565.2 at `wsl-20` with no contention. `avg10` means 0.32, 0.99, 1.91, 4.97, and it lags, mean 4.61 in `both-after` at a loss of 1.050. r +0.597 and +0.751. `full` stayed 0. That fits PSI seeing only the guest's own run queue, which held about 21 tasks for 20 vCPUs throughout | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| `CPU Contention Time Per Dispatch` shows lost CPU | **False here.** 0.000000 on every VP in all 299 rounds, as is `CPU Wake Up Time Per Dispatch`. `% Total Core Run Time` is 0.000000 too | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| `CPU Wait Time Per Dispatch` shows lost CPU | **No.** `_Total` rose inside the contended cases, 1236.73 to 1353.10, 1425.89, 2499.94, but it is higher still with the machine quiet, 3454.25 to 4233.72, so it cannot be read alone. r +0.580, and -0.217 inside `both-win-20`. Unit not established | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| `% Guest-Relative Utilization` shows lost CPU | **No.** Summed, 20.00 at every contended level. It reads what the guest believes | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| `Processor Queue Length` and the count of all `Ready` threads tell which side is short | **No.** The two are one figure: r +0.8852, rank +0.9443 over 220 rounds, and 7.12 for both at `both-win-5`. 2.27 at `wsl-20`, 22.92 at `win-20`, 39.64 at `both-win-20`. They show a shortage on Windows but not whose, and do not rise with the WSL loss: `win-20` reads higher than `both-win-5`. Read per process, `Ready` does separate them. Same kernel counts, so bindings only | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| Windows loses work to the VM | Observed, one trial: the 20 thread burner did 45664.0 units/s alone and 31679.3 beside WSL 20, a loss of 30.625 percent where WSL lost 71.143. Windows' own count shows it: burner process time 19.320 cores alone, 13.253 beside WSL. `both-win-5` and `both-win-10` have no Windows-alone reference; there the burner got 4.947 of 5 and 8.130 of 10 cores, the second in the step with other work. Windows busy reads 100.000 throughout, so it does not show this | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-burn-windows-2026-11-01.txt` |
| Windows' other programs wait for Windows load, not for the VM | Observed, one trial: `Ready` threads outside the VM and the burner averaged 25.97 at `win-20` and 1.36 at `wsl-20`, both with every LP at 100. Whether those programs were slower was not measured | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| `vmmemWSL` thread priority changes under contention | Observed, one trial, cause not shown. Idle, the 20 busy threads wait at priority 15. Under load they run at 15 or at 1, and `Ready` ones sit at 1: `both-win-20` averaged 13.12 `Ready` at 1, 0.05 `Ready` at 15 and 6.79 running at 15, with the burner left at its default. That one side lost 71.143 and the other 30.625 fits this, but it was not tested | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-probe-2026-11-01.txt` |

The guest's view of a vCPU the host has taken:

| claim | status | evidence |
|---|---|---|
| Guest busy stays high when the host has taken the vCPU | **Observed, one trial: yes.** At `both-win-20` guest busy read 99.997 percent, lowest 99.817, and the burner's threads counted 19.718 cores of their own CPU time, while `Hv VP` ran 5.735 cores and the work fell 71.143 percent. `idle` 0.04 ticks/s, `steal` 0. Guest busy is therefore what the guest wanted, not what it got | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-guest-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-burn-wsl-2026-11-01.txt` |
| Equal `Hv VP` time is equal work | **False here**, on this hybrid CPU. In `both-win-20` vCPUs 10 to 17 ran 32.13 to 32.79 percent and did 551 to 571 units/s, the other twelve 24.83 to 27.30 percent and 661 to 739; r -0.889 over 20 vCPUs. The same eight were the slow ones at `wsl-20` and in the whole WSL burner of the contended cases. Across the VM, units per `Hv VP` core-second held 2254.0 to 2397.7 | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-burn-wsl-2026-11-01.txt` |
| A process's `/proc/pid/stat` `processor` is where it works | **False here** for a process with threads: it is the leader's. `wsl-1` read 10 while the work ran on vCPU 1. `/proc/pid/task/*/stat` was right: among the burner's own reported vCPUs in 2434 of 2436 readings. The field exists and moves. `num_threads` read threads plus one, as it should | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-guest-2026-11-01.txt` |
| `procs_running`, `procs_blocked`, D tasks | Observed, one trial. `procs_running` 2.00, 11.00, 21.09 at 1, 10, 20 threads, and it barely rose when the host took the CPU: 21.50, then 22.32 at `both-win-20`. `procs_blocked` at most 0.17 on average, D tasks at most 2. None shows the loss | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| `/proc/pressure` memory and io | Observed, one trial: `memory` `some` and `full` 0 in every case. `io` `some` 105271.2 us/s and `avg10` 16.44 at `win-20` with the guest idle, 27.1 to 3132.1 in other uncontended cases. The source was not identified and it did not repeat at `both-win-20`, 2078.9, so it is `Unresolved` | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| Guest clock under contention | Observed, one trial: Windows less WSL read +0.057, +0.055, +0.055 s at the start and +0.004, +0.013, +0.003 s at the end, so the guest's seconds held up while it lost CPU. This speaks to this run only, not to section "WSL clock rate" | `experiments/05-cpu-contention/results/run1-marks-2026-11-01.txt`, `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |

Where the load ran, and what else moved:

| claim | status | evidence |
|---|---|---|
| One unpinned guest thread smears across vCPUs | **No.** It stayed on vCPU 1 for the whole of `wsl-1`, 0.00 moves a second by the burner's own look, every 64 units. It is spread below that: `Hv VP 1` ran 98.9 percent across LPs, the busiest 40.4, 28.1, 28.0, 22.0, at 154.2 migrations a second. Up to 10 threads the guest moved a thread at most 0.42 times a second | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-burn-wsl-2026-11-01.txt` |
| Migrations under sustained load | Observed, one trial: `Logical Processor Migrations/sec` summed 1715.4 at `wsl-20`, 0.083 per dispatch, against 0.421 at baseline, and 930.2 at `both-win-20`. | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| The VM fills P LPs first | Observed, one trial: at `wsl-10`, P LPs averaged 80.87 percent busy and E LPs 15.25, and the only LPs above 20 percent were the 12 P LPs | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| Guest `cpuN` lines up with a Windows LP while busy | **No.** Over `wsl-1` to `wsl-10`, r +0.360 on the same number against +0.388 off it, largest on the same number in 0 of 20. At 20 threads vCPUs 10 to 17 did 1701 to 1957 units/s and the rest 2480 to 2667, about the E and P LP rates of `win-20`, but at `wsl-2` vCPUs 12 and 13 shared threads at the P rate, so that is not fixed | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| A `vmmemWSL` thread carries one vCPU | Observed, one session: over `baseline` to `wsl-10`, 18 of the 20 busiest threads follow one `Hv VP` at r 0.999 or better, naming 19 different VPs; the other two read 0.927 and 0.767. VPs 4 and 6 stepped together, so they cannot be told apart | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-probe-2026-11-01.txt` |
| Per-LP DPC and interrupt | Readable, meaning `Not checked`. From the NT call, multiples of 156250, and above 0 in 451 and 786 of 5980 readings: a 1 s delta on one LP is usually 0. Mean 0.093 to 0.187 and 0.179 to 0.291 percent, largest 4.688 and 6.250, with no difference between cases that means anything: nothing here loaded interrupts | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| Per-LP user and kernel under load | Sound as arithmetic, same kernel counts: against `\Processor Information` mean gap 0.150 and 0.208 points, largest 7.309 and 9.313, p99 1.260 and 1.473, over 5980 readings. `kernel` holds the VM, as above, so it is not the same quantity on an LP that ran the VM and one that did not | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| A Windows `Ready` count is a free "short of CPU" figure | Observed, one trial: threads in `Ready` or `DeferredReady` from the rows the probe reads, without opening a process. At most 0.36 on average up to 10 threads, 2.55 at `wsl-20`, range 2 to 6, 27.72 at `win-20`, range 6 to 58, 40.91 at `both-win-20`, range 21 to 116. Moves in all three, in the WSL case only at saturation. It is noisy, one snapshot | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-probe-2026-11-01.txt` |
| `% Processor Utility` at all-core load | Observed, one trial: `_Total` 131.995 while Windows busy is 100.000, ratio 1.3200, equal to `% Processor Performance` 131.996. P LPs 145.708 and E LPs 111.427. Windows busy and Utility part by the clock's ratio to 3500: at all-core load the two do not agree and neither is wrong | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-probe-2026-11-01.txt` |
| `Actual Frequency` at all-core load | Observed, one trial: P 5098.2-5100.2, E 3899.8-3900.2 at `wsl-20`, and `% Processor Performance` of 145.708 and 111.428 is the same pair. `% of Maximum Frequency` read 100.000 on both, so it is a share of each class's own top. The burner's rate does not contradict this: 2692.4 units/s on a P LP at all-core, 2777.4 for one thread alone, 0.9694, so the P clock did not visibly fall. That is the first check outside Windows' account, and only of no fall: the rate gives no MHz, and E over P is 0.6201 where the clocks' is 0.7647 | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-burn-windows-2026-11-01.txt` |
| `Thermal Zone` temperature moves under sustained load | **False here.** 301 and 3010 in all 299 rounds, including 217.0 s of burners, with `% Passive Limit` 100 and `Throttle Reasons` 0. It is not a live temperature | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| `% Performance Limit` and `Performance Limit Flags` move | `Not checked`, still: 100 and 0 on every LP in all 299 rounds, at all-core load too. Nothing was limited that these report, or they do not report, and a run this short cannot tell which | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt` |
| Status of a failed whole-array PDH read | Observed: `Hv LP` `% Idle Time` failed 81 times, rounds 85 to 269, with 0x800007d8, `PDH_CALC_NEGATIVE_VALUE`, only around saturation. `\Processor Information` `% Processor Performance` and `Actual Frequency` failed once each, in round 243, with 0x800007d6, `PDH_CALC_NEGATIVE_DENOMINATOR`. These are this run's failures, and that experiment 04's four were the same is `Not checked` | `experiments/05-cpu-contention/results/analysis-2026-11-01.txt`, `experiments/05-cpu-contention/results/run1-probe-2026-11-01.txt` |

## Memory and process collectors checked

Run 2026-11-02, non-elevated, on whatever the machine was doing. Same machine line as the top of this
file (`experiments/06-memory-process/results/run1-static-2026-11-02.txt`). No load was started. The only
processes started were idle ones the run ended itself: `sleep` in the guest and `ping.exe` to 127.0.0.1.
There were two runs. Run 1 read both sides beside `winwsltop --record 60`, with 25 Windows rounds and
219 guest rounds. Run 2 read Windows alone, 10 rounds, with more `\Memory` counters. A figure here is
one idle state, not a range of states. `experiments/06-memory-process/README.md` says how independent
each comparison is: on both sides the second reading comes from the same OS's own counts, so the checks
show bindings and units, with one exception. The exception is the `vmmemWSL` row, where the two
readings come from different kernels.

| claim | status | evidence |
|---|---|---|
| Windows `available` is `\Memory\Available Bytes` | Observed, bindings only. The counter moved 1.750-22.887 MiB between the readings either side of the collector (run 1). The collector sat inside them in 15 of 25 rounds and 3 of 10, and outside by at most 3100672 B and 7766016 B. A round from the first reading to the last took 2.340-2.834 s, and `Get-Counter` does not say when within its call it sampled | `experiments/06-memory-process/results/analysis-2026-11-02.txt`, `experiments/06-memory-process/results/analysis-run2-2026-11-02.txt` |
| Windows `commit_used` and `commit_limit` are `\Memory\Committed Bytes` and `Commit Limit` | Observed, bindings only. Commit sat inside in 11 of 25 and 5 of 10 rounds, outside by at most 2756608 B and 5906432 B, while the counter moved up to 24.371 and 25.379 MiB. Limit was equal in 25 of 25 and 10 of 10, and it never moved | same |
| Windows `cache` (`GetPerformanceInfo` `SystemCache`) is a known `\Memory` figure | **Unresolved.** It matches no sum tried. It is above standby (reserve + normal + core) by 809.0 to 823.6 MiB, below standby + modified by 844.7 to 977.7 MiB, and above standby + `Cache Bytes` by 160.3 to 161.6 MiB in all 25 rounds of run 1 and 160.6 to 161.8 MiB in all 10 of run 2. None of the five counters run 2 added equals that gap. Task Manager's "Cached" was not read, so whether this matches it is `Not checked` | same |
| Windows per-process working set and private bytes are `Get-Process` `WorkingSet64` and `PrivateMemorySize64` | Observed, bindings only (.NET reads the same NT call), so the hand-written struct's offsets and units hold. Run 1: 8011 comparisons over 364 pids. Working set equal 7729, inside 278, outside 4. Private equal 7777, inside 230, outside 4. Run 2: 3160 over 335 pids, outside 2 each. Outside by at most 581632 B and 630784 B | same |
| Windows create time is constant while a process lives | Observed. The collector's key equalled `Get-Process` `StartTime` (`GetProcessTimes`, another path to the same field) in 7882 and 3040 reads, and differed in 0. `StartTime` could not be read in 8154 and 3281, access denied non-elevated. Every pid had one create time across the rounds it was seen in (403 and 345 pids), the long `ping.exe` in 25 of 25 and 10 of 10 rounds | same |
| Windows create time differs after a pid is reused | `Not checked`: no reuse was seen. The 25 and 10 short `ping.exe` had distinct pids, and no pid's create time changed | same |
| Guest swap used is `free -b` Swap used | `Not checked`: 0 in the collector, `free -b` and `/proc/meminfo` throughout, with 8589934592 B of swap. A value that never moved | `experiments/06-memory-process/results/analysis-2026-11-02.txt` |
| Guest per-process RSS is `/proc/pid/status` `VmRSS` and `ps` RSS | **No.** The collector shows `/proc/pid/stat` field 24 times the page size. It equals the bracketing reads 1775 times, sits inside them 16 times and is never outside, over 71 pids. `VmRSS` and `ps` are higher. Read in the same pass, `VmRSS` minus stat rss was 73728 to 2568192 B and above 0 in all 10159 reads, so the collector is never above `VmRSS` and is low by up to 2568192 B. Why the kernel's two figures differ was not checked | same |
| Guest per-process cores are `/proc/pid/stat` utime + stime over the collector's own interval | Observed, bindings only (the same file). Cores times step times HZ lands within 0.0507 of a whole tick (HZ 100), which is the rounding to 3 decimals. Per interval, 302 comparisons with ticks moving and 0 outside, plus 1359 at zero. Over the 59.580 s window, all 7 processes that used CPU were inside. `ps` `cputimes` agrees at its 1 s resolution only. Guest ticks and guest `CLOCK_MONOTONIC` are both the guest's, so the WSL clock rate below does not enter | same |
| The guest's page cache is held as `vmmemWSL` working set | **Unresolved.** One record line holds both sides: `vmmemWSL` 2628.1-2642.1 MiB, guest used (total less available) 1726.3-1748.6 MiB, cached 360.6-364.9 MiB, total less `MemFree` 1865.8-1885.7 MiB. Working set exceeded used + cached by 522.9 to 541.7 MiB, and total less `MemFree` by 748.1 to 767.1 MiB. So the cache fits inside it, but so would a VM overhead of that size. Cache moved 4.2 MiB, too little to test whether working set follows it. A test needs the cache to change, which this run did not do | same |
| Guest `starttime` is constant while a process lives | Observed. The run's `sleep`, pid 48992, read `starttime` 616497 in all 219 rounds, and each of 139 pids had one value | same |
| Guest `starttime` differs after a pid is reused | `Not checked`: no reuse is possible in this run. `ns_last_pid` went from 48991 to 49944 with `pid_max` 4194304 | `experiments/06-memory-process/results/analysis-2026-11-02.txt`, `experiments/06-memory-process/results/run1-static-2026-11-02.txt` |

## Guest process walk checked

Run 2026-11-04, one run of 121 rounds, non-elevated, as uid 1000 in `Ubuntu`, the only distro in the
registry. Same machine line as the top of this file; Windows 10.0.26200.9711 and the kernel are in
`run1-static-2026-11-04.txt`. No GPU load. One CPU load of 20 s (`churn.py`: 80 children of about 0.2 s
each), which ended itself. The probe calls the collector's own `processes()`. The independent count is
the cgroup hierarchy: this distro's cgroup namespace is the initial one, so `cgroup.procs` lists every
task in the VM and prints a task outside this PID namespace as pid 0. `/proc/1/comm` is `systemd`, so
the walk sees this distro's PID namespace only. One run, one idle state: a figure here is not a range
of states.

| claim | status | evidence |
|---|---|---|
| The collector's walk reads every process in this distro's PID namespace | Observed, one run: it read every numeric `/proc` entry it listed in 121 of 121 rounds (45 to 71), with 0 failed reads. The cgroup pids in this namespace matched the walk except 1 each way over the run, read moments apart | `experiments/07-guest-walk/results/analysis-2026-11-04.txt`, `experiments/07-guest-walk/results/run1-walk-2026-11-04.txt` |
| The collector's walk reaches every process in the WSL VM | **No.** 232 to 270 thread groups printed as pid 0, all in the root cgroup, against the walk's 45 to 71 of 282 to 341: the walk saw 0.158 to 0.208 of them. Walk threads plus cgroup threads at 0 fell inside `/proc/loadavg`'s task count read either side in 121 of 121 rounds. Which of the hidden tasks are kernel threads, the VM's own init or WSLg was not established without privileges | same |
| WSLg's processes are in the walk | **No.** WSLg was running (`/mnt/wslg` present, `DISPLAY=:0`, `WAYLAND_DISPLAY=wayland-0`) and 0 pids in this distro's `/proc` were named `weston`, `Xwayland` or `pulseaudio` | `experiments/07-guest-walk/results/run1-wslg-2026-11-04.txt` |
| Other distros are running | `Not checked`: only `Ubuntu` is registered, so none could be. `wsl.exe -l -v` was not run | `experiments/07-guest-walk/results/run1-static-2026-11-04.txt` |
| Process cores summed over the walk are the guest's CPU | **No.** Idle, 99 s: 8.640 core-seconds over processes, 10.774 in this distro's cgroups (which keep exited processes' time), 13.224 in the root cgroup, 13.220 from `/proc/stat` busy; the sum was 0.654 of busy. Under churn, 21 s: 1.650 against 19.031, 19.872 and 19.880, 0.083 of busy. The cgroups' excess over the sum during churn, less the idle rate, was 16.928 core-seconds against the children's own 16.720 CPU-seconds, so a process that exits between walks is lost to the sum | `experiments/07-guest-walk/results/analysis-2026-11-04.txt`, `experiments/07-guest-walk/results/run1-churn-2026-11-04.txt` |
| RSS summed over the walk is the guest's memory in use | **No** as a whole, though close here: 2034417664 to 2153005056 B against `MemTotal - MemAvailable` 2074980352 to 2180792320 B, ratio 0.972 to 1.006. Shared pages count once per process and hidden tasks are left out, so agreement here is not a property of the sum | `experiments/07-guest-walk/results/analysis-2026-11-04.txt` |
| Summing costs little | Observed: the sums alone took 0.0200 ms mean, 0.0760 ms at most; the walk itself 0.802 ms mean, 2.730 ms at most | same |

A sum of non-negative terms is at or above each term, so no row here records that.

## WSL clock rate (2026-10-31)

No raw file was saved for this section; the numbers were read live.

| check | result |
|---|---|
| WSL `CLOCK_MONOTONIC` against Windows wall clock | **3.68% fast** over a 40 s `sleep`: 40.24 s in WSL, 38.81 s on Windows. `wt-wsl`'s ping fit agrees at about 3.7% |
| WSL wall clock against Windows wall clock | -2.21% over the same window, because it is stepped back: a +1.1 s offset jump was seen once |
| Cause | Unknown. Which clock is right was not checked against an outside reference; this is one machine, one load state |

Consequences: WSL durations are not interchangeable with Windows durations, WSL wall time can jump,
and the rate may explain the 645-matmul second in experiment 01. Align through `clock_offset` only.
