#include "tests/wireless/WirelessInfo.h"

#include <memory>

#include <windows.h>
#include <wlanapi.h>
#include <bluetoothapis.h>

// Native API only (no netsh, no PowerShell): wlanapi talks to the WLAN
// service, bthprops to the Bluetooth stack. Both answer in milliseconds,
// except the Wi-Fi scan (a few seconds, waited for with a time limit).

namespace {

// The WLAN service calls this from its own thread when a scan ends.
void WINAPI onWlanNotification(PWLAN_NOTIFICATION_DATA data, PVOID scanDone)
{
    if (data->NotificationSource == WLAN_NOTIFICATION_SOURCE_ACM
        && (data->NotificationCode == wlan_notification_acm_scan_complete
            || data->NotificationCode == wlan_notification_acm_scan_fail))
        SetEvent(static_cast<HANDLE>(scanDone));
}

// Asks for a fresh scan and waits for its end (max 6 s). Without it the
// list is what Windows saw at its last scan, maybe a minute ago.
QString scan(HANDLE client, const GUID& adapter)
{
    const HANDLE scanDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!scanDone)
        return QStringLiteral("scan: CreateEvent error %1").arg(GetLastError());
    const std::unique_ptr<void, decltype(&CloseHandle)> eventGuard(scanDone, &CloseHandle);

    if (WlanRegisterNotification(client, WLAN_NOTIFICATION_SOURCE_ACM, TRUE, &onWlanNotification, scanDone,
                                 nullptr, nullptr) != ERROR_SUCCESS)
        return QStringLiteral("scan: no notification, cached list used");
    QString result;
    const DWORD err = WlanScan(client, &adapter, nullptr, nullptr, nullptr);
    if (err != ERROR_SUCCESS)
        result = QStringLiteral("scan: WlanScan error %1, cached list used").arg(err);
    else if (WaitForSingleObject(scanDone, 6000) != WAIT_OBJECT_0)
        result = QStringLiteral("scan: not finished in 6 s, list so far used");
    else
        result = QStringLiteral("scan: done");
    // Unregister before the event is closed: no late callback on a dead handle.
    WlanRegisterNotification(client, WLAN_NOTIFICATION_SOURCE_NONE, TRUE, nullptr, nullptr, nullptr, nullptr);
    return result;
}

} // namespace

WifiInfo readWifi()
{
    WifiInfo info;
    QStringList raw;
    DWORD version = 0;
    HANDLE client = nullptr;
    const DWORD opened = WlanOpenHandle(2, nullptr, &version, &client);
    if (opened == ERROR_SERVICE_NOT_ACTIVE || opened == ERROR_SERVICE_DISABLED) {
        // The WLAN service runs only when Windows knows a Wi-Fi adapter.
        info.raw = QStringLiteral("WLAN service not running (error %1): no Wi-Fi adapter known to Windows").arg(opened);
        return info;
    }
    if (opened != ERROR_SUCCESS) {
        info.error = QStringLiteral("WlanOpenHandle error %1").arg(opened);
        return info;
    }
    const auto closeClient = [](HANDLE h) { WlanCloseHandle(h, nullptr); };
    const std::unique_ptr<void, decltype(closeClient)> clientGuard(client, closeClient);

    PWLAN_INTERFACE_INFO_LIST adapters = nullptr;
    const DWORD listed = WlanEnumInterfaces(client, nullptr, &adapters);
    if (listed != ERROR_SUCCESS) {
        info.error = QStringLiteral("WlanEnumInterfaces error %1").arg(listed);
        return info;
    }
    const std::unique_ptr<void, decltype(&WlanFreeMemory)> adaptersGuard(adapters, &WlanFreeMemory);
    if (adapters->dwNumberOfItems == 0) {
        info.raw = QStringLiteral("WLAN service running, no Wi-Fi adapter");
        return info;
    }
    const WLAN_INTERFACE_INFO& adapter = adapters->InterfaceInfo[0];   // first adapter only
    info.present = true;
    info.name = QString::fromWCharArray(adapter.strInterfaceDescription).trimmed();
    raw << QStringLiteral("adapter: %1 (state %2, %3 adapters)")
               .arg(info.name).arg(int(adapter.isState)).arg(adapters->dwNumberOfItems);

    // Radio: on if one of its PHYs is on in software AND hardware.
    DWORD size = 0;
    PWLAN_RADIO_STATE radio = nullptr;
    if (WlanQueryInterface(client, &adapter.InterfaceGuid, wlan_intf_opcode_radio_state, nullptr, &size,
                           reinterpret_cast<PVOID*>(&radio), nullptr) == ERROR_SUCCESS) {
        const std::unique_ptr<void, decltype(&WlanFreeMemory)> radioGuard(radio, &WlanFreeMemory);
        bool on = false;
        for (DWORD i = 0; i < radio->dwNumberOfPhys; ++i) {
            const auto& phy = radio->PhyRadioState[i];
            raw << QStringLiteral("radio %1: software %2, hardware %3 (1 = on, 2 = off)")
                       .arg(phy.dwPhyIndex).arg(int(phy.dot11SoftwareRadioState)).arg(int(phy.dot11HardwareRadioState));
            if (phy.dot11SoftwareRadioState == dot11_radio_state_on && phy.dot11HardwareRadioState == dot11_radio_state_on)
                on = true;
        }
        info.radioOff = radio->dwNumberOfPhys > 0 && !on;
    } else {
        raw << QStringLiteral("radio state not readable");
    }
    if (info.radioOff) {
        info.raw = raw.join('\n');
        return info;
    }

    raw << scan(client, adapter.InterfaceGuid);
    PWLAN_AVAILABLE_NETWORK_LIST networks = nullptr;
    const DWORD got = WlanGetAvailableNetworkList(client, &adapter.InterfaceGuid, 0, nullptr, &networks);
    if (got != ERROR_SUCCESS) {
        info.error = QStringLiteral("WlanGetAvailableNetworkList error %1").arg(got);
        info.raw = raw.join('\n');
        return info;
    }
    const std::unique_ptr<void, decltype(&WlanFreeMemory)> networksGuard(networks, &WlanFreeMemory);
    info.counted = true;
    for (DWORD i = 0; i < networks->dwNumberOfItems; ++i) {
        const DOT11_SSID& ssid = networks->Network[i].dot11Ssid;
        info.ssids << QString::fromUtf8(reinterpret_cast<const char*>(ssid.ucSSID), int(ssid.uSSIDLength));
    }
    raw << QStringLiteral("%1 entries in the network list").arg(info.ssids.size());
    info.raw = raw.join('\n');
    return info;
}

BluetoothInfo readBluetooth()
{
    BluetoothInfo info;
    BLUETOOTH_FIND_RADIO_PARAMS params{};
    params.dwSize = DWORD(sizeof params);
    HANDLE radio = nullptr;
    const HBLUETOOTH_RADIO_FIND found = BluetoothFindFirstRadio(&params, &radio);
    if (!found) {
        const DWORD err = GetLastError();
        if (err == ERROR_NO_MORE_ITEMS)
            info.raw = QStringLiteral("no Bluetooth radio");
        else
            info.error = QStringLiteral("BluetoothFindFirstRadio error %1").arg(err);
        return info;
    }
    const std::unique_ptr<void, decltype(&CloseHandle)> radioGuard(radio, &CloseHandle);
    const std::unique_ptr<void, decltype(&BluetoothFindRadioClose)> findGuard(found, &BluetoothFindRadioClose);

    info.present = true;
    BLUETOOTH_RADIO_INFO radioInfo{};
    radioInfo.dwSize = DWORD(sizeof radioInfo);
    if (BluetoothGetRadioInfo(radio, &radioInfo) == ERROR_SUCCESS)
        info.name = QString::fromWCharArray(radioInfo.szName).trimmed();
    // Whether "off in Settings" can be seen here is not known yet: only
    // listed, never judged, until a real run shows it.
    info.raw = QStringLiteral("radio: %1, manufacturer 0x%2, connectable %3, discoverable %4")
                   .arg(info.name)
                   .arg(radioInfo.manufacturer, 4, 16, QLatin1Char('0'))
                   .arg(BluetoothIsConnectable(radio) ? QStringLiteral("yes") : QStringLiteral("no"),
                        BluetoothIsDiscoverable(radio) ? QStringLiteral("yes") : QStringLiteral("no"));
    return info;
}
