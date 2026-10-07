#include "tests/battery/BatteryInfo.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace {

// No quotation marks inside: it is passed as one command-line argument.
// Capacities are in mWh.
const char* const kScript =
    "$d = Get-CimInstance -Namespace root/wmi -ClassName BatteryStaticData "
    "-ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty DesignedCapacity; "
    "$f = Get-CimInstance -Namespace root/wmi -ClassName BatteryFullChargedCapacity "
    "-ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullChargedCapacity; "
    "$c = Get-CimInstance -Namespace root/wmi -ClassName BatteryCycleCount "
    "-ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty CycleCount; "
    "[pscustomobject]@{design=$d; full=$f; cycles=$c} | ConvertTo-Json -Compress";

} // namespace

BatteryInfo readBattery()
{
    BatteryInfo info;

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

    const QJsonObject o = QJsonDocument::fromJson(out).object();
    const bool hasDesign = o["design"].isDouble();
    const bool hasFull = o["full"].isDouble();

    if (!hasDesign && !hasFull)
        return info;                      // no battery: present == false

    info.present = true;
    info.name = QStringLiteral("Battery");
    if (!hasDesign || !hasFull) {
        info.error = QStringLiteral("Incomplete WMI data");
        return info;
    }
    info.designCapacity = o["design"].toDouble();
    info.fullCapacity = o["full"].toDouble();
    const int cycles = o["cycles"].toInt(0);
    info.cycles = cycles > 0 ? cycles : -1;   // 0 usually means "not reported"
    return info;
}
