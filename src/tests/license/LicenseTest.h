#pragma once
#include <QFutureWatcher>
#include "core/ITest.h"
#include "tests/license/LicenseInfo.h"

// Windows activation + BitLocker of the system drive. Not activated or
// BitLocker on is a Warn, not a Fail: the technician can fix it, the hardware
// is not bad. Skipped on Linux.
class LicenseTest : public ITest {
    Q_OBJECT
public:
    explicit LicenseTest(QObject* parent = nullptr);

    QString name() const override { return tr("Windows licence"); }
    QString id() const override { return QStringLiteral("windows_licence"); }
    void run() override;

    // Pure function: easy to unit test without Windows.
    static TestResult evaluate(const LicenseInfo& info);

private:
    // The two parts of the row, merged by evaluate() (the worse status wins).
    static TestResult licencePart(const LicenseInfo& info);
    static TestResult bitlockerPart(const LicenseInfo& info);

    QFutureWatcher<LicenseInfo> m_watcher;
};
