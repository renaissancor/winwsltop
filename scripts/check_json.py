#!/usr/bin/env python3
"""Reads `winwsltop --once` on stdin. Fails unless it is one JSON line and docs name every key.

The collector's own fields under wsl.data are documented in wsl-collector/PROTOCOL.md."""
import json
import re
import sys


def keys(value, skip=("data",)):
    if isinstance(value, dict):
        for k, v in value.items():
            yield k
            if k not in skip:
                yield from keys(v, skip)
    elif isinstance(value, list):
        for v in value:
            yield from keys(v, skip)


lines = [line for line in sys.stdin.read().splitlines() if line.strip()]
if len(lines) != 1:
    sys.exit(f"expected one line, got {len(lines)}")
snapshot = json.loads(lines[0])
documented = set(re.findall(r"`([a-z_0-9]+)`", open(sys.argv[1], encoding="utf-8").read()))
missing = sorted(set(keys(snapshot)) - documented)
if missing:
    sys.exit("not in the doc: " + ", ".join(missing))
