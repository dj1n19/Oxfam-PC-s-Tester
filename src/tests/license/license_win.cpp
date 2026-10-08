#include "tests/license/LicenseInfo.h"
#include "tests/common/PowerShell.h"

namespace {

// 55c92734-... is the ApplicationID of Windows itself (not Office). The WQL
// filter keeps the query fast: listing every SoftwareLicensingProduct takes
// many seconds. The OEM key is turned into a boolean inside PowerShell, so
// it never reaches this program.
const char* const kScript = R"ps(
$p = Get-CimInstance SoftwareLicensingProduct -Filter "ApplicationID='55c92734-d682-4d71-983e-d6ec3f16059f' AND PartialProductKey IS NOT NULL" | Select-Object -First 1
$s = Get-CimInstance SoftwareLicensingService
[pscustomobject]@{
    found             = [bool]$p
    name              = $p.Name
    description       = $p.Description
    status            = $p.LicenseStatus
    oemKeyPresent     = -not [string]::IsNullOrEmpty($s.OA3xOriginalProductKey)
    oemKeyDescription = $s.OA3xOriginalProductKeyDescription
} | ConvertTo-Json -Compress
)ps";

} // namespace

LicenseInfo readLicense()
{
    const PowerShellResult ps = runPowerShell(QString::fromLatin1(kScript), 45000);
    if (!ps.error.isEmpty()) {
        LicenseInfo info;
        info.error = ps.error;
        return info;
    }
    return parseLicenseJson(ps.out);
}
