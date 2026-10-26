#include "tests/cputemp/CpuTempInfo.h"

#include <QStringList>

CpuReading windowsCpuReading(const std::vector<ThermalZone>& zones, double speedPct, bool onBattery)
{
    CpuReading r;
    QStringList seen;
    for (const ThermalZone& zone : zones) {
        seen << QStringLiteral("%1 = %2 K").arg(zone.name).arg(zone.kelvin);
        const double celsius = zone.kelvin - 273.15;
        if (celsius <= 0 || celsius > 150)
            continue;   // 0 K or absurd: an unused zone, not a reading
        // Several zones: keep the hottest, it is the closest to the CPU.
        if (!r.hasTemp || celsius > r.celsius) {
            r.hasTemp = true;
            r.celsius = celsius;
            r.source = zone.name;
        }
    }

    // 0 is never a real speed (a counter not ready yet): treat it as missing.
    if (speedPct > 0) {
        r.hasSpeed = true;
        r.speedPct = speedPct;
    }
    r.onBattery = onBattery;
    r.raw = QStringLiteral("zones: %1; speed: %2 %; on battery: %3")
                .arg(seen.isEmpty() ? QStringLiteral("none") : seen.join(QStringLiteral(", ")))
                .arg(speedPct)
                .arg(onBattery ? QStringLiteral("yes") : QStringLiteral("no"));
    return r;
}
