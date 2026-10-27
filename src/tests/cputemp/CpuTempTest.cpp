#include "tests/cputemp/CpuTempTest.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <QThread>
#include <QtConcurrent>

namespace {

// 30 s is enough for a dead fan or dried paste to show (the CPU reaches its
// limit in seconds), and keeps the test well under the 60 s watchdog.
constexpr int kLoadSeconds = 30;

// Under full load a CPU sensor always moves by more than this: +20 C in 30 s
// on Arch (coretemp). A sensor that barely moves is not measuring the CPU:
// fixed ACPI zone (ThinkPad L380), or a case/board zone that warms slowly
// (Surface Pro 7 \_SB._SAN.TZ01: +1 to +3 C at 220 % speed, 7 PCs, 2026-10-27).
constexpr double kMinRiseC = 5.0;

// The first seconds of the load do not count for the speed: the cores are
// still waking up. Real Latitude 7420: 266 % of base from the start, then a
// slow fall to ~240 % as Intel's turbo budget (~28 s) runs out: normal, not
// throttling. Counted in time, not in readings: under full load a Windows
// reading can take several seconds, and "the first 5 readings" then left
// nothing at all (real run: "Speeds under load:" empty).
constexpr qint64 kSettleMs = 5000;

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
    // Plain QThreads, not the QtConcurrent pool: this function already runs
    // in that pool. Low priority so the readings still get CPU time.
    std::atomic<bool> stop{false};
    std::atomic<double> sink{0};
    std::vector<std::unique_ptr<QThread>> workers;
    int threads = 0;
    // Called by sampleCpu() after the reading at rest: load only if there is
    // something to watch (do not heat the CPU for nothing).
    const auto startLoad = [&](const CpuReading& idle) {
        if (!idle.error.isEmpty() || (!idle.hasTemp && !idle.hasSpeed))
            return false;
        threads = std::max(1, QThread::idealThreadCount());
        for (int i = 0; i < threads; ++i) {
            workers.emplace_back(QThread::create(burn, std::cref(stop), std::ref(sink)));
            workers.back()->start(QThread::LowPriority);
        }
        return true;
    };
    const std::vector<CpuReading> readings = sampleCpu(seconds, startLoad);

    stop.store(true);
    for (auto& w : workers)
        w->wait();   // a QThread must be finished before it is deleted

    CpuLoadRun run = collect(readings);
    run.seconds = seconds;
    run.threads = threads;
    return run;
}

CpuLoadRun CpuTempTest::collect(const std::vector<CpuReading>& readings)
{
    CpuLoadRun run;
    if (readings.empty()) {   // sampleCpu() always gives the reading at rest; never crash anyway
        run.idle.error = QStringLiteral("No reading at all");
        return run;
    }
    run.idle = readings.front();
    run.onBattery = run.idle.onBattery;
    for (std::size_t i = 1; i < readings.size(); ++i) {
        const CpuReading& r = readings[i];
        ++run.loadReadings;
        run.lastMs = r.atMs;
        // Every reading that does not give what the one at rest gave is listed:
        // an empty result must always come with a reason in the details.
        if (!r.error.isEmpty()) {
            run.sampleErrors += r.error + QLatin1Char('\n');
            continue;
        }
        if (r.hasTemp)
            run.samples.push_back(r.celsius);
        else if (run.idle.hasTemp)
            run.sampleErrors += QStringLiteral("no temperature at %1 s\n").arg(r.atMs / 1000.0, 0, 'f', 1);
        if (r.hasSpeed) {
            if (r.atMs > kSettleMs)
                run.speeds.push_back(r.speedPct);
        } else if (run.idle.hasSpeed) {
            run.sampleErrors += QStringLiteral("no speed at %1 s: %2\n").arg(r.atMs / 1000.0, 0, 'f', 1).arg(r.raw);
        }
        run.onBattery = run.onBattery || r.onBattery;
    }
    return run;
}

TestResult CpuTempTest::evaluate(const CpuLoadRun& run, const Thresholds& thresholds)
{
    TestResult r;
    const CpuReading& idle = run.idle;

    if (!idle.error.isEmpty()) {
        r.status = Status::Error;
        r.summary = tr("Could not read the CPU temperature (see details)");
        r.details = idle.error + QLatin1Char('\n') + idle.raw;
        return r;
    }
    if (!idle.hasTemp && !idle.hasSpeed) {
        r.status = Status::Skipped;
        r.summary = tr("No CPU temperature sensor or speed counter readable on this PC");
        r.details = idle.raw;
        return r;
    }

    r.details = QStringLiteral("Load: %1 threads, %2 s\nReadings under load: %3, the last at %4 s\n")
                    .arg(run.threads).arg(run.seconds).arg(run.loadReadings).arg(run.lastMs / 1000.0, 0, 'f', 1);
    if (idle.hasTemp) {
        r.details += QStringLiteral("Sensor: %1\nIdle: %2 C\nTemperatures (C):")
                         .arg(idle.source).arg(idle.celsius, 0, 'f', 1);
        for (double c : run.samples)
            r.details += QStringLiteral(" %1").arg(c, 0, 'f', 1);
        r.details += QLatin1Char('\n');
    }
    if (idle.hasSpeed) {
        r.details += QStringLiteral("Idle speed: %1 % of base\nSpeeds under load (% of base, first %2 s left out):")
                         .arg(idle.speedPct, 0, 'f', 0).arg(kSettleMs / 1000);
        for (double p : run.speeds)
            r.details += QStringLiteral(" %1").arg(p, 0, 'f', 0);
        r.details += QLatin1Char('\n');
    }
    if (run.onBattery)
        r.details += QStringLiteral("On battery during the test\n");
    if (!run.sampleErrors.isEmpty())
        r.details += QStringLiteral("Failed readings:\n") + run.sampleErrors;
    r.details += QStringLiteral("\nAt rest:\n") + idle.raw;

    // Each half gives a status and a part of the summary; the worse status wins.
    r.status = Status::Skipped;
    QStringList parts;
    if (idle.hasTemp) {
        const TestResult temp = judgeTemperature(run, thresholds);
        r.status = worse(r.status, temp.status);
        parts << temp.summary;
    }
    if (idle.hasSpeed) {
        const TestResult speed = judgeSpeed(run, thresholds);
        r.status = worse(r.status, speed.status);
        parts << speed.summary;
    }
    r.summary = parts.join(QStringLiteral(", "));
    r.summary[0] = r.summary[0].toUpper();   // "speed ..." alone starts the line
    return r;
}

TestResult CpuTempTest::judgeTemperature(const CpuLoadRun& run, const Thresholds& thresholds)
{
    TestResult r;
    if (run.samples.empty()) {
        r.status = Status::Error;
        r.summary = tr("No temperature reading during the load test (see details)");
        return r;
    }
    // Without a rule we would silently show green: refuse instead.
    if (!thresholds.has(QStringLiteral("cpu_temp_peak_c"))) {
        r.status = Status::Error;
        r.summary = tr("thresholds.json missing or incomplete: copy the whole program folder again");
        return r;
    }

    const double idle = run.idle.celsius;
    const double peak = std::max(idle, *std::max_element(run.samples.begin(), run.samples.end()));
    r.status = thresholds.evaluate(QStringLiteral("cpu_temp_peak_c"), peak);
    r.summary = tr("Peak %1 C under load (idle %2 C)").arg(peak, 0, 'f', 0).arg(idle, 0, 'f', 0);

    // Too hot is reported whatever the sensor. But "cool" from a sensor that
    // ignores the load proves nothing: no green for it.
    if (r.status == Status::Pass && peak - idle < kMinRiseC) {
        r.status = Status::Skipped;
        r.summary = tr("Sensor barely reacted to load (%1 C): not the CPU, cannot judge").arg(peak, 0, 'f', 0);
    }
    return r;
}

TestResult CpuTempTest::judgeSpeed(const CpuLoadRun& run, const Thresholds& thresholds)
{
    TestResult r;
    if (run.speeds.empty()) {
        r.status = Status::Error;
        r.summary = tr("No speed reading during the load test (see details)");
        return r;
    }
    if (!thresholds.has(QStringLiteral("cpu_speed_min_pct"))) {
        r.status = Status::Error;
        r.summary = tr("thresholds.json missing or incomplete: copy the whole program folder again");
        return r;
    }

    // Overheating lasts: once too hot, the CPU stays slow (throttles below its
    // base speed). A single reading is a snapshot and can dip for a moment
    // (real Arch run: 220 % with dips to 162 %), so we take the median of the
    // second half, the hottest part: one dip cannot move it.
    std::vector<double> late(run.speeds.begin() + static_cast<std::ptrdiff_t>(run.speeds.size() / 2),
                             run.speeds.end());
    std::sort(late.begin(), late.end());
    const double typical = late[late.size() / 2];
    r.status = thresholds.evaluate(QStringLiteral("cpu_speed_min_pct"), typical);
    if (r.status == Status::Pass) {
        r.summary = tr("speed %1 % of base").arg(typical, 0, 'f', 0);
    } else if (run.onBattery) {
        // Windows and Linux slow the CPU on battery on purpose: never a Fail.
        r.status = Status::Warn;
        r.summary = tr("slowed to %1 % of base speed on battery: plug in the charger and press START again")
                        .arg(typical, 0, 'f', 0);
    } else {
        r.summary = tr("slowed to %1 % of base speed under load: overheating? (fan, dust, thermal paste)")
                        .arg(typical, 0, 'f', 0);
    }
    return r;
}
