#pragma once
#include <QByteArray>
#include <QString>

// One CPU temperature reading. Filled by cputemp_linux.cpp or
// cputemp_win.cpp (CMake picks one).
struct CpuTempReading {
    bool available = false;   // false = this PC exposes no readable sensor
    double celsius = 0;
    QString source;           // which sensor: "coretemp Package id 0", ACPI zone name...
    QString error;            // non-empty = the probe itself failed
    QString raw;              // every sensor seen, copied into the test details
};

// Blocking (PowerShell on Windows takes 1-3 s): CpuTempTest calls it from a
// worker thread, many times during the load test.
CpuTempReading readCpuTemperature();

// Parses the JSON printed by cputemp_win.cpp's PowerShell script. Pure,
// compiled on both OSes so the unit tests can run it on Linux.
CpuTempReading parseThermalZones(const QByteArray& json);
