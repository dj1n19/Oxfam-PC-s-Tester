#pragma once
#include <QByteArray>
#include <QString>

// Windows activation status. Filled by license_win.cpp or license_linux.cpp.
// The product key itself is NEVER read into the program: PowerShell only
// tells us whether a firmware (OEM) key exists.
struct LicenseInfo {
    bool applicable = true;     // false on Linux
    bool found = false;         // a Windows licence with a product key is installed
    QString name;               // "Windows(R), Professional edition"
    QString description;        // "Windows(R) Operating System, OEM_DM channel"
    int status = -1;            // SoftwareLicensingProduct.LicenseStatus, 1 = licensed
    bool oemKeyPresent = false; // a key is embedded in the firmware (OA3)
    QString oemKeyDescription;  // "[4.0] Professional OEM:DM" (no key in it)
    QString error;              // non-empty = the probe failed
    QString raw;
};

// Blocking (WMI licensing queries take 2-10 s): called from a worker thread.
LicenseInfo readLicense();

// Parses the JSON printed by license_win.cpp's script (LicenseParsing.cpp).
// Plain Qt, so the unit tests run it on Linux.
LicenseInfo parseLicenseJson(const QByteArray& json);
