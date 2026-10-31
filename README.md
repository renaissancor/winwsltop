# winwsltop

A native Windows monitor for a desktop that runs Windows and WSL2 together, focused on the GPU they
share: which side is using it, and what that does to the other side's work.

Status: experiment 01 (can Windows tell WSL's GPU work apart from its own?) passed at VM level with
one trial per case, and the VM-level split, both collectors, the recorder and a terminal screen exist.
What each side can really measure is recorded in [`docs/measurable.md`](docs/measurable.md).

## Build

Edit in WSL; MSVC runs on Windows through interop:

```bash
scripts/build.sh                                                  # winwsltop.exe
scripts/build.sh experiments/01-gpu-attribution/counter_probe.cpp # one source
```

Prints the Windows path of the `.exe`, which can be run from WSL. `winwsltop` opens the live screen;
`--text` prints one frame, `--once` one JSON snapshot, and `--record N` N of them, one a second.
