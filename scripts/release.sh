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
rm -rf dist  # A refused run must not leave an older binary looking valid.
scripts/check.sh --windows

win_exe=$(scripts/build.sh release | tail -1)
exe=$(wslpath -u "$win_exe")
win_root=${win_exe%\\build\\release\\winwsltop.exe}
fail() { echo "REFUSED: $1" >&2; exit 1; }

[ "$("$exe" --version | tr -d '\r')" = "winwsltop ${version#v}" ] || fail "--version does not say ${version#v}: change kVersion in src/main.cpp"

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
python3 scripts/check_binary.py "$exe" \
  "$(cd /mnt/c && cmd.exe /c 'echo %USERNAME%' | tr -d '\r')" \
  "$(cd /mnt/c && cmd.exe /c 'echo %COMPUTERNAME%' | tr -d '\r')" \
  "$(whoami)" "$(hostname)" 'Users\' 'AppData' '.pdb' '/home/' \
  || fail "the binary failed the private string check"

# It runs from a folder salted like a Downloads folder: every file here is something Windows or Python
# would pick up if the search order allowed it, and any one of them taking effect fails this step.
# A fake wsl.exe or json.py yields no WSL data, a fake DLL stops the program loading, and the fake
# collector announces itself. WSL must answer through the embedded collector.
tmp_win=$(cd /mnt/c && cmd.exe /c 'echo %TEMP%' | tr -d '\r')"\\winwsltop-release-$$"
tmp=$(wslpath -u "$tmp_win")
[ -n "$tmp" ] || fail "no temporary folder"
mkdir -p "$tmp/bin" "$tmp/wsl-collector"
trap 'rm -rf "$tmp"' EXIT
cp "$exe" "$tmp/bin/winwsltop.exe"
cp /mnt/c/Windows/System32/whoami.exe "$tmp/bin/wsl.exe"
printf 'raise SystemExit(1)\n' > "$tmp/bin/json.py"
printf 'not a dll' > "$tmp/bin/pdh.dll"
printf 'not a dll' > "$tmp/bin/IPHLPAPI.DLL"
printf '<?xml version="1.0"?><assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"><file name="pdh.dll"/></assembly>' > "$tmp/bin/winwsltop.exe.manifest"
printf 'import json\nprint(json.dumps({"schema": 1, "distro": "PLANTED"}), flush=True)\n' > "$tmp/wsl-collector/collector.py"
# Eight samples, not one: a collector that dies after its first line must show up as stale data.
json=$(cd "$tmp/bin" && ./winwsltop.exe --record 8 | tr -d '\r') || fail "the binary did not run beside planted files"
case "$json" in *PLANTED*) fail "the on-disk collector ran" ;; esac
printf '%s\n' "$json" | tail -1 | python3 scripts/check_json.py docs/json-output.md --full \
  || fail "the last sample from a salted folder is not a full snapshot with live data"

mkdir -p dist
cp "$exe" dist/winwsltop.exe
(cd dist && sha256sum winwsltop.exe > winwsltop.exe.sha256)
echo "ready: dist/winwsltop.exe, $(wc -c < dist/winwsltop.exe) bytes, $(cut -c1-12 dist/winwsltop.exe.sha256), imports: $(echo $deps)"
