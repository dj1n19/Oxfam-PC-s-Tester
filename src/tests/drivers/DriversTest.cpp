#include "tests/drivers/DriversTest.h"

#include <QCoreApplication>
#include <QStringList>
#include <QtConcurrent>

namespace {

// Windows Device Manager error codes (learn.microsoft.com, "Device Manager error messages").
constexpr int kNotConnected = 45;

QString meaning(int code)
{
    switch (code) {
    case -1: return QCoreApplication::translate("DriversTest", "no driver");
    case 1:  return QCoreApplication::translate("DriversTest", "not configured");
    case 3:  return QCoreApplication::translate("DriversTest", "driver corrupted or low memory");
    case 10: return QCoreApplication::translate("DriversTest", "cannot start");
    case 12: return QCoreApplication::translate("DriversTest", "not enough resources");
    case 14: return QCoreApplication::translate("DriversTest", "restart needed");
    case 18: return QCoreApplication::translate("DriversTest", "reinstall driver");
    case 19: return QCoreApplication::translate("DriversTest", "registry problem");
    case 21: return QCoreApplication::translate("DriversTest", "being removed");
    case 22: return QCoreApplication::translate("DriversTest", "disabled");
    case 24: return QCoreApplication::translate("DriversTest", "not present or no driver");
    case 28: return QCoreApplication::translate("DriversTest", "no driver installed");
    case 31: return QCoreApplication::translate("DriversTest", "not working properly");
    case 32: return QCoreApplication::translate("DriversTest", "driver service disabled");
    case 37: return QCoreApplication::translate("DriversTest", "driver failed to initialise");
    case 39: return QCoreApplication::translate("DriversTest", "driver missing or corrupted");
    case 43: return QCoreApplication::translate("DriversTest", "stopped after reporting problems");
    case 52: return QCoreApplication::translate("DriversTest", "unsigned driver");
    default: return QCoreApplication::translate("DriversTest", "error code %1").arg(code);
    }
}

} // namespace

DriversTest::DriversTest(QObject* parent) : ITest(parent)
{
    connect(&m_watcher, &QFutureWatcher<DriverInfo>::finished, this, [this] {
        emit finished(evaluate(m_watcher.result()));
    });
}

void DriversTest::run()
{
    m_watcher.setFuture(QtConcurrent::run(&readDrivers));
}

TestResult DriversTest::evaluate(const DriverInfo& info)
{
    TestResult r;
    if (!info.error.isEmpty()) {
        r.status = Status::Error;
        r.summary = tr("Could not list devices");
        r.details = info.error + QLatin1Char('\n') + info.raw;
        return r;
    }

    QStringList problems, ignored;
    for (const DeviceProblem& p : info.problems) {
        const QString line = QStringLiteral("%1 (%2)").arg(p.name, meaning(p.code));
        // Code 45 = device remembered by Windows but unplugged (the previous
        // owner's USB stick...): not a problem of this PC.
        if (p.code == kNotConnected)
            ignored << line + QStringLiteral(" - ") + p.id;
        else
            problems << line;
    }

    QString details;
    if (!problems.isEmpty())
        details += QStringLiteral("Problems:\n  %1\n\n").arg(problems.join("\n  "));
    if (!ignored.isEmpty())
        details += QStringLiteral("Ignored, not connected:\n  %1\n\n").arg(ignored.join("\n  "));
    r.details = details + QStringLiteral("Raw:\n") + info.raw;

    if (problems.isEmpty()) {
        r.status = Status::Pass;
        r.summary = tr("No driver problem");
        return r;
    }
    r.status = Status::Warn;
    r.summary = tr("%1 device(s): %2").arg(problems.size()).arg(problems.join(", "));
    return r;
}
