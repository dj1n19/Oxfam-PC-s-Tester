#include "tests/disk/DiskInfo.h"

#include <unistd.h>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {

// The runner's watchdog stops an automatic test after 60 s: leave it room.
constexpr int kPasswordTimeoutMs = 50'000;

std::vector<SmartctlRun> direct(const QString& smartctl, const std::vector<ScannedDevice>& devices)
{
    std::vector<SmartctlRun> runs;
    for (const ScannedDevice& dev : devices)
        runs.push_back(runSmartctl(smartctl, dev));
    return runs;
}

std::vector<SmartctlRun> allFailed(const std::vector<ScannedDevice>& devices, const QString& error,
                                   SmartctlFailure failure = SmartctlFailure::None)
{
    std::vector<SmartctlRun> runs;
    for (const ScannedDevice& dev : devices) {
        SmartctlRun r;
        r.device = dev.name;
        r.type = dev.type;
        r.error = error;
        r.failure = failure;
        runs.push_back(std::move(r));
    }
    return runs;
}

// An AppImage runs from a FUSE mount that only the user who started it can
// read: even root gets "permission denied" there, so pkexec could not start
// the bundled smartctl. Copy it to a private temporary folder first (a static
// binary: the file alone is enough). Anywhere else the path is used as it is.
QString rootReadable(const QString& smartctl, const QTemporaryDir& tmp, QString* error)
{
    const QString appDir = qEnvironmentVariable("APPDIR");   // set by the AppImage runtime
    if (appDir.isEmpty() || !smartctl.startsWith(appDir + QLatin1Char('/')))
        return smartctl;
    const QString copy = tmp.filePath(QStringLiteral("smartctl"));
    if (!tmp.isValid() || !QFile::copy(smartctl, copy)
        || !QFile::setPermissions(copy, QFile::ReadOwner | QFile::ExeOwner)) {
        *error = QStringLiteral("Could not copy smartctl out of the AppImage into %1").arg(tmp.path());
        return {};
    }
    return copy;
}

} // namespace

// /sys/block/sdb is a link into the device tree: through ".../usb1/..." for
// a USB disk. NVMe names (/dev/nvme0) have no /sys/block entry: never USB.
QStringList usbDevices(const std::vector<ScannedDevice>& devices, QString* raw)
{
    QStringList usb, seen;
    for (const ScannedDevice& dev : devices) {
        const QString sysPath = QFileInfo(QStringLiteral("/sys/block/") + QFileInfo(dev.name).fileName())
                                    .canonicalFilePath();
        seen << QStringLiteral("%1 -> %2").arg(dev.name, sysPath.isEmpty() ? QStringLiteral("-") : sysPath);
        if (sysPath.contains(QLatin1String("/usb")))
            usb << dev.name;
    }
    *raw = QStringLiteral("sysfs:\n") + seen.join(QLatin1Char('\n'));
    return usb;
}

// Reading SMART needs root. Running the whole app as root breaks audio and
// camera (they live in the user session), so only smartctl is elevated, with
// pkexec, in ONE call for all disks: one password prompt.
std::vector<SmartctlRun> readDevices(const QString& smartctl, const std::vector<ScannedDevice>& devices)
{
    if (geteuid() == 0 || devices.empty())
        return direct(smartctl, devices);

    const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
    if (pkexec.isEmpty())
        return direct(smartctl, devices);   // will say "cannot open disk (run as root?)"

    // Device names come from smartctl --scan, but they end up in a root shell:
    // refuse anything unusual instead of trying to escape it.
    std::vector<ScannedDevice> safe;
    std::vector<SmartctlRun> refused;
    for (const ScannedDevice& dev : devices) {
        if (isSafeShellWord(dev.name) && (dev.type.isEmpty() || isSafeShellWord(dev.type))) {
            safe.push_back(dev);
        } else {
            SmartctlRun r;
            r.device = dev.name;
            r.type = dev.type;
            r.error = QStringLiteral("Unusual device name or type refused (not passed to a root shell)");
            refused.push_back(std::move(r));
        }
    }
    if (safe.empty())
        return refused;

    QTemporaryDir tmp;   // removed (with the copy) when this function returns
    QString error;
    const QString rootSmartctl = rootReadable(smartctl, tmp, &error);
    if (rootSmartctl.isEmpty()) {
        std::vector<SmartctlRun> runs = allFailed(safe, error);
        for (SmartctlRun& r : refused)
            runs.push_back(std::move(r));
        return runs;
    }

    QProcess p;
    // pkexec needs absolute paths and gives the program a clean environment.
    p.start(pkexec, {QStringLiteral("/bin/sh"), QStringLiteral("-c"), buildBatchScript(rootSmartctl, safe)});
    std::vector<SmartctlRun> runs;
    if (!p.waitForStarted(5000)) {
        runs = allFailed(safe, QStringLiteral("Could not start pkexec"));
    } else if (!p.waitForFinished(kPasswordTimeoutMs)) {
        p.kill();
        p.waitForFinished();
        runs = allFailed(safe, QStringLiteral("Administrator password not entered in time (pkexec)"),
                         SmartctlFailure::PasswordTimeout);
    } else if (p.exitStatus() != QProcess::NormalExit) {
        runs = allFailed(safe, QStringLiteral("pkexec crashed"));
    } else if (p.exitCode() == 126 || p.exitCode() == 127) {
        // pkexec: 126 = dialog dismissed, 127 = not authorised (wrong password, a
        // user who is not an administrator, no polkit agent...). Its own message
        // tells which: keep it for the details (real case: ASUS on Mint, 127).
        const QString said = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
        runs = allFailed(safe, QStringLiteral("Administrator password refused or cancelled (pkexec exit code %1)%2")
                                   .arg(p.exitCode())
                                   .arg(said.isEmpty() ? QString() : QStringLiteral("\npkexec said: ") + said),
                         SmartctlFailure::PasswordRefused);
    } else {
        runs = parseBatchOutput(p.readAllStandardOutput(), safe);
    }
    for (SmartctlRun& r : refused)
        runs.push_back(std::move(r));
    return runs;
}
