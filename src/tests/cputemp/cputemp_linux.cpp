#include "tests/cputemp/CpuTempInfo.h"

#include <optional>
#include <QDir>
#include <QFile>

namespace {

std::optional<QString> readText(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return std::nullopt;
    return QString::fromUtf8(f.readAll()).trimmed();
}

// sysfs temperatures are in millidegrees C. 0 or absurd values = unused input.
std::optional<double> readMilliCelsius(const QString& path)
{
    const auto text = readText(path);
    if (!text)
        return std::nullopt;
    bool ok = false;
    const double c = text->toDouble(&ok) / 1000.0;
    if (!ok || c <= 0 || c > 150)
        return std::nullopt;
    return c;
}

// CPU drivers: Intel, AMD, and the out-of-tree AMD driver.
bool isCpuDriver(const QString& name)
{
    return name == QLatin1String("coretemp") || name == QLatin1String("k10temp")
        || name == QLatin1String("zenpower");
}

// One hwmon CPU driver: the package/die value if labelled, else the hottest
// input. Tctl on some old Ryzen has a +20 C offset, so Tdie is preferred.
std::optional<CpuTempReading> readHwmon(const QDir& dir, const QString& name, QString& raw)
{
    std::optional<CpuTempReading> hottest;
    std::optional<CpuTempReading> preferred;
    int preferredRank = 99;
    const QStringList preferredLabels = {QStringLiteral("Package id 0"), QStringLiteral("Tdie"),
                                         QStringLiteral("Tctl")};

    for (const QString& file : dir.entryList({QStringLiteral("temp*_input")}, QDir::Files, QDir::Name)) {
        const auto c = readMilliCelsius(dir.filePath(file));
        if (!c)
            continue;
        const QString prefix = file.left(file.indexOf(QLatin1Char('_')));   // "temp1"
        const QString label = readText(dir.filePath(prefix + QStringLiteral("_label"))).value_or(prefix);
        raw += QStringLiteral("%1 %2: %3 C\n").arg(name, label).arg(*c, 0, 'f', 1);

        CpuTempReading r;
        r.available = true;
        r.celsius = *c;
        r.source = name + QLatin1Char(' ') + label;
        if (!hottest || r.celsius > hottest->celsius)
            hottest = r;
        const int rank = static_cast<int>(preferredLabels.indexOf(label));
        if (rank >= 0 && rank < preferredRank) {
            preferred = r;
            preferredRank = rank;
        }
    }
    return preferred ? preferred : hottest;
}

} // namespace

CpuTempReading readCpuTemperature()
{
    QString raw;

    // 1. A CPU driver under /sys/class/hwmon (almost every x86 PC).
    const QDir hwmon(QStringLiteral("/sys/class/hwmon"));
    for (const QString& entry : hwmon.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QDir dir(hwmon.filePath(entry));
        const QString name = readText(dir.filePath(QStringLiteral("name"))).value_or(QString());
        if (!isCpuDriver(name))
            continue;
        if (auto r = readHwmon(dir, name, raw)) {
            r->raw = raw;
            return *r;
        }
    }

    // 2. Thermal zones: the Intel package zone first, then the ACPI one
    //    (motherboard zone, like on Windows: may not follow the CPU).
    const QDir thermal(QStringLiteral("/sys/class/thermal"));
    const QStringList zones = thermal.entryList({QStringLiteral("thermal_zone*")}, QDir::Dirs, QDir::Name);
    for (const QString& wanted : {QStringLiteral("x86_pkg_temp"), QStringLiteral("acpitz")}) {
        for (const QString& zone : zones) {
            const QDir dir(thermal.filePath(zone));
            if (readText(dir.filePath(QStringLiteral("type"))) != wanted)
                continue;
            const auto c = readMilliCelsius(dir.filePath(QStringLiteral("temp")));
            if (!c)
                continue;
            raw += QStringLiteral("%1 %2: %3 C\n").arg(zone, wanted).arg(*c, 0, 'f', 1);
            CpuTempReading r;
            r.available = true;
            r.celsius = *c;
            r.source = wanted;
            r.raw = raw;
            return r;
        }
    }

    CpuTempReading r;   // available = false: Skipped, not Error
    r.raw = raw.isEmpty() ? QStringLiteral("No coretemp/k10temp/zenpower hwmon, no x86_pkg_temp/acpitz zone") : raw;
    return r;
}
