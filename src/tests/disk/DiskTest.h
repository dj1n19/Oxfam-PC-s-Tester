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

    QString name() const override { return tr("Disks (SMART)"); }
    QString id() const override { return QStringLiteral("disk"); }
    void run() override;

    // Pure functions: unit tested with sample smartctl outputs.
    static TestResult evaluateDisk(const SmartctlRun& run, const Thresholds& thresholds);
    static TestResult evaluate(const DiskScan& scan, const Thresholds& thresholds);

private:
    static TestResult evaluateDisks(const DiskScan& scan, const Thresholds& thresholds);   // evaluate() minus the USB note

    Thresholds m_thresholds;
    QFutureWatcher<DiskScan> m_watcher;
};
