#pragma once
#include <QString>

// Windows activation status and BitLocker of the system drive (one row: both are
// Windows' state, not hardware). Filled by license_win.cpp or license_linux.cpp.
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
    QString error;              // non-empty = the licence probe failed
    QString raw;

    // BitLocker of the system drive, as Explorer shows it (shell property
    // System.Volume.BitLockerProtection): 0 or 2 = off, 1, 3 or 5 = on,
    // anything else = a code we do not know yet. -1 = not read (bitlockerError).
    int bitlocker = -1;
    QString drive;              // "C:"
    QString bitlockerError;
};

// Blocking (asks Windows' licensing service and the shell): called from a worker thread.
LicenseInfo readLicense();
