#include "tests/battery/BatteryTest.h"

#include <QtConcurrent>

BatteryTest::BatteryTest(Thresholds thresholds, QObject* parent)
    : ITest(parent), m_thresholds(std::move(thresholds))
{
    connect(&m_watcher, &QFutureWatcher<BatteryInfo>::finished, this, [this] {
        emit finished(evaluate(m_watcher.result(), m_thresholds));
    });
}

void BatteryTest::run()
{
    m_watcher.setFuture(QtConcurrent::run(&readBattery));
}

TestResult BatteryTest::evaluate(const BatteryInfo& info, const Thresholds& thresholds)
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
    if (info.designCapacity <= 0) {
        r.status = Status::Error;
        r.summary = tr("Battery gives no design capacity: health cannot be judged (see details)");
        return r;
    }
    // Without a rule we would silently show green: refuse instead.
    if (!thresholds.has(QStringLiteral("battery_health_pct"))) {
        r.status = Status::Error;
        r.summary = tr("thresholds.json missing or incomplete: copy the whole program folder again");
        return r;
    }

    const double health = info.fullCapacity / info.designCapacity * 100.0;
    r.status = thresholds.evaluate(QStringLiteral("battery_health_pct"), health);
    r.note = QStringLiteral("%1 %").arg(health, 0, 'f', 0);
    r.summary = tr("Health %1% of design capacity").arg(health, 0, 'f', 0);
    if (info.cycles > 0)
        r.summary += tr(", %1 cycles").arg(info.cycles);
    return r;
}
