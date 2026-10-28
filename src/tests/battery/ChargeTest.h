#pragma once
#include <QFutureWatcher>
#include "core/ITest.h"
#include "tests/battery/BatteryInfo.h"

// Does the PC see its charger, and does the battery take the charge?
// Same probe as BatteryTest (readBattery()), judged on other values.
class ChargeTest : public ITest {
    Q_OBJECT
public:
    explicit ChargeTest(QObject* parent = nullptr);

    QString name() const override { return tr("Charger"); }
    QString id() const override { return QStringLiteral("charger"); }
    void run() override;

    // Pure function: easy to unit test without any hardware.
    static TestResult evaluate(const BatteryInfo& info);

private:
    QFutureWatcher<BatteryInfo> m_watcher;
};
