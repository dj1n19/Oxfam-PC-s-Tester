#include "tests/disk/DiskInfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

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

    scan.disks = readDevices(smartctlPath, devices);   // OS-specific: may elevate
    return scan;
}

SmartctlRun runSmartctl(const QString& smartctlPath, const ScannedDevice& dev)
{
    const ProcessOutput o = run(smartctlPath, smartctlArgs(dev));
    SmartctlRun r;
    r.device = dev.name;
    r.type = dev.type;
    r.error = o.error;
    r.exitCode = o.exitCode;
    r.json = o.out;
    return r;
}

QStringList smartctlArgs(const ScannedDevice& dev)
{
    // -d <type> as found by --scan: needed for many USB bridges.
    QStringList args{"--json", "-a"};
    if (!dev.type.isEmpty())
        args << "-d" << dev.type;
    args << dev.name;
    return args;
}

bool isSafeShellWord(const QString& s)
{
    // Device names and types from --scan look like "/dev/nvme0", "sat",
    // "sntasmedia/sat". Anything else is refused rather than escaped: these
    // words end up in a command run as root.
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9/_.,+-]+$"));
    return re.match(s).hasMatch();
}

namespace {
QString shellQuote(const QString& s)
{
    QString q = s;
    q.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}
} // namespace

QString buildBatchScript(const QString& smartctlPath, const std::vector<ScannedDevice>& devices)
{
    // One line per disk; the marker line tells where each output ends and
    // carries smartctl's exit code (a bit field, so it must be kept).
    // Every device must be safe: parseBatchOutput() matches outputs to devices
    // by position, so silently skipping one would shift all the others.
    QStringList lines;
    for (const ScannedDevice& dev : devices) {
        if (!isSafeShellWord(dev.name) || (!dev.type.isEmpty() && !isSafeShellWord(dev.type)))
            return {};
        QStringList words{shellQuote(smartctlPath)};
        for (const QString& a : smartctlArgs(dev))
            words << shellQuote(a);
        lines << words.join(' ') + QStringLiteral("; echo \"%1$?\"").arg(QLatin1String(kBatchMarker));
    }
    return lines.join('\n');
}

std::vector<SmartctlRun> parseBatchOutput(const QByteArray& out, const std::vector<ScannedDevice>& devices)
{
    std::vector<SmartctlRun> runs;
    QByteArray current;
    size_t next = 0;
    for (const QByteArray& line : out.split('\n')) {
        if (line.startsWith(kBatchMarker)) {
            if (next >= devices.size())
                break;
            SmartctlRun r;
            r.device = devices[next].name;
            r.type = devices[next].type;
            bool ok = false;
            r.exitCode = line.mid(int(qstrlen(kBatchMarker))).trimmed().toInt(&ok);
            if (!ok)
                r.error = QStringLiteral("Unreadable exit code line: %1").arg(QString::fromUtf8(line));
            r.json = current;
            runs.push_back(std::move(r));
            current.clear();
            ++next;
        } else {
            current += line + '\n';
        }
    }
    // Devices without a marker: the batch stopped early.
    for (; next < devices.size(); ++next) {
        SmartctlRun r;
        r.device = devices[next].name;
        r.type = devices[next].type;
        r.error = QStringLiteral("No output for this disk from the elevated smartctl batch");
        runs.push_back(std::move(r));
    }
    return runs;
}
