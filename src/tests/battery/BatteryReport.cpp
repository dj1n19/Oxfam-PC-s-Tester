#include "tests/battery/BatteryInfo.h"

#include <QXmlStreamReader>

// Expected shape (namespace http://schemas.microsoft.com/battery/2012):
//   <BatteryReport> ... <Batteries>
//     <Battery> <Id>..</Id> <DesignCapacity>57000</DesignCapacity>
//               <FullChargeCapacity>22359</FullChargeCapacity>
//               <CycleCount>0</CycleCount> ... </Battery>
//   </Batteries> ...
// Capacities are in mWh. A desktop has an empty <Batteries/>.
BatteryInfo parseBatteryReport(const QByteArray& xml)
{
    BatteryInfo info;
    QXmlStreamReader reader(xml);

    // Only the first <Battery> is read (multi-battery: later).
    bool inBattery = false;
    QString design, full, cycles;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const auto name = reader.name();   // local name: the namespace is ignored
            if (name == QLatin1String("Battery")) {
                inBattery = true;
                info.present = true;
            } else if (inBattery) {
                if (name == QLatin1String("Id"))
                    info.name = reader.readElementText().trimmed();
                else if (name == QLatin1String("DesignCapacity"))
                    design = reader.readElementText().trimmed();
                else if (name == QLatin1String("FullChargeCapacity"))
                    full = reader.readElementText().trimmed();
                else if (name == QLatin1String("CycleCount"))
                    cycles = reader.readElementText().trimmed();
            }
        } else if (reader.isEndElement() && inBattery && reader.name() == QLatin1String("Battery")) {
            break;
        }
    }

    if (reader.hasError() && !inBattery) {
        info.error = QStringLiteral("Unreadable battery report XML: %1").arg(reader.errorString());
        return info;
    }
    if (!info.present)
        return info;   // no <Battery>: desktop

    info.raw = QStringLiteral("powercfg %1: DesignCapacity=%2 FullChargeCapacity=%3 CycleCount=%4")
                   .arg(info.name, design, full, cycles);

    bool okDesign = false, okFull = false;
    info.designCapacity = design.toDouble(&okDesign);
    info.fullCapacity = full.toDouble(&okFull);
    if (!okDesign || !okFull) {
        info.error = QStringLiteral("Battery report has no numeric DesignCapacity/FullChargeCapacity");
        return info;
    }

    const int c = cycles.toInt();
    info.cycles = c > 0 ? c : -1;   // 0 usually means "not reported"
    return info;
}
