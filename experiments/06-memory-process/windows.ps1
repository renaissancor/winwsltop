# Rounds of Get-Counter and Get-Process around one mem_probe.exe run each. Reads only, with one
# exception: each round starts one ping.exe to 127.0.0.1, which does no work, and waits for it to end,
# so that pids are freed and may be reused. A long ping.exe lives across all rounds for the create-time
# check. Both are this script's own children; nothing else is started, stopped or signalled.
# Names outside $keep print as "other" (the repo is public). Tab-separated, one record per line.
# -AllMemory adds the \Memory byte counts that run 1 did not read, to find what SystemCache holds.
param([string]$Probe, [int]$Rounds = 30, [switch]$AllMemory)
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$keep = 'Idle', 'System', 'Registry', 'smss', 'csrss', 'wininit', 'services', 'lsass', 'svchost', 'winlogon', 'dwm',
        'explorer', 'fontdrvhost', 'conhost', 'vmmemWSL', 'vmwp', 'vmcompute', 'wslservice', 'wslhost', 'wslrelay', 'wsl',
        'ping', 'powershell', 'mem_probe', 'winwsltop'
$set = '\Memory\Available Bytes', '\Memory\Committed Bytes', '\Memory\Commit Limit', '\Memory\Cache Bytes',
       '\Memory\System Cache Resident Bytes', '\Memory\Standby Cache Reserve Bytes',
       '\Memory\Standby Cache Normal Priority Bytes', '\Memory\Standby Cache Core Bytes',
       '\Memory\Modified Page List Bytes', '\Memory\Free & Zero Page List Bytes'
if ($AllMemory) {
  $set += '\Memory\Pool Paged Resident Bytes', '\Memory\Pool Paged Bytes', '\Memory\Pool Nonpaged Bytes',
          '\Memory\System Code Resident Bytes', '\Memory\System Driver Resident Bytes'
}
function now { [DateTime]::UtcNow.ToFileTimeUtc() }
function counters($tag) {
  "t`t$tag`_start`t$(now)"
  foreach ($c in (Get-Counter -Counter $set).CounterSamples) {
    "gc`t$tag`t$($c.Path -replace '^.*\\', '')`t$($c.RawValue)"
  }
  "t`t$tag`_end`t$(now)"
}
function processes($tag) {
  "t`t$tag`_start`t$(now)"
  foreach ($p in Get-Process) {
    $n = if ($keep -contains $p.ProcessName) { $p.ProcessName } else { 'other' }
    $st = try { $p.StartTime.ToFileTimeUtc() } catch { -1 }
    "gp`t$tag`t$($p.Id)`t$n`t$st`t$($p.WorkingSet64)`t$($p.PrivateMemorySize64)"
  }
  "t`t$tag`_end`t$(now)"
}
$long = Start-Process -FilePath ping.exe -ArgumentList '-n', '600', '127.0.0.1' -WindowStyle Hidden -PassThru
"long_ping`t$($long.Id)`t$($long.StartTime.ToFileTimeUtc())"
for ($r = 1; $r -le $Rounds; $r++) {
  "== round $r"
  $s = Start-Process -FilePath ping.exe -ArgumentList '-n', '1', '127.0.0.1' -WindowStyle Hidden -PassThru
  "short_ping`t$($s.Id)`t$($s.StartTime.ToFileTimeUtc())"
  $s.WaitForExit()
  counters 'c1'
  processes 'p1'
  & $Probe once
  processes 'p2'
  counters 'c2'
}
# The long ping is this script's own child; it is ended here rather than left to run out.
"long_ping_end`t$($long.Id)`t$($long.StartTime.ToFileTimeUtc())`t$($long.HasExited)"
Stop-Process -Id $long.Id
