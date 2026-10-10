#pragma once
#include <vector>
#include <QCoreApplication>
#include <QString>
#include "core/TestResult.h"

struct Verdict {
    Status level = Status::Pass;   // Pass / Warn / Fail / Error (for colouring)
    QString text;
};

// Business rule for the shop, kept out of the UI.
inline Verdict computeVerdict(const std::vector<Status>& statuses)
{
    bool fail = false, error = false, warn = false;
    for (Status s : statuses) {
        fail  = fail  || s == Status::Fail;
        error = error || s == Status::Error;
        warn  = warn  || s == Status::Warn;
    }
    if (fail)  return {Status::Fail,  QCoreApplication::translate("Verdict", "DO NOT SELL - needs repair")};
    if (error) return {Status::Error, QCoreApplication::translate("Verdict", "INCOMPLETE - check the ERROR rows, then press START again")};
    if (warn)  return {Status::Warn,  QCoreApplication::translate("Verdict", "SELL WITH NOTE - see warnings")};
    return {Status::Pass, QCoreApplication::translate("Verdict", "OK FOR SALE")};
}
