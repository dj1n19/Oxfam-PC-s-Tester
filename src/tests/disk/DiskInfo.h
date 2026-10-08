#pragma once
#include <vector>
#include <QByteArray>
#include <QString>

// smartctl has the same command line on Linux and Windows, so the probe
// (Smartctl.cpp) is shared: no *_linux.cpp / *_win.cpp here.

struct ScannedDevice {
    QString name;   // "/dev/sda", "/dev/nvme0"
    QString type;   // "sat", "nvme", ... passed back with -d
};

// One "smartctl --json -a" call.
struct SmartctlRun {
    QString device;
    QString type;
    int exitCode = -1;   // bit field, see "man smartctl", section RETURN VALUES
    QByteArray json;
    QString error;       // non-empty = smartctl could not be run at all
};

struct DiskScan {
    QString error;       // non-empty = smartctl missing, or --scan failed
    QByteArray scanJson;
    std::vector<SmartctlRun> disks;
};

// Blocking (about 1 s per disk): DiskTest calls it from a worker thread.
// smartctlPath empty = not found.
DiskScan readDisks(const QString& smartctlPath);

// Parses "smartctl --scan --json". Pure function, unit tested.
std::vector<ScannedDevice> parseScan(const QByteArray& json, QString* error);
