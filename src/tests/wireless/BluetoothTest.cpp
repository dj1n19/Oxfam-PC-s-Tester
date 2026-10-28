#include "tests/wireless/BluetoothTest.h"

#include <QtConcurrent>

BluetoothTest::BluetoothTest(QObject* parent) : ITest(parent)
{
    connect(&m_watcher, &QFutureWatcher<std::pair<BluetoothInfo, FormFactor>>::finished, this, [this] {
        const auto& [info, form] = m_watcher.result();
        emit finished(evaluate(info, form));
    });
}

void BluetoothTest::run()
{
    m_watcher.setFuture(QtConcurrent::run([] {
        return std::make_pair(readBluetooth(), SystemInfoTest::formFactorFromChassis(readSystemInfo().chassisType));
    }));
}

TestResult BluetoothTest::evaluate(const BluetoothInfo& info, FormFactor form)
{
    TestResult r;
    r.details = info.raw;

    if (!info.error.isEmpty()) {
        r.status = Status::Error;
        r.summary = tr("Could not read the Bluetooth adapter (see details)");
        r.details = info.error + QLatin1Char('\n') + info.raw;
        return r;
    }
    if (!info.present) {
        // Warn, not Fail: some older laptops were sold without Bluetooth.
        if (form == FormFactor::Laptop) {
            r.status = Status::Warn;
            r.summary = tr("No Bluetooth found: none on this model, or its driver is not installed");
        } else {
            r.status = Status::Skipped;
            r.summary = tr("No Bluetooth (desktop?)");
        }
        return r;
    }
    if (info.radioOff) {
        r.status = Status::Warn;
        r.summary = tr("Bluetooth is off (key, switch or airplane mode): turn it on and run this test again");
        return r;
    }
    r.status = Status::Pass;
    r.summary = info.name.isEmpty() ? tr("Bluetooth adapter found") : tr("Bluetooth adapter found: %1").arg(info.name);
    return r;
}
