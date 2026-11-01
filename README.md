# winwsltop

A terminal monitor for one Windows PC running WSL2, built around the question Windows Task Manager and
`nvidia-smi` each answer only half of: **who is using the GPU, Windows or WSL?**

One small `.exe`, nothing to install inside WSL, and it only ever reads.

```text
winwsltop  21:43:50  data 1.2 s old  every 1.0 s
GPU ─────────────────────────── device and engine counters can differ, the difference is unexplained
 91%  device · NVML                                 52%  WSL VM · engine counters,
                                                         VM level, not exact shares
100│⢀⣤⣴⣶⣶⣤⣄                          ⣀⣤⣴⣶⣦⣤⣀       100│             ⣿
   │⣿⣿⣿⣿⣿⣿⣿⣿⣦⣄                     ⣴⣾⣿⣿⣿⣿⣿⣿⣿⣷⣦        │             ⣿
   │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣶⡄                  ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣦⡄     │⢀⣀⣠⣤⣤⣀⣀      ⣿                   ⣀⣀⣠⣤⣄⣀⣀
   │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣧⣀                ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿     │⣿⣿⣿⣿⣿⣿⣿⣿⣶⣦⣀  ⣿                ⢠⣶⣾⣿⣿⣿⣿⣿⣿⣿⣷⣶⣤⡀
   │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣧⣄        ⣀⣴⡆   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿     │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣦⣿⡀               ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷
  0│⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣦⣤⣀⣀⣠⣤⣾⣿⣿⡇   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿    0│⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣤⣄⣀⣀⣀⣀⣀⣤⣶⣿⡇   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
    -60s ──────────────────────────────────── now      -60s ──────────────────────────────────── now
 36%  Windows · engine counters
100│
   │
   │
   │                 ⢀⣀⣀⣀⣀⣀
   │           ⢀⣀⣤⣶⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣶⡆                 ⢀
  0│⣄⣀⣀⣀⣀⣀⣀⣤⣤⣴⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇   ⢰⣤⣤⣀⣀⣀⣀⣀⣀⣀⣤⣤⣶⣿⣿
    -60s ──────────────────────────────────── now
9.0/12.0 GiB  GPU memory · NVML, used of total     187/200 W  power · NVML, of limit
100│                                               100│⣀⣤⣤⣤⣤⣤⣄⣀                        ⢀⣀⣤⣤⣤⣤⣤⣀⡀
   │⣿⣿⣿⣿⣿⣿⣿⣿⣶⣶⣤⣄⣀                 ⢰⣶⣾⣿⣿⣿⣿⣿⣿⣿⣷⣶⣶⣤⣄     │⣿⣿⣿⣿⣿⣿⣿⣿⣷⣦⣀                   ⢠⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣤⡀
   │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣶⣶⣦⣤⣤⣤⣶⣶⣶⣿⡇   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿     │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣤⡀          ⢀⡀   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
   │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿     │⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣤⣄⣀⣀⣀⣀⣀⣤⣶⣿⡇   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
  0│⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿    0│⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇   ⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
    -60s ──────────────────────────────────── now      -60s ──────────────────────────────────── now
device  util ━━━━━━━━━─  91%   mem ━━━━━━━━── 9.0/12.0 GiB  free 3.0 GiB  187/200 W  71°C  2730 MHz
PROCESSES ───────────────────────────────── two systems, two lists: never compare or add across them
Windows  top 3 of 3 by GPU%, busiest engine      │ WSL /dev/dxg holders, no usage measured
 GPU%     pid name                  LOCAL        │     pid name  top 2 of 2
   52   11736 vmwp.exe                4.2 WSL VM │    4121 python3
   36    9002 Game.Shipping-Win64-…   1.4        │    4388 한글추론기
    1    5120 -                         -        │ dxg_denied 36: may miss a root holder
[1] overall  [2] gpu  [3] cpu  [q] quit
```

*The GPU panel, drawn from the test fixture's made-up data. A wide terminal shows more.*

## Why

Run a training job in WSL while something runs on Windows, and the usual tools disagree or stay silent.
Task Manager doesn't show the Linux processes. Inside WSL, `nvidia-smi` here listed no per-process use at
all, even under load. winwsltop reads both sides at once and keeps them apart:

- **GPU use on each side:** Windows processes, and the whole WSL VM as one figure.
- **Who holds the GPU in WSL:** the Linux processes that have `/dev/dxg` open.
- **The device itself:** utilisation, memory, power, temperature and clock.
- **CPU, memory, disk and network** for Windows, and CPU and processes for WSL.
- **A one-minute history** for each graph, and a JSON record you can save.

Every figure names its source. Windows and WSL figures are never added together, because they describe
overlapping resources, and a missing reading shows as a gap, not a zero.

## Get it

Download `winwsltop.exe` from the [latest release](https://github.com/renaissancor/winwsltop/releases/latest)
and run it in Windows Terminal. It runs from any folder, with no installer and no administrator rights.

The binary isn't code-signed, so Windows SmartScreen may warn you the first time. Each release has a
`.sha256` file to check it against, or you can [build it yourself](#build-from-source).

## Use

| key | panel |
|---|---|
| `1` | Overall |
| `2` | GPU: graphs of device, WSL VM, Windows, memory and power, then who is using it |
| `3` | CPU: Windows and WSL side by side, per-core rows, both process lists |
| `q` | Quit (Ctrl+C works too) |

```text
winwsltop                  the live screen
winwsltop --text           print one frame
winwsltop --once           print one JSON line, takes about 3 s
winwsltop --record 60      print 60 JSON lines, one a second
winwsltop --distro NAME    read this WSL distro instead of the default
winwsltop --help           all options, and --version
```

## For agents and scripts

It's safe to run unattended: it only reads, starts no GPU load, and ends its own collector when it exits.
Use `--once` or `--record N`, not the live screen. Stdout carries one JSON object per line and nothing else.

```bash
# From WSL or Git Bash. Any folder works, including the Linux filesystem.
curl -fsSL -o winwsltop.exe https://github.com/renaissancor/winwsltop/releases/latest/download/winwsltop.exe
chmod +x winwsltop.exe

# Who is using the GPU right now?
./winwsltop.exe --once | jq '{util: .gpu_device.util_pct, split: .gpu_split, wsl_holders: .wsl.data.dxg_pids}'
```

Exit 0 means it ran, not that every reading succeeded, and exit 2 means bad arguments. Before trusting a
figure, check `gpu_split.valid`, `gpu_engines_known` and whether `wsl.data` is null.
[`docs/json-output.md`](docs/json-output.md) has every field, its units and how not to misread it.

To change the code rather than run it, start at [`AGENTS.md`](AGENTS.md).

## Requirements

- Windows with WSL2, and `python3` 3.8 or later in the distro. The Linux collector is built into the
  `.exe` and uses only the standard library.
- A terminal with UTF-8 and braille glyphs for the graphs. Windows Terminal is the target.
- An NVIDIA driver for the device, memory and power readings.

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

This project records a counter only after checking what it reads on real hardware.
[`docs/measurable.md`](docs/measurable.md) is the ledger of what was observed, with the raw output for
each number, and it says plainly what was false or never checked. The experiments behind it are in
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
