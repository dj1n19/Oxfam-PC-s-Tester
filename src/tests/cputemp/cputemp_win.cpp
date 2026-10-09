#include "tests/cputemp/CpuTempInfo.h"

#include "tests/common/PowerShell.h"

namespace {

// The real CPU sensor needs a kernel driver on Windows: we read the ACPI
// thermal zone instead (admin, which the manifest gives us). On many PCs
// the class is "Not supported": catch that and report an empty list, so
// it becomes Skipped and not Error. -Depth: the zones sit 2 levels deep.
const char* const kScript =
    "$z = @(); $e = ''; "
    "try { $z = @(Get-CimInstance -Namespace root/wmi -ClassName MSAcpi_ThermalZoneTemperature "
    "| Select-Object InstanceName, CurrentTemperature) } "
    "catch { $e = $_.Exception.Message }; "
    "ConvertTo-Json -InputObject @{ zones = $z; unavailable = $e } -Depth 4 -Compress";

} // namespace

CpuTempReading readCpuTemperature()
{
    // Shorter timeout than the default: this runs every few seconds while
    // the CPU is busy, and the whole test must end before the 60 s watchdog.
    const PowerShellResult ps = runPowerShell(QString::fromLatin1(kScript), 10000);
    if (!ps.error.isEmpty()) {
        CpuTempReading r;
        r.error = ps.error;
        return r;
    }
    return parseThermalZones(ps.out);
}
