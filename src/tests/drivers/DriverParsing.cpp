#include "tests/drivers/DriverInfo.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

DriverInfo parsePnpEntities(const QByteArray& json)
{
    DriverInfo info;
    info.raw = QString::fromUtf8(json);

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        info.error = QStringLiteral("Unexpected PowerShell output: %1").arg(parseError.errorString());
        return info;
    }

    for (const QJsonValue& v : doc.array()) {
        const QJsonObject o = v.toObject();
        if (!o["ConfigManagerErrorCode"].isDouble())
            continue;
        DeviceProblem p;
        p.id = o["DeviceID"].toString();
        p.name = o["Name"].toString();
        if (p.name.isEmpty())
            p.name = p.id;   // devices without a driver often have no name
        p.code = o["ConfigManagerErrorCode"].toInt();
        if (p.code != 0)
            info.problems.push_back(p);
    }
    return info;
}

// PCI base classes (PCI Code and ID Assignment Specification).
// Only devices a buyer would miss: many chipset functions (system
// peripherals, RAM, ISA bridge...) normally run without a Linux driver.
bool pciClassMatters(unsigned classCode)
{
    const unsigned base = (classCode >> 16) & 0xff;
    const unsigned sub = (classCode >> 8) & 0xff;
    switch (base) {
    case 0x01:   // storage
    case 0x02:   // network
    case 0x03:   // display
    case 0x04:   // multimedia (audio, video)
    case 0x0d:   // wireless (Bluetooth...)
        return true;
    case 0x0c:   // serial bus: only USB controllers
        return sub == 0x03;
    default:
        return false;
    }
}

QString pciClassName(unsigned classCode)
{
    switch ((classCode >> 16) & 0xff) {
    case 0x01: return QStringLiteral("Storage controller");
    case 0x02: return QStringLiteral("Network controller");
    case 0x03: return QStringLiteral("Display controller");
    case 0x04: return QStringLiteral("Multimedia controller");
    case 0x0c: return QStringLiteral("USB controller");
    case 0x0d: return QStringLiteral("Wireless controller");
    default:   return QStringLiteral("PCI device");
    }
}
