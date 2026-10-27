#include "tests/battery/ChargeTest.h"

#include <QtConcurrent>

namespace {

// Fixed rules, not thresholds to tune: a battery this empty is in its slow
// pre-charge (a ThinkPad 13 at 0 % waits ~10 min before charging, developer's
// report 2026-10-27), one this full may stop charging on purpose.
constexpr double kEmptyPct = 5;
constexpr double kFullPct = 95;

} // namespace

ChargeTest::ChargeTest(QObject* parent) : ITest(parent)
{
    connect(&m_watcher, &QFutureWatcher<BatteryInfo>::finished, this, [this] {
        emit finished(evaluate(m_watcher.result()));
    });
}

void ChargeTest::run()
{
    m_watcher.setFuture(QtConcurrent::run(&readBattery));
}

TestResult ChargeTest::evaluate(const BatteryInfo& info)
{
    TestResult r;
    r.details = info.raw;

    if (!info.error.isEmpty()) {
        r.status = Status::Error;
        r.summary = tr("Could not read the battery (see details)");
        r.details = info.error + QLatin1Char('\n') + info.raw;
        return r;
    }
    if (!info.present) {
        r.status = Status::Skipped;
        r.summary = tr("No battery (desktop?)");
        return r;
    }
    if (info.acOnline < 0) {
        r.status = Status::Error;
        r.summary = tr("The system does not say whether a charger is connected (see details)");
        return r;
    }
    // Fail, not Error: a charger that is plugged in but not seen means a bad
    // socket, cable or charger. If it was simply not plugged in, the summary
    // says how to check again.
    if (info.acOnline == 0) {
        r.status = Status::Fail;
        r.summary = tr("No charger detected: plug it in and run this test again. "
                       "If it is plugged in: socket, cable or charger faulty");
        return r;
    }

    const QString pct = info.chargePct >= 0 ? QStringLiteral("%1 %").arg(info.chargePct, 0, 'f', 0) : tr("level unknown");
    // From here the charger is seen: socket, cable and charger work. What is
    // left to know is whether the battery takes the charge.
    switch (info.state) {
    case ChargeState::Charging:
        r.status = Status::Pass;
        r.summary = info.rateW > 0 ? tr("Charging at %1 W (%2)").arg(info.rateW, 0, 'f', 1).arg(pct)
                                   : tr("Charging (%1)").arg(pct);
        return r;
    case ChargeState::Discharging:
        // On the charger and still emptying: the charger is too weak for this PC.
        r.status = Status::Warn;
        r.summary = tr("Charger detected but the battery is discharging (%1): charger too weak for this PC?").arg(pct);
        return r;
    case ChargeState::Full:
    case ChargeState::NotCharging:
    case ChargeState::Unknown:
        break;
    }
    if (info.state == ChargeState::Full || info.chargePct >= kFullPct) {
        r.status = Status::Pass;
        r.summary = tr("Charger detected, battery full (%1)").arg(pct);
    } else if (info.chargePct >= 0 && info.chargePct <= kEmptyPct) {
        r.status = Status::Warn;
        r.summary = tr("Charger detected, empty battery not charging yet (%1): normal for a few minutes, "
                       "run this test again in 10 minutes").arg(pct);
    } else {
        // Many ex-company laptops stop charging at 60-80 % on purpose.
        r.status = Status::Pass;
        r.summary = tr("Charger detected, battery not charging (%1): charge limit set in the BIOS or vendor app?")
                        .arg(pct);
    }
    return r;
}
