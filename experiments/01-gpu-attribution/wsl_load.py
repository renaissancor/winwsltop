"""WSL CUDA load: 3072x3072 matmuls for N seconds, printing throughput per second."""
import sys, time
import torch

seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 15
a = torch.randn(3072, 3072, device="cuda")
b = torch.randn(3072, 3072, device="cuda")
start = time.monotonic()
print("start", flush=True)
for sec in range(1, seconds + 1):
    n, worst = 0, 0.0
    while time.monotonic() - start < sec:
        t = time.monotonic()
        c = a @ b
        torch.cuda.synchronize()
        worst = max(worst, time.monotonic() - t)
        n += 1
    print("sec=%d matmuls=%d worst_ms=%.1f" % (sec, n, worst * 1000), flush=True)
