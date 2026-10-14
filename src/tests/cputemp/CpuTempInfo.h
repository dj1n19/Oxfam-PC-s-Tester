#pragma once
#include <functional>
#include <vector>
#include <QByteArray>
#include <QString>

// One reading of the CPU: temperature and speed together (on Windows, one
// JSON line of the PowerShell script). Filled by cputemp_linux.cpp or
// cputemp_win.cpp (CMake picks one).
struct CpuReading {
    bool hasTemp = false;     // false = this PC exposes no readable temperature sensor
    double celsius = 0;
    QString source;           // which sensor: "coretemp Package id 0", ACPI zone name...

    // Speed in % of the CPU's base (nominal) speed: above 100 = turbo, below
    // 100 under load = throttling. Works on PCs without a temperature sensor.
    bool hasSpeed = false;
    double speedPct = 0;
    bool onBattery = false;   // the OS slows the CPU on battery: not a fault

    QString error;            // non-empty = the probe itself failed
    QString raw;              // every value seen, copied into the test details
};

// Takes one reading at rest, then calls startLoad(that reading): it starts the
// CPU load and returns true, or returns false (nothing to watch) to stop at
// once. Then one reading about every second for `seconds`. The first item is
// the reading at rest. Blocking (~seconds): call it from a worker thread.
// Windows: ONE PowerShell runs the whole time and prints a line per reading,
// started before the load: a new PowerShell per reading took more than 10 s
// to start under full load (Latitude 7420), so every reading timed out.
std::vector<CpuReading> sampleCpu(int seconds, const std::function<bool(const CpuReading&)>& startLoad);

// Parses the JSON printed by cputemp_win.cpp's PowerShell script. Pure,
// compiled on both OSes so the unit tests can run it on Linux.
CpuReading parseWindowsCpu(const QByteArray& json);
