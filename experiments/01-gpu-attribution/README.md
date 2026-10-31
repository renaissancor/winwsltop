# Experiment 01: can Windows tell WSL's GPU work apart from its own?

Hypothesis, to be tested and not assumed: WSL GPU work appears in `\GPU Engine(*)` under the
`vmmemWSL` PID, or under another stable PID. Run 2 found it under `vmwp.exe`, the parent of
`vmmemWSL`, so the probe marks that PID as `[vm]`.

`counter_probe.exe [seconds]` prints every engine instance with non-zero use, once a second, and
marks rows whose PID is `vmmemWSL` or its parent.

Runs, recorded under `results/`:

1. idle baseline
2. WSL CUDA load only
3. Windows GPU load only
4. both together

A 1 s sample can miss short bursts, so repeat any ambiguous case with a shorter interval.

| result | meaning |
|---|---|
| WSL load shows consistently under `vmmemWSL` | VM-level split is measured |
| WSL load shows under another stable PID | split may still work; identify that PID |
| only unattributable totals move | the split would be inference; rethink the core feature |

**Result (2026-10-31): passed at VM level, one trial per case.** See `docs/measurable.md`. `d3d_load.exe` and
`wsl_load.py` are the loads; each prints its own throughput per second.
