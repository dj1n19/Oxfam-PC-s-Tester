#pragma once
#include <functional>
#include <vector>
#include <QtGlobal>
#include <QString>

// One reading of the CPU: temperature and speed together. Filled by
// cputemp_linux.cpp or cputemp_win.cpp (CMake picks one).
struct CpuReading {
    bool hasTemp = false;     // false = this PC exposes no readable temperature sensor
    double celsius = 0;
    QString source;           // which sensor: "coretemp Package id 0", ACPI zone name...

    // Speed in % of the CPU's base (nominal) speed: above 100 = turbo, below
    // 100 under load = throttling. Works on PCs without a temperature sensor.
    bool hasSpeed = false;
    double speedPct = 0;
    bool onBattery = false;   // the OS slows the CPU on battery: not a fault
    qint64 atMs = 0;          // when it was read, in ms after the load started (0 = at rest)

    QString error;            // non-empty = the probe itself failed
    QString raw;              // every value seen, copied into the test details
};

// Takes one reading at rest, then calls startLoad(that reading): it starts the
// CPU load and returns true, or returns false (nothing to watch) to stop at
// once. Then one reading about every second for `seconds`. The first item is
// the reading at rest. Blocking (~seconds): call it from a worker thread.
std::vector<CpuReading> sampleCpu(int seconds, const std::function<bool(const CpuReading&)>& startLoad);

// A Windows ACPI thermal zone, as the "Thermal Zone Information" counter gives it.
struct ThermalZone {
    QString name;             // "\_TZ.THM0"
    double kelvin = 0;
};

// Turns what cputemp_win.cpp read into one reading: the hottest plausible
// zone, the speed (<= 0 = counter not ready: no speed) and the power source.
// Pure, compiled on both OSes so the unit tests can run it on Linux.
CpuReading windowsCpuReading(const std::vector<ThermalZone>& zones, double speedPct, bool onBattery);
