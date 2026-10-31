# What Windows calls each disk and adapter, for the row-by-row join with host_probe.exe rows.
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
"windows`t$((Get-CimInstance Win32_OperatingSystem).BuildNumber)"
$p = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
"elevated`t$($p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))"
foreach ($d in Get-Disk) { "disk`t$($d.Number)`t$($d.FriendlyName)`t$($d.BusType)`t$($d.Size)`t$($d.IsBoot)`t$($d.IsSystem)" }
foreach ($d in Get-PhysicalDisk) { "physicaldisk`t$($d.DeviceId)`t$($d.FriendlyName)`t$($d.MediaType)`t$($d.BusType)" }
foreach ($d in Get-Partition) { if ($d.DriveLetter) { "partition`t$($d.DiskNumber)`t$($d.DriveLetter)" } }
foreach ($a in Get-NetAdapter -IncludeHidden) {
  "adapter`t$($a.ifIndex)`t$($a.Name)`t$($a.InterfaceDescription)`t$($a.Status)`t$($a.Speed)`t$($a.ReceiveLinkSpeed)`t$($a.TransmitLinkSpeed)`t$($a.HardwareInterface)`t$($a.Virtual)`t$($a.Hidden)"
}
