#pragma once
#include <QCoreApplication>
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
    case Status::Pass:    return QCoreApplication::translate("Status", "PASS");
    case Status::Warn:    return QCoreApplication::translate("Status", "WARN");
    case Status::Fail:    return QCoreApplication::translate("Status", "FAIL");
    case Status::Skipped: return QCoreApplication::translate("Status", "SKIPPED");
    case Status::Error:   return QCoreApplication::translate("Status", "ERROR");
    }
    return QStringLiteral("?");
}
