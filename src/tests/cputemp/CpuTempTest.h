#pragma once
#include <vector>
#include <QFutureWatcher>
#include "core/ITest.h"
#include "core/Thresholds.h"
#include "tests/cputemp/CpuTempInfo.h"

// What the load test measured: one reading at rest, then readings taken
// while every core is busy.
struct CpuLoadRun {
    CpuReading idle;
    std::vector<double> samples;   // temperatures (C), during the load
    std::vector<double> speeds;    // % of base speed, during the load (first seconds left out)
    bool onBattery = false;        // at least one reading was on battery
    QString sampleErrors;          // readings that failed during the load
    int loadReadings = 0;          // readings taken during the load (good or not)
    qint64 lastMs = 0;             // when the last one came, ms after the load started
    int threads = 0;
    int seconds = 0;
};

class CpuTempTest : public ITest {
    Q_OBJECT
public:
    explicit CpuTempTest(Thresholds thresholds, QObject* parent = nullptr);

    // Not "temperature": on Windows only the speed is judged.
    QString name() const override { return tr("CPU under load"); }
    bool runsLast() const override { return true; }   // ~30 s of full load: after the interactive tests
    void run() override;

    // Blocking (~30 s): loads every core and samples the temperature.
    static CpuLoadRun runLoad(int seconds);

    // Pure: sorts sampleCpu()'s readings (first = at rest) into a CpuLoadRun.
    static CpuLoadRun collect(const std::vector<CpuReading>& readings);

    // Pure function: easy to unit test without any hardware.
    static TestResult evaluate(const CpuLoadRun& run, const Thresholds& thresholds);

private:
    // The two halves of evaluate(): status + one part of the summary each.
    static TestResult judgeTemperature(const CpuLoadRun& run, const Thresholds& thresholds);
    static TestResult judgeSpeed(const CpuLoadRun& run, const Thresholds& thresholds);

    Thresholds m_thresholds;
    QFutureWatcher<CpuLoadRun> m_watcher;
};
