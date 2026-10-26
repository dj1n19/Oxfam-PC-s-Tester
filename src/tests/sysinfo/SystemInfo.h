#pragma once
#include <QByteArray>
#include <QString>

// Identity of the PC under test. Filled by sysinfo_linux.cpp or
// sysinfo_win.cpp (CMake picks one). Empty string = could not be read.
struct SystemInfo {
    QString vendor;
    QString model;
    QString serial;
    QString serialError;        // why serial is empty (often: needs root/admin)
    QString cpu;
    double ramBytes = 0;        // Linux: usable RAM (a bit below installed); Windows: installed RAM
    int chassisType = 2;        // SMBIOS code, 2 = Unknown (mapped in SystemInfoTest)
    QString error;              // non-empty = the probe itself failed
    QString raw;                // raw values, copied into the test details
};

// Blocking function (file reads, Windows API calls): SystemInfoTest calls it
// from a worker thread, never from the UI thread.
SystemInfo readSystemInfo();

// What the SMBIOS table (the firmware's own description of the PC) says.
// Windows gives the raw table (GetSystemFirmwareTable); Linux has the same
// bytes in /sys/firmware/dmi/tables/DMI, but root-only, so Linux reads the
// already decoded /sys/class/dmi/id files instead.
struct SmbiosIds {
    QString vendor;             // type 1 "System Information"
    QString model;
    QString serial;
    bool hasSystem = false;     // a type 1 structure was found
    int chassisType = 2;        // type 3 "Enclosure", 2 = Unknown
};

// Pure: walks the structures of a raw SMBIOS table (Smbios.cpp, unit tested).
// A broken or truncated table stops the walk; what was read so far is kept.
SmbiosIds parseSmbios(const QByteArray& table);
