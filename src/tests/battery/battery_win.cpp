#include "tests/battery/BatteryInfo.h"

#include <QStringList>

#include <cstddef>
#include <memory>
#include <vector>

#include <windows.h>
#include <setupapi.h>
#include <batclass.h>

// No powercfg: "powercfg /batteryreport" also gathers weeks of usage history
// to build its report, and timed out (30 s) on every shop laptop just after
// start-up. Like powercfg, we ask the battery driver itself, as in Microsoft's
// "Enumerating Battery Devices" sample: milliseconds, works as a normal user.
// (WMI's BatteryStaticData was dropped earlier: "Generic Failure" on real laptops.)

namespace {

// GUID_DEVICE_BATTERY (batclass.h), written out: using the SDK constant needs
// initguid.h tricks to get it defined once.
constexpr GUID kBatteryInterface = {0x72631e54, 0x78a4, 0x11d0, {0xbc, 0xf7, 0x00, 0xaa, 0x00, 0xb7, 0xb3, 0x2a}};

bool queryInformation(HANDLE battery, ULONG tag, BATTERY_QUERY_INFORMATION_LEVEL level, void* out, DWORD outSize)
{
    BATTERY_QUERY_INFORMATION query{};
    query.BatteryTag = tag;
    query.InformationLevel = level;
    DWORD returned = 0;
    return DeviceIoControl(battery, IOCTL_BATTERY_QUERY_INFORMATION, &query, DWORD(sizeof query),
                           out, outSize, &returned, nullptr);
}

enum class Read { Battery, NotSystemBattery, Failed };

// Reads one battery device into *info. *why = what happened, for the details.
Read readOne(const wchar_t* path, BatteryInfo* info, QString* why)
{
    const HANDLE battery = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (battery == INVALID_HANDLE_VALUE) {
        *why = QStringLiteral("CreateFile error %1").arg(GetLastError());
        return Read::Failed;
    }
    const std::unique_ptr<void, decltype(&CloseHandle)> guard(battery, &CloseHandle);

    // The tag names the battery now in the slot. 0 = slot empty.
    ULONG wait = 0;   // do not wait for a battery to be inserted
    ULONG tag = 0;
    DWORD returned = 0;
    if (!DeviceIoControl(battery, IOCTL_BATTERY_QUERY_TAG, &wait, DWORD(sizeof wait), &tag, DWORD(sizeof tag),
                         &returned, nullptr) || tag == BATTERY_TAG_INVALID) {
        *why = QStringLiteral("no battery in this slot (error %1)").arg(GetLastError());
        return Read::NotSystemBattery;
    }

    BATTERY_INFORMATION bi{};
    if (!queryInformation(battery, tag, BatteryInformation, &bi, DWORD(sizeof bi))) {
        *why = QStringLiteral("IOCTL_BATTERY_QUERY_INFORMATION error %1").arg(GetLastError());
        return Read::Failed;
    }
    // Short-term = a UPS plugged in by USB, not the laptop's battery.
    if (!(bi.Capabilities & BATTERY_SYSTEM_BATTERY) || (bi.Capabilities & BATTERY_IS_SHORT_TERM)) {
        *why = QStringLiteral("not a system battery (capabilities 0x%1)").arg(bi.Capabilities, 0, 16);
        return Read::NotSystemBattery;
    }

    wchar_t name[128] = {};
    queryInformation(battery, tag, BatteryDeviceName, name, DWORD(sizeof name - sizeof(wchar_t)));
    info->present = true;
    info->name = QString::fromWCharArray(name).trimmed();
    // BATTERY_UNKNOWN_CAPACITY (0xFFFFFFFF) taken as a number would give a
    // false health figure: 0 = unknown, which BatteryTest reports as Error.
    const auto known = [](ULONG c) { return c == BATTERY_UNKNOWN_CAPACITY ? 0.0 : double(c); };
    info->designCapacity = known(bi.DesignedCapacity);
    info->fullCapacity = known(bi.FullChargedCapacity);
    info->cycles = bi.CycleCount > 0 ? int(bi.CycleCount) : -1;   // 0 usually means "not reported"
    // RELATIVE: capacities are not in mWh. The ratio is all we use anyway.
    *why = QStringLiteral("%1: DesignedCapacity=%2 FullChargedCapacity=%3 CycleCount=%4 chemistry=%5%6")
               .arg(info->name)
               .arg(bi.DesignedCapacity)
               .arg(bi.FullChargedCapacity)
               .arg(bi.CycleCount)
               .arg(QString::fromLatin1(reinterpret_cast<const char*>(bi.Chemistry), sizeof bi.Chemistry))
               .arg((bi.Capabilities & BATTERY_CAPACITY_RELATIVE) ? QStringLiteral(" (relative units)")
                                                                  : QStringLiteral(" (mWh)"));
    return Read::Battery;
}

} // namespace

BatteryInfo readBattery()
{
    BatteryInfo info;
    const HDEVINFO set = SetupDiGetClassDevsW(&kBatteryInterface, nullptr, nullptr,
                                              DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        info.error = QStringLiteral("SetupDiGetClassDevs(battery) failed (error %1)").arg(GetLastError());
        return info;
    }
    const std::unique_ptr<void, decltype(&SetupDiDestroyDeviceInfoList)> guard(set, &SetupDiDestroyDeviceInfoList);

    QStringList seen;
    bool failed = false;
    SP_DEVICE_INTERFACE_DATA iface{};
    iface.cbSize = DWORD(sizeof iface);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &kBatteryInterface, i, &iface); ++i) {
        // First call: size of the device path. The vector's memory is aligned
        // for any type, as the detail structure needs.
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &needed, nullptr);
        std::vector<std::byte> buffer(needed > 0 ? needed : 1);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = DWORD(sizeof *detail);
        if (needed == 0 || !SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, needed, nullptr, nullptr)) {
            seen << QStringLiteral("battery %1: no device path (error %2)").arg(i).arg(GetLastError());
            failed = true;
            continue;
        }

        QString why;
        const Read read = readOne(detail->DevicePath, &info, &why);
        seen << QStringLiteral("battery %1: %2").arg(i).arg(why);
        if (read == Read::Battery) {
            info.raw = seen.join(QLatin1Char('\n'));
            return info;   // first system battery only (multi-battery: dropped)
        }
        if (read == Read::Failed)
            failed = true;
    }

    info.raw = seen.isEmpty() ? QStringLiteral("no battery device") : seen.join(QLatin1Char('\n'));
    // A battery that could not be read is not a desktop: never Skipped then.
    if (failed)
        info.error = QStringLiteral("A battery was found but could not be read");
    return info;
}
