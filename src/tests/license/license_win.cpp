#include "tests/license/LicenseInfo.h"

#include <QUuid>

#include <memory>

#include <windows.h>
#include <slpublic.h>

// No PowerShell and no WMI (too slow just after start-up): the licensing
// library WMI's SoftwareLicensingProduct is built on (slc.dll).

namespace {

// ApplicationID of Windows itself (not Office): 55c92734-d682-4d71-983e-d6ec3f16059f
constexpr SLID kWindowsAppId = {0x55c92734, 0xd682, 0x4d71, {0x98, 0x3e, 0xd6, 0xec, 0x3f, 0x16, 0x05, 0x9f}};

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

} // namespace

LicenseInfo readLicense()
{
    LicenseInfo info;
    info.oemKeyPresent = oemKeyInFirmware();

    HSLC slc = nullptr;
    HRESULT hr = SLOpen(&slc);
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
