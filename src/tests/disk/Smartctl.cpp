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

int physicalDriveNumber(const QString& smartctlName)
{
    // Beyond 26 disks smartctl uses two letters: never on a shop PC.
    static const QRegularExpression re(QStringLiteral("^/dev/sd([a-z])$"));
    const QRegularExpressionMatch m = re.match(smartctlName);
    return m.hasMatch() ? m.captured(1).at(0).unicode() - u'a' : -1;
}

DiskScan readDisks(const QString& smartctlPath)
{
    DiskScan scan;
    if (smartctlPath.isEmpty()) {
        scan.error = QStringLiteral("smartctl not found (looked in tools/ next to the program, then in PATH)");
        scan.failure = SmartctlFailure::NotFound;
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

    const QStringList usb = usbDevices(devices, &scan.busInfo);   // OS-specific
    std::vector<ScannedDevice> internal;
    for (const ScannedDevice& dev : devices) {
        if (usb.contains(dev.name))
            scan.usbLeftOut << dev.name;
        else
            internal.push_back(dev);
    }
    scan.disks = readDevices(smartctlPath, internal);   // OS-specific: may elevate
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
        // Speed first, each dd line prefixed: its report goes to stderr, so it is
        // moved to stdout (2>&1) and marked, never mixed with smartctl's JSON.
        QStringList dd{QStringLiteral("LC_ALL=C timeout")};
        for (const QString& a : ddArgs(linuxBlockDevice(dev.name)))
            dd << shellQuote(a);
        QStringList words{shellQuote(smartctlPath)};
        for (const QString& a : smartctlArgs(dev))
            words << shellQuote(a);
        lines << dd.join(' ') + QStringLiteral(" 2>&1 | sed 's/^/%1/'; ").arg(QLatin1String(kDdPrefix))
                     + words.join(' ') + QStringLiteral("; echo \"%1$?\"").arg(QLatin1String(kBatchMarker));
    }
    return lines.join('\n');
}

std::vector<SmartctlRun> parseBatchOutput(const QByteArray& out, const std::vector<ScannedDevice>& devices)
{
    std::vector<SmartctlRun> runs;
    QByteArray current, dd;
    size_t next = 0;
    for (const QByteArray& line : out.split('\n')) {
        if (line.startsWith(kDdPrefix)) {
            dd += line.mid(int(qstrlen(kDdPrefix))) + '\n';
        } else if (line.startsWith(kBatchMarker)) {
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
            r.speedRaw = QStringLiteral("timeout %1 (as root, pkexec batch):\n%2")
                             .arg(ddArgs(linuxBlockDevice(r.device)).join(' '), QString::fromUtf8(dd).trimmed());
            r.readMBps = parseDdSpeed(QString::fromUtf8(dd));
            runs.push_back(std::move(r));
            current.clear();
            dd.clear();
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

QString linuxBlockDevice(const QString& smartctlName)
{
    static const QRegularExpression re(QStringLiteral("^/dev/nvme[0-9]+$"));
    return re.match(smartctlName).hasMatch() ? smartctlName + QStringLiteral("n1") : smartctlName;
}

QStringList ddArgs(const QString& blockDevice)
{
    // SIGUSR1: dd prints its report and goes on; SIGKILL (-k) 1 s later stops
    // it. SIGINT would be simpler but dd ignores it under timeout (checked:
    // no report at all).
    return {QStringLiteral("-s"), QStringLiteral("USR1"), QStringLiteral("-k"), QStringLiteral("1"),
            QString::number(kSpeedMaxSeconds),
            QStringLiteral("dd"), QStringLiteral("if=") + blockDevice, QStringLiteral("of=/dev/null"),
            QStringLiteral("bs=1M"), QStringLiteral("count=%1").arg(kSpeedMiB), QStringLiteral("iflag=direct")};
}

double parseDdSpeed(const QString& ddOutput)
{
    // The last match: a stopped dd prints its report twice (seen on Arch).
    static const QRegularExpression re(QStringLiteral("^(\\d+) bytes .*copied, ([0-9.]+) s"),
                                       QRegularExpression::MultilineOption);
    double mbps = -1;
    for (auto it = re.globalMatch(ddOutput); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const double bytes = m.captured(1).toDouble();
        const double seconds = m.captured(2).toDouble();
        mbps = (bytes >= kSpeedMinMiB * 1024.0 * 1024.0 && seconds > 0) ? bytes / 1e6 / seconds : -1;
    }
    return mbps;
}
