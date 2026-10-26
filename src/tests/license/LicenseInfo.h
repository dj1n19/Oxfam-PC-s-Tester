#pragma once
#include <QString>

// Windows activation status. Filled by license_win.cpp or license_linux.cpp.
// The product key itself is NEVER read into the program: only whether a
// firmware (OEM) key exists.
struct LicenseInfo {
    bool applicable = true;     // false on Linux
    QString name;               // "Windows(R), Professional edition"
    QString description;        // "Windows(R) Operating System, OEM_DM channel"
    // Windows' SL_LICENSING_STATUS of the best installed licence: 1 = licensed,
    // 2 = grace period, 3 = notification (not activated). -1 = no licence at all.
    int status = -1;
    bool oemKeyPresent = false; // a key is embedded in the firmware (ACPI MSDM table)
    QString error;              // non-empty = the probe failed
    QString raw;
};

// Blocking (asks Windows' licensing service): called from a worker thread.
LicenseInfo readLicense();
