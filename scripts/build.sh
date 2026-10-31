#!/usr/bin/env bash
# Mirrors this tree to the Windows drive and compiles one source with MSVC there.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
src=${1:-all}   # "all" builds winwsltop.exe; otherwise one source, relative to the repo
cd /mnt/c
local_app=$(cmd.exe /c 'echo %LOCALAPPDATA%' | tr -d '\r')
win_root="$local_app\\winwsltop\\$(basename "$root")"   # one mirror per worktree
mirror=$(wslpath -u "$win_root")
mkdir -p "$mirror"
if command -v rsync >/dev/null; then
  rsync -a --delete --exclude .git --exclude build "$root/" "$mirror/"
else
  find "$mirror" -mindepth 1 -delete
  (cd "$root" && tar --exclude=.git --exclude=build -cf - .) | tar -xf - -C "$mirror"
fi
if [ "$src" = all ]; then win_src=all; exe=winwsltop; else win_src="$win_root\\${src//\//\\}"; exe=$(basename "${src%.*}"); fi
out="$win_root\\build"
cd "$mirror"
# A running .exe cannot be overwritten but can be renamed, so an open winwsltop survives a rebuild.
rm -f build/*.old-*.exe 2>/dev/null || true
target=$([ "$src" = all ] && echo winwsltop || basename "${src%.*}")
[ -e "build/$target.exe" ] && mv -f "build/$target.exe" "build/$target.old-$$.exe" 2>/dev/null || true
cmd.exe /c "cd /d $win_root && scripts\\msvc.cmd $win_src $out" | tr -d '\r'
status=${PIPESTATUS[0]}
[ "$status" -eq 0 ] || exit "$status"
echo "$out\\$exe.exe"
