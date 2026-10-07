#pragma once
#include <QFutureWatcher>
#include "core/ITest.h"
#include "core/Thresholds.h"
#include "tests/battery/BatteryInfo.h"

class BatteryTest : public ITest {
    Q_OBJECT
public:
    explicit BatteryTest(Thresholds thresholds, QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("Battery"); }
    void run() override;

    // Pure function: easy to unit test without any hardware.
    static TestResult evaluate(const BatteryInfo& info, const Thresholds& thresholds);

private:
    Thresholds m_thresholds;
    QFutureWatcher<BatteryInfo> m_watcher;
};
