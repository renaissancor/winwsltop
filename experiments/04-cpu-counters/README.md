# Experiment 04: which CPU counters are readable here, and do they mean anything?

Question: which CPU counters can a non-elevated program read on this machine, on the Windows side and
in the WSL guest, which of them move, and does Windows' figure for the VM follow what the guest says
it did? No load is started, GPU or CPU, and no setting is changed: every run reads whatever the
machine is doing. Build from the repo root, then run:

    scripts/build.sh experiments/04-cpu-counters/cpu_probe.cpp
    scripts/build.sh
    experiments/04-cpu-counters/collect.sh <build dir as a /mnt/c path> <date> <dir for full record lines>
    python3 experiments/04-cpu-counters/analyze.py experiments/04-cpu-counters/results <date> \
      > experiments/04-cpu-counters/results/analysis-<date>.txt

Run the builds before `collect.sh`, never during it: MSVC is a CPU load.

- `cpu_probe.cpp`: `topology` prints the token, the OS build, CPUID, `GetLogicalProcessorInformationEx`,
  CPU sets and `CallNtPowerInformation`. `run <n>` prints, once a second, every counter in its tables,
  all fields of the NT call `src/host/cpu.cpp` makes, and the process and thread rows of `vmmemWSL`
  and `vmwp` from `SystemProcessInformation`. `cost <n>` times each counter group on its own query,
  in a rotating order. No process is opened.
- `wsl_sample.py <n>`: `/proc/stat`, `/proc/loadavg`, `/proc/pressure/cpu`, `/proc/cpuinfo` MHz and
  the root `cpu.stat`, once a second. `wsl_static.sh`: what the guest offers, in one pass.
- `windows.ps1`: `Win32_Processor` and three temperature sources through WMI, selected properties
  only, because that class also holds a serial number.
- `collect.sh`: runs all of it. Three concurrent windows, each of 70 probe and guest rounds around one
  `winwsltop --record 60`, then 60 rounds of `cost` alone.
- `slim_record.py`: `results/window*-record-*.txt` are not the full `--record` lines. A full line lists
  every process on the machine, about 100 KB a line and naming this PC's programs, and this repo is
  public. The cut keeps `cpu`, `vm`, `cost_ms`, the timing keys, the numeric `wsl` fields, the rows of
  the VM's own processes and of `Idle`, `System` and `winwsltop.exe`, and the sum of cores over all
  rows. The kept fields are copied unchanged. A claim that needs a dropped process cannot cite these.
- `analyze.py`: turns the files above into `results/analysis-<date>.txt`. It needs no live machine.

How independent each comparison is:

| comparison | independent of | shows |
|---|---|---|
| NT call against `\Processor Information` | nothing: the same kernel counts | bindings, units, what each field holds |
| `% Processor Time` against `\Hyper-V Hypervisor Logical Processor` | the Windows scheduler's accounting, not the machine | numbering, and a coarse check of busy |
| Guest `/proc/stat` against `\Hyper-V Hypervisor Virtual Processor` | yes: another kernel counts one side, the hypervisor the other | what the VM did, at VM and at vCPU level |
| Guest `/proc/stat` against `vmmemWSL` process time | yes: another kernel counts one side | that `vmmem_cores` follows the guest |
| `Parking Status` against the CPU set Parked flag | nothing, and read at different moments | that both exist, not that they agree |
| `Actual Frequency`, `% Processor Performance`, `% Processor Utility` | nothing was available | changing values only |

Nothing here reads a frequency or a temperature outside Windows' own account. The guest's `cpu MHz`
never moves, and the guest has no thermal zones.

Reading the correlations: `r` is Pearson's, computed over one session's natural activity, where a few
bursts dominate. `analyze.py` therefore also prints rank r, r with the burst rounds left out, and the
largest |r| at lags of 5 to 15 records as a chance level. A high r with a shared burst proves little:
`Hv VP 0` reaches r of about 0.9 with five different Windows LPs.

The bursts were not started by this experiment and their source was not identified. Window 2's opens
in the first records after `winwsltop` started, so the capture may have contributed to that one.

`cost` caveat: the groups run one straight after another each round, so a group after the first can
find a provider's data warm. The analysis prints each group's time when it ran first in its round.

The probe does not print the status of a whole-array read that failed, so the four such reads in the
2026-11-01 run have no status on file. A rerun should print it.

**Result (2026-11-01):** see `docs/measurable.md`, section "CPU counters checked".
