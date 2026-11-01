#!/usr/bin/env bash
# Builds the publishable winwsltop.exe into dist/ and refuses it unless it is safe to hand to a stranger.
# Publishes nothing: docs/releases/ holds the notes, and a person runs gh release create.
set -euo pipefail
version=${1:?usage: scripts/release.sh vX.Y.Z}
[[ $version =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "version must look like v0.1.0" >&2; exit 2; }
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
[ -f "docs/releases/$version.md" ] || { echo "docs/releases/$version.md is missing" >&2; exit 1; }
[ -z "$(git status --porcelain)" ] || { echo "the tree is not clean" >&2; exit 1; }
scripts/check.sh --windows

win_exe=$(scripts/build.sh release | tail -1)
exe=$(wslpath -u "$win_exe")
win_root=${win_exe%\\build\\release\\winwsltop.exe}
fail() { echo "REFUSED: $1" >&2; exit 1; }

# Only DLLs every supported Windows ships. NVML is loaded at run time and is optional.
deps=$(cd /mnt/c && cmd.exe /c "cd /d $win_root && scripts\\msvc.cmd dependents $win_exe" | tr -d '\r' | grep -i -E '^ +[a-z0-9_.-]+\.dll$' | tr -d ' ' | tr 'A-Z' 'a-z' | sort -u)
[ -n "$deps" ] || fail "no import list from dumpbin"
while read -r dll; do
  case "$dll" in
    kernel32.dll|ntdll.dll|pdh.dll|iphlpapi.dll|user32.dll|advapi32.dll|shell32.dll|psapi.dll|ws2_32.dll|ole32.dll|oleaut32.dll) ;;
    *) fail "depends on $dll, which may not be on a stranger's machine" ;;
  esac
done <<< "$deps"

# Nothing about the machine that built it. Names come from this machine, so none are written here.
win_user=$(cd /mnt/c && cmd.exe /c 'echo %USERNAME%' | tr -d '\r')
win_host=$(cd /mnt/c && cmd.exe /c 'echo %COMPUTERNAME%' | tr -d '\r')
for needle in "$win_user" "$win_host" "$(whoami)" "$(hostname)" 'Users\' '.pdb' 'AppData'; do
  [ -n "$needle" ] || fail "an identity to search for came back empty"
  if LC_ALL=C grep -a -i -q -F -- "$needle" "$exe"; then fail "the binary contains a private string or debug path"; fi
done

# It runs alone: from an empty folder, unelevated, with no repo and no build tools on PATH.
tmp_win=$(cd /mnt/c && cmd.exe /c 'echo %TEMP%' | tr -d '\r')"\\winwsltop-release-$$"
tmp=$(wslpath -u "$tmp_win")
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT
cp "$exe" "$tmp/winwsltop.exe"
json=$(cd "$tmp" && env -i PATH=/mnt/c/Windows/System32 ./winwsltop.exe --once | tr -d '\r')
printf '%s' "$json" | python3 -c '
import json, sys
s = json.loads(sys.stdin.read().strip().splitlines()[-1])
print("ran from an empty folder, snapshot keys:", len(s))
' || fail "--once from an empty folder did not print a JSON snapshot"

mkdir -p dist
cp "$exe" dist/winwsltop.exe
(cd dist && sha256sum winwsltop.exe > winwsltop.exe.sha256)
echo "ready: dist/winwsltop.exe, $(wc -c < dist/winwsltop.exe) bytes, $(cut -c1-12 dist/winwsltop.exe.sha256), imports: $(echo $deps)"
