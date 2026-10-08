#include "tests/license/LicenseTest.h"

#include <QtConcurrent>

namespace {

// SoftwareLicensingProduct.LicenseStatus (Microsoft docs)
QString statusText(int s)
{
    switch (s) {
    case 0: return QStringLiteral("unlicensed");
    case 1: return QStringLiteral("activated");
    case 2: return QStringLiteral("initial grace period");
    case 3: return QStringLiteral("grace period (hardware changed)");
    case 4: return QStringLiteral("non-genuine grace period");
    case 5: return QStringLiteral("notification mode (not activated)");
    case 6: return QStringLiteral("extended grace period");
    default: return QStringLiteral("unknown status %1").arg(s);
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
        r.summary = QStringLiteral("Not Windows");
        return r;
    }
    if (!info.error.isEmpty()) {
        r.status = Status::Error;
        r.summary = QStringLiteral("Could not read Windows licence");
        r.details = info.error + QLatin1Char('\n') + info.raw;
        return r;
    }

    const QString oem = info.oemKeyPresent
        ? QStringLiteral("OEM key in firmware: yes %1").arg(info.oemKeyDescription).trimmed()
        : QStringLiteral("OEM key in firmware: no");
    r.details = QStringLiteral("Product: %1\nChannel: %2\nStatus: %3 (%4)\n%5\n\nRaw:\n%6")
                    .arg(info.name, info.description)
                    .arg(info.status)
                    .arg(info.found ? statusText(info.status) : QStringLiteral("no licence installed"),
                         oem, info.raw);

    if (!info.found) {
        r.status = Status::Warn;
        r.summary = QStringLiteral("No Windows licence installed | %1").arg(oem);
        return r;
    }
    if (info.status == 1) {
        r.status = Status::Pass;
        r.summary = QStringLiteral("Activated: %1 | %2").arg(info.name, oem);
        return r;
    }
    r.status = Status::Warn;
    r.summary = QStringLiteral("NOT activated (%1): %2 | %3").arg(statusText(info.status), info.name, oem);
    // Most refurbished PCs can be activated again with the firmware key.
    if (info.oemKeyPresent)
        r.details.prepend(QStringLiteral("Hint: a firmware OEM key exists, activation with it should work.\n\n"));
    return r;
}
