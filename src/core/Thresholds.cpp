#include "core/Thresholds.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

bool Thresholds::loadFromFile(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Cannot open %1: %2").arg(path, file.errorString());
        return false;
    }
    return loadFromJson(file.readAll(), error);
}

bool Thresholds::loadFromJson(const QByteArray& json, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("Invalid JSON: %1").arg(parseError.errorString());
        return false;
    }

    QHash<QString, Rule> rules;   // filled aside: all or nothing
    const QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        if (!o.contains("warn") || !o.contains("fail")) {
            if (error) *error = QStringLiteral("Rule '%1' needs \"warn\" and \"fail\"").arg(it.key());
            return false;
        }
        rules.insert(it.key(), Rule{o["warn"].toDouble(), o["fail"].toDouble()});
    }
    m_rules = std::move(rules);
    return true;
}

Status Thresholds::evaluate(const QString& key, double value) const
{
    const auto it = m_rules.constFind(key);
    if (it == m_rules.cend())
        return Status::Pass;

    const Rule& r = it.value();
    if (r.fail < r.warn) {                 // lower is worse
        if (value < r.fail) return Status::Fail;
        if (value < r.warn) return Status::Warn;
    } else {                               // higher is worse
        if (value >= r.fail) return Status::Fail;
        if (value >= r.warn) return Status::Warn;
    }
    return Status::Pass;
}
