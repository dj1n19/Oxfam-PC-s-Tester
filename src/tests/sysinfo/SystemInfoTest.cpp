#include "tests/sysinfo/SystemInfoTest.h"

#include <cmath>

#include <QStringList>

namespace {

// English: only in the details (the report), not in the panel.
QString formFactorName(FormFactor f)
{
    switch (f) {
    case FormFactor::Desktop: return QStringLiteral("Desktop");
    case FormFactor::Laptop:  return QStringLiteral("Laptop");
    case FormFactor::Unknown: break;
    }
    return QStringLiteral("Unknown");
}

} // namespace

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
        r.summary = tr("Could not read system info (see details)");
        r.details = info.error + QLatin1Char('\n') + info.raw;
        return r;
    }

    // Model, CPU and RAM are always readable without privileges. If one is
    // missing, the probe or its parsing is wrong: never show that as green.
    QStringList missing;
    if (info.model.isEmpty()) missing << tr("model");
    if (info.cpu.isEmpty())   missing << tr("CPU");
    if (info.ramBytes <= 0)   missing << tr("RAM");

    r.note = QStringLiteral("%1 %2").arg(info.vendor, info.model).simplified();

    // Two lines for the panel: which PC (with its serial, to copy), then
    // what it is made of. The form factor is only in the details (report).
    const QString identity = QStringLiteral("%1 %2").arg(info.vendor, info.model).simplified();
    // The serial is optional (developer's choice): on Linux it is root-only,
    // and the app runs as the normal user. Shown as unknown, with the reason.
    const QString serial = info.serial.isEmpty() ? tr("serial unknown") : tr("S/N %1").arg(info.serial);

    const double gib = info.ramBytes / (1024.0 * 1024.0 * 1024.0);
    QString cpu = info.cpu;
    if (info.cores > 0)
        cpu += QStringLiteral(" (%1)").arg(tr("%1 cores / %2 threads").arg(info.cores).arg(info.threads));
    else if (info.threads > 0)
        cpu += QStringLiteral(" (%1)").arg(tr("%1 threads").arg(info.threads));
    QStringList hardware{info.os, cpu, tr("%1 GiB RAM").arg(gib, 0, 'f', 1)};

    // Diagonal from the monitor's own size (EDID). Some drivers give nothing
    // or nonsense: then the resolution alone.
    const double inches = std::hypot(info.screenMm.width(), info.screenMm.height()) / 25.4;
    if (!info.screenPixels.isEmpty()) {
        hardware << (inches >= 7 && inches <= 40
                         ? tr("screen %1\" %2x%3").arg(inches, 0, 'f', 1)
                               .arg(info.screenPixels.width()).arg(info.screenPixels.height())
                         : tr("screen %1x%2").arg(info.screenPixels.width()).arg(info.screenPixels.height()));
    }
    hardware.removeAll(QString());

    const FormFactor form = formFactorFromChassis(info.chassisType);
    const QString screen = QStringLiteral("Screen: %1 x %2 mm, %3 x %4 px")
                               .arg(info.screenMm.width()).arg(info.screenMm.height())
                               .arg(info.screenPixels.width()).arg(info.screenPixels.height());
    const QString serialLine = info.serial.isEmpty()
        ? QStringLiteral("Serial not read: %1\n(Linux: /sys/class/dmi/id/product_serial "
                         "is readable by root only.)").arg(info.serialError)
        : QStringLiteral("Serial: %1").arg(info.serial);
    r.details = QStringLiteral("Form factor: %1 (SMBIOS chassis type %2)\n%3\n%4\n\n%5")
                    .arg(formFactorName(form)).arg(info.chassisType).arg(serialLine, screen, info.raw);

    const QString summary = QStringList{identity, serial}.join(QStringLiteral(" | "))
                          + QLatin1Char('\n') + hardware.join(QStringLiteral(" | "));

    if (!missing.isEmpty()) {
        r.status = Status::Error;
        r.summary = tr("Missing: %1 | %2").arg(missing.join(", "), summary);
        return r;
    }

    r.status = Status::Pass;
    r.summary = summary;
    return r;
}
