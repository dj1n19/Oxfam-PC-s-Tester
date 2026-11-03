#include "tests/license/LicenseInfo.h"

#include <QUuid>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <windows.h>
#include <slpublic.h>
#include <shobjidl.h>   // IShellItem2
#include <shlobj.h>     // SHCreateItemFromParsingName
#include <propsys.h>    // PSGetPropertyKeyFromName

// No PowerShell and no WMI (too slow just after start-up): the licensing
// library WMI's SoftwareLicensingProduct is built on (slc.dll).

namespace {

// ApplicationID of Windows itself (not Office): 55c92734-d682-4d71-983e-d6ec3f16059f
constexpr SLID kWindowsAppId = {0x55c92734, 0xd682, 0x4d71, {0x98, 0x3e, 0xd6, 0xec, 0x3f, 0x16, 0x05, 0x9f}};

// SL_E_SERVICE_STOPPING: the licensing service (sppsvc) stops itself when idle,
// and a call at that moment fails; the next one starts it again (Surface Pro,
// 2026-11-02: ERROR, then OK 7 s later).
constexpr HRESULT kServiceStopping = static_cast<HRESULT>(0xC004F075L);

// Which licence tells the truth: every Windows edition (Home, Pro...) has an
// entry, and those without a product key say "unlicensed". The best one wins.
int rank(SLLICENSINGSTATUS s)
{
    switch (s) {
    case SL_LICENSING_STATUS_LICENSED:        return 3;
    case SL_LICENSING_STATUS_IN_GRACE_PERIOD: return 2;
    case SL_LICENSING_STATUS_NOTIFICATION:    return 1;
    default:                                  return 0;
    }
}

QString hex(HRESULT hr)
{
    return QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

// A text value of a licence ("Name", "Description"), empty if missing.
QString skuText(HSLC slc, const SLID& sku, const wchar_t* valueName)
{
    SLDATATYPE type{};
    UINT size = 0;
    PBYTE value = nullptr;
    if (FAILED(SLGetProductSkuInformation(slc, &sku, valueName, &type, &size, &value)))
        return {};
    QString text;
    if (type == SL_DATA_SZ)
        text = QString::fromWCharArray(reinterpret_cast<const wchar_t*>(value));
    LocalFree(value);
    return text.trimmed();
}

// Only the SIZE of the ACPI MSDM table is asked: the key inside it never
// enters the program.
bool oemKeyInFirmware()
{
    constexpr DWORD kAcpi = ('A' << 24) | ('C' << 16) | ('P' << 8) | 'I';
    // ACPI tables are named by their signature as it lies in memory: "MSDM".
    constexpr DWORD kMsdm = 'M' | ('S' << 8) | ('D' << 16) | (DWORD('M') << 24);
    return GetSystemFirmwareTable(kAcpi, kMsdm, nullptr, 0) > 0;
}

// BitLocker as Explorer shows it (the padlock on the drive). WMI's
// Win32_EncryptableVolume is off limits (slow after start-up), manage-bde is
// a program to parse in every language. The shell property is one local
// call and needs no admin. Its key is looked up by name: no hard-coded GUID.
void readBitLocker(LicenseInfo& info)
{
    wchar_t windowsDir[MAX_PATH] = {};
    const UINT len = GetSystemWindowsDirectoryW(windowsDir, MAX_PATH);
    if (len < 3 || len >= MAX_PATH || windowsDir[1] != L':') {
        info.bitlockerError = QStringLiteral("GetSystemWindowsDirectoryW gave no drive letter");
        return;
    }
    info.drive = QString::fromWCharArray(windowsDir, 2);           // "C:"
    const std::wstring root = std::wstring(windowsDir, 2) + L"\\";  // "C:\"

    // COM must be started on this worker thread (a thread pool thread: start
    // it and stop it again, so the next task finds the thread as it was).
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool mustUninit = SUCCEEDED(init);   // S_FALSE (already started) must be balanced too
    // RPC_E_CHANGED_MODE: COM already runs in the other mode here; it still works.
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
        info.bitlockerError = QStringLiteral("CoInitializeEx failed (%1)").arg(hex(init));
        return;
    }

    PROPERTYKEY key{};
    HRESULT hr = PSGetPropertyKeyFromName(L"System.Volume.BitLockerProtection", &key);
    IShellItem2* item = nullptr;
    if (SUCCEEDED(hr))
        hr = SHCreateItemFromParsingName(root.c_str(), nullptr, IID_PPV_ARGS(&item));
    else
        info.bitlockerError = QStringLiteral("PSGetPropertyKeyFromName failed (%1)").arg(hex(hr));

    if (item) {
        int value = -1;
        hr = item->GetInt32(key, &value);
        if (SUCCEEDED(hr))
            info.bitlocker = value;
        else   // also when the property is empty
            info.bitlockerError = QStringLiteral("GetInt32(BitLockerProtection) on %1 failed (%2)")
                                      .arg(QString::fromStdWString(root), hex(hr));
        item->Release();
    } else if (info.bitlockerError.isEmpty()) {
        info.bitlockerError = QStringLiteral("SHCreateItemFromParsingName(%1) failed (%2)")
                                  .arg(QString::fromStdWString(root), hex(hr));
    }

    if (mustUninit)
        CoUninitialize();
}

} // namespace

LicenseInfo readLicense()
{
    LicenseInfo info;
    info.oemKeyPresent = oemKeyInFirmware();
    // First, and whatever the licence gives: its own result in its own fields.
    readBitLocker(info);

    HSLC slc = nullptr;
    HRESULT hr = SLOpen(&slc);
    // A few tries, 1 s apart (worker thread: the window does not freeze).
    for (int tries = 1; hr == kServiceStopping && tries < 5; ++tries) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        hr = SLOpen(&slc);
    }
    if (FAILED(hr)) {
        info.error = QStringLiteral("SLOpen failed (%1)").arg(hex(hr));
        return info;
    }
    const std::unique_ptr<void, decltype(&SLClose)> slcGuard(slc, &SLClose);

    UINT count = 0;
    SL_LICENSING_STATUS* statuses = nullptr;
    hr = SLGetLicensingStatusInformation(slc, &kWindowsAppId, nullptr, nullptr, &count, &statuses);
    if (FAILED(hr)) {
        info.error = QStringLiteral("SLGetLicensingStatusInformation failed (%1)").arg(hex(hr));
        return info;
    }
    const std::unique_ptr<SL_LICENSING_STATUS, decltype(&LocalFree)> statusGuard(statuses, &LocalFree);

    const SL_LICENSING_STATUS* best = nullptr;
    int unlicensed = 0;
    for (UINT i = 0; i < count; ++i) {
        const SL_LICENSING_STATUS& s = statuses[i];
        if (rank(s.eStatus) == 0) {
            ++unlicensed;
            continue;
        }
        info.raw += QStringLiteral("SKU %1: status %2, grace %3 min left, reason %4\n")
                        .arg(QUuid(s.SkuId).toString())
                        .arg(int(s.eStatus))
                        .arg(s.dwGraceTime)
                        .arg(hex(s.hrReason));
        if (!best || rank(s.eStatus) > rank(best->eStatus))
            best = &s;
    }
    info.raw += QStringLiteral("%1 licences, %2 unlicensed (editions without a key)\nOEM key in firmware (ACPI MSDM): %3")
                    .arg(count).arg(unlicensed)
                    .arg(info.oemKeyPresent ? QStringLiteral("yes") : QStringLiteral("no"));

    if (best) {   // else: no licence at all, status stays -1
        info.status = int(best->eStatus);
        info.name = skuText(slc, best->SkuId, L"Name");
        info.description = skuText(slc, best->SkuId, L"Description");
    }
    return info;
}
