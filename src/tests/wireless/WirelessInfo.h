#pragma once
#include <QString>
#include <QStringList>

// What the OS tells us about the Wi-Fi and Bluetooth radios. Filled by
// wireless_linux.cpp or wireless_win.cpp (CMake picks one).

struct WifiInfo {
    bool present = false;       // a Wi-Fi adapter with a working driver
    bool radioOff = false;      // switched off: Fn key, switch, airplane mode, system setting
    bool counted = false;       // false = the networks could not be listed (Linux without NetworkManager)
    QStringList ssids;          // one per network seen; "" = hidden network
    QString name;               // adapter description or driver
    QString error;              // non-empty = reading failed
    QString raw;                // raw values, copied into the test details
};

struct BluetoothInfo {
    bool present = false;
    bool radioOff = false;      // Linux: rfkill only (bluetoothctl "power off" is not seen)
    QString name;
    QString error;
    QString raw;
};

// Blocking functions (a Wi-Fi scan takes a few seconds): the tests call
// them from a worker thread, never from the UI thread.
WifiInfo readWifi();
BluetoothInfo readBluetooth();
