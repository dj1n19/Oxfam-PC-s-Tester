#include "tests/disk/DiskInfo.h"

#include <cstddef>
#include <memory>
#include <string>

#include <QElapsedTimer>

#include <windows.h>
#include <winioctl.h>

namespace {

// A few STORAGE_BUS_TYPE values, for the details only.
QString busName(int bus)
{
    switch (bus) {
    case BusTypeAta:  return QStringLiteral("ATA");
    case BusTypeRAID: return QStringLiteral("RAID");
    case BusTypeUsb:  return QStringLiteral("USB");
    case BusTypeSata: return QStringLiteral("SATA");
    case BusTypeSd:   return QStringLiteral("SD card");
    case BusTypeNvme: return QStringLiteral("NVMe");
    default:          return QStringLiteral("other");
    }
}

// The disk's bus (STORAGE_BUS_TYPE), or -1 with *why set.
int busType(int driveNumber, QString* why)
{
    const std::wstring path = QStringLiteral("\\\\.\\PhysicalDrive%1").arg(driveNumber).toStdWString();
    // Access 0: asking for the description of a disk needs no read right.
    const HANDLE disk = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (disk == INVALID_HANDLE_VALUE) {
        *why = QStringLiteral("CreateFile error %1").arg(GetLastError());
        return -1;
    }
    const std::unique_ptr<void, decltype(&CloseHandle)> guard(disk, &CloseHandle);

    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;
    // The descriptor is followed by vendor/model strings: room for them, or
    // some drivers refuse to answer.
    alignas(STORAGE_DEVICE_DESCRIPTOR) std::byte buffer[1024] = {};
    DWORD bytes = 0;
    if (!DeviceIoControl(disk, IOCTL_STORAGE_QUERY_PROPERTY, &query, DWORD(sizeof query),
                         buffer, DWORD(sizeof buffer), &bytes, nullptr)) {
        *why = QStringLiteral("IOCTL_STORAGE_QUERY_PROPERTY error %1").arg(GetLastError());
        return -1;
    }
    const auto* descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer);
    if (bytes < offsetof(STORAGE_DEVICE_DESCRIPTOR, BusType) + sizeof descriptor->BusType) {
        *why = QStringLiteral("answer too short (%1 bytes)").arg(bytes);
        return -1;
    }
    return int(descriptor->BusType);
}

// Reads the start of the disk, never writes. FILE_FLAG_NO_BUFFERING goes past
// Windows' cache (else a second run would measure memory): the buffer must
// then be sector-aligned (VirtualAlloc gives a page) and each read a multiple
// of the sector size (1 MiB is, for 512 and 4096-byte sectors).
void measureSpeed(SmartctlRun& run)
{
    const int number = physicalDriveNumber(run.device);
    if (number < 0) {
        run.speedRaw = QStringLiteral("%1: no PhysicalDrive number for this name, speed not measured").arg(run.device);
        return;
    }
    const QString name = QStringLiteral("\\\\.\\PhysicalDrive%1").arg(number);
    const std::wstring path = name.toStdWString();
    const HANDLE disk = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (disk == INVALID_HANDLE_VALUE) {
        run.speedRaw = QStringLiteral("%1: CreateFile error %2").arg(name).arg(GetLastError());
        return;
    }
    const std::unique_ptr<void, decltype(&CloseHandle)> guard(disk, &CloseHandle);

    constexpr DWORD kChunk = 1024 * 1024;
    const auto release = [](void* p) { VirtualFree(p, 0, MEM_RELEASE); };
    const std::unique_ptr<void, decltype(release)> buffer(
        VirtualAlloc(nullptr, kChunk, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE), release);
    if (!buffer) {
        run.speedRaw = QStringLiteral("VirtualAlloc error %1").arg(GetLastError());
        return;
    }

    constexpr qint64 kWanted = qint64(kSpeedMiB) * 1024 * 1024;
    qint64 total = 0;
    DWORD readError = 0;
    QElapsedTimer timer;
    timer.start();
    while (total < kWanted && timer.elapsed() < kSpeedMaxSeconds * 1000) {
        DWORD got = 0;
        if (!ReadFile(disk, buffer.get(), kChunk, &got, nullptr)) {
            readError = GetLastError();
            break;
        }
        if (got == 0)
            break;   // end of the disk (a tiny one)
        total += got;
    }
    const double seconds = double(timer.nsecsElapsed()) / 1e9;

    run.speedRaw = QStringLiteral("%1: %2 MiB read in %3 s (unbuffered, sequential)")
                       .arg(name).arg(total / (1024 * 1024)).arg(seconds, 0, 'f', 3);
    if (readError != 0)
        run.speedRaw += QStringLiteral(", ReadFile error %1").arg(readError);
    if (total >= qint64(kSpeedMinMiB) * 1024 * 1024 && seconds > 0)
        run.readMBps = double(total) / 1e6 / seconds;
}

} // namespace

// The program already runs as administrator (manifest): call smartctl directly.
std::vector<SmartctlRun> readDevices(const QString& smartctlPath, const std::vector<ScannedDevice>& devices)
{
    std::vector<SmartctlRun> runs;
    for (const ScannedDevice& dev : devices) {
        SmartctlRun run = runSmartctl(smartctlPath, dev);
        measureSpeed(run);
        runs.push_back(std::move(run));
    }
    return runs;
}

// No PowerShell (Get-Disk was too slow just after start-up): each disk is
// asked for its bus directly, like Disk Management does.
QStringList usbDevices(const std::vector<ScannedDevice>& devices, QString* raw)
{
    QStringList usb, seen;
    for (const ScannedDevice& dev : devices) {
        const int number = physicalDriveNumber(dev.name);
        if (number < 0) {
            seen << QStringLiteral("%1: not a /dev/sdX name, checked").arg(dev.name);
            continue;
        }
        QString why;
        const int bus = busType(number, &why);
        if (bus < 0) {
            seen << QStringLiteral("%1 = PhysicalDrive%2: %3, checked").arg(dev.name).arg(number).arg(why);
            continue;
        }
        seen << QStringLiteral("%1 = PhysicalDrive%2: bus %3 (%4)").arg(dev.name).arg(number).arg(bus).arg(busName(bus));
        if (bus == BusTypeUsb)
            usb << dev.name;
    }
    *raw = QStringLiteral("Disk buses (IOCTL_STORAGE_QUERY_PROPERTY):\n") + seen.join(QLatin1Char('\n'));
    return usb;
}
