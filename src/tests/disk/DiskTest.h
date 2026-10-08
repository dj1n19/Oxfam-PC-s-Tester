#pragma once
#include <QFutureWatcher>
#include "core/ITest.h"
#include "core/Thresholds.h"
#include "tests/disk/DiskInfo.h"

// SMART health of every disk, via the bundled smartctl. One row in the
// table; its status is the worst of all disks.
class DiskTest : public ITest {
    Q_OBJECT
public:
    explicit DiskTest(Thresholds thresholds, QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("Disks (SMART)"); }
    void run() override;

    // Pure functions: unit tested with sample smartctl outputs.
    static TestResult evaluateDisk(const SmartctlRun& run, const Thresholds& thresholds);
    static TestResult evaluate(const DiskScan& scan, const Thresholds& thresholds);

private:
    Thresholds m_thresholds;
    QFutureWatcher<DiskScan> m_watcher;
};
