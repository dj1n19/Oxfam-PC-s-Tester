#pragma once
#include <QFutureWatcher>
#include <utility>
#include "core/ITest.h"
#include "tests/sysinfo/SystemInfoTest.h"
#include "tests/wireless/WirelessInfo.h"

// Is there a Bluetooth adapter, and is it on? Devices nearby are not
// searched: slow, and it depends on what lies around the workshop.
class BluetoothTest : public ITest {
    Q_OBJECT
public:
    explicit BluetoothTest(QObject* parent = nullptr);

    QString name() const override { return tr("Bluetooth"); }
    void run() override;

    // Pure function: easy to unit test without any hardware.
    static TestResult evaluate(const BluetoothInfo& info, FormFactor form);

private:
    QFutureWatcher<std::pair<BluetoothInfo, FormFactor>> m_watcher;
};
