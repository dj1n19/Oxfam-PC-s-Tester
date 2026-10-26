#include "tests/cputemp/CpuTempInfo.h"

#include <QElapsedTimer>
#include <QThread>

#include <cstddef>
#include <memory>

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

// No PowerShell and no WMI: PowerShell took > 10 s to start under full load
// and its first counter query > 12 s just after start-up (2026-10-15). PDH
// reads the same performance counters in-process, in milliseconds.
// Speed: "% Processor Performance" = what WMI called PercentProcessorPerformance.
// Temperature: "Thermal Zone Information" = the ACPI thermal zones that WMI's
// MSAcpi_ThermalZoneTemperature gave (on many PCs a motherboard zone, or none).
// PdhAddEnglishCounter: counter names are translated on French/Dutch Windows,
// the English ones always work.

namespace {

constexpr DWORD kSampleMs = 1000;

QString hex(PDH_STATUS status)
{
    return QStringLiteral("0x%1").arg(quint32(status), 8, 16, QLatin1Char('0'));
}

bool valid(DWORD counterStatus)
{
    return counterStatus == PDH_CSTATUS_VALID_DATA || counterStatus == PDH_CSTATUS_NEW_DATA;
}

CpuReading failure(const QString& error)
{
    CpuReading r;
    r.error = error;
    return r;
}

// Every instance of a wildcard counter ("Thermal Zone Information(*)").
std::vector<ThermalZone> readZones(PDH_HCOUNTER counter)
{
    std::vector<ThermalZone> zones;
    DWORD size = 0;
    DWORD count = 0;
    // First call: size only.
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, nullptr) != PDH_STATUS(PDH_MORE_DATA))
        return zones;
    std::vector<std::byte> buffer(size);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, items) != ERROR_SUCCESS)
        return zones;
    for (DWORD i = 0; i < count; ++i) {
        if (valid(items[i].FmtValue.CStatus))
            zones.push_back({QString::fromWCharArray(items[i].szName), items[i].FmtValue.doubleValue});
    }
    return zones;
}

// One reading: collects every counter of the query once more.
CpuReading readOnce(PDH_HQUERY query, PDH_HCOUNTER speed, PDH_HCOUNTER thermal)
{
    const PDH_STATUS status = PdhCollectQueryData(query);
    if (status != ERROR_SUCCESS)
        return failure(QStringLiteral("PdhCollectQueryData failed (%1)").arg(hex(status)));

    double speedPct = 0;
    PDH_FMT_COUNTERVALUE value{};
    if (speed && PdhGetFormattedCounterValue(speed, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS
        && valid(value.CStatus))
        speedPct = value.doubleValue;

    const std::vector<ThermalZone> zones = thermal ? readZones(thermal) : std::vector<ThermalZone>{};

    // ACLineStatus 0 = not on the charger.
    SYSTEM_POWER_STATUS power{};
    const bool onBattery = GetSystemPowerStatus(&power) && power.ACLineStatus == 0;
    return windowsCpuReading(zones, speedPct, onBattery);
}

} // namespace

std::vector<CpuReading> sampleCpu(int seconds, const std::function<bool(const CpuReading&)>& startLoad)
{
    PDH_HQUERY query = nullptr;
    const PDH_STATUS opened = PdhOpenQueryW(nullptr, 0, &query);
    if (opened != ERROR_SUCCESS)
        return {failure(QStringLiteral("PdhOpenQuery failed (%1)").arg(hex(opened)))};
    const std::unique_ptr<void, decltype(&PdhCloseQuery)> guard(query, &PdhCloseQuery);

    // A missing counter is not a failure: that PC simply has no speed counter
    // or no thermal zone. Said in the details of the reading at rest.
    QString notes;
    PDH_HCOUNTER speed = nullptr;
    PDH_STATUS added = PdhAddEnglishCounterW(query, L"\\Processor Information(_Total)\\% Processor Performance", 0, &speed);
    if (added != ERROR_SUCCESS) {
        speed = nullptr;
        notes += QStringLiteral("\nspeed counter not available (%1)").arg(hex(added));
    }
    PDH_HCOUNTER thermal = nullptr;
    added = PdhAddEnglishCounterW(query, L"\\Thermal Zone Information(*)\\Temperature", 0, &thermal);
    if (added != ERROR_SUCCESS) {
        thermal = nullptr;
        notes += QStringLiteral("\nthermal zone counter not available (%1)").arg(hex(added));
    }

    // 1. The reading at rest. A speed is a rate: it needs two collections,
    // the first one only marks the start.
    PdhCollectQueryData(query);
    QThread::msleep(kSampleMs);
    std::vector<CpuReading> readings{readOnce(query, speed, thermal)};
    readings.front().raw += notes;
    if (!startLoad(readings.front()))
        return readings;

    // 2. One reading per second under load.
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < seconds * 1000LL) {
        QThread::msleep(kSampleMs);
        readings.push_back(readOnce(query, speed, thermal));
        readings.back().atMs = timer.elapsed();
    }
    return readings;
}
