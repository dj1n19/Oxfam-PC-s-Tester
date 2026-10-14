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
    QString note;      // key value alone, for the delivery sheet: "85 %" (battery), "Dell Latitude 7420" (model)
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

// The more serious of two results, to combine several checks into one row
// (several disks, temperature + speed). Fail > Error > Warn > Pass > Skipped:
// a Skipped part never hides what another part found.
inline Status worse(Status a, Status b)
{
    const auto severity = [](Status s) {
        switch (s) {
        case Status::Fail:    return 4;
        case Status::Error:   return 3;
        case Status::Warn:    return 2;
        case Status::Pass:    return 1;
        case Status::Skipped: return 0;
        }
        return 3;
    };
    return severity(a) >= severity(b) ? a : b;
}
