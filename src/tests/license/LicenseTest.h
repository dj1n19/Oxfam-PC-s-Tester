#pragma once
#include <QFutureWatcher>
#include "core/ITest.h"
#include "tests/license/LicenseInfo.h"

// Windows activation. Not activated is a Warn, not a Fail: the technician
// can fix it, the hardware is not bad. Skipped on Linux.
class LicenseTest : public ITest {
    Q_OBJECT
public:
    explicit LicenseTest(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("Windows licence"); }
    void run() override;

    // Pure function: easy to unit test without Windows.
    static TestResult evaluate(const LicenseInfo& info);

private:
    QFutureWatcher<LicenseInfo> m_watcher;
};
