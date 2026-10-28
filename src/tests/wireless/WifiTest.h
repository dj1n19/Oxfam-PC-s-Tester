#pragma once
#include <QFutureWatcher>
#include <utility>
#include "core/ITest.h"
#include "tests/sysinfo/SystemInfoTest.h"
#include "tests/wireless/WirelessInfo.h"

// Is there a Wi-Fi adapter, is it on, does it see networks? No network seen
// with the radio on usually means an antenna cable left unplugged after a
// screen or keyboard was changed.
class WifiTest : public ITest {
    Q_OBJECT
public:
    explicit WifiTest(QObject* parent = nullptr);

    QString name() const override { return tr("Wi-Fi"); }
    QString id() const override { return QStringLiteral("wifi"); }
    void run() override;

    // Pure function: easy to unit test without any hardware. The form factor
    // decides whether a missing adapter is a fault (laptop) or normal (desktop).
    static TestResult evaluate(const WifiInfo& info, FormFactor form);

private:
    QFutureWatcher<std::pair<WifiInfo, FormFactor>> m_watcher;
};
