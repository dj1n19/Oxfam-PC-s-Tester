#include "tests/battery/BatteryInfo.h"

#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

// WMI's BatteryStaticData (design capacity) fails with "Generic Failure" on
// real laptops (ThinkPad 13, Latitude 7420), even as administrator.
// powercfg's battery report has the same data and works as a normal user.
BatteryInfo readBattery()
{
    BatteryInfo info;

    // powercfg can only write to a file. QTemporaryDir deletes it on return,
    // so nothing is left on the PC.
    QTemporaryDir dir;
    if (!dir.isValid()) {
        info.error = QStringLiteral("Cannot create a temporary folder: %1").arg(dir.errorString());
        return info;
    }
    const QString path = dir.filePath(QStringLiteral("battery.xml"));

    QProcess p;
    p.start(QStringLiteral("powercfg.exe"),
            {"/batteryreport", "/xml", "/output", path});
    if (!p.waitForStarted(5000)) {
        info.error = QStringLiteral("Could not start powercfg");
        return info;
    }
    if (!p.waitForFinished(30000)) {
        p.kill();
        p.waitForFinished();
        info.error = QStringLiteral("powercfg timed out");
        return info;
    }
    // powercfg messages are localised: only the exit code is trusted.
    const QString output = QString::fromLocal8Bit(p.readAllStandardOutput() + p.readAllStandardError()).trimmed();
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        info.error = QStringLiteral("powercfg failed (exit code %1): %2").arg(p.exitCode()).arg(output);
        return info;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        info.error = QStringLiteral("powercfg wrote no report: %1\n%2").arg(file.errorString(), output);
        return info;
    }
    return parseBatteryReport(file.readAll());
}
