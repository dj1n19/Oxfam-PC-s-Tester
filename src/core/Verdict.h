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

// When only some tests ran (one test alone, e.g. after a battery change and
// a restart): the verdict of those, and says so. A partial pass is shown in
// the WARN colour: the other tests were not checked this time.
inline Verdict computeVerdict(const std::vector<Status>& ranStatuses, int totalTests)
{
    Verdict v = computeVerdict(ranStatuses);
    const int ran = static_cast<int>(ranStatuses.size());
    if (ran >= totalTests)
        return v;
    v.text = QCoreApplication::translate("Verdict", "PARTIAL (%1 of %2 tests) - %3").arg(ran).arg(totalTests).arg(v.text);
    if (v.level == Status::Pass)
        v.level = Status::Warn;
    return v;
}
