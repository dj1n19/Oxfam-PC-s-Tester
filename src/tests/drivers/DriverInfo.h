#pragma once
#include <vector>
#include <QByteArray>
#include <QString>

struct DeviceProblem {
    QString name;
    QString id;      // Windows DeviceID, Linux PCI slot
    int code = -1;   // Windows ConfigManagerErrorCode; -1 = Linux "no driver"
};

// Filled by drivers_linux.cpp or drivers_win.cpp (CMake picks one).
struct DriverInfo {
    std::vector<DeviceProblem> problems;
    QString error;   // non-empty = the probe itself failed
    QString raw;     // raw output, copied into the test details
};

// Blocking (PowerShell on Windows takes 1-3 s): DriversTest calls it
// from a worker thread, never from the UI thread.
DriverInfo readDrivers();

// Pure helpers (DriverParsing.cpp), compiled on both OSes so the unit
// tests can run them on Linux.

// Parses the JSON array printed by drivers_win.cpp's PowerShell script.
DriverInfo parsePnpEntities(const QByteArray& json);

// Does a PCI device of this class matter to a buyer if it has no driver?
// classCode is the 24-bit value of /sys/bus/pci/devices/*/class (0x020000).
bool pciClassMatters(unsigned classCode);
QString pciClassName(unsigned classCode);
