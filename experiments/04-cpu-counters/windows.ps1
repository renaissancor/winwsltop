# Second paths to identity and temperature. Reads only. Non-elevated. Selected properties only:
# Win32_Processor also holds a serial number and a processor id, which this file must not print.
$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
function Try-Read($name, [scriptblock]$body) {
  "=== $name"
  try { & $body | Format-List | Out-String -Width 200 } catch { "error: $($_.Exception.GetType().Name) hresult 0x$($_.Exception.HResult.ToString('X8')): $($_.Exception.Message)" }
}
Try-Read 'Win32_OperatingSystem' { Get-CimInstance Win32_OperatingSystem | Select-Object BuildNumber, Version }
Try-Read 'Win32_Processor' { Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed, CurrentClockSpeed, L2CacheSize, L3CacheSize }
Try-Read 'root/wmi MSAcpi_ThermalZoneTemperature' { Get-CimInstance -Namespace root/wmi -ClassName MSAcpi_ThermalZoneTemperature -ErrorAction Stop | Select-Object InstanceName, CurrentTemperature, CriticalTripPoint, PassiveTripPoint }
Try-Read 'Win32_PerfFormattedData_Counters_ThermalZoneInformation' { Get-CimInstance Win32_PerfFormattedData_Counters_ThermalZoneInformation -ErrorAction Stop | Select-Object Name, Temperature, HighPrecisionTemperature, PercentPassiveLimit, ThrottleReasons }
Try-Read 'Win32_TemperatureProbe' { Get-CimInstance Win32_TemperatureProbe -ErrorAction Stop | Select-Object Name, CurrentReading, Status }
"count Win32_TemperatureProbe: $(@(Get-CimInstance Win32_TemperatureProbe -ErrorAction SilentlyContinue).Count)"
Try-Read 'root/cimv2 Win32_Fan count' { [pscustomobject]@{ Count = @(Get-CimInstance Win32_Fan -ErrorAction Stop).Count } }
