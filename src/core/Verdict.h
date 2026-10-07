#pragma once
#include <vector>
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
    if (fail)  return {Status::Fail,  QStringLiteral("DO NOT SELL - needs repair")};
    if (error) return {Status::Error, QStringLiteral("INCOMPLETE - a test could not run")};
    if (warn)  return {Status::Warn,  QStringLiteral("SELL WITH NOTE - see warnings")};
    return {Status::Pass, QStringLiteral("OK FOR SALE")};
}
