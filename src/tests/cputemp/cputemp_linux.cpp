#include "tests/cputemp/CpuTempInfo.h"

#include <optional>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QThread>

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
std::optional<CpuReading> readHwmon(const QDir& dir, const QString& name, QString& raw)
{
    std::optional<CpuReading> hottest;
    std::optional<CpuReading> preferred;
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

        CpuReading r;
        r.hasTemp = true;
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

// The temperature part of readCpu(): hwmon first, then thermal zones.
CpuReading readTemperature()
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
            CpuReading r;
            r.hasTemp = true;
            r.celsius = *c;
            r.source = wanted;
            r.raw = raw;
            return r;
        }
    }

    CpuReading r;   // hasTemp = false: no sensor, not Error
    r.raw = raw.isEmpty() ? QStringLiteral("No coretemp/k10temp/zenpower hwmon, no x86_pkg_temp/acpitz zone\n") : raw;
    return r;
}

std::optional<double> readKhz(const QString& path)
{
    bool ok = false;
    const double khz = readText(path).value_or(QString()).toDouble(&ok);
    return ok && khz > 0 ? std::optional<double>(khz) : std::nullopt;
}

// Average current speed of every core, in % of the base speed. Only the
// intel_pstate driver gives the base speed (base_frequency, no root needed);
// other drivers (AMD, acpi-cpufreq) leave hasSpeed false.
void readSpeed(CpuReading& r)
{
    const QDir cpus(QStringLiteral("/sys/devices/system/cpu"));
    double cur = 0, base = 0;
    int cores = 0;
    for (const QString& cpu : cpus.entryList({QStringLiteral("cpu[0-9]*")}, QDir::Dirs, QDir::Name)) {
        const QString dir = cpus.filePath(cpu) + QStringLiteral("/cpufreq/");
        const auto c = readKhz(dir + QStringLiteral("scaling_cur_freq"));
        const auto b = readKhz(dir + QStringLiteral("base_frequency"));
        if (!c || !b)
            continue;
        cur += *c;
        base += *b;
        ++cores;
    }
    if (cores == 0) {
        r.raw += QStringLiteral("Speed: no cpufreq base_frequency (only intel_pstate has it)\n");
        return;
    }
    r.hasSpeed = true;
    r.speedPct = 100.0 * cur / base;
    r.raw += QStringLiteral("Speed: %1 cores, average %2 MHz, base %3 MHz = %4 %\n")
                 .arg(cores).arg(cur / cores / 1000, 0, 'f', 0).arg(base / cores / 1000, 0, 'f', 0)
                 .arg(r.speedPct, 0, 'f', 0);
}

// On battery = a battery exists, and a charger input exists but none is
// online. Unknown (no charger input listed) counts as "not on battery":
// better no hint than a wrong one. USB-C chargers show up as type "USB".
void readPower(CpuReading& r)
{
    const QDir supplies(QStringLiteral("/sys/class/power_supply"));
    bool battery = false, charger = false, online = false;
    for (const QString& entry : supplies.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QDir dir(supplies.filePath(entry));
        const QString type = readText(dir.filePath(QStringLiteral("type"))).value_or(QString());
        if (type == QLatin1String("Battery")) {
            battery = true;
        } else if (type == QLatin1String("Mains") || type == QLatin1String("USB")) {
            charger = true;
            online = online || readText(dir.filePath(QStringLiteral("online"))) == QLatin1String("1");
        }
    }
    r.onBattery = battery && charger && !online;
    if (r.onBattery)
        r.raw += QStringLiteral("Power: on battery\n");
}

CpuReading readCpu()
{
    CpuReading r = readTemperature();
    readSpeed(r);
    readPower(r);
    return r;
}

} // namespace

// Reading sysfs takes microseconds even under load: one call per reading.
std::vector<CpuReading> sampleCpu(int seconds, const std::function<bool(const CpuReading&)>& startLoad)
{
    std::vector<CpuReading> readings{readCpu()};
    if (!startLoad(readings.front()))
        return readings;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < seconds * 1000LL) {
        QThread::msleep(1000);   // worker thread: sleeping here never blocks the UI
        readings.push_back(readCpu());
    }
    return readings;
}
