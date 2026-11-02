# winwsltop

A terminal monitor for one Windows PC running WSL2, built around the question Windows Task Manager and
`nvidia-smi` each answer only half of: **who is using the GPU, Windows or WSL?**

One small `.exe` that only observes. Inside WSL, all it needs is `python3`.

```text
winwsltop  21:43:50  data 1.2 s old  every 1.0 s
GPU ─────────────────────────── device and engine counters can differ, the difference is unexplained
 91%  device · NVML                                 52%  WSL VM · engine counters,
                                                         VM level, not exact shares*
100│⣀⣤⣴⣶⣶⣤⣄                          ⣀⣤⣴⣶⣦⣤⣀       100│             ⣿
   │⣿⣿⣿⣿⣿⣿⣿⣿⣦⣄                     ⣴⣾⣿⣿⣿⣿⣿⣿⣿⣷⣦        │             ⣿
   │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣶⡄                  ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣦⡄     │⣀⣀⣠⣤⣤⣀⣀      ⣿                   ⣀⣀⣠⣤⣄⣀⣀
   │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣧⣀                ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿     │⣿⣿⣿⣿⣿⣿⣿⣿⣶⣦⣀  ⣿                ⢠⣶⣾⣿⣿⣿⣿⣿⣿⣿⣷⣶⣤⡀
   │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣧⣄        ⣀⣴⡆   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿     │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣦⣿⡀               ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷
  0│⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣦⣤⣀⣀⣠⣤⣾⣿⣿⡇   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿    0│⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣤⣄⣀⣀⣀⣀⣀⣤⣶⣿⡇   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
    -60s ───────── avg 59% max 95% ────────── now      -60s ──────── avg 37% max 112%* ───────── now
 36%  Windows · engine counters
100│
   │
   │
   │                 ⢀⣀⣀⣀⣀⣀
   │           ⢀⣀⣤⣶⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣶⡆                 ⢀
  0│⣀⣀⣀⣀⣀⣀⣀⣤⣤⣴⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇   ⢰⣤⣤⣀⣀⣀⣀⣀⣀⣀⣤⣤⣶⣿⣿
    -60s ───────── avg 16% max 38% ────────── now
9.0/12.0 GiB  GPU memory · NVML, used of total     187/200 W  power · NVML, of limit
100│                                               100│⣀⣤⣤⣤⣤⣤⣄⣀                        ⢀⣀⣤⣤⣤⣤⣤⣀⡀
   │⣿⣿⣿⣿⣿⣿⣿⣿⣶⣶⣤⣄⣀                 ⢰⣶⣾⣿⣿⣿⣿⣿⣿⣿⣷⣶⣶⣤⣄     │⣿⣿⣿⣿⣿⣿⣿⣿⣷⣦⣀                   ⢠⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣤⡀
   │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣶⣶⣦⣤⣤⣤⣶⣶⣶⣿⡇   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿     │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣤⡀          ⢀⡀   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
   │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿     │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣤⣄⣀⣀⣀⣀⣀⣤⣶⣿⡇   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
  0│⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿    0│⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
    -60s ─────── avg 8.4 max 9.8 GiB ──────── now      -60s ──────── avg 129 max 182 W ───────── now
device  util ━━━━━━━━━─  91%   mem ━━━━━━━━── 9.0/12.0 GiB  free 3.0 GiB  187/200 W  71°C  2730 MHz
PROCESSES ───────────────────────────────── two systems, two lists: never compare or add across them
Windows  top 3 of 3 by GPU%, busiest engine      │ WSL /dev/dxg holders, no usage measured
 GPU%     pid name                  LOCAL        │     pid name  top 2 of 2
   52   11736 vmwp.exe                4.2 WSL VM │    4121 python3
   36    9002 Game.Shipping-Win64-…   1.4        │    4388 한글추론기
    1    5120 -                         -        │ dxg_denied 36: may miss a root holder
1 overview  2 gpu  3 cpu  4 mem  5 disk  6 net  7 procs  8 vm cost                  ←→ 1 min  q quit
```

*The GPU panel, drawn from the test suite's sample data rather than a live run. A wide terminal shows more.*

## Why

Run a training job in WSL while something runs on Windows, and the usual tools disagree or stay silent.
Task Manager doesn't show the Linux processes. Inside WSL, `nvidia-smi` here listed no per-process use at
all, even under load. winwsltop reads both sides at once and keeps them apart:

- **GPU use on each side:** Windows processes, and the whole WSL VM as one figure.
- **Who holds the GPU in WSL:** the Linux processes that have `/dev/dxg` open.
- **The device itself:** utilisation, memory, power, temperature and clock.
- **When the two sides fight over the CPU:** WSL can't see this for itself. In one test it read 99.997% busy,
  with `steal` at 0, while doing 71% less work. The CPU panel shows what Windows really gave the VM.
- **CPU, memory, disk and network** for Windows, and CPU, memory and processes for WSL.
- **History** for each graph over the last 1, 5 or 10 minutes, the selected process's own history on the CPU and
  memory panels, and a JSON record you can save.

Each graph names its source. Windows and WSL figures are never added together, because they
describe overlapping resources, and a sample that never arrived shows as a gap in the graph, not a zero. On the CPU panel, on the memory
panel's WSL used and cache, and in the selected process's history, one late WSL reading carries the last value
for a second, because the two sides' clocks slip.

## Get it

Download `winwsltop.exe` from the [latest release](https://github.com/renaissancor/winwsltop/releases/latest)
and run it in Windows Terminal. It runs from any folder, with no installer and no administrator rights.

The binary isn't code-signed, so Windows SmartScreen may warn you the first time. Each release has a
`.sha256` file to check it against, or you can [build it yourself](#build-from-source).

## Use

| key | does |
|---|---|
| `1` | Overview: the GPU and its split, Windows CPU, memory, disk and network, WSL CPU, memory and pressure, the VM, then both process lists |
| `2` | GPU: graphs of device, WSL VM, Windows, memory and power, then Windows GPU users and WSL `/dev/dxg` holders |
| `3` | CPU: what each side says and what the VM costs, in words, then a timeline of every thread on both sides, then both process lists and the selected process's history. Needs a window 100 wide and 29 tall at 20 threads, or it shows a simpler panel |
| `4` | Memory: each side's used and cache, the VM's working set on its own row, both sides over time, then both process lists by memory and the selected process's history |
| `5`-`8` | Disk, network, processes and VM cost: not built yet; each page says where its figures are today |
| `←` `→` | On a panel with a graph: show the last 1, 5 or 10 minutes. History is kept in memory from launch, so a new run starts empty |
| `↑` `↓` | On panels 3 and 4: select a process. A long list scrolls with it, and the history below shows it |
| `tab` | On panels 3 and 4: move the selection between the Windows list and the WSL list |
| `esc` | Back to panel 1. Each panel keeps its selection |
| `q` | Quit. `ㅂ` with Korean input on, Alt+Q and Ctrl+C work too |

A figure marked `?` is one the tool shows but has not yet checked against a second source on this machine:
Windows cache, WSL swap, WSL load, WSL steal and WSL's process count. The WSL process memory column reads `stat rss`, the kernel's `/proc/pid/stat`
figure, which runs a little below what `ps` shows.

The CPU panel is built for a dark terminal. A light theme stays readable only in part.

### What `WSL short` means

`WSL short ≈14 of 20 vCPUs, inferred` is the gap between how busy WSL says it was and how much CPU Windows
charged the VM. It's the one cross-system figure in the tool, and it's an inference, so it says so. In a
test with both sides using every thread, it tracked the work WSL really lost, at three levels of contention,
one trial each. It isn't a measurement, and it's limited to what was tested:

- It needs WSL at 99% or more and Windows at 95% or more. Anywhere else it reads `n/a`, which doesn't mean
  nothing was lost. A partly busy WSL under contention hasn't been tested.
- A `.wslconfig` limit on processors turns it off, as `lost CPU n/a, vCPUs differ`.
- It was only tested against CPU load, not memory, disk or GPU load, so a game beside a build may read `n/a`.
- It's a 5-second mean, so it lags, and it fades over about 7 seconds after Windows goes quiet.

The tool shows no CPU temperature or clock speed. No temperature was readable here without administrator
rights, and the live clock figures don't yet agree with each other. Its busy figures can also differ from
Task Manager's.

```text
winwsltop                  the live screen
winwsltop --text           print one frame
winwsltop --once           print one JSON line, takes about 3 s, or 8 s if WSL doesn't answer
winwsltop --record 60      print 60 JSON lines, one a second
winwsltop --distro NAME    read this WSL distro instead of the default
winwsltop --help           all options, and --version
```

## For agents and scripts

It's safe to run unattended: it changes no settings, starts no GPU load, and ends its own collector when it
exits. The one side effect is that WSL starts the distro if it was stopped. Use `--once` or `--record N`,
not the live screen. Stdout carries one JSON object per line and nothing else.

```bash
# From WSL or Git Bash. Any folder works, including the Linux filesystem.
curl -fsSL -o winwsltop.exe https://github.com/renaissancor/winwsltop/releases/latest/download/winwsltop.exe
chmod +x winwsltop.exe

# The GPU split, plus which WSL processes hold the GPU: holders, not usage. Needs jq.
./winwsltop.exe --once | jq '{util: .gpu_device.util_pct, split: .gpu_split, wsl_holders: .wsl.data.dxg_pids}'
```

Exit 0 means it ran, not that every reading succeeded, and exit 2 means bad arguments. Before trusting a
figure, check `gpu_split.valid` and `gpu_engines_known`, and check that `wsl.data` isn't null and `wsl.age_s`
is under 5.
[`docs/json-output.md`](docs/json-output.md) has every field, its units and how not to misread it.

To change the code rather than run it, start at [`AGENTS.md`](AGENTS.md).

## Requirements

- 64-bit Windows with WSL2, a default distro (or `--distro`), and `python3` in that distro. The Linux
  collector is built into the `.exe` and uses only the standard library. It's written for Python 3.8 or
  later, and has only been run on 3.12.
- A terminal with UTF-8 and braille glyphs for the graphs. Windows Terminal is the target.
- An NVIDIA driver for the device, memory and power readings.
- To run it from a WSL shell, as below, WSL interop must be on, which is the default.

Without a working distro, the Windows side keeps running and the screen says WSL data is unavailable.

## What it can't tell you

- **Which Linux process is using how much GPU.** That isn't measured. The split between Windows and WSL
  is at VM level only, and `/dev/dxg` shows who holds the device, not how much they use.
- **Exact shares.** Device and engine counters can disagree, and the difference is unexplained.
- **Whether the two sides are hurting each other.** High utilisation can hide a slowdown, so a busy
  graph is not a verdict.

## Tested on

One machine: Windows build 26200, WSL2 kernel 6.6.114.1, an NVIDIA RTX 4070. The Windows-versus-WSL split
passed at VM level with one trial per case. AMD and Intel GPUs, other distros, Windows 10 and the legacy
console are all untested, so reports from other machines are welcome, including failures.

## How it's checked

[`docs/measurable.md`](docs/measurable.md) is the ledger of what was observed on real hardware, with the
raw output for each number where it was saved. It says plainly what turned out false, what was only
partly checked, and what was never checked. The experiments behind it are in
[`experiments/`](experiments/).

## Build from source

You need WSL plus Visual Studio with the C++ tools on Windows. Source is edited in WSL, and MSVC runs on
Windows through interop:

```bash
scripts/build.sh            # builds winwsltop.exe and prints its Windows path
scripts/check.sh --windows  # all checks, including the build and the render checks
```

The design, ownership rules and contributor docs start at [`docs/README.md`](docs/README.md).

## Licence

MIT, see [`LICENSE`](LICENSE).
