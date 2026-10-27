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

// The kernel's words in power_supply/<battery>/status.
ChargeState stateFromText(const QString& status)
{
    if (status == QLatin1String("Charging")) return ChargeState::Charging;
    if (status == QLatin1String("Discharging")) return ChargeState::Discharging;
    if (status == QLatin1String("Not charging")) return ChargeState::NotCharging;
    if (status == QLatin1String("Full")) return ChargeState::Full;
    return ChargeState::Unknown;
}

BatteryInfo readBattery()
{
    BatteryInfo info;
    const QDir root(QStringLiteral("/sys/class/power_supply"));
    const QStringList entries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    // Charger: any "Mains" supply (AC adapter), or a "USB" one (USB-C
    // charging) that is online. No such supply listed at all = unknown.
    QStringList chargers;
    for (const QString& entry : entries) {
        const auto type = readText(root.filePath(entry) + "/type");
        if (type != QString("Mains") && type != QString("USB"))
            continue;
        const auto online = readNumber(root.filePath(entry) + "/online");
        chargers << QStringLiteral("%1 (%2) online=%3").arg(entry, *type, online ? QString::number(*online) : "?");
        if (online && *online > 0)
            info.acOnline = 1;
        else if (online && info.acOnline == -1 && type == QString("Mains"))
            info.acOnline = 0;
    }

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

        const QString status = readText(dir + "/status").value_or(QString());
        info.state = stateFromText(status);
        if (const auto pct = readNumber(dir + "/capacity"))
            info.chargePct = *pct;
        // power_now in uW; some batteries give current_now (uA) and voltage_now (uV).
        const auto power = readNumber(dir + "/power_now");
        const auto current = readNumber(dir + "/current_now");
        const auto voltage = readNumber(dir + "/voltage_now");
        const double watts = power ? *power / 1e6 : (current && voltage ? *current * *voltage / 1e12 : 0);
        if (info.state == ChargeState::Charging && watts > 0)
            info.rateW = watts;
        info.raw += QStringLiteral("\nstatus=%1 capacity=%2 power_now=%3 current_now=%4 voltage_now=%5\nchargers: %6")
                        .arg(status)
                        .arg(info.chargePct)
                        .arg(power ? QString::number(*power, 'f', 0) : "-", current ? QString::number(*current, 'f', 0) : "-",
                             voltage ? QString::number(*voltage, 'f', 0) : "-",
                             chargers.isEmpty() ? QStringLiteral("none listed") : chargers.join(", "));
        return info;   // first system battery only (multi-battery: dropped)
    }
    return info;       // present == false
}
