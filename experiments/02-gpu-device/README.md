# Experiment 02: do the GPU device and memory collectors read true?

No GPU load is started; every run here is a read on the idle desktop. Build from the repo root:

    scripts/build.sh experiments/02-gpu-device/gpu_probe.cpp
    scripts/build.sh experiments/02-gpu-device/nvml_absent.cpp

- `gpu_probe.exe [rounds] [rows]` prints both collectors, once a second, plus NVML's v1 and v2 memory
  calls side by side. It `#include`s the collector sources, since `build.sh` compiles one file.
- `nvml_absent.exe missing|wrong` redirects `LoadLibraryExW` at compile time, so the absent-library
  paths run without changing the machine. Exit 0 means `present` stayed false.
- `compare.sh <build dir>` alternates the probe with `nvidia-smi`, then brackets `Get-Counter`
  (`counters.ps1`, copied beside the exe) between two probe reads. Output is in `results/`.

`nvidia-smi` is itself an NVML client, so agreement shows the bindings and units are right. It is not
an independent measurement of the hardware.

**Result (2026-10-31):** see `docs/measurable.md`.
