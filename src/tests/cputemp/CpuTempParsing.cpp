#include "tests/cputemp/CpuTempInfo.h"

#include <QStringList>

CpuReading windowsCpuReading(const std::vector<ThermalZone>& zones, double speedPct, bool onBattery)
{
    CpuReading r;
    QStringList seen;
    for (const ThermalZone& zone : zones)
        seen << QStringLiteral("%1 = %2 K").arg(zone.name).arg(zone.kelvin);

    // 0 is never a real speed (a counter not ready yet): treat it as missing.
    if (speedPct > 0) {
        r.hasSpeed = true;
        r.speedPct = speedPct;
    }
    r.onBattery = onBattery;
    r.raw = QStringLiteral("ACPI zones (not used, not the CPU): %1; speed: %2 %; on battery: %3")
                .arg(seen.isEmpty() ? QStringLiteral("none") : seen.join(QStringLiteral(", ")))
                .arg(speedPct)
                .arg(onBattery ? QStringLiteral("yes") : QStringLiteral("no"));
    return r;
}
