#pragma once
#include <vector>
#include <QFutureWatcher>
#include "core/ITest.h"
#include "core/Thresholds.h"
#include "tests/cputemp/CpuTempInfo.h"

// What the load test measured: one reading at rest, then readings taken
// while every core is busy.
struct CpuLoadRun {
    CpuTempReading idle;
    std::vector<double> samples;   // C, during the load
    QString sampleErrors;          // readings that failed during the load
    int threads = 0;
    int seconds = 0;
};

class CpuTempTest : public ITest {
    Q_OBJECT
public:
    explicit CpuTempTest(Thresholds thresholds, QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("CPU temperature"); }
    void run() override;

    // Blocking (~30 s): loads every core and samples the temperature.
    static CpuLoadRun runLoad(int seconds);

    // Pure function: easy to unit test without any hardware.
    static TestResult evaluate(const CpuLoadRun& run, const Thresholds& thresholds);

private:
    Thresholds m_thresholds;
    QFutureWatcher<CpuLoadRun> m_watcher;
};
