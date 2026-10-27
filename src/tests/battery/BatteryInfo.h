#pragma once
#include <QString>

// What the battery is doing now, as the OS says it.
enum class ChargeState { Unknown, Charging, Discharging, NotCharging, Full };

// What the OS tells us about the battery. Filled by battery_linux.cpp or
// battery_win.cpp (CMake picks one). Only the ratio full/design matters,
// so the unit (mWh, uWh, mAh...) does not.
struct BatteryInfo {
    bool present = false;       // false on a desktop
    double designCapacity = 0;
    double fullCapacity = 0;
    int cycles = -1;            // -1 = unknown (many laptops report 0)
    // For the charger test (ChargeTest):
    int acOnline = -1;          // charger detected: 1 yes, 0 no, -1 unknown
    ChargeState state = ChargeState::Unknown;
    double chargePct = -1;      // current charge, % of full; -1 = unknown
    double rateW = 0;           // charging power in W (> 0 = charging); 0 = unknown or none
    QString name;
    QString error;              // non-empty = reading failed
    QString raw;                // raw values, copied into the test details
};

// Blocking function (file reads on Linux, battery driver calls on Windows):
// BatteryTest and ChargeTest call it from a worker thread, never from the UI thread.
BatteryInfo readBattery();
