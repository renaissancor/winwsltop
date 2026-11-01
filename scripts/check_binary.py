#!/usr/bin/env python3
"""usage: check_binary.py <exe> <needle>...

Fails if the binary holds any needle, case-insensitively, in ASCII or UTF-16. A needle that came back
empty or as an unexpanded %VARIABLE% also fails: a search for nothing proves nothing."""
import sys

data = open(sys.argv[1], "rb").read().lower()
for needle in sys.argv[2:]:
    if not needle.strip() or "%" in needle:
        sys.exit("check_binary: an identity to search for did not resolve")
    text = needle.lower()
    if text.encode("utf-8") in data or text.encode("utf-16-le") in data:
        sys.exit("check_binary: a private string or debug path is in the binary")
