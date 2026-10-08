#pragma once
#include <QFutureWatcher>
#include "core/ITest.h"
#include "tests/sysinfo/SystemInfo.h"

enum class FormFactor { Unknown, Desktop, Laptop };

class SystemInfoTest : public ITest {
    Q_OBJECT
public:
    explicit SystemInfoTest(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("System info"); }
    void run() override;

    // Pure functions: easy to unit test without any hardware.
    static FormFactor formFactorFromChassis(int smbiosChassisType);
    static TestResult evaluate(const SystemInfo& info);

private:
    QFutureWatcher<SystemInfo> m_watcher;
};
