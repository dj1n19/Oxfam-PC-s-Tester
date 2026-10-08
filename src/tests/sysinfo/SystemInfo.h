#pragma once
#include <QString>

// Identity of the PC under test. Filled by sysinfo_linux.cpp or
// sysinfo_win.cpp (CMake picks one). Empty string = could not be read.
struct SystemInfo {
    QString vendor;
    QString model;
    QString serial;
    QString serialError;        // why serial is empty (often: needs root/admin)
    QString cpu;
    double ramBytes = 0;        // usable RAM seen by the OS (a bit below installed)
    int chassisType = 2;        // SMBIOS code, 2 = Unknown (mapped in SystemInfoTest)
    QString error;              // non-empty = the probe itself failed
    QString raw;                // raw values, copied into the test details
};

// Blocking function (PowerShell on Windows takes ~1-2 s): SystemInfoTest
// calls it from a worker thread, never from the UI thread.
SystemInfo readSystemInfo();
