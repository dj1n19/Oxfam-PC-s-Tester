#include "tests/cputemp/CpuTempTest.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <QElapsedTimer>
#include <QThread>
#include <QtConcurrent>

namespace {

// 30 s is enough for a dead fan or dried paste to show (the CPU reaches its
// limit in seconds), and keeps the test well under the 60 s watchdog.
constexpr int kLoadSeconds = 30;

// Under full load a CPU sensor always moves by more than this. A value that
// does not move is not measuring the CPU (fixed ACPI zone on some PCs).
constexpr double kMinRiseC = 1.0;

// Keeps one core busy until stop is set. The result goes to an atomic so the
// compiler cannot remove the loop as "useless".
void burn(const std::atomic<bool>& stop, std::atomic<double>& sink)
{
    double x = 1.0;
    while (!stop.load(std::memory_order_relaxed)) {
        for (int i = 0; i < 100000; ++i)
            x = std::sqrt(x + i);
    }
    sink.store(x, std::memory_order_relaxed);
}

} // namespace

CpuTempTest::CpuTempTest(Thresholds thresholds, QObject* parent)
    : ITest(parent), m_thresholds(std::move(thresholds))
{
    connect(&m_watcher, &QFutureWatcher<CpuLoadRun>::finished, this, [this] {
        emit finished(evaluate(m_watcher.result(), m_thresholds));
    });
}

void CpuTempTest::run()
{
    m_watcher.setFuture(QtConcurrent::run(&CpuTempTest::runLoad, kLoadSeconds));
}

CpuLoadRun CpuTempTest::runLoad(int seconds)
{
    CpuLoadRun run;
    run.seconds = seconds;
    run.idle = readCpuTemperature();
    if (!run.idle.error.isEmpty() || !run.idle.available)
        return run;   // nothing to watch: do not heat the CPU for nothing

    // Plain QThreads, not the QtConcurrent pool: this function already runs
    // in that pool. Low priority so the temperature readings (PowerShell on
    // Windows) still get CPU time while every core is busy.
    std::atomic<bool> stop{false};
    std::atomic<double> sink{0};
    std::vector<std::unique_ptr<QThread>> workers;
    run.threads = std::max(1, QThread::idealThreadCount());
    for (int i = 0; i < run.threads; ++i) {
        workers.emplace_back(QThread::create(burn, std::cref(stop), std::ref(sink)));
        workers.back()->start(QThread::LowPriority);
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < seconds * 1000LL) {
        QThread::msleep(1000);   // worker thread: sleeping here never blocks the UI
        const CpuTempReading r = readCpuTemperature();
        if (r.error.isEmpty() && r.available)
            run.samples.push_back(r.celsius);
        else
            run.sampleErrors += (r.error.isEmpty() ? QStringLiteral("no reading") : r.error) + QLatin1Char('\n');
    }

    stop.store(true);
    for (auto& w : workers)
        w->wait();   // a QThread must be finished before it is deleted
    return run;
}

TestResult CpuTempTest::evaluate(const CpuLoadRun& run, const Thresholds& thresholds)
{
    TestResult r;
    const CpuTempReading& idle = run.idle;

    if (!idle.error.isEmpty()) {
        r.status = Status::Error;
        r.summary = QStringLiteral("Could not read CPU temperature");
        r.details = idle.error + QLatin1Char('\n') + idle.raw;
        return r;
    }
    if (!idle.available) {
        r.status = Status::Skipped;
        r.summary = QStringLiteral("No CPU temperature sensor readable on this PC");
        r.details = idle.raw;
        return r;
    }

    r.details = QStringLiteral("Sensor: %1\nIdle: %2 C\nLoad: %3 threads, %4 s\nSamples (C):")
                    .arg(idle.source).arg(idle.celsius, 0, 'f', 1).arg(run.threads).arg(run.seconds);
    for (double c : run.samples)
        r.details += QStringLiteral(" %1").arg(c, 0, 'f', 1);
    if (!run.sampleErrors.isEmpty())
        r.details += QStringLiteral("\nFailed readings:\n") + run.sampleErrors;
    r.details += QStringLiteral("\n\nAt rest:\n") + idle.raw;

    if (run.samples.empty()) {
        r.status = Status::Error;
        r.summary = QStringLiteral("No temperature reading during the load test");
        return r;
    }
    // Without a rule we would silently show green: refuse instead.
    if (!thresholds.has(QStringLiteral("cpu_temp_peak_c"))) {
        r.status = Status::Error;
        r.summary = QStringLiteral("No threshold configured (thresholds.json)");
        return r;
    }

    const double peak = std::max(idle.celsius, *std::max_element(run.samples.begin(), run.samples.end()));
    const double rise = peak - idle.celsius;
    r.status = thresholds.evaluate(QStringLiteral("cpu_temp_peak_c"), peak);
    r.summary = QStringLiteral("Peak %1 C under load (idle %2 C)")
                    .arg(peak, 0, 'f', 0).arg(idle.celsius, 0, 'f', 0);

    // Too hot is reported whatever the sensor. But "cool" from a sensor that
    // ignores the load proves nothing: no green for it.
    if (r.status == Status::Pass && rise < kMinRiseC) {
        r.status = Status::Skipped;
        r.summary = QStringLiteral("Sensor did not react to load (%1 C): not the CPU, cannot judge")
                        .arg(peak, 0, 'f', 0);
    }
    return r;
}
