#include "tests/sysinfo/SystemInfo.h"

#include <QSettings>

#include <cstring>
#include <windows.h>

// No PowerShell and no WMI: both are very slow just after start-up (timeouts
// on shop laptops, 2026-10-15). These Windows calls answer in milliseconds.
// WMI's Win32_ComputerSystem/BIOS/SystemEnclosure read this same SMBIOS table.
SystemInfo readSystemInfo()
{
    SystemInfo info;

    // 'RSMB' = the raw SMBIOS table. First call: size only.
    constexpr DWORD kRsmb = ('R' << 24) | ('S' << 16) | ('M' << 8) | 'B';
    const UINT size = GetSystemFirmwareTable(kRsmb, 0, nullptr, 0);
    QByteArray buffer(qsizetype(size), '\0');
    if (size < 8 || GetSystemFirmwareTable(kRsmb, 0, buffer.data(), size) != size) {
        info.error = QStringLiteral("GetSystemFirmwareTable(RSMB) failed (size %1, error %2)")
                         .arg(size).arg(GetLastError());
        return info;
    }
    // RawSMBIOSData: 4 bytes (calling method, major, minor, DMI revision),
    // then the table length (4 bytes), then the table itself.
    quint32 length = 0;
    std::memcpy(&length, buffer.constData() + 4, sizeof length);
    const SmbiosIds ids = parseSmbios(buffer.mid(8, length));
    info.vendor = ids.vendor;
    info.model = ids.model;
    info.serial = ids.serial;
    if (info.serial.isEmpty())
        info.serialError = QStringLiteral("the SMBIOS system serial number is empty");
    info.chassisType = ids.chassisType;

    // The CPU name Windows shows in Settings. Reading the registry is fine:
    // the app never writes to it. simplified(): Intel pads the name with spaces.
    const QSettings cpuKey(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"),
                           QSettings::NativeFormat);
    info.cpu = cpuKey.value(QStringLiteral("ProcessorNameString")).toString().simplified();

    // Installed RAM (Windows takes it from SMBIOS too), in KiB. If the
    // firmware does not say, the RAM Windows can use, a bit lower.
    QString ramSource = QStringLiteral("installed");
    ULONGLONG kib = 0;
    if (GetPhysicallyInstalledSystemMemory(&kib)) {
        info.ramBytes = double(kib) * 1024.0;
    } else {
        ramSource = QStringLiteral("usable (installed unknown)");
        MEMORYSTATUSEX mem{};
        mem.dwLength = sizeof mem;
        if (GlobalMemoryStatusEx(&mem))
            info.ramBytes = double(mem.ullTotalPhys);
    }

    info.raw = QStringLiteral("SMBIOS %1.%2, %3 bytes, system structure %4\n"
                              "vendor=%5\nmodel=%6\nserial=%7\nchassis type=%8\n"
                              "registry ProcessorNameString=%9\nRAM %10 bytes (%11)")
                   .arg(int(quint8(buffer[1]))).arg(int(quint8(buffer[2]))).arg(length)
                   .arg(ids.hasSystem ? QStringLiteral("found") : QStringLiteral("MISSING"))
                   .arg(info.vendor, info.model, info.serial.isEmpty() ? info.serialError : info.serial)
                   .arg(info.chassisType)
                   .arg(info.cpu)
                   .arg(info.ramBytes, 0, 'f', 0)
                   .arg(ramSource);
    return info;
}
