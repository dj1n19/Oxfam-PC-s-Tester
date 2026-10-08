#include "tests/sysinfo/SystemInfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace {

// No quotation marks inside: it is passed as one command-line argument.
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

    QProcess ps;
    ps.start(QStringLiteral("powershell.exe"),
             {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
              "-Command", QString::fromLatin1(kScript)});
    if (!ps.waitForStarted(5000)) {
        info.error = QStringLiteral("Could not start PowerShell");
        return info;
    }
    if (!ps.waitForFinished(20000)) {
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

    const QByteArray out = ps.readAllStandardOutput().trimmed();
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
