#include "tests/battery/BatteryInfo.h"

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

std::optional<double> readNumber(const QString& path)
{
    const auto text = readText(path);
    if (!text)
        return std::nullopt;
    bool ok = false;
    const double v = text->toDouble(&ok);
    return ok ? std::optional<double>(v) : std::nullopt;
}

} // namespace

BatteryInfo readBattery()
{
    BatteryInfo info;
    const QDir root(QStringLiteral("/sys/class/power_supply"));
    const QStringList entries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QString& entry : entries) {
        const QString dir = root.filePath(entry);
        if (readText(dir + "/type") != QString("Battery"))
            continue;                                   // AC adapter, USB...
        if (readText(dir + "/scope") == QString("Device"))
            continue;                                   // wireless mouse, etc.

        info.present = true;
        info.name = entry;

        // Newer kernels expose energy_* (uWh), others charge_* (uAh).
        for (const char* prefix : {"energy", "charge"}) {
            const auto design = readNumber(dir + "/" + prefix + "_full_design");
            const auto full   = readNumber(dir + "/" + prefix + "_full");
            if (design && full) {
                info.designCapacity = *design;
                info.fullCapacity = *full;
                info.raw = QString("%1: %2_full_design=%3 %2_full=%4")
                               .arg(entry, prefix).arg(*design).arg(*full);
                break;
            }
        }
        if (info.designCapacity <= 0 && info.fullCapacity <= 0)
            info.error = QString("No energy_*/charge_* capacity files in %1").arg(dir);

        const auto cycles = readNumber(dir + "/cycle_count");
        if (cycles && *cycles > 0)
            info.cycles = static_cast<int>(*cycles);
        return info;   // first system battery only (multi-battery: later)
    }
    return info;       // present == false
}
