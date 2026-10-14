#include "tests/cputemp/CpuTempInfo.h"

#include "tests/common/PowerShell.h"

namespace {

// Temperature: the real CPU sensor needs a kernel driver on Windows, so we
// read the ACPI thermal zone instead (admin, which the manifest gives us). On
// many PCs (Latitude 7420) the class is "Not supported": catch that and report
// an empty list, so it becomes "no sensor" and not Error.
// Speed: the performance counter class, not Get-Counter: Get-Counter wants
// the counter names in the language of Windows (French, Dutch...), the CIM
// class names are always English. Its own try: a missing counter must not
// lose the temperature.
// Battery: BatteryStatus 1 = discharging (on battery), seen on a real Latitude.
// -Depth: the zones sit 2 levels deep.
const char* const kScript =
    "$z = @(); $e = ''; "
    "try { $z = @(Get-CimInstance -Namespace root/wmi -ClassName MSAcpi_ThermalZoneTemperature "
    "| Select-Object InstanceName, CurrentTemperature) } "
    "catch { $e = $_.Exception.Message }; "
    "$p = $null; $pe = ''; "
    "try { $p = Get-CimInstance Win32_PerfFormattedData_Counters_ProcessorInformation -Filter \"Name='_Total'\" "
    "| Select-Object PercentProcessorPerformance, PercentProcessorUtility } "
    "catch { $pe = $_.Exception.Message }; "
    "$b = @(Get-CimInstance Win32_Battery | Select-Object BatteryStatus); "
    "ConvertTo-Json -InputObject @{ zones = $z; unavailable = $e; cpu = $p; cpuError = $pe; battery = $b } "
    "-Depth 4 -Compress";

} // namespace

CpuReading readCpu()
{
    // Shorter timeout than the default: this runs every few seconds while
    // the CPU is busy, and the whole test must end before the 60 s watchdog.
    const PowerShellResult ps = runPowerShell(QString::fromLatin1(kScript), 10000);
    if (!ps.error.isEmpty()) {
        CpuReading r;
        r.error = ps.error;
        return r;
    }
    return parseWindowsCpu(ps.out);
}
