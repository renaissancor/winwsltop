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
    "": {"schema", "unix_ms", "interval_s", "cpu", "memory", "vm", "processes", "disks", "nets",
         "gpu_engines_known", "gpu_engines", "gpu_device", "gpu_memory", "gpu_split", "wsl", "cost_ms"},
    "cpu": {"busy_pct", "core_pct"},
    "memory": {"total", "available", "cache", "commit_used", "commit_limit"},
    "vm": {"vmmem_pid", "vmwp_pid", "vmmem_working_set", "vmmem_cores"},
    "gpu_split": {"valid", "vm_pct", "windows_pct", "vm_local", "windows_local", "source"},
    "wsl": {"age_s", "restarts", "clock_known", "clock_offset_s", "data"},
    "cost_ms": {"cpu", "memory", "processes", "disk", "net", "gpu", "gpu_device", "gpu_memory"},
}
ROWS = {
    "processes": {"pid", "ppid", "name", "cores", "working_set", "private", "threads", "handles"},
    "disks": {"name", "read_bytes_s", "write_bytes_s", "busy_pct"},
    "nets": {"name", "in_bytes_s", "out_bytes_s", "link_bits_s", "hardware"},
    "gpu_engines": {"pid", "engine", "pct", "source"},
    "gpu_memory": {"pid", "dedicated", "shared", "local"},
}
DEVICE_ALWAYS = {"present", "name"}
DEVICE_OPTIONAL = {"mem_used", "mem_total", "util_pct", "power_w", "power_limit_w", "temp_c", "sm_mhz"}
WSL_DATA = {"schema", "distro", "kernel", "unix_s", "mono_s", "busy_pct", "core_pct", "steal_pct", "memory",
            "pressure", "process_count", "processes", "dxg_pids", "dxg_denied", "cost_ms"}


def fail(why):
    sys.exit("check_json: " + why)


def same(where, got, want):
    if set(got) != want:
        fail(f"{where or 'top level'}: missing {sorted(want - set(got))}, extra {sorted(set(got) - want)}")


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
device = snapshot["gpu_device"]
if not DEVICE_ALWAYS <= set(device) <= DEVICE_ALWAYS | DEVICE_OPTIONAL:
    fail(f"gpu_device keys: {sorted(device)}")
if snapshot["schema"] != 1:
    fail("schema is not 1")

data = snapshot["wsl"]["data"]
if isinstance(data, dict):
    same("wsl.data", data, WSL_DATA)
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
    if not device["present"] or "util_pct" not in device or "mem_total" not in device:
        fail("no GPU device readings: NVML did not load")
    if not snapshot["gpu_engines_known"] or not snapshot["gpu_split"]["valid"]:
        fail("no GPU engine readings: PDH did not work")
    if not snapshot["disks"] or not snapshot["nets"] or not snapshot["gpu_memory"]:
        fail("no disk, network or GPU memory rows")

def named_in(path):
    return set(re.findall(r"`([a-z_0-9]+)`", open(path, encoding="utf-8").read()))


every = set().union(*OBJECTS.values(), *ROWS.values(), DEVICE_ALWAYS, DEVICE_OPTIONAL)
missing = sorted(every - named_in(sys.argv[1])) + sorted(WSL_DATA - named_in("wsl-collector/PROTOCOL.md"))
if missing:
    fail("not in the docs: " + ", ".join(missing))
