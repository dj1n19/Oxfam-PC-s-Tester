#include "tests/disk/DiskInfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

namespace {

struct ProcessOutput {
    QString error;       // empty = it ran (exit code may still be non-zero)
    int exitCode = -1;
    QByteArray out;
};

ProcessOutput run(const QString& exe, const QStringList& args)
{
    ProcessOutput o;
    QProcess p;
    p.start(exe, args);
    if (!p.waitForStarted(5000)) {
        o.error = QStringLiteral("Could not start %1").arg(exe);
        return o;
    }
    if (!p.waitForFinished(20000)) {
        p.kill();
        p.waitForFinished();
        o.error = QStringLiteral("smartctl timed out (%1)").arg(args.join(' '));
        return o;
    }
    if (p.exitStatus() != QProcess::NormalExit) {
        o.error = QStringLiteral("smartctl crashed (%1)").arg(args.join(' '));
        return o;
    }
    o.exitCode = p.exitCode();
    o.out = p.readAllStandardOutput();
    return o;
}

} // namespace

std::vector<ScannedDevice> parseScan(const QByteArray& json, QString* error)
{
    std::vector<ScannedDevice> devices;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("Unreadable smartctl --scan output: %1").arg(parseError.errorString());
        return devices;
    }
    const QJsonArray list = doc.object()["devices"].toArray();
    for (const QJsonValue& v : list) {
        const QJsonObject d = v.toObject();
        const QString name = d["name"].toString();
        if (!name.isEmpty())
            devices.push_back({name, d["type"].toString()});
    }
    return devices;
}

DiskScan readDisks(const QString& smartctlPath)
{
    DiskScan scan;
    if (smartctlPath.isEmpty()) {
        scan.error = QStringLiteral("smartctl not found (looked in tools/ next to the program, then in PATH)");
        return scan;
    }

    const ProcessOutput s = run(smartctlPath, {"--scan", "--json"});
    if (!s.error.isEmpty()) {
        scan.error = s.error;
        return scan;
    }
    scan.scanJson = s.out;
    if (s.exitCode != 0) {
        scan.error = QStringLiteral("smartctl --scan failed (exit code %1)").arg(s.exitCode);
        return scan;
    }

    QString parseError;
    const std::vector<ScannedDevice> devices = parseScan(s.out, &parseError);
    if (!parseError.isEmpty()) {
        scan.error = parseError;
        return scan;
    }

    for (const ScannedDevice& dev : devices) {
        // -d <type> as found by --scan: needed for many USB bridges.
        QStringList args{"--json", "-a"};
        if (!dev.type.isEmpty())
            args << "-d" << dev.type;
        args << dev.name;

        const ProcessOutput o = run(smartctlPath, args);
        SmartctlRun r;
        r.device = dev.name;
        r.type = dev.type;
        r.error = o.error;
        r.exitCode = o.exitCode;
        r.json = o.out;
        scan.disks.push_back(std::move(r));
    }
    return scan;
}
