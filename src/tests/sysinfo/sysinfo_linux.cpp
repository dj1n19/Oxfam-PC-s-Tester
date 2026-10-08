#include "tests/sysinfo/SystemInfo.h"

#include <QFile>
#include <QRegularExpression>

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

    info.cpu = procField(readText(QStringLiteral("/proc/cpuinfo")), QStringLiteral("model name"));

    // "MemTotal:  15989996 kB" (kB here means KiB)
    const QString memTotal = procField(readText(QStringLiteral("/proc/meminfo")), QStringLiteral("MemTotal"));
    const double kib = memTotal.section(QLatin1Char(' '), 0, 0).toDouble(&ok);
    if (ok)
        info.ramBytes = kib * 1024.0;

    info.raw = QStringLiteral("sys_vendor=%1\nproduct_name=%2\nproduct_serial=%3\n"
                              "chassis_type=%4\ncpuinfo model name=%5\nmeminfo MemTotal=%6")
                   .arg(info.vendor, info.model,
                        info.serial.isEmpty() ? info.serialError : info.serial)
                   .arg(info.chassisType)
                   .arg(info.cpu, memTotal);
    return info;
}
