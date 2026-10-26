#pragma once
#include <vector>
#include <QByteArray>
#include <QString>
#include <QStringList>

// smartctl has the same command line on Linux and Windows, so most of the
// probe (Smartctl.cpp) is shared. Only readDevices() differs: on Linux, as a
// normal user, it elevates with pkexec (smartctl_linux.cpp / smartctl_win.cpp).

// Failures the technician can fix: DiskTest then says what to do instead of
// a generic "could not run". Anything else is None, with the cause in error.
enum class SmartctlFailure { None, NotFound, PasswordRefused, PasswordTimeout };

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
    SmartctlFailure failure = SmartctlFailure::None;
};

struct DiskScan {
    QString error;       // non-empty = smartctl missing, or --scan failed
    SmartctlFailure failure = SmartctlFailure::None;
    QByteArray scanJson;
    std::vector<SmartctlRun> disks;
    QStringList usbLeftOut;   // devices on a USB bus, not read (not part of the PC)
    QString busInfo;          // what the OS said about the buses (details)
};

// Blocking (about 1 s per disk): DiskTest calls it from a worker thread.
// smartctlPath empty = not found.
DiskScan readDisks(const QString& smartctlPath);

// Parses "smartctl --scan --json". Pure function, unit tested.
std::vector<ScannedDevice> parseScan(const QByteArray& json, QString* error);

// USB disks (the tester's own stick, an external drive) are not part of the PC
// being sold: readDisks() leaves them out. OS-specific: Linux looks at sysfs,
// Windows asks each disk for its bus (IOCTL_STORAGE_QUERY_PROPERTY). Returns
// the device names on a USB bus; raw = what the OS said, for the details. If
// the OS cannot tell, that disk is not in the list: it is checked (never hide
// a disk because a query failed).
QStringList usbDevices(const std::vector<ScannedDevice>& devices, QString* raw);

// smartctl's Windows name for \\.\PhysicalDriveN is /dev/sd + letter N
// (a = 0): returns N, or -1 for any other name. Pure, unit tested.
int physicalDriveNumber(const QString& smartctlName);

// Reads every scanned device. OS-specific; called by readDisks().
std::vector<SmartctlRun> readDevices(const QString& smartctlPath, const std::vector<ScannedDevice>& devices);

// One direct "smartctl --json -a [-d type] name" call (no elevation).
SmartctlRun runSmartctl(const QString& smartctlPath, const ScannedDevice& dev);
QStringList smartctlArgs(const ScannedDevice& dev);

// pkexec batch: ONE elevated shell runs smartctl for every disk, so the
// technician types the password once. Pure helpers, unit tested.
inline constexpr char kBatchMarker[] = "OXFAM_SMARTCTL_EXIT=";
bool isSafeShellWord(const QString& s);
QString buildBatchScript(const QString& smartctlPath, const std::vector<ScannedDevice>& devices);   // empty if a word is unsafe
std::vector<SmartctlRun> parseBatchOutput(const QByteArray& out, const std::vector<ScannedDevice>& devices);
