#pragma once
#include <QString>

// Error = the test could not run (no permission, tool missing...).
// Fail  = the test ran and the hardware is bad.
// Never show green for an Error.
enum class Status { Pass, Warn, Fail, Skipped, Error };

struct TestResult {
    Status status = Status::Skipped;
    QString summary;   // one line, shown in the list
    QString details;   // raw readings, shown when the row is selected
};

inline QString statusLabel(Status s)
{
    switch (s) {
    case Status::Pass:    return QStringLiteral("PASS");
    case Status::Warn:    return QStringLiteral("WARN");
    case Status::Fail:    return QStringLiteral("FAIL");
    case Status::Skipped: return QStringLiteral("SKIPPED");
    case Status::Error:   return QStringLiteral("ERROR");
    }
    return QStringLiteral("?");
}
