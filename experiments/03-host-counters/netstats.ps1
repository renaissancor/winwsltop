# Get-NetAdapterStatistics, stamped on both sides of the call, for the bracket in compare.sh.
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$t0 = [DateTime]::UtcNow.ToFileTimeUtc()
$s = Get-NetAdapterStatistics -IncludeHidden -ErrorAction SilentlyContinue
$t1 = [DateTime]::UtcNow.ToFileTimeUtc()
foreach ($a in $s) { "ns`t$t0`t$t1`t$($a.Name)`t$($a.InterfaceDescription)`t$($a.ReceivedBytes)`t$($a.SentBytes)" }
