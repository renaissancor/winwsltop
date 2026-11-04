"""Short-lived CPU children for experiment 07: does CPU time of a process that exits between two walks
reach the sum? Starts one child every 0.25 s for <seconds>; each spins 0.2 s on CLOCK_MONOTONIC and exits,
so none lives a whole walk interval. Waits for every child, so nothing is left running. Prints its start and
end on CLOCK_MONOTONIC (the probe's clock), the children started, and their CPU time from os.times().
    python3 -I experiments/07-guest-walk/churn.py <seconds>
"""
import json, os, subprocess, sys, time

SPIN = "import time\ne=time.monotonic()+0.2\nwhile time.monotonic()<e: pass\n"


def main():
    seconds = float(sys.argv[1])
    t0 = time.monotonic()
    c0 = os.times()
    n = 0
    while time.monotonic() - t0 < seconds:
        subprocess.run([sys.executable, "-I", "-c", SPIN], check=True)
        n += 1
        time.sleep(max(0.0, t0 + n * 0.25 - time.monotonic()))
    t1 = time.monotonic()
    c1 = os.times()
    cpu = (c1.children_user - c0.children_user) + (c1.children_system - c0.children_system)
    print(json.dumps({"churn_start_mono": round(t0, 3), "churn_end_mono": round(t1, 3), "children": n,
                      "children_cpu_s": round(cpu, 3), "children_cores": round(cpu / (t1 - t0), 3)}))


if __name__ == "__main__":
    main()
