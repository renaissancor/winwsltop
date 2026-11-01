#!/usr/bin/env bash
# Repository checks. Without arguments, the Linux-side checks only; with --windows, also builds
# winwsltop.exe and render_check.exe through scripts/build.sh and runs the render checks.
# Each rule here is one that a document states; the document names this script as its check.
set -uo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
failed=0
fail() { echo "FAIL $*"; failed=1; }
pass() { echo "ok   $*"; }

# 1. The embedded collector is the committed collector.py, byte for byte (contracts/collect.md rule 5).
python3 - <<'PY' && pass "embedded collector matches wsl-collector/collector.py" || fail "src/wsl/collector_embed.cpp is stale: run python3 wsl-collector/embed.py"
import re, sys
src = open("wsl-collector/collector.py", "rb").read()
cpp = open("src/wsl/collector_embed.cpp").read()
body = cpp.split("[] = {", 1)[1].split("};", 1)[0]
emb = bytes(int(x) for x in re.findall(r"\d+", body))
sys.exit(0 if emb == src else 1)
PY

# 2. Every tracked file is owned by exactly one area (ARCHITECTURE.md, Areas table).
python3 - <<'PY' && pass "every tracked file has exactly one owner" || fail "ownership gaps or overlaps, see above"
import re, subprocess, sys
text = open("docs/ARCHITECTURE.md").read()
table = text.split("## Areas", 1)[1].split("## ", 1)[0]
owners = {}
for line in table.splitlines():
    cells = [c.strip() for c in line.strip().strip("|").split("|")]
    if len(cells) < 2 or cells[0] in ("area", "") or cells[0].startswith("---"):
        continue
    paths = re.findall(r"`([^`]+)`", cells[1])
    if len(set(paths)) != len(paths):
        print("    duplicate path in row", cells[0]); sys.exit(1)
    owners[cells[0]] = paths
files = subprocess.check_output(["git", "ls-files"], text=True).split()
bad = 0
for f in files:
    hits = [a for a, paths in owners.items() if any(f == p or (p.endswith("/") and f.startswith(p)) for p in paths)]
    if len(hits) != 1:
        bad += 1
        print("   ", f, "owned by", hits or "nobody")
sys.exit(1 if bad else 0)
PY

# 3. Every tracked .md is listed in docs/README.md (the index).
python3 - <<'PY' && pass "every .md file is in docs/README.md" || fail "unindexed documents, see above"
import os, re, subprocess, sys
idx = open("docs/README.md").read()
linked = {os.path.normpath(os.path.join("docs", p.split("#")[0])) for p in re.findall(r"\]\(([^)]+)\)", idx)}
bad = 0
for f in subprocess.check_output(["git", "ls-files", "*.md"], text=True).split():
    if f != "docs/README.md" and f not in linked:
        bad += 1
        print("   ", f)
sys.exit(1 if bad else 0)
PY

# 4. Every relative link in every .md resolves to a tracked file.
python3 - <<'PY' && pass "all relative links resolve" || fail "broken links, see above"
import os, re, subprocess, sys
files = set(subprocess.check_output(["git", "ls-files"], text=True).split())
bad = 0
for f in subprocess.check_output(["git", "ls-files", "*.md"], text=True).split():
    for target in re.findall(r"\]\(([^)]+)\)", open(f).read()):
        if "://" in target or target.startswith("#"):
            continue
        t = os.path.normpath(os.path.join(os.path.dirname(f), target.split("#")[0]))
        if t not in files and not any(x.startswith(t.rstrip("/") + "/") for x in files):
            bad += 1
            print("   ", f, "->", target)
sys.exit(1 if bad else 0)
PY

# 5. Every results file the ledger names exists (contracts/evidence.md rule 1).
python3 - <<'PY' && pass "every results file cited in docs/measurable.md exists" || fail "missing results files, see above"
import glob, os, re, sys
text = open("docs/measurable.md").read()
have = {os.path.basename(p) for p in glob.glob("experiments/*/results/*")}
bad = 0
for name in set(re.findall(r"`([\w.-]+-\d{4}-\d{2}-\d{2}\.txt)`", text)):
    if name not in have:
        bad += 1
        print("   ", name)
for path in set(re.findall(r"`(experiments/[^`]+\.txt)`", text)):
    if not os.path.exists(path):
        bad += 1
        print("   ", path)
sys.exit(1 if bad else 0)
PY

# 6. Every role file names a contract that exists, and every area in ARCHITECTURE.md has a contract.
python3 - <<'PY' && pass "roles and contracts line up" || fail "role or contract mismatch, see above"
import glob, os, re, sys
bad = 0
areas = re.findall(r"^\| (\w+) \| `", open("docs/ARCHITECTURE.md").read().split("## Areas")[1].split("## ")[0], re.M)
for a in areas:
    if not os.path.exists(f"docs/contracts/{a}.md"):
        bad += 1; print("    no contract for area", a)
    if not os.path.exists(f".claude/agents/{a}.md"):
        bad += 1; print("    no role file for area", a)
for r in glob.glob(".claude/agents/*.md"):
    t = open(r).read()
    if not t.startswith("---\nname: "):
        bad += 1; print("    no frontmatter in", r)
    for c in re.findall(r"docs/contracts/(\w+)\.md", t):
        if not os.path.exists(f"docs/contracts/{c}.md"):
            bad += 1; print("   ", r, "names missing contract", c)
sys.exit(1 if bad else 0)
PY

# 7. CRLF where Windows needs it, LF everywhere else (.gitattributes).
if grep -rIl $'\r' --exclude-dir=.git --exclude=*.cmd --exclude=*.ps1 . >/dev/null; then
  fail "CRLF in a non-Windows file: $(grep -rIl $'\r' --exclude-dir=.git --exclude=*.cmd --exclude=*.ps1 . | tr '\n' ' ')"
else
  pass "line endings follow .gitattributes"
fi

if [ "${1:-}" = "--windows" ]; then
  # 8. The program and the render checks build with /W4 /WX, and the render checks pass (contracts/ui.md).
  exe=$(scripts/build.sh 2>&1 | tail -1) && [ -n "$exe" ] && pass "winwsltop.exe built: $exe" || fail "winwsltop.exe did not build"
  chk=$(scripts/build.sh src/ui/render_check.cpp 2>&1 | tail -1) || fail "render_check.exe did not build"
  if [ -n "${chk:-}" ]; then
    out=$("$(wslpath -u "$chk")" 2>&1 | tr -d '\r' | tail -1)
    case "$out" in *", 0 failed") pass "render_check: $out" ;; *) fail "render_check: $out" ;; esac
  fi

  # 9. What scripts and agents rely on: exit codes, one JSON line, and every key documented.
  if [ -n "${exe:-}" ]; then
    w=$(wslpath -u "$exe")
    "$w" --help | grep -q 'usage: winwsltop' && pass "--help prints usage, exit 0" || fail "--help"
    "$w" --version | grep -q '^winwsltop ' && pass "--version" || fail "--version"
    code=0; "$w" --record abc >/dev/null 2>&1 || code=$?
    [ "$code" -eq 2 ] && pass "a bad --record is exit 2" || fail "a bad --record exited $code"
    if "$w" --once | tr -d '\r' | python3 scripts/check_json.py docs/json-output.md; then
      pass "--once is one JSON line, every key documented"
    else
      fail "--once output, or a key missing from docs/json-output.md"
    fi
  fi
fi

[ "$failed" -eq 0 ] && echo "all checks passed" || { echo "some checks failed"; exit 1; }
