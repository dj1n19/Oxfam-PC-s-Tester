#include "tests/disk/DiskInfo.h"

#include "tests/common/PowerShell.h"

// The program already runs as administrator (manifest): call smartctl directly.
std::vector<SmartctlRun> readDevices(const QString& smartctlPath, const std::vector<ScannedDevice>& devices)
{
    std::vector<SmartctlRun> runs;
    for (const ScannedDevice& dev : devices)
        runs.push_back(runSmartctl(smartctlPath, dev));
    return runs;
}

// smartctl's /dev/sdX on Windows is \\.\PhysicalDriveN, which Get-Disk calls
// disk number N. BusType as text ("USB", "NVMe", "SATA"...), not its number.
QStringList usbDevices(const std::vector<ScannedDevice>& /*devices*/, QString* raw)
{
    const PowerShellResult ps = runPowerShell(QStringLiteral(
        "$d = @(Get-Disk | Select-Object Number, @{n='BusType'; e={[string]$_.BusType}}); "
        "ConvertTo-Json -InputObject $d -Compress"), 20000);
    if (!ps.error.isEmpty()) {
        *raw = QStringLiteral("Get-Disk failed, every disk checked: %1").arg(ps.error);
        return {};
    }
    *raw = QStringLiteral("Get-Disk: %1").arg(QString::fromUtf8(ps.out));
    QString error;
    const QStringList usb = parseUsbDisks(ps.out, &error);
    if (!error.isEmpty())
        *raw += QStringLiteral("\n%1, every disk checked").arg(error);
    return usb;
}
