# Two readings beside the collector, once a second, passively. Get-Counter reads the same
# \PhysicalDisk provider. WMI's MSDiskDriver_Performance is a separate path to cumulative disk totals.
param([int]$Samples = 75)
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$set = '\PhysicalDisk(*)\Disk Read Bytes/sec', '\PhysicalDisk(*)\Disk Write Bytes/sec', '\PhysicalDisk(*)\% Idle Time'
Get-Counter -Counter $set -SampleInterval 1 -MaxSamples $Samples | ForEach-Object {
  $ts = $_.Timestamp.ToFileTimeUtc()
  foreach ($c in $_.CounterSamples) {
    $leaf = $c.Path -replace '^.*\\', ''
    "gc`t$ts`t$($c.InstanceName)`t$leaf`t$($c.RawValue)`t$($c.SecondValue)`t$($c.CookedValue.ToString('R'))"
  }
  foreach ($w in Get-CimInstance -Namespace root\wmi -ClassName MSDiskDriver_Performance) {
    $p = $w.PerfData
    "wmi`t$($p.QueryTime)`t$($w.InstanceName)`t$($p.StorageManagerName)`t$($p.StorageDeviceNumber)`t$($p.BytesRead)`t$($p.BytesWritten)`t$($p.IdleTime)"
  }
}
