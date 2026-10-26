#include "tests/drivers/DriverInfo.h"

#include <memory>

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

// No PowerShell and no WMI (too slow just after start-up): SetupAPI lists the
// devices like Device Manager does, and CM_Get_DevNode_Status gives the same
// problem code that WMI calls ConfigManagerErrorCode.

namespace {

// Text property of a device, empty if missing. 512 characters is plenty for a
// device name; a longer one fails and the device ID is shown instead.
QString deviceText(HDEVINFO set, SP_DEVINFO_DATA& device, DWORD property)
{
    wchar_t buffer[512] = {};
    if (!SetupDiGetDeviceRegistryPropertyW(set, &device, property, nullptr, reinterpret_cast<PBYTE>(buffer),
                                           DWORD(sizeof buffer - sizeof(wchar_t)), nullptr))
        return {};
    return QString::fromWCharArray(buffer).trimmed();
}

} // namespace

DriverInfo readDrivers()
{
    DriverInfo info;

    // DIGCF_PRESENT: only plugged-in devices. Windows also remembers unplugged
    // ones (the previous owner's USB stick...): they are not this PC's problem.
    const HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        info.error = QStringLiteral("SetupDiGetClassDevs failed (error %1)").arg(GetLastError());
        return info;
    }
    // Frees the list on every return path (no raw cleanup to forget).
    const std::unique_ptr<void, decltype(&SetupDiDestroyDeviceInfoList)> guard(set, &SetupDiDestroyDeviceInfoList);

    int count = 0;
    int unreadable = 0;
    SP_DEVINFO_DATA device{};
    device.cbSize = DWORD(sizeof device);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &device); ++i) {
        ++count;
        ULONG status = 0;
        ULONG problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, device.DevInst, 0) != CR_SUCCESS) {
            ++unreadable;   // vanished between the listing and this call
            continue;
        }
        if (!(status & DN_HAS_PROBLEM))
            continue;

        DeviceProblem p;
        p.code = int(problem);
        wchar_t id[MAX_DEVICE_ID_LEN] = {};
        if (CM_Get_Device_IDW(device.DevInst, id, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS)
            p.id = QString::fromWCharArray(id);
        p.name = deviceText(set, device, SPDRP_FRIENDLYNAME);
        if (p.name.isEmpty())
            p.name = deviceText(set, device, SPDRP_DEVICEDESC);
        if (p.name.isEmpty())
            p.name = p.id;   // devices without a driver often have no name
        info.problems.push_back(p);
        info.raw += QStringLiteral("code %1: %2 - %3\n").arg(p.code).arg(p.name, p.id);
    }
    if (GetLastError() != ERROR_NO_MORE_ITEMS) {
        info.error = QStringLiteral("SetupDiEnumDeviceInfo stopped after %1 devices (error %2)")
                         .arg(count).arg(GetLastError());
        return info;
    }
    info.raw += QStringLiteral("%1 devices present, %2 with a problem").arg(count).arg(info.problems.size());
    if (unreadable > 0)
        info.raw += QStringLiteral(", %1 status unreadable").arg(unreadable);
    return info;
}
