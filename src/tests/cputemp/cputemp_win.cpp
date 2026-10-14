#include "tests/cputemp/CpuTempInfo.h"

#include <QElapsedTimer>
#include <QProcess>

#include "tests/common/PowerShell.h"

namespace {

// One reading = one JSON line. The script loops by itself: PowerShell starts
// once, before the load (see sampleCpu() in CpuTempInfo.h).
// Temperature: the real CPU sensor needs a kernel driver on Windows, so we
// read the ACPI thermal zone instead (admin, which the manifest gives us). On
// many PCs (Latitude 7420) the class is "Not supported": catch that and report
// an empty list, so it becomes "no sensor" and not Error. Asked only until it
// fails: WMI is slow to fail, and under load every second counts.
// Speed: the performance counter class, not Get-Counter: Get-Counter wants
// the counter names in the language of Windows (French, Dutch...), the CIM
// class names are always English. Its own try: a missing counter must not
// lose the temperature.
// Battery: BatteryStatus 1 = discharging (on battery), seen on a real Latitude.
// [Console]::Out + Flush: each line reaches the program at once, not when
// PowerShell's output buffer happens to be full. -Depth: zones are 2 levels deep.
// %1 = seconds of readings after the first one.
const char* const kScript =
    "$sw = [Diagnostics.Stopwatch]::StartNew(); $first = $true; $e = ''; "
    "while ($first -or $sw.Elapsed.TotalSeconds -lt %1) { "
    "$z = @(); "
    "if (-not $e) { try { $z = @(Get-CimInstance -Namespace root/wmi -ClassName MSAcpi_ThermalZoneTemperature "
    "| Select-Object InstanceName, CurrentTemperature) } "
    "catch { $e = $_.Exception.Message } }; "
    "$p = $null; $pe = ''; "
    "try { $p = Get-CimInstance Win32_PerfFormattedData_Counters_ProcessorInformation -Filter \"Name='_Total'\" "
    "| Select-Object PercentProcessorPerformance, PercentProcessorUtility } "
    "catch { $pe = $_.Exception.Message }; "
    "$b = @(Get-CimInstance Win32_Battery | Select-Object BatteryStatus); "
    "$j = ConvertTo-Json -InputObject @{ zones = $z; unavailable = $e; cpu = $p; cpuError = $pe; battery = $b } "
    "-Depth 4 -Compress; "
    "[Console]::Out.WriteLine($j); [Console]::Out.Flush(); "
    "if ($first) { $first = $false; $sw.Restart() }; "
    "Start-Sleep -Seconds 1 }";

// PowerShell needs 1-3 s to start (more on an old PC). Worst case for a 30 s
// test: 5 s to start + 12 + 30 + 8 = 55 s, under the runner's 60 s watchdog.
constexpr int kFirstLineMs = 12000;
constexpr int kExtraMs = 8000;   // after the last expected reading

CpuReading failure(const QString& error)
{
    CpuReading r;
    r.error = error;
    return r;
}

void stopProcess(QProcess& ps)
{
    ps.kill();
    ps.waitForFinished();
}

} // namespace

std::vector<CpuReading> sampleCpu(int seconds, const std::function<bool(const CpuReading&)>& startLoad)
{
    QProcess ps;
    if (!startPowerShell(ps, QString::fromLatin1(kScript).arg(seconds)))
        return {failure(QStringLiteral("Could not start PowerShell"))};

    // 1. The reading at rest, before any load.
    QElapsedTimer timer;
    timer.start();
    while (!ps.canReadLine() && ps.state() != QProcess::NotRunning && timer.elapsed() < kFirstLineMs)
        ps.waitForReadyRead(500);
    if (!ps.canReadLine()) {
        const bool running = ps.state() != QProcess::NotRunning;
        stopProcess(ps);
        return {failure(running ? QStringLiteral("PowerShell gave no reading within %1 s").arg(kFirstLineMs / 1000)
                                : QStringLiteral("PowerShell failed (exit code %1): %2")
                                      .arg(ps.exitCode())
                                      .arg(QString::fromLocal8Bit(ps.readAllStandardError()).trimmed()))};
    }
    std::vector<CpuReading> readings{parseWindowsCpu(ps.readLine().trimmed())};
    if (!startLoad(readings.front())) {
        stopProcess(ps);
        return readings;
    }

    // 2. One line per second until the script ends by itself.
    timer.restart();
    for (;;) {
        while (ps.canReadLine()) {
            readings.push_back(parseWindowsCpu(ps.readLine().trimmed()));
            readings.back().atMs = timer.elapsed();
        }
        if (ps.state() == QProcess::NotRunning)
            break;
        if (timer.elapsed() > seconds * 1000LL + kExtraMs) {
            stopProcess(ps);
            readings.push_back(failure(QStringLiteral("PowerShell did not end after %1 s").arg(seconds)));
            return readings;
        }
        ps.waitForReadyRead(500);
    }
    if (ps.exitStatus() != QProcess::NormalExit || ps.exitCode() != 0)
        readings.push_back(failure(QStringLiteral("PowerShell failed (exit code %1): %2")
                                       .arg(ps.exitCode())
                                       .arg(QString::fromLocal8Bit(ps.readAllStandardError()).trimmed())));
    return readings;
}
