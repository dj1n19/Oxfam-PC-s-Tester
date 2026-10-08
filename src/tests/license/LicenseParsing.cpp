#include "tests/license/LicenseInfo.h"

#include <QJsonDocument>
#include <QJsonObject>

LicenseInfo parseLicenseJson(const QByteArray& json)
{
    LicenseInfo info;
    info.raw = QString::fromUtf8(json);

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        info.error = QStringLiteral("Unexpected PowerShell output: %1").arg(parseError.errorString());
        return info;
    }

    const QJsonObject o = doc.object();
    if (!o["found"].isBool() || !o["oemKeyPresent"].isBool()) {
        info.error = QStringLiteral("PowerShell output misses \"found\" or \"oemKeyPresent\"");
        return info;
    }
    info.found = o["found"].toBool();
    info.oemKeyPresent = o["oemKeyPresent"].toBool();
    info.oemKeyDescription = o["oemKeyDescription"].toString();
    if (info.found) {
        if (!o["status"].isDouble()) {
            info.error = QStringLiteral("Windows licence found but no LicenseStatus");
            return info;
        }
        info.status = o["status"].toInt();
        info.name = o["name"].toString();
        info.description = o["description"].toString();
    }
    return info;
}
