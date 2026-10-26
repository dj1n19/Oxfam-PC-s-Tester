#include "tests/disk/DiskInfo.h"

#include <cstddef>
#include <memory>

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

} // namespace

// The program already runs as administrator (manifest): call smartctl directly.
std::vector<SmartctlRun> readDevices(const QString& smartctlPath, const std::vector<ScannedDevice>& devices)
{
    std::vector<SmartctlRun> runs;
    for (const ScannedDevice& dev : devices)
        runs.push_back(runSmartctl(smartctlPath, dev));
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
