#pragma once
#include <QByteArray>
#include <QSize>
#include <QSizeF>
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
    int cores = 0;              // physical cores, 0 = unknown
    int threads = 0;            // logical processors, 0 = unknown
    QString os;                 // "Windows 11 Pro 24H2 (build 26100.2033)", "Linux Mint 22"
    int chassisType = 2;        // SMBIOS code, 2 = Unknown (mapped in SystemInfoTest)
    // The screen the window is on. Not read by the probe: Qt Gui knows it,
    // and oxcore must not use Qt Gui. MainWindow fills these.
    QSizeF screenMm;            // physical size from the monitor (EDID); empty = unknown
    QSize screenPixels;         // native resolution
    QString error;              // non-empty = the probe itself failed
    QString raw;                // raw values, copied into the test details
};

// Blocking function (file reads, Windows API calls): MainWindow calls it
// from a worker thread, never from the UI thread.
SystemInfo readSystemInfo();

// Pure helpers of the probes (SystemInfoParsing.cpp), unit tested on Linux.
// "Windows 11 Pro 24H2 (build 26100.2033)". The registry's ProductName still
// says "Windows 10" on Windows 11: the build number (22000 and up) decides.
QString windowsVersionName(const QString& productName, const QString& displayVersion, int build, int ubr);
// Physical cores and logical processors from /proc/cpuinfo (0 if unknown).
void countCpuCores(const QString& cpuinfo, int* cores, int* threads);

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
