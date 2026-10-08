#pragma once
#include <QString>

// What the OS tells us about the battery. Filled by battery_linux.cpp or
// battery_win.cpp (CMake picks one). Only the ratio full/design matters,
// so the unit (mWh, uWh, mAh...) does not.
struct BatteryInfo {
    bool present = false;       // false on a desktop
    double designCapacity = 0;
    double fullCapacity = 0;
    int cycles = -1;            // -1 = unknown (many laptops report 0)
    QString name;
    QString error;              // non-empty = reading failed
    QString raw;                // raw values, copied into the test details
};

// Blocking function (may take a few seconds on Windows): BatteryTest calls
// it from a worker thread, never from the UI thread.
BatteryInfo readBattery();

// Parses the XML written by "powercfg /batteryreport /xml" (Windows).
// Kept out of battery_win.cpp so it is plain Qt code that the unit tests
// can run on Linux with real sample files.
BatteryInfo parseBatteryReport(const QByteArray& xml);
