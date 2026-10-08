#pragma once
#include <QFutureWatcher>
#include "core/ITest.h"
#include "tests/drivers/DriverInfo.h"

// Devices with a missing or failing driver. A driver problem is a Warn,
// not a Fail: the technician can usually fix it, the hardware is not bad.
class DriversTest : public ITest {
    Q_OBJECT
public:
    explicit DriversTest(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("Drivers"); }
    void run() override;

    // Pure function: easy to unit test without any hardware.
    static TestResult evaluate(const DriverInfo& info);

private:
    QFutureWatcher<DriverInfo> m_watcher;
};
