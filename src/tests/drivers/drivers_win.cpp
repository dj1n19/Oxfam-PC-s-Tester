#include "tests/drivers/DriverInfo.h"

#include "tests/common/PowerShell.h"

namespace {

// @(...) keeps a JSON array even with 0 or 1 device.
const char* const kScript =
    "$d = @(Get-CimInstance Win32_PnPEntity "
    "| Where-Object { $_.ConfigManagerErrorCode -ne 0 } "
    "| Select-Object Name, DeviceID, PNPClass, ConfigManagerErrorCode); "
    "ConvertTo-Json -InputObject $d -Compress";

} // namespace

DriverInfo readDrivers()
{
    const PowerShellResult ps = runPowerShell(QString::fromLatin1(kScript));
    if (!ps.error.isEmpty()) {
        DriverInfo info;
        info.error = ps.error;
        return info;
    }
    return parsePnpEntities(ps.out);
}
