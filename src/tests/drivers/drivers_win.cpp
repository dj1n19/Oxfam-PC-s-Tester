#include "tests/drivers/DriverInfo.h"

#include <QProcess>

namespace {

// No quotation marks inside: it is passed as one command-line argument.
// @(...) keeps a JSON array even with 0 or 1 device; -ErrorAction Stop
// turns a WMI failure into a non-zero exit code.
const char* const kScript =
    "$d = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop "
    "| Where-Object { $_.ConfigManagerErrorCode -ne 0 } "
    "| Select-Object Name, DeviceID, PNPClass, ConfigManagerErrorCode); "
    "ConvertTo-Json -InputObject $d -Compress";

} // namespace

DriverInfo readDrivers()
{
    DriverInfo info;

    QProcess ps;
    ps.start(QStringLiteral("powershell.exe"),
             {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
              "-Command", QString::fromLatin1(kScript)});
    if (!ps.waitForStarted(5000)) {
        info.error = QStringLiteral("Could not start PowerShell");
        return info;
    }
    if (!ps.waitForFinished(30000)) {
        ps.kill();
        ps.waitForFinished();
        info.error = QStringLiteral("PowerShell timed out");
        return info;
    }
    if (ps.exitStatus() != QProcess::NormalExit || ps.exitCode() != 0) {
        info.error = QStringLiteral("PowerShell failed: %1")
                         .arg(QString::fromUtf8(ps.readAllStandardError()).trimmed());
        return info;
    }
    return parsePnpEntities(ps.readAllStandardOutput().trimmed());
}
