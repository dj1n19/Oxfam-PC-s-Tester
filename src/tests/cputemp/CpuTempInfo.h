#pragma once
#include <QByteArray>
#include <QString>

// One reading of the CPU: temperature and speed together, so Windows needs
// one PowerShell call per reading, not two. Filled by cputemp_linux.cpp or
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

// Blocking (PowerShell on Windows takes 1-3 s): CpuTempTest calls it from a
// worker thread, many times during the load test.
CpuReading readCpu();

// Parses the JSON printed by cputemp_win.cpp's PowerShell script. Pure,
// compiled on both OSes so the unit tests can run it on Linux.
CpuReading parseWindowsCpu(const QByteArray& json);
