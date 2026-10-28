#include "core/TestChoice.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

bool TestChoice::loadFromJson(const QByteArray& json, QString* error)
{
    ticked.clear();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("Invalid JSON: %1").arg(parseError.errorString());
        return false;
    }
    std::map<QString, bool> read;   // filled aside: all or nothing
    const QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        // "yes", 1, "false"...: refused rather than guessed.
        if (!it.value().isBool()) {
            if (error) *error = QStringLiteral("\"%1\" must be true or false").arg(it.key());
            return false;
        }
        read[it.key()] = it.value().toBool();
    }
    ticked = std::move(read);
    return true;
}

bool TestChoice::isTicked(const QString& id) const
{
    const auto it = ticked.find(id);
    return it == ticked.end() || it->second;
}

QByteArray TestChoice::toJson(const std::vector<std::pair<QString, bool>>& tests)
{
    // Written by hand: QJsonDocument would sort the keys alphabetically.
    QByteArray out = "{\n";
    for (size_t i = 0; i < tests.size(); ++i) {
        out += "    \"" + tests[i].first.toUtf8() + "\": " + (tests[i].second ? "true" : "false");
        out += i + 1 < tests.size() ? ",\n" : "\n";
    }
    out += "}\n";
    return out;
}
