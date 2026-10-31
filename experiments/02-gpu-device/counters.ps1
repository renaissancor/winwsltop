# Independent reading of the GPU memory counters: per-pid sums, adapter totals, and process names.
$set = '\GPU Process Memory(*)\Dedicated Usage','\GPU Process Memory(*)\Shared Usage','\GPU Process Memory(*)\Local Usage','\GPU Process Memory(*)\Non Local Usage'
$s = (Get-Counter $set).CounterSamples
"instances $(($s | Where-Object { $_.Path -like '*\dedicated usage' }).Count)"
"luids $((($s | ForEach-Object { if ($_.InstanceName -match 'luid_(0x[0-9a-f]+_0x[0-9a-f]+)') { $Matches[1] } }) | Sort-Object -Unique) -join ' ')"
$rows = @{}
foreach ($c in $s) {
  if ($c.InstanceName -notmatch '^pid_(\d+)_') { continue }
  $p = [uint32]$Matches[1]
  if (-not $rows.ContainsKey($p)) { $rows[$p] = @{ d = 0.0; sh = 0.0; l = 0.0; n = 0.0 } }
  $k = switch -Wildcard ($c.Path) { '*\dedicated usage' {'d'} '*\shared usage' {'sh'} '*\non local usage' {'n'} default {'l'} }
  $rows[$p][$k] += $c.CookedValue
}
foreach ($p in $rows.Keys) {
  $r = $rows[$p]
  $proc = Get-Process -Id $p -ErrorAction SilentlyContinue
  $name = if ($proc) { $proc.ProcessName } else { '?' }
  'row {0} {1} {2} {3} {4} {5}' -f $p, [uint64]$r.d, [uint64]$r.sh, [uint64]$r.l, [uint64]$r.n, $name
}
foreach ($a in (Get-Counter '\GPU Adapter Memory(*)\Dedicated Usage','\GPU Adapter Memory(*)\Shared Usage','\GPU Local Adapter Memory(*)\Local Usage').CounterSamples) {
  'adapter {0} {1:N1} MiB {2}' -f $a.InstanceName, ($a.CookedValue / 1048576), ($a.Path -replace '^.*\\','')
}
