#include "tests/cputemp/CpuTempInfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

CpuTempReading parseThermalZones(const QByteArray& json)
{
    CpuTempReading r;
    r.raw = QString::fromUtf8(json);

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        r.error = QStringLiteral("Unexpected PowerShell output: %1").arg(parseError.errorString());
        return r;
    }

    // Many PCs answer "Not supported" for this WMI class: not a probe
    // failure, the PC simply exposes no zone (see cputemp_win.cpp).
    for (const QJsonValue& v : doc.object()["zones"].toArray()) {
        const QJsonObject zone = v.toObject();
        if (!zone["CurrentTemperature"].isDouble())
            continue;
        // ACPI gives tenths of a kelvin (3132 = 40.05 C).
        const double celsius = zone["CurrentTemperature"].toDouble() / 10.0 - 273.15;
        if (celsius <= 0 || celsius > 150)
            continue;   // 0 K or absurd: an unused zone, not a reading
        // Several zones: keep the hottest, it is the closest to the CPU.
        if (!r.available || celsius > r.celsius) {
            r.available = true;
            r.celsius = celsius;
            r.source = zone["InstanceName"].toString();
        }
    }
    return r;
}
