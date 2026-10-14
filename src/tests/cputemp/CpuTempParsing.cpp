#include "tests/cputemp/CpuTempInfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

CpuReading parseWindowsCpu(const QByteArray& json)
{
    CpuReading r;
    r.raw = QString::fromUtf8(json);

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        r.error = QStringLiteral("Unexpected PowerShell output: %1").arg(parseError.errorString());
        return r;
    }
    const QJsonObject root = doc.object();

    // Many PCs answer "Not supported" for this WMI class: not a probe
    // failure, the PC simply exposes no zone (see cputemp_win.cpp).
    for (const QJsonValue& v : root["zones"].toArray()) {
        const QJsonObject zone = v.toObject();
        if (!zone["CurrentTemperature"].isDouble())
            continue;
        // ACPI gives tenths of a kelvin (3132 = 40.05 C).
        const double celsius = zone["CurrentTemperature"].toDouble() / 10.0 - 273.15;
        if (celsius <= 0 || celsius > 150)
            continue;   // 0 K or absurd: an unused zone, not a reading
        // Several zones: keep the hottest, it is the closest to the CPU.
        if (!r.hasTemp || celsius > r.celsius) {
            r.hasTemp = true;
            r.celsius = celsius;
            r.source = zone["InstanceName"].toString();
        }
    }

    // 0 is never a real speed (a counter not ready yet): treat it as missing.
    const QJsonValue perf = root["cpu"].toObject()["PercentProcessorPerformance"];
    if (perf.isDouble() && perf.toDouble() > 0) {
        r.hasSpeed = true;
        r.speedPct = perf.toDouble();
    }

    // One battery may come as an object instead of a list (PowerShell unwraps
    // single-item arrays): accept both.
    const QJsonValue battery = root["battery"];
    const QJsonArray batteries = battery.isArray() ? battery.toArray() : QJsonArray{battery};
    for (const QJsonValue& v : batteries)
        r.onBattery = r.onBattery || v.toObject()["BatteryStatus"].toInt() == 1;
    return r;
}
