#pragma once
#include <QHash>
#include <QString>
#include "core/TestResult.h"

// Reads thresholds.json: { "key": { "warn": X, "fail": Y }, ... }
// Direction is deduced: fail < warn  => lower values are worse (battery %)
//                       fail >= warn => higher values are worse (bad sectors)
class Thresholds {
public:
    bool loadFromFile(const QString& path, QString* error = nullptr);
    bool loadFromJson(const QByteArray& json, QString* error = nullptr);

    bool has(const QString& key) const { return m_rules.contains(key); }
    Status evaluate(const QString& key, double value) const;   // Pass if no rule

private:
    struct Rule { double warn = 0; double fail = 0; };
    QHash<QString, Rule> m_rules;
};
