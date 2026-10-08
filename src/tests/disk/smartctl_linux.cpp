#include "tests/disk/DiskInfo.h"

#include <unistd.h>
#include <QProcess>
#include <QStandardPaths>

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

std::vector<SmartctlRun> allFailed(const std::vector<ScannedDevice>& devices, const QString& error)
{
    std::vector<SmartctlRun> runs;
    for (const ScannedDevice& dev : devices) {
        SmartctlRun r;
        r.device = dev.name;
        r.type = dev.type;
        r.error = error;
        runs.push_back(std::move(r));
    }
    return runs;
}

} // namespace

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

    QProcess p;
    // pkexec needs absolute paths and gives the program a clean environment.
    p.start(pkexec, {QStringLiteral("/bin/sh"), QStringLiteral("-c"), buildBatchScript(smartctl, safe)});
    std::vector<SmartctlRun> runs;
    if (!p.waitForStarted(5000)) {
        runs = allFailed(safe, QStringLiteral("Could not start pkexec"));
    } else if (!p.waitForFinished(kPasswordTimeoutMs)) {
        p.kill();
        p.waitForFinished();
        runs = allFailed(safe, QStringLiteral("Administrator password not entered in time (pkexec)"));
    } else if (p.exitStatus() != QProcess::NormalExit) {
        runs = allFailed(safe, QStringLiteral("pkexec crashed"));
    } else if (p.exitCode() == 126 || p.exitCode() == 127) {
        // pkexec: 126 = dialog dismissed / not authorised, 127 = authentication failed
        runs = allFailed(safe, QStringLiteral("Administrator password refused or cancelled (pkexec exit code %1)")
                                   .arg(p.exitCode()));
    } else {
        runs = parseBatchOutput(p.readAllStandardOutput(), safe);
    }
    for (SmartctlRun& r : refused)
        runs.push_back(std::move(r));
    return runs;
}
