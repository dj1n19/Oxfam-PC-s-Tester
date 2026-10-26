#include "tests/license/LicenseTest.h"

#include <QCoreApplication>
#include <QtConcurrent>

namespace {

// SL_LICENSING_STATUS (slpublic.h). The grace period has no detail here
// (first install, hardware changed...): dwGraceTime is in the details.
QString statusText(int s)
{
    switch (s) {
    case 0: return QCoreApplication::translate("LicenseTest", "unlicensed");
    case 1: return QCoreApplication::translate("LicenseTest", "activated");
    case 2: return QCoreApplication::translate("LicenseTest", "grace period");
    case 3: return QCoreApplication::translate("LicenseTest", "notification mode (not activated)");
    default: return QCoreApplication::translate("LicenseTest", "unknown status %1").arg(s);
    }
}

} // namespace

LicenseTest::LicenseTest(QObject* parent) : ITest(parent)
{
    connect(&m_watcher, &QFutureWatcher<LicenseInfo>::finished, this, [this] {
        emit finished(evaluate(m_watcher.result()));
    });
}

void LicenseTest::run()
{
    m_watcher.setFuture(QtConcurrent::run(&readLicense));
}

TestResult LicenseTest::evaluate(const LicenseInfo& info)
{
    TestResult r;
    if (!info.applicable) {
        r.status = Status::Skipped;
        r.summary = tr("Not Windows");
        return r;
    }
    if (!info.error.isEmpty()) {
        r.status = Status::Error;
        r.summary = tr("Could not read the Windows licence (see details)");
        r.details = info.error + QLatin1Char('\n') + info.raw;
        return r;
    }

    const QString oem = info.oemKeyPresent
        ? tr("OEM key in firmware: yes")
        : tr("OEM key in firmware: no");
    r.details = QStringLiteral("Product: %1\nChannel: %2\nStatus: %3 (%4)\n%5\n\nRaw:\n%6")
                    .arg(info.name, info.description)
                    .arg(info.status)
                    .arg(info.status >= 0 ? statusText(info.status) : QStringLiteral("no licence installed"),
                         oem, info.raw);

    if (info.status < 0) {
        r.status = Status::Warn;
        r.summary = tr("No Windows licence installed | %1").arg(oem);
        return r;
    }
    if (info.status == 1) {
        r.status = Status::Pass;
        r.summary = tr("Activated: %1 | %2").arg(info.name, oem);
        return r;
    }
    r.status = Status::Warn;
    r.summary = tr("NOT activated (%1): %2 | %3").arg(statusText(info.status), info.name, oem);
    // Most refurbished PCs can be activated again with the firmware key.
    if (info.oemKeyPresent)
        r.details.prepend(QStringLiteral("Hint: a firmware OEM key exists, activation with it should work.\n\n"));
    return r;
}
