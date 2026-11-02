#!/usr/bin/env python3
"""Reads `winwsltop --once` on stdin and checks it against what docs/json-output.md promises.

usage: check_json.py docs/json-output.md [--full]

Key sets are fixed here, per object and in both directions, and a key added to the output has to be
added here and to its doc. --full is for the release machine, which has WSL and an NVIDIA GPU: it also
requires every collector to have returned real data, so a binary that runs but measures nothing fails."""
import json
import re
import sys

OBJECTS = {
    "": {"schema", "unix_ms", "interval_s", "cpu", "cpu_layout_known", "cpu_layout", "memory", "vm", "processes",
         "ready_threads", "vm_ready_threads", "disks", "nets", "gpu_engines_known", "gpu_engines", "gpu_device",
         "gpu_memory", "gpu_split", "lost_cpu_inferred", "cpu_derived", "wsl", "cost_ms"},
    "cpu": {"busy_pct", "user_pct", "kernel_pct", "core_pct"},
    "memory": {"total", "available", "cache", "commit_used", "commit_limit"},
    "vm": {"vmmem_pid", "vmwp_pid", "vmmem_working_set", "vmmem_cores", "vmmem_cores_known"},
    "gpu_split": {"valid", "vm_pct", "windows_pct", "vm_local", "windows_local", "source"},
    "lost_cpu_inferred": {"state", "vcpus", "source"},
    "cpu_derived": {"vm_cpu_pct", "own_kernel_pct", "ready_threads_mean", "vm_ready_threads_mean", "source"},
    "wsl": {"age_s", "restarts", "clock_known", "clock_offset_s", "data"},
    "cost_ms": {"cpu", "memory", "processes", "disk", "net", "gpu", "gpu_device", "gpu_memory"},
}
ROWS = {
    "cpu_layout": {"core", "thread", "efficiency"},
    "processes": {"pid", "ppid", "name", "cores", "working_set", "private", "threads", "handles", "cores_known",
                  "create_time"},
    "disks": {"name", "read_bytes_s", "write_bytes_s", "busy_pct"},
    "nets": {"name", "in_bytes_s", "out_bytes_s", "link_bits_s", "hardware"},
    "gpu_engines": {"pid", "engine", "pct", "source"},
    "gpu_memory": {"pid", "dedicated", "shared", "local"},
}
DEVICE_ALWAYS = {"present", "name"}
DEVICE_OPTIONAL = {"mem_used", "mem_total", "util_pct", "power_w", "power_limit_w", "temp_c", "sm_mhz"}
WSL_DATA = {"schema", "distro", "kernel", "unix_s", "mono_s", "busy_pct", "core_pct", "cpu_split", "load",
            "steal_pct", "memory", "pressure", "process_count", "processes", "dxg_pids", "dxg_denied", "cost_ms"}
WSL_SPLIT = {"user", "sys", "iowait"}
WSL_PROCESS = {"pid", "name", "state", "rss", "cores", "threads", "on_gpu", "rated", "start"}
LOST_STATES = {"collecting", "no_data", "guest_under_99", "windows_under_95", "vcpus_differ", "known"}


def fail(why):
    sys.exit("check_json: " + why)


def same(where, got, want):
    if set(got) != want:
        fail(f"{where or 'top level'}: missing {sorted(want - set(got))}, extra {sorted(set(got) - want)}")


def number(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def count(v):
    return isinstance(v, int) and not isinstance(v, bool) and v >= 0


lines = [line for line in sys.stdin.read().splitlines() if line.strip()]
if len(lines) != 1:
    fail(f"expected one line, got {len(lines)}")
snapshot = json.loads(lines[0])
if not isinstance(snapshot, dict):
    fail("not an object")

for name, want in OBJECTS.items():
    obj = snapshot if not name else snapshot.get(name)
    if not isinstance(obj, dict):
        fail(f"{name} is not an object")
    same(name, obj, want)
for name, want in ROWS.items():
    if not isinstance(snapshot[name], list):
        fail(f"{name} is not a list")
    for row in snapshot[name]:
        same(name, row, want)
if not snapshot["processes"] or not snapshot["cpu"]["core_pct"]:
    fail("no processes or no CPU cores: a real machine has both")
cpu, vm, layout = snapshot["cpu"], snapshot["vm"], snapshot["cpu_layout"]
# A healthy run knows these. A failed read or a zero-tick window can leave them null, and this gate wants neither.
if not number(cpu["user_pct"]) or not number(cpu["kernel_pct"]):
    fail(f"cpu.user_pct {cpu['user_pct']}, cpu.kernel_pct {cpu['kernel_pct']}: a healthy CPU read gives both")
if not count(snapshot["ready_threads"]):
    fail(f"ready_threads is {snapshot['ready_threads']}: a healthy process read gives a count")
# Not found is the one unknown a good read can give, and it must never read as a count of 0.
if vm["vmmem_pid"]:
    if not count(snapshot["vm_ready_threads"]):
        fail(f"vm_ready_threads is {snapshot['vm_ready_threads']} with vmmemWSL found")
elif snapshot["vm_ready_threads"] is not None or vm["vmmem_cores_known"] is not False:
    fail("no vmmemWSL, yet vm_ready_threads or vmmem_cores_known says it was read")
if not all(isinstance(row["cores_known"], bool) and count(row["create_time"]) for row in snapshot["processes"]):
    fail("a processes row's cores_known is not a boolean or its create_time not a count")
# A first reading's 0 is unmeasured: a row that does not know its cores must not claim any.
if any(not row["cores_known"] and row["cores"] != 0 for row in snapshot["processes"]):
    fail("a processes row has cores without cores_known")
if not isinstance(vm["vmmem_cores_known"], bool) or not isinstance(snapshot["cpu_layout_known"], bool):
    fail("vmmem_cores_known and cpu_layout_known are booleans")
if snapshot["cpu_layout_known"] != bool(layout) or not all(count(v) for row in layout for v in row.values()):
    fail(f"cpu_layout_known is {snapshot['cpu_layout_known']} with {len(layout)} rows, or a row is not counts")
lost, derived = snapshot["lost_cpu_inferred"], snapshot["cpu_derived"]
if lost["source"] != "inferred" or derived["source"] != "derived":
    fail(f"lost_cpu_inferred.source is {lost['source']}, cpu_derived.source is {derived['source']}")
if lost["state"] not in LOST_STATES:
    fail(f"lost_cpu_inferred.state is {lost['state']}")
# Unknown is null: a 0 here outside `known` would read as nothing lost.
if lost["state"] == "known" and not (number(lost["vcpus"]) and lost["vcpus"] >= 0):
    fail(f"lost_cpu_inferred is known with vcpus {lost['vcpus']}")
if lost["state"] != "known" and lost["vcpus"] is not None:
    fail(f"lost_cpu_inferred is {lost['state']} with vcpus {lost['vcpus']}, not null")
for name in OBJECTS["cpu_derived"] - {"source"}:
    if derived[name] is not None and not (number(derived[name]) and derived[name] >= 0):
        fail(f"cpu_derived.{name} is {derived[name]}: a figure or null")
if vm["vmmem_cores_known"] != (derived["vm_cpu_pct"] is not None):
    fail(f"vmmem_cores_known is {vm['vmmem_cores_known']} with cpu_derived.vm_cpu_pct {derived['vm_cpu_pct']}")
device = snapshot["gpu_device"]
if not DEVICE_ALWAYS <= set(device) <= DEVICE_ALWAYS | DEVICE_OPTIONAL:
    fail(f"gpu_device keys: {sorted(device)}")
if snapshot["schema"] != 1:
    fail("schema is not 1")

data = snapshot["wsl"]["data"]
if isinstance(data, dict):
    same("wsl.data", data, WSL_DATA)
    for row in data["processes"]:
        same("wsl.data.processes", row, WSL_PROCESS)
        if not isinstance(row["rated"], bool) or not count(row["start"]):
            fail(f"wsl.data.processes pid {row['pid']}: rated is not a boolean or start not a count")
        if not row["rated"] and row["cores"] != 0:
            fail(f"wsl.data.processes pid {row['pid']} has cores without rated")
    # null is the only unknown here: the screen reads either, and a half-shaped value would be a defect.
    if data["cpu_split"] is not None:
        same("wsl.data.cpu_split", data["cpu_split"], WSL_SPLIT)
    if data["load"] is not None and (not isinstance(data["load"], list) or len(data["load"]) != 3):
        fail(f"wsl.data.load is {data['load']}, not three numbers or null")
if "--full" in sys.argv:
    if not isinstance(data, dict):
        fail("wsl.data is null: the collector did not run")
    wsl = snapshot["wsl"]
    if not 0 <= wsl["age_s"] < 5:
        fail(f"wsl.age_s is {wsl['age_s']}: the collector is not live")
    if wsl["restarts"] != 0:
        fail(f"the collector restarted {wsl['restarts']} times")
    if data["schema"] != 1 or not data["core_pct"] or not data["processes"]:
        fail("wsl.data has no real content")
    if not isinstance(data["busy_pct"], (int, float)):
        fail("wsl.data.busy_pct is not a number")
    split = data["cpu_split"]
    if not isinstance(split, dict) or not all(number(v) for v in split.values()):
        fail(f"wsl.data.cpu_split is {split}: /proc/stat was read, so it is known")
    if abs(split["user"] + split["sys"] - data["busy_pct"]) > 0.05:
        fail(f"wsl.data.cpu_split user plus sys is not busy_pct {data['busy_pct']}: {split}")
    if not isinstance(data["load"], list) or not all(number(v) for v in data["load"]):
        fail(f"wsl.data.load is {data['load']}: /proc/loadavg did not read")
    if not all(count(row["threads"]) for row in data["processes"]):
        fail("a wsl.data.processes row has no threads count")
    if abs(cpu["user_pct"] + cpu["kernel_pct"] - cpu["busy_pct"]) > 0.01:
        fail(f"cpu.user_pct plus kernel_pct is not busy_pct: {cpu['user_pct']}, {cpu['kernel_pct']}, {cpu['busy_pct']}")
    if not snapshot["cpu_layout_known"] or len(layout) != len(cpu["core_pct"]):
        fail(f"cpu_layout has {len(layout)} rows for {len(cpu['core_pct'])} logical CPUs")
    if len({(row["core"], row["thread"]) for row in layout}) != len(layout):
        fail("two cpu_layout rows name the same thread of the same core")
    # Windows' list is read every sample, so after the priming call almost every row was seen before.
    if not any(row["cores_known"] for row in snapshot["processes"]):
        fail("no processes row has cores_known: the processes were read last sample")
    if not any(row["rated"] for row in data["processes"]):
        fail("no wsl.data.processes row is rated: the collector has read its processes before")
    if not vm["vmmem_pid"] or not vm["vmmem_cores_known"]:
        fail("vmmem_cores_known is false: the VM is running and was seen last sample")
    # Printed to 3 places, so the sum is checked to two of them.
    if abs(derived["vm_cpu_pct"] - 100 * vm["vmmem_cores"] / len(cpu["core_pct"])) > 0.01:
        fail(f"cpu_derived.vm_cpu_pct {derived['vm_cpu_pct']} is not vmmem_cores {vm['vmmem_cores']} over {len(cpu['core_pct'])} logical CPUs")
    if not number(derived["own_kernel_pct"]) or abs(derived["own_kernel_pct"] - max(0, cpu["kernel_pct"] - derived["vm_cpu_pct"])) > 0.01:
        fail(f"cpu_derived.own_kernel_pct {derived['own_kernel_pct']} is not kernel_pct {cpu['kernel_pct']} less vm_cpu_pct, clamped")
    if not device["present"] or "util_pct" not in device or "mem_total" not in device:
        fail("no GPU device readings: NVML did not load")
    if not snapshot["gpu_engines_known"] or not snapshot["gpu_split"]["valid"]:
        fail("no GPU engine readings: PDH did not work")
    if not snapshot["disks"] or not snapshot["nets"] or not snapshot["gpu_memory"]:
        fail("no disk, network or GPU memory rows")

def named_in(path):
    return set(re.findall(r"`([a-z_0-9]+)`", open(path, encoding="utf-8").read()))


every = set().union(*OBJECTS.values(), *ROWS.values(), DEVICE_ALWAYS, DEVICE_OPTIONAL, LOST_STATES)
missing = sorted(every - named_in(sys.argv[1])) + sorted((WSL_DATA | WSL_SPLIT | WSL_PROCESS) - named_in("wsl-collector/PROTOCOL.md"))
if missing:
    fail("not in the docs: " + ", ".join(missing))
