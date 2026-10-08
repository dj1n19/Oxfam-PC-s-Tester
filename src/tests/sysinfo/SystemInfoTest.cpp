#include "tests/sysinfo/SystemInfoTest.h"

#include <QStringList>
#include <QtConcurrent>

namespace {

QString formFactorName(FormFactor f)
{
    switch (f) {
    case FormFactor::Desktop: return QStringLiteral("Desktop");
    case FormFactor::Laptop:  return QStringLiteral("Laptop");
    case FormFactor::Unknown: break;
    }
    return QStringLiteral("Unknown form factor");
}

} // namespace

SystemInfoTest::SystemInfoTest(QObject* parent) : ITest(parent)
{
    connect(&m_watcher, &QFutureWatcher<SystemInfo>::finished, this, [this] {
        emit finished(evaluate(m_watcher.result()));
    });
}

void SystemInfoTest::run()
{
    m_watcher.setFuture(QtConcurrent::run(&readSystemInfo));
}

// SMBIOS spec, "System Enclosure or Chassis Types". Linux and Windows both
// give us this same number, so the mapping lives here and not per OS.
FormFactor SystemInfoTest::formFactorFromChassis(int t)
{
    switch (t) {
    case 3: case 4: case 5: case 6: case 7:          // desktop, low profile, pizza box, (mini) tower
    case 13: case 15: case 16: case 24: case 35: case 36:   // all-in-one, space-saving, lunch box, sealed, mini PC, stick
        return FormFactor::Desktop;
    case 8: case 9: case 10: case 11: case 14:       // portable, laptop, notebook, hand held, sub notebook
    case 30: case 31: case 32:                       // tablet, convertible, detachable
        return FormFactor::Laptop;
    default:
        return FormFactor::Unknown;                  // 1 Other, 2 Unknown, servers...
    }
}

TestResult SystemInfoTest::evaluate(const SystemInfo& info)
{
    TestResult r;
    if (!info.error.isEmpty()) {
        r.status = Status::Error;
        r.summary = QStringLiteral("Could not read system info");
        r.details = info.error + QLatin1Char('\n') + info.raw;
        return r;
    }

    // Model, CPU and RAM are always readable without privileges. If one is
    // missing, the probe or its parsing is wrong: never show that as green.
    QStringList missing;
    if (info.model.isEmpty()) missing << QStringLiteral("model");
    if (info.cpu.isEmpty())   missing << QStringLiteral("CPU");
    if (info.ramBytes <= 0)   missing << QStringLiteral("RAM");

    const double gib = info.ramBytes / (1024.0 * 1024.0 * 1024.0);
    QStringList parts;
    parts << QStringLiteral("%1 %2").arg(info.vendor, info.model).trimmed()
          << info.cpu
          << QStringLiteral("%1 GiB RAM").arg(gib, 0, 'f', 1)
          << formFactorName(formFactorFromChassis(info.chassisType));
    parts.removeAll(QString());

    // The serial is optional (developer's choice): on Linux it is root-only,
    // and the app runs as the normal user. Shown as unknown, with the reason.
    if (info.serial.isEmpty()) {
        parts << QStringLiteral("serial unknown");
        r.details = QStringLiteral("Serial not read: %1\n(Linux: /sys/class/dmi/id/product_serial "
                                   "is readable by root only.)\n\n%2")
                        .arg(info.serialError, info.raw);
    } else {
        r.details = QStringLiteral("Serial: %1\n\n%2").arg(info.serial, info.raw);
    }

    if (!missing.isEmpty()) {
        r.status = Status::Error;
        r.summary = QStringLiteral("Missing: %1 | %2").arg(missing.join(", "), parts.join(" | "));
        return r;
    }

    r.status = Status::Pass;
    r.summary = parts.join(" | ");
    return r;
}
