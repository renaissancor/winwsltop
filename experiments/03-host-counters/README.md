# Experiment 03: do the disk and network collectors read true?

Question: do `src/host/disk.cpp` and `src/host/network.cpp` report what this machine's disks and
adapters did? No load is started, GPU or otherwise, and no setting is changed: every run reads
whatever disk and network traffic the machine already has. Build from the repo root, then run:

    scripts/build.sh experiments/03-host-counters/host_probe.cpp
    experiments/03-host-counters/compare.sh <build dir as a /mnt/c path> <date>

- `host_probe.cpp` `#include`s both collector sources and opens their private members, so the raw
  values it prints are the ones the collector read: its PDH counters, its `% Idle Time` before the
  clamp, its stored octets and its elapsed time. Direct `GetIfTable2` reads either side of each sample
  and the filter's reason for each dropped row are printed beside them.
- `compare.sh` copies the `.ps1` files beside the exe, runs everything below and saves it in
  `results/`. `analyze.py` turns those files into `analysis-<date>.txt` and can be rerun alone.
- Rows: `identity.ps1` lists `Get-Disk`, `Get-Partition` and `Get-NetAdapter -IncludeHidden`; the
  probe's `rows` mode lists all of `GetIfTable2`. They are joined on interface index.
- Network bracket, 10 rounds: raw octets, `Get-NetAdapterStatistics` (`netstats.ps1`), raw octets.
  The Windows reading must fall between the two raw reads.
- Concurrent windows, 3 of them: `counters.ps1` starts first and runs 75 one-second samples of
  `Get-Counter` and WMI, the probe runs 60 one-second rounds inside them. Rates depend on their window,
  so cumulative totals are compared, not rates: the collector's rate times its time, and its raw delta,
  must fall between the delta over the samples inside the window and the delta over the samples just
  outside it. `% Idle Time` is compared as idle time over elapsed time from the same cumulative values.
- Device side: the probe reads the NVMe health log (log page 2) at both ends of each window, from a
  handle opened with no access rights.
- Guest side: `/proc/net/dev` in WSL is read between two raw Windows reads at both ends of each window.

How independent each comparison is:

| comparison | independent of | shows |
|---|---|---|
| `Get-Counter` on `\PhysicalDisk` | nothing: the same PDH provider | bindings, units, instance handling |
| WMI `MSDiskDriver_Performance` | PDH, but it returns the same count as `Get-Counter` | a second path, not a second measurement |
| NVMe Data Units Read and Written | Windows' disk accounting: the drive keeps them | bytes, at 512000-byte resolution, disk 0 only |
| `Get-NetAdapterStatistics` | an API path only; a shared source is not ruled out | bindings and units |
| `/proc/net/dev` `eth0` against `vEthernet` | Windows: another kernel counts them | octets on the WSL adapter only |
| `Get-NetAdapter` link speed and hardware flag | an API path only; a shared source is not ruled out | bindings and units |

Nothing here is independent for `% Idle Time`, or for the physical NIC. A person reading Task Manager
is not part of this experiment. `IOCTL_DISK_PERFORMANCE` was not tried: Microsoft describes it as
enabling performance counters, and this experiment changes nothing.

A rerun should take one more NVMe read about a minute after the last window: the 2026-11-01 run has no
later reading to show whether its gap from the drive was durable.

**Result (2026-11-01):** see `docs/measurable.md`, section "Disk and network collectors checked".
