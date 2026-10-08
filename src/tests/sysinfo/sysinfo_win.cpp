#include "tests/sysinfo/SystemInfo.h"
#include "tests/common/PowerShell.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

// ChassisTypes is an array; the first entry is the one that matters.
const char* const kScript =
    "$cs = Get-CimInstance Win32_ComputerSystem; "
    "$b = Get-CimInstance Win32_BIOS; "
    "$p = Get-CimInstance Win32_Processor | Select-Object -First 1; "
    "$e = Get-CimInstance Win32_SystemEnclosure | Select-Object -First 1; "
    "[pscustomobject]@{vendor=$cs.Manufacturer; model=$cs.Model; serial=$b.SerialNumber; "
    "cpu=$p.Name; ram=$cs.TotalPhysicalMemory; chassis=@($e.ChassisTypes)[0]} "
    "| ConvertTo-Json -Compress";

} // namespace

SystemInfo readSystemInfo()
{
    SystemInfo info;

    const PowerShellResult ps = runPowerShell(QString::fromLatin1(kScript), 20000);
    if (!ps.error.isEmpty()) {
        info.error = ps.error;
        return info;
    }

    const QByteArray& out = ps.out;
    info.raw = QString::fromUtf8(out);

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(out, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        info.error = QStringLiteral("Unexpected PowerShell output: %1").arg(parseError.errorString());
        return info;
    }

    const QJsonObject o = doc.object();
    info.vendor = o["vendor"].toString().trimmed();
    info.model  = o["model"].toString().trimmed();
    info.serial = o["serial"].toString().trimmed();
    if (info.serial.isEmpty())
        info.serialError = QStringLiteral("Win32_BIOS.SerialNumber is empty");
    info.cpu = o["cpu"].toString().trimmed();
    info.ramBytes = o["ram"].toDouble(0);
    info.chassisType = o["chassis"].toInt(2);
    return info;
}
