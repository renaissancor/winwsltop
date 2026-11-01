# Roadmap: better than btop

`btop` shows one machine. winwsltop's reason to exist is what `btop` cannot show: two systems sharing
one GPU, what each costs the other, and a record of it. Host panels only need to be good enough.
In order. Everything here is PROPOSED until a contract's decision log records it. Read `CLAUDE.md`
and `docs/measurable.md` first; two items below are also open questions in `docs/contracts/`.

## 1. Now
- Windows GPU holders first, or one holders block under GPU. Today `warp.exe`, the biggest user, is row 6.
- Show `unattributed` between device totals and the split, with a short source label per figure.
- About 10 Windows rows, idle virtual NICs hidden, `dxg_denied` dimmed unless GPU use has no visible holder.
- Test the split and `Local Usage` under a real load in the merged build. Check whether WSL memory
  appears under `vmwp.exe`. Only with the user's go-ahead for GPU load.

## 2. What beats btop
- **History:** 60 s graphs exist, and the CPU panel keeps 15 minutes (`contracts/ui.md`). Still PROPOSED:
  per-process history and longer windows. An in-memory ring per GPU side, CPU, memory and process. Braille chart of WSL vs Windows
  over 1, 10 and 60 minutes as the centrepiece.
- **Contention verdict:** a plain line when both sides are busy and the GPU is near its limit, with the
  moment it began. Utilization alone hid -46% and -59% in experiment 01.
- **Workload truth:** optional throughput or latency from workloads, such as vLLM `/metrics` in WSL.
- **Keys and alerts:** `sshvllmtop`'s processes & history view, selection, range, sort, legend, ATTENTION block.
- **Replay:** `--replay FILE` through the same `render()`. `btop` has no memory of last night's failure.

## 3. Also needed
- Which clock is right, against an outside reference (`docs/measurable.md`, WSL clock rate).
- Why NVML reads 11-20% at idle while engine counters read about 6%.
- Repeat experiment 01 with a game or a second workload, more than once.
- Colours, braille and Korean text in Windows Terminal need a person's eyes; legacy conhost is unchecked.
- A GUI as a separate process over the JSON stream, drawing without the GPU it is measuring.
