#include "tests/sysinfo/SystemInfo.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSysInfo>

namespace {

const QString kDmi = QStringLiteral("/sys/class/dmi/id/");

// Returns the trimmed content, or an empty string with *why set.
QString readText(const QString& path, QString* why = nullptr)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (why) *why = QStringLiteral("%1: %2").arg(path, f.errorString());
        return {};
    }
    return QString::fromUtf8(f.readAll()).trimmed();
}

// First "key : value" line of a /proc file, e.g. "model name : Intel ...".
QString procField(const QString& text, const QString& key)
{
    const QRegularExpression re(QStringLiteral("^%1\\s*:\\s*(.+)$")
                                    .arg(QRegularExpression::escape(key)),
                                QRegularExpression::MultilineOption);
    return re.match(text).captured(1).trimmed();
}

// Native resolution of the built-in screen: the kernel lists each
// connector's modes in /sys/class/drm, the preferred (native) one first.
// eDP/LVDS/DSI = a laptop's own panel. Qt's figure was 1.6 x too big on
// Mint with display scaling (3072x1728 for a 1920x1080 panel, 2026-10-28).
QSize builtInPanel(QString* raw)
{
    const QDir drm(QStringLiteral("/sys/class/drm"));
    for (const QString& c : drm.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name)) {
        if (!c.contains(QLatin1String("-eDP-")) && !c.contains(QLatin1String("-LVDS-")) && !c.contains(QLatin1String("-DSI-")))
            continue;
        if (readText(drm.filePath(c) + "/status") != QLatin1String("connected"))
            continue;
        const QString first = readText(drm.filePath(c) + "/modes").section(QLatin1Char('\n'), 0, 0);
        *raw = QStringLiteral("drm %1: %2").arg(c, first.isEmpty() ? QStringLiteral("no mode") : first);
        return parseDrmMode(first);
    }
    *raw = QStringLiteral("drm: no built-in panel connected");
    return {};
}

} // namespace

SystemInfo readSystemInfo()
{
    SystemInfo info;
    info.vendor = readText(kDmi + "sys_vendor");
    info.model  = readText(kDmi + "product_name");
    // product_serial is mode 0400 root: a normal user gets "Permission denied".
    info.serial = readText(kDmi + "product_serial", &info.serialError);

    bool ok = false;
    const int chassis = readText(kDmi + "chassis_type").toInt(&ok);
    if (ok)
        info.chassisType = chassis;

    const QString cpuinfo = readText(QStringLiteral("/proc/cpuinfo"));
    info.cpu = procField(cpuinfo, QStringLiteral("model name"));
    countCpuCores(cpuinfo, &info.cores, &info.threads);
    // PRETTY_NAME of /etc/os-release: "Linux Mint 22", "Arch Linux".
    info.os = QSysInfo::prettyProductName();
    QString panel;
    info.screenPixels = builtInPanel(&panel);

    // "MemTotal:  15989996 kB" (kB here means KiB)
    const QString memTotal = procField(readText(QStringLiteral("/proc/meminfo")), QStringLiteral("MemTotal"));
    const double kib = memTotal.section(QLatin1Char(' '), 0, 0).toDouble(&ok);
    if (ok)
        info.ramBytes = kib * 1024.0;

    info.raw = QStringLiteral("sys_vendor=%1\nproduct_name=%2\nproduct_serial=%3\n"
                              "chassis_type=%4\ncpuinfo model name=%5\nmeminfo MemTotal=%6\n"
                              "cpuinfo: %7 cores, %8 processors\nos=%9 (kernel %10)\n%11")
                   .arg(info.vendor, info.model,
                        info.serial.isEmpty() ? info.serialError : info.serial)
                   .arg(info.chassisType)
                   .arg(info.cpu, memTotal)
                   .arg(info.cores).arg(info.threads)
                   .arg(info.os, QSysInfo::kernelVersion(), panel);
    return info;
}
