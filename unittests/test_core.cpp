#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>
#include <QtTest>

#include "core/ProgramFolder.h"
#include "core/TestRunner.h"
#include "core/Thresholds.h"
#include "core/Verdict.h"
#include "delivery/DeliverySheet.h"
#include "delivery/XlsxEditor.h"
#include "report/RunReport.h"
#include "tests/battery/BatteryTest.h"
#include "tests/disk/DiskTest.h"
#include "tests/drivers/DriversTest.h"
#include "tests/license/LicenseTest.h"
#include "tests/keyboard/KeyLayout.h"
#include "tests/audio/AudioCheck.h"
#include "tests/camera/FrameCheck.h"
#include "tests/cputemp/CpuTempTest.h"
#include "tests/sysinfo/SystemInfoTest.h"
#include "miniz.h"

// Lets QCOMPARE print Status values on failure (found via ADL).
char *toString(Status s)
{
    return qstrdup(qPrintable(statusLabel(s)));
}

// Fake test for the runner: answers only when the unit test calls answer().
class FakeTest : public ITest {
public:
    FakeTest(bool user, bool last) : m_user(user), m_last(last) {}
    QString name() const override { return QStringLiteral("fake"); }
    bool needsUser() const override { return m_user; }
    bool runsLast() const override { return m_last; }
    void run() override { ++runs; }
    void answer(Status s)
    {
        TestResult r;
        r.status = s;
        emit finished(r);
    }
    int runs = 0;

private:
    bool m_user;
    bool m_last;
};

class TestCore : public QObject {
    Q_OBJECT

    static Thresholds loaded()
    {
        Thresholds t;
        const bool ok = t.loadFromJson(R"({
            "battery_health_pct": {"warn": 70, "fail": 50},
            "bad_sectors":        {"warn": 1,  "fail": 50},
            "disk_reallocated_sectors": {"warn": 1,  "fail": 50},
            "disk_pending_sectors":     {"warn": 1,  "fail": 10},
            "nvme_percentage_used":     {"warn": 80, "fail": 100},
            "disk_temperature_c":       {"warn": 55, "fail": 65},
            "cpu_temp_peak_c":          {"warn": 90, "fail": 97},
            "cpu_speed_min_pct":        {"warn": 100, "fail": 70}
        })");
        Q_ASSERT(ok);
        Q_UNUSED(ok);
        return t;
    }

    static BatteryInfo battery(double full)
    {
        BatteryInfo b;
        b.present = true;
        b.designCapacity = 50000;
        b.fullCapacity = full;
        return b;
    }

    static SystemInfo fullSystem()
    {
        SystemInfo s;
        s.vendor = "LENOVO";
        s.model = "ThinkPad T480";
        s.serial = "PF1ABCDE";
        s.cpu = "Intel(R) Core(TM) i5-8350U CPU @ 1.70GHz";
        s.ramBytes = 8.0 * 1024 * 1024 * 1024;
        s.chassisType = 10;   // Notebook
        return s;
    }

    static CpuLoadRun cpuRun(double idle, std::vector<double> samples)
    {
        CpuLoadRun run;
        run.idle.hasTemp = true;
        run.idle.celsius = idle;
        run.idle.source = "test";
        run.samples = std::move(samples);
        return run;
    }

    // A PC with no temperature sensor but a speed counter (Latitude 7420).
    static CpuLoadRun speedRun(std::vector<double> speeds, bool onBattery = false)
    {
        CpuLoadRun run;
        run.idle.hasSpeed = true;
        run.idle.speedPct = 85;
        run.speeds = std::move(speeds);
        run.onBattery = onBattery;
        return run;
    }

    // Golden samples live in unittests/samples/ (found via QFINDTESTDATA).
    static QByteArray sample(const QString& file)
    {
        QFile f(QFINDTESTDATA("samples/" + file));
        if (!f.open(QIODevice::ReadOnly))
            qFatal("Missing test sample %s", qPrintable(file));
        return f.readAll();
    }

    // Every file inside an .xlsx (a zip), in order: to compare two versions.
    static std::vector<std::pair<QString, QByteArray>> zipEntries(const QByteArray& zip)
    {
        std::vector<std::pair<QString, QByteArray>> entries;
        mz_zip_archive z{};
        if (!mz_zip_reader_init_mem(&z, zip.constData(), size_t(zip.size()), 0))
            return entries;
        for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z); ++i) {
            char name[512] = {};
            mz_zip_reader_get_filename(&z, i, name, sizeof name);
            size_t size = 0;
            void* data = mz_zip_reader_extract_to_heap(&z, i, &size, 0);
            entries.emplace_back(QString::fromUtf8(name), QByteArray(static_cast<const char*>(data), qsizetype(size)));
            mz_free(data);
        }
        mz_zip_reader_end(&z);
        return entries;
    }

    // Style number (s="...") of a cell in a sheet's raw XML, whatever the attribute order.
    static QString cellStyle(const QByteArray& xml, const QString& ref)
    {
        const QRegularExpression cell(QStringLiteral("<c [^>]*r=\"%1\"[^>]*>").arg(ref));
        const QRegularExpression style(QStringLiteral(" s=\"(\\d+)\""));
        return style.match(cell.match(QString::fromUtf8(xml)).captured(0)).captured(1);
    }

    static const QString& checkTab()
    {
        static const QString tab = QStringLiteral("Double V\u00e9rification PC");
        return tab;
    }

    // The French copy was saved by OnlyOffice, the Dutch one by LibreOffice:
    // their XML differs (dropdowns stored differently), the layout does not.
    static void sheetFiles()
    {
        QTest::addColumn<QString>("file");
        QTest::newRow("fr OnlyOffice") << QStringLiteral("delivery_sheet_fr.xlsx");
        QTest::newRow("nl LibreOffice") << QStringLiteral("delivery_sheet_nl.xlsx");
    }

private slots:
    void lowerIsWorse()
    {
        const Thresholds t = loaded();
        QCOMPARE(t.evaluate("battery_health_pct", 90), Status::Pass);
        QCOMPARE(t.evaluate("battery_health_pct", 70), Status::Pass);
        QCOMPARE(t.evaluate("battery_health_pct", 69), Status::Warn);
        QCOMPARE(t.evaluate("battery_health_pct", 49), Status::Fail);
    }

    void higherIsWorse()
    {
        const Thresholds t = loaded();
        QCOMPARE(t.evaluate("bad_sectors", 0), Status::Pass);
        QCOMPARE(t.evaluate("bad_sectors", 1), Status::Warn);
        QCOMPARE(t.evaluate("bad_sectors", 50), Status::Fail);
    }

    void badJsonIsRejected()
    {
        Thresholds t;
        QString err;
        QVERIFY(!t.loadFromJson("{ not json", &err));
        QVERIFY(!t.loadFromJson(R"({"x": {"warn": 1}})", &err));
        QVERIFY(!t.has("x"));
    }

    void batteryHealth()
    {
        const Thresholds t = loaded();
        QCOMPARE(BatteryTest::evaluate(battery(40000), t).status, Status::Pass);  // 80 %
        QCOMPARE(BatteryTest::evaluate(battery(30000), t).status, Status::Warn);  // 60 %
        QCOMPARE(BatteryTest::evaluate(battery(20000), t).status, Status::Fail);  // 40 %
    }

    void batteryAbsentIsSkipped()
    {
        QCOMPARE(BatteryTest::evaluate(BatteryInfo{}, loaded()).status, Status::Skipped);
    }

    void batteryReadErrorIsError()
    {
        BatteryInfo b;
        b.error = "boom";
        QCOMPARE(BatteryTest::evaluate(b, loaded()).status, Status::Error);
    }

    void missingThresholdIsErrorNotPass()
    {
        QCOMPARE(BatteryTest::evaluate(battery(40000), Thresholds{}).status, Status::Error);
    }

    // smartctl samples are hand-made from the smartctl 7.x JSON format
    // (serials removed). Replace them with real outputs when available.
    static SmartctlRun smart(const QString& file, int exitCode)
    {
        SmartctlRun r;
        r.device = "/dev/test";
        r.type = "auto";
        r.exitCode = exitCode;
        r.json = sample(file);
        return r;
    }

    // Real smartctl 7.5 outputs (Windows, shop laptops, 2026-10-15; serials and
    // EUI-64 anonymised). Each must give what the app showed on that PC.
    void smartctlRealSamples_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("summary");
        QTest::newRow("Dell 7420 Samsung PM9A1") << "smartctl_real_samsung_pm9a1.json"
            << "/dev/test PM9A1 NVMe Samsung 256GB 256 GB: SMART OK, 6% worn, 31 C, 1480 h";
        QTest::newRow("Dell 7430 WD SN810") << "smartctl_real_wd_sn810.json"
            << "/dev/test PC SN810 NVMe WDC 512GB 512 GB: SMART OK, 21% worn, 26 C, 2868 h";
        QTest::newRow("Lenovo 20M6 Intel") << "smartctl_real_intel_ssdpekkf256g8l.json"
            << "/dev/test INTEL SSDPEKKF256G8L 256 GB: SMART OK, 6% worn, 42 C, 14621 h";
        QTest::newRow("Lenovo 20M6 Samsung MZVLW") << "smartctl_real_samsung_mzvlw256.json"
            << "/dev/test SAMSUNG MZVLW256HEHP-000L7 256 GB: SMART OK, 5% worn, 37 C, 3860 h";
        QTest::newRow("Surface Pro 7 SK Hynix") << "smartctl_real_skhynix_hfb1m8.json"
            << "/dev/test HFB1M8MO331C0MR 256 GB: SMART OK, 25% worn, 46 C, 2585 h";
        QTest::newRow("HP ZBook WD SN730") << "smartctl_real_wd_sn730.json"
            << "/dev/test WDC PC SN730 SDBPNTY-1T00-1006 1024 GB: SMART OK, 1% worn, 29 C, 3307 h";
        QTest::newRow("HP ZBook Micron") << "smartctl_real_micron_mtfdkba1t0tfh.json"
            << "/dev/test MTFDKBA1T0TFH-1BC1AABHA 1024 GB: SMART OK, 3% worn, 22 C, 2454 h";
    }

    void smartctlRealSamples()
    {
        QFETCH(QString, file);
        QFETCH(QString, summary);
        const TestResult r = DiskTest::evaluateDisk(smart(file, 0), loaded());
        QCOMPARE(r.status, Status::Pass);
        QCOMPARE(r.summary, summary);
        QVERIFY(r.details.contains("\"serial_number\": \"ANONYMISED\""));   // raw JSON kept in the details
    }

    // The tester's own USB stick claims "SMART OK" with a fake 0 C: USB disks
    // are left out by their bus (Windows IOCTL / sysfs), not by what smartctl says.
    void smartctlUsbLeftOut()
    {
        QCOMPARE(DiskTest::evaluateDisk(smart("smartctl_real_usb_sandisk_cruzer.json", 4), loaded()).summary,
                 QString("/dev/test SanDisk Cruzer 16 GB: SMART OK, 0 C"));

        // Windows: /dev/sd + letter N = \\.\PhysicalDriveN, asked for its bus.
        QCOMPARE(physicalDriveNumber("/dev/sda"), 0);
        QCOMPARE(physicalDriveNumber("/dev/sdc"), 2);
        QCOMPARE(physicalDriveNumber("/dev/nvme0"), -1);   // other names: not asked, so checked
        QCOMPARE(physicalDriveNumber("/dev/sdaa"), -1);
        QCOMPARE(physicalDriveNumber("/dev/sd"), -1);

        DiskScan scan;
        scan.disks = {smart("smartctl_real_intel_ssdpekkf256g8l.json", 0)};
        scan.usbLeftOut = {"/dev/sdb"};
        const TestResult r = DiskTest::evaluate(scan, loaded());
        QCOMPARE(r.status, Status::Pass);
        QVERIFY(!r.summary.contains("SanDisk"));
        QVERIFY(r.details.contains("Left out (USB, not part of this PC): /dev/sdb"));

        scan.disks.clear();   // only a USB stick: nothing of the PC was checked
        QCOMPARE(DiskTest::evaluate(scan, loaded()).status, Status::Error);
    }

    void smartctlScan()
    {
        QString err;
        const auto devices = parseScan(sample("smartctl_scan.json"), &err);
        QVERIFY(err.isEmpty());
        QCOMPARE(devices.size(), size_t(2));
        QCOMPARE(devices[0].name, QString("/dev/sda"));
        QCOMPARE(devices[0].type, QString("sat"));
        QCOMPARE(devices[1].type, QString("nvme"));
        parseScan("garbage", &err);
        QVERIFY(!err.isEmpty());
    }

    void diskNvmeOk()
    {
        const TestResult r = DiskTest::evaluateDisk(smart("smartctl_nvme_ok.json", 0), loaded());
        QCOMPARE(r.status, Status::Pass);
        QVERIFY2(r.summary.contains("512 GB"), qPrintable(r.summary));
        QVERIFY2(r.summary.contains("2% worn"), qPrintable(r.summary));
        QVERIFY2(r.summary.contains("1234 h"), qPrintable(r.summary));
    }

    void diskAtaWornIsFail()
    {
        // 60 reallocated >= 50 -> Fail, 3 pending -> Warn: the worst wins.
        const TestResult r = DiskTest::evaluateDisk(smart("smartctl_ata_worn.json", 0), loaded());
        QCOMPARE(r.status, Status::Fail);
        QVERIFY2(r.summary.contains("60 reallocated (FAIL)"), qPrintable(r.summary));
        QVERIFY2(r.summary.contains("3 pending (WARN)"), qPrintable(r.summary));
        QVERIFY2(r.summary.contains("31000 h"), qPrintable(r.summary));
    }

    void diskSmartFailedIsFail()
    {
        // exit 24 = bits 3 + 4: disk failing, data still valid.
        const TestResult r = DiskTest::evaluateDisk(smart("smartctl_ata_failed.json", 24), loaded());
        QCOMPARE(r.status, Status::Fail);
        QVERIFY(r.summary.contains("SMART FAILED"));
    }

    void diskNoSmartIsSkipped()
    {
        QCOMPARE(DiskTest::evaluateDisk(smart("smartctl_usb_nosmart.json", 4), loaded()).status,
                 Status::Skipped);
    }

    void diskPermissionDeniedIsError()
    {
        const TestResult r = DiskTest::evaluateDisk(smart("smartctl_permission_denied.json", 2), loaded());
        QCOMPARE(r.status, Status::Error);
        QVERIFY(r.details.contains("Permission denied"));
        QVERIFY(r.details.contains("root"));
    }

    void diskMissingThresholdIsError()
    {
        QCOMPARE(DiskTest::evaluateDisk(smart("smartctl_nvme_ok.json", 0), Thresholds{}).status,
                 Status::Error);
    }

    void diskScanCombinesWorst()
    {
        DiskScan scan;
        scan.disks = {smart("smartctl_nvme_ok.json", 0), smart("smartctl_usb_nosmart.json", 4)};
        QCOMPARE(DiskTest::evaluate(scan, loaded()).status, Status::Pass);   // USB stick ignored
        scan.disks.push_back(smart("smartctl_ata_worn.json", 0));
        QCOMPARE(DiskTest::evaluate(scan, loaded()).status, Status::Fail);
    }

    void diskNothingCheckedIsError()
    {
        DiskScan scan;
        QCOMPARE(DiskTest::evaluate(scan, loaded()).status, Status::Error);   // no disk
        scan.disks = {smart("smartctl_usb_nosmart.json", 4)};
        QCOMPARE(DiskTest::evaluate(scan, loaded()).status, Status::Error);   // only skipped
        scan = DiskScan{};
        scan.error = "smartctl not found";
        QCOMPARE(DiskTest::evaluate(scan, loaded()).status, Status::Error);
    }

    // Fixable failures get one line saying what to do, not one error per disk.
    void diskFixableFailuresSayWhatToDo()
    {
        DiskScan scan;
        scan.error = "smartctl not found";
        scan.failure = SmartctlFailure::NotFound;
        QVERIFY(DiskTest::evaluate(scan, loaded()).summary.contains("tools folder"));

        scan = DiskScan{};
        for (const char* dev : {"/dev/sda", "/dev/nvme0"}) {
            SmartctlRun run;
            run.device = dev;
            run.error = "Administrator password refused or cancelled (pkexec exit code 126)";
            run.failure = SmartctlFailure::PasswordRefused;
            scan.disks.push_back(run);
        }
        const TestResult r = DiskTest::evaluate(scan, loaded());
        QCOMPARE(r.status, Status::Error);
        QVERIFY2(r.summary.contains("press START again"), qPrintable(r.summary));
        QVERIFY2(!r.summary.contains("/dev/sda"), qPrintable(r.summary));   // said once, not per disk
    }

    // Devices as drivers_win.cpp reports them (SetupAPI: name, device ID,
    // problem code), from a ThinkPad with chipset drivers missing.
    void driversWindowsProblems()
    {
        DriverInfo info;
        info.problems = {
            {"PCI Data Acquisition and Signal Processing Controller",
             "PCI\\VEN_8086&DEV_9D31&SUBSYS_506D17AA&REV_21\\3&11583659&0&A0", 28},
            {"Intel(R) Wireless Bluetooth(R)", "USB\\VID_8087&PID_0A2B\\5&2F2D5B5&0&7", 22},
        };
        info.raw = "code 28: ...";
        const TestResult r = DriversTest::evaluate(info);
        QCOMPARE(r.status, Status::Warn);
        QVERIFY2(r.summary.startsWith("2 device(s)"), qPrintable(r.summary));
        QVERIFY(r.summary.contains("no driver installed"));
        QVERIFY(r.summary.contains("disabled"));
        QVERIFY(r.details.contains("code 28: ..."));   // raw readings kept
    }

    void driversWindowsNoProblem()
    {
        QCOMPARE(DriversTest::evaluate(DriverInfo{}).status, Status::Pass);
    }

    void driversProbeFailureIsError()
    {
        DriverInfo info;
        info.error = "SetupDiGetClassDevs failed (error 5)";
        const TestResult r = DriversTest::evaluate(info);
        QCOMPARE(r.status, Status::Error);
        QVERIFY(r.details.contains("error 5"));
    }

    void driversPciClasses()
    {
        QVERIFY(pciClassMatters(0x020000));    // Ethernet
        QVERIFY(pciClassMatters(0x028000));    // Wi-Fi
        QVERIFY(pciClassMatters(0x030000));    // VGA
        QVERIFY(pciClassMatters(0x040300));    // HD audio
        QVERIFY(pciClassMatters(0x0c0330));    // USB xHCI
        QVERIFY(!pciClassMatters(0x0c0500));   // SMBus
        QVERIFY(!pciClassMatters(0x088000));   // system peripheral
        QVERIFY(!pciClassMatters(0x050000));   // RAM
        QVERIFY(!pciClassMatters(0x060100));   // ISA bridge
    }

    // As license_win.cpp fills it (SL_LICENSING_STATUS of the best licence).
    static LicenseInfo windowsLicense(int status, bool oemKey)
    {
        LicenseInfo info;
        info.name = "Windows(R), Professional edition";
        info.description = "Windows(R) Operating System, OEM_DM channel";
        info.status = status;
        info.oemKeyPresent = oemKey;
        return info;
    }

    void licenseActivated()
    {
        const TestResult r = LicenseTest::evaluate(windowsLicense(1, true));
        QCOMPARE(r.status, Status::Pass);
        QVERIFY(r.summary.contains("Professional"));
        QVERIFY(r.summary.contains("OEM key in firmware: yes"));
    }

    void licenseNotActivatedIsWarn()
    {
        const TestResult r = LicenseTest::evaluate(windowsLicense(3, true));
        QCOMPARE(r.status, Status::Warn);
        QVERIFY(r.summary.contains("NOT activated (notification mode"));
        QVERIFY(r.details.startsWith("Hint"));   // firmware key exists
        QVERIFY(LicenseTest::evaluate(windowsLicense(2, false)).summary.contains("grace period"));
    }

    void licenseNoneIsWarn()
    {
        const TestResult r = LicenseTest::evaluate(windowsLicense(-1, false));
        QCOMPARE(r.status, Status::Warn);
        QVERIFY(r.summary.contains("No Windows licence installed"));
        QVERIFY(r.summary.contains("OEM key in firmware: no"));
    }

    void licenseProbeFailureIsError()
    {
        LicenseInfo info;
        info.error = "SLGetLicensingStatusInformation failed (0xc004f012)";
        const TestResult r = LicenseTest::evaluate(info);
        QCOMPARE(r.status, Status::Error);
        QVERIFY(r.details.contains("0xc004f012"));
    }

    void licenseOnLinuxIsSkipped()
    {
        LicenseInfo info;
        info.applicable = false;
        QCOMPARE(LicenseTest::evaluate(info).status, Status::Skipped);
    }

    // The real config file: a typo in it would fail here, not in the shop.
    static KeyLayout shippedLayout()
    {
        QFile f(QFINDTESTDATA("../config/keyboard_layout.json"));
        if (!f.open(QIODevice::ReadOnly))
            qFatal("config/keyboard_layout.json not found");
        return parseKeyLayout(f.readAll());
    }

    void keyLayoutShipped()
    {
        const KeyLayout l = shippedLayout();
        QVERIFY2(l.error.isEmpty(), qPrintable(l.error));
        const KeyTracker t(l);
        QVERIFY(t.requiredCount() > 60);
        QVERIFY(t.missingRequired().contains("Esc"));
        QVERIFY(!t.missingRequired().contains("PrtSc"));   // optional
    }

    void keyLayoutBadJsonIsError()
    {
        QVERIFY(!parseKeyLayout("nope").error.isEmpty());
        QVERIFY(!parseKeyLayout("{\"rows\": []}").error.isEmpty());
        QVERIFY(!parseKeyLayout("{\"rows\": [[{\"label\": \"A\", \"code\": \"zz\"}]]}").error.isEmpty());
    }

    void scanCodesLinux()
    {
        // xkb keycode = evdev + 8
        QCOMPARE(scanCodeFromXkb(1 + 8), 0x01);      // Esc
        QCOMPARE(scanCodeFromXkb(30 + 8), 0x1E);     // A on QWERTY = Q on AZERTY: same key
        QCOMPARE(scanCodeFromXkb(86 + 8), 0x56);     // ISO < key
        QCOMPARE(scanCodeFromXkb(88 + 8), 0x58);     // F12
        QCOMPARE(scanCodeFromXkb(29 + 8), 0x1D);     // left Ctrl
        QCOMPARE(scanCodeFromXkb(97 + 8), 0x11D);    // right Ctrl
        QCOMPARE(scanCodeFromXkb(100 + 8), 0x138);   // AltGr
        QCOMPARE(scanCodeFromXkb(103 + 8), 0x148);   // Up
        QCOMPARE(scanCodeFromXkb(69 + 8), 0x145);    // NumLock, not Pause
        QCOMPARE(scanCodeFromXkb(119 + 8), 0x45);    // Pause
        QCOMPARE(scanCodeFromXkb(3), 0);             // invalid
        QCOMPARE(scanCodeFromXkb(240 + 8), 0);       // unknown key
    }

    void cpuTempPeak()
    {
        const Thresholds t = loaded();
        QCOMPARE(CpuTempTest::evaluate(cpuRun(40, {60, 75, 80}), t).status, Status::Pass);
        QCOMPARE(CpuTempTest::evaluate(cpuRun(40, {80, 92}), t).status, Status::Warn);
        QCOMPARE(CpuTempTest::evaluate(cpuRun(40, {90, 100}), t).status, Status::Fail);
    }

    void cpuTempStaticSensorIsNotPass()
    {
        // ACPI zone stuck at 27.8 C under full load: not the CPU.
        const Thresholds t = loaded();
        QCOMPARE(CpuTempTest::evaluate(cpuRun(27.8, {27.8, 27.8}), t).status, Status::Skipped);
        // Stuck but too hot is still reported.
        QCOMPARE(CpuTempTest::evaluate(cpuRun(99, {99, 99}), t).status, Status::Fail);
    }

    void cpuTempMissingDataIsNotPass()
    {
        const Thresholds t = loaded();
        QCOMPARE(CpuTempTest::evaluate(CpuLoadRun{}, t).status, Status::Skipped);   // no sensor
        QCOMPARE(CpuTempTest::evaluate(cpuRun(40, {}), t).status, Status::Error);   // no sample
        QCOMPARE(CpuTempTest::evaluate(cpuRun(40, {70}), Thresholds{}).status, Status::Error);
        CpuLoadRun failed;
        failed.idle.error = "boom";
        QCOMPARE(CpuTempTest::evaluate(failed, t).status, Status::Error);
    }

    void cpuSpeedThrottling()
    {
        const Thresholds t = loaded();
        // Real Latitude 7420 on the charger: turbo, then the slow fall of the turbo budget.
        const TestResult ok = CpuTempTest::evaluate(speedRun({266, 266, 263, 256, 241}), t);
        QCOMPARE(ok.status, Status::Pass);
        QVERIFY(ok.summary.startsWith("Speed 256 %"));   // median of the second half: 263 256 241
        // One dip is a snapshot, not throttling; a lasting drop is.
        QCOMPARE(CpuTempTest::evaluate(speedRun({220, 220, 220, 90, 220, 220}), t).status, Status::Pass);
        QCOMPARE(CpuTempTest::evaluate(speedRun({220, 220, 90, 90, 90}), t).status, Status::Warn);
        QCOMPARE(CpuTempTest::evaluate(speedRun({180, 95}), t).status, Status::Warn);   // under base speed
        QCOMPARE(CpuTempTest::evaluate(speedRun({180, 60}), t).status, Status::Fail);   // far under
        QCOMPARE(CpuTempTest::evaluate(speedRun({100}), t).status, Status::Pass);       // exactly base
        // On battery the OS slows the CPU on purpose: a hint, never a Fail.
        const TestResult battery = CpuTempTest::evaluate(speedRun({180, 60}, true), t);
        QCOMPARE(battery.status, Status::Warn);
        QVERIFY(battery.summary.contains("charger"));
        QCOMPARE(CpuTempTest::evaluate(speedRun({211}, true), t).status, Status::Pass);
    }

    void cpuTempAndSpeedWorseWins()
    {
        const Thresholds t = loaded();
        CpuLoadRun run = cpuRun(40, {60, 70});
        run.idle.hasSpeed = true;
        run.speeds = {60};
        QCOMPARE(CpuTempTest::evaluate(run, t).status, Status::Fail);    // cool but throttling
        run.speeds = {240};
        QCOMPARE(CpuTempTest::evaluate(run, t).status, Status::Pass);
        QVERIFY(CpuTempTest::evaluate(run, t).summary.contains("240 %"));
        // A sensor that ignores the load (Skipped) does not hide a good speed.
        run.idle.celsius = 27.8;
        run.samples = {27.8, 27.8};
        QCOMPARE(CpuTempTest::evaluate(run, t).status, Status::Pass);
    }

    void cpuSpeedMissingDataIsNotPass()
    {
        QCOMPARE(CpuTempTest::evaluate(speedRun({}), loaded()).status, Status::Error);      // no sample
        QCOMPARE(CpuTempTest::evaluate(speedRun({240}), Thresholds{}).status, Status::Error);
    }

    // Real Windows run: under full load a reading took ~6 s, so "leave out the
    // first 5 readings" left no speed at all. The settling time is in seconds.
    void cpuCollectSlowReadings()
    {
        const auto speed = [](qint64 atMs, double pct) {
            CpuReading r;
            r.hasSpeed = true;
            r.speedPct = pct;
            r.atMs = atMs;
            return r;
        };
        CpuReading noSpeed;
        noSpeed.atMs = 18000;
        noSpeed.raw = "{\"cpu\":null}";
        const CpuLoadRun run = CpuTempTest::collect(
            {speed(0, 273), speed(1000, 270), speed(6200, 266), speed(12400, 250), noSpeed, speed(24800, 241)});

        QCOMPARE(run.idle.speedPct, 273.0);
        QCOMPARE(run.speeds, (std::vector<double>{266, 250, 241}));   // the 1 s reading is left out
        QCOMPARE(run.loadReadings, 5);
        QCOMPARE(run.lastMs, qint64(24800));
        QVERIFY(run.sampleErrors.contains("no speed at 18.0 s"));     // a reason in the details
        QCOMPARE(CpuTempTest::evaluate(run, loaded()).status, Status::Pass);
        QVERIFY(CpuTempTest::collect({}).idle.error.size() > 0);       // never a silent empty run
    }

    void cpuWindowsReading()
    {
        // Values read on a real Latitude 7420 (WMI then): no ACPI zone, 88 % at rest on battery.
        const CpuReading rest = windowsCpuReading({}, 88, true);
        QVERIFY(rest.error.isEmpty());
        QVERIFY(!rest.hasTemp);
        QVERIFY(rest.hasSpeed);
        QCOMPARE(rest.speedPct, 88.0);
        QVERIFY(rest.onBattery);
        QVERIFY(rest.raw.contains("zones: none"));

        // A counter that is not ready gives 0: not a speed.
        QVERIFY(!windowsCpuReading({}, 0, false).hasSpeed);
    }

    void runReportText()
    {
        RunReport report;
        report.when = QDateTime(QDate(2026, 10, 15), QTime(14, 32, 5));
        report.version = "v1.0.0-beta.1";
        report.model = "Dell Latitude 7420";
        report.verdict = "SELL WITH NOTE - see warnings";
        TestResult cpu;
        cpu.status = Status::Warn;
        cpu.summary = "Slowed to 83 % of base speed on battery";
        cpu.details = "Load: 8 threads, 30 s\nIdle speed: 123 % of base\n";
        report.rows.push_back({"CPU temperature", cpu});
        report.rows.push_back({"Camera", TestResult{}});   // never run: Skipped, empty

        const QString text = formatRunReport(report);
        QVERIFY(text.startsWith("Lazy Djin's Tester v1.0.0-beta.1 - 2026-10-15 14:32:05\nPC: Dell Latitude 7420\n"
                                "Verdict: SELL WITH NOTE - see warnings\n"));
        QVERIFY(text.contains("\n==== [WARN] CPU temperature ====\nSlowed to 83 % of base speed on battery\n\n"
                              "Load: 8 threads, 30 s\nIdle speed: 123 % of base\n"));   // details as in the pane
        QVERIFY(text.contains("==== [SKIPPED] Camera ===="));
        report.model.clear();
        QVERIFY(formatRunReport(report).contains("PC: unknown\n"));
        report.version.clear();
        QVERIFY(formatRunReport(report).startsWith("Lazy Djin's Tester (unknown version) - "));
        // smartctl.exe output has \r\n: one line break in the file, not two.
        report.rows[0].result.details = "{\r\n  \"a\": 1\r\n}\r\n";
        QVERIFY(formatRunReport(report).contains("{\n  \"a\": 1\n}"));
        QVERIFY(!formatRunReport(report).contains('\r'));
    }

    // Root = the folder above the program's folder: app/ in the package,
    // build/ when developing. Linux: above the .AppImage FILE's folder.
    void rootFolder()
    {
        QDir expected(QCoreApplication::applicationDirPath());
        expected.cdUp();
        QCOMPARE(folderAroundProgram().absolutePath(), expected.absolutePath());
        QCOMPARE(configFile("thresholds.json"), expected.absoluteFilePath("config/thresholds.json"));

        qputenv("APPIMAGE", "/media/stick/Lazy Djin's Tester/app/lazy-djin-tester-x86_64.AppImage");
        QCOMPARE(folderAroundProgram().absolutePath(), QString("/media/stick/Lazy Djin's Tester"));
        qunsetenv("APPIMAGE");

        // build/logs when the tests run: created on first use.
        QString error;
        const QString logs = rootSubfolder("logs", &error);
        QVERIFY2(!logs.isEmpty(), qPrintable(error));
        QCOMPARE(logs, expected.absoluteFilePath("logs"));
        QVERIFY(QDir(logs).exists());
    }

    void runReportFileName_()
    {
        const QDateTime when(QDate(2026, 10, 15), QTime(14, 32, 5));
        QCOMPARE(runReportFileName(when, "Dell Latitude 7420"), QString("2026-10-15 14-32-05 Dell Latitude 7420.txt"));
        // Characters Windows refuses, and trailing dots/spaces, never reach the file name.
        QCOMPARE(runReportFileName(when, "LENOVO 20HD/0001:x?. "), QString("2026-10-15 14-32-05 LENOVO 20HD_0001_x_.txt"));
        QCOMPARE(runReportFileName(when, ""), QString("2026-10-15 14-32-05 unknown PC.txt"));
        QCOMPARE(runReportFileName(when, " ... "), QString("2026-10-15 14-32-05 unknown PC.txt"));
    }

    // Real runs of the app on Windows (2026-10-14).
    void cpuRealWindowsRuns()
    {
        const Thresholds t = loaded();

        // ThinkPad L380: the ACPI zone stays at 77.1 C under load (not the CPU),
        // the speed is live and settles at ~150 %. Values of the real output (WMI then).
        CpuLoadRun lenovo;
        lenovo.idle = windowsCpuReading({{"ACPI\\ThermalZone\\THM0_0", 350.2}}, 67, false);
        QVERIFY(lenovo.idle.hasTemp);
        QCOMPARE(lenovo.idle.celsius, 350.2 - 273.15);
        QCOMPARE(lenovo.idle.speedPct, 67.0);
        lenovo.samples.assign(6, lenovo.idle.celsius);
        lenovo.speeds = {184, 160, 151, 150, 148, 150};
        const TestResult l = CpuTempTest::evaluate(lenovo, t);
        QCOMPARE(l.status, Status::Pass);   // a deaf sensor does not hide a good speed
        QVERIFY(l.summary.contains("barely reacted"));
        QVERIFY(l.summary.contains("150 %"));

        // Latitude 7420, 3rd load in a row on battery: turbo budget spent, under base speed.
        CpuLoadRun dell = speedRun({266, 266, 210, 106, 82, 86, 82, 91, 83, 84, 93, 82, 81}, true);
        const TestResult d = CpuTempTest::evaluate(dell, t);
        QCOMPARE(d.status, Status::Warn);   // on battery: never Fail
        QVERIFY(d.summary.contains("83 %"));
        QVERIFY(d.summary.contains("charger"));

        // Surface Pro 7 (Win11, 2026-10-27): the zone rises by 1.0 C at 220 % speed.
        // A case zone, not the CPU: the speed alone decides.
        CpuLoadRun surface = speedRun({220, 220, 220, 196, 178, 176, 177}, false);
        surface.idle = windowsCpuReading({{"\\_SB._SAN.TZ01", 322.05}, {"\\_SB._SAN.TZ02", 298}}, 63, false);
        QCOMPARE(qRound(surface.idle.celsius), 49);
        surface.samples = {48.9, 48.9, 48.9, 49.9};
        const TestResult s = CpuTempTest::evaluate(surface, t);
        QCOMPARE(s.status, Status::Pass);
        QVERIFY(s.summary.contains("barely reacted"));
        QVERIFY(!s.summary.contains("Peak"));
        // +3 C (another Surface, 29.9 -> 32.9): still not the CPU.
        surface.idle = windowsCpuReading({{"\\_SB._SAN.TZ01", 303.05}}, 63, false);
        surface.samples = {29.9, 30.9, 31.9, 32.9};
        QVERIFY(CpuTempTest::evaluate(surface, t).summary.contains("barely reacted"));
    }

    void thermalZones()
    {
        const CpuReading r = windowsCpuReading({{"\\_TZ.TZ00", 313.2}, {"\\_TZ.THM0", 333.2}, {"\\_TZ.TZ01", 0}}, 0, false);
        QVERIFY(r.hasTemp);
        QCOMPARE(qRound(r.celsius), 60);   // hottest zone; the 0 K zone is ignored
        QCOMPARE(r.source, QString("\\_TZ.THM0"));
        QVERIFY(r.raw.contains("\\_TZ.TZ01 = 0 K"));   // still listed in the details

        QVERIFY(!windowsCpuReading({{"\\_TZ.BAD", 600}}, 0, false).hasTemp);   // 327 C: absurd
    }

    void scanCodesWindows()
    {
        QCOMPARE(scanCodeFromWindows(0x1E), 0x1E);
        QCOMPARE(scanCodeFromWindows(0x11D), 0x11D);         // extended bit kept
        QCOMPARE(scanCodeFromWindows(0x2000 | 0x38), 0x38);  // reserved bits dropped
        QCOMPARE(scanCodeFromWindows(0xE04B), 0x14B);        // Qt 6.8+: Left arrow
        QCOMPARE(scanCodeFromWindows(0xE038), 0x138);        // Qt 6.8+: AltGr
    }

    void keyTrackerFlow()
    {
        KeyTracker t(shippedLayout());
        QVERIFY(!t.press(0x7F));   // not in the layout
        QVERIFY(t.press(0x01));
        QVERIFY(t.press(0x137));   // optional PrtSc: counted, but not required
        QCOMPARE(t.requiredPressedCount(), 1);
        QVERIFY(!t.allRequiredPressed());

        const TestResult broken = t.result(KeyTracker::Outcome::KeyBroken);
        QCOMPARE(broken.status, Status::Fail);
        QVERIFY(broken.summary.contains("F1"));
        QVERIFY(broken.details.contains("0x7f"));
        QCOMPARE(t.result(KeyTracker::Outcome::Skipped).status, Status::Skipped);

        const KeyLayout l = shippedLayout();
        for (const auto& row : l.rows)
            for (const Key& k : row)
                if (k.required)
                    t.press(k.code);
        QVERIFY(t.allRequiredPressed());
        QVERIFY(t.missingRequired().isEmpty());
        QCOMPARE(t.result(KeyTracker::Outcome::AllPressed).status, Status::Pass);
    }

    void audioToneOneChannel()
    {
        const QByteArray d = makeTone(Channel::Left, 48000, 440, 1000);
        QCOMPARE(d.size(), qsizetype(48000 * 2 * 2));   // 1 s, 2 channels, 2 bytes
        const auto* s = reinterpret_cast<const std::int16_t*>(d.constData());
        int maxLeft = 0, maxRight = 0;
        for (int i = 0; i < 48000; ++i) {
            maxLeft = std::max(maxLeft, std::abs(int(s[2 * i])));
            maxRight = std::max(maxRight, std::abs(int(s[2 * i + 1])));
        }
        QVERIFY(maxLeft > 10000);      // clearly audible
        QVERIFY(maxLeft < 32767);      // no clipping
        QCOMPARE(maxRight, 0);         // the other side is silent
        QCOMPARE(int(s[0]), 0);        // fade in: starts at 0, no click

        const QByteArray r = makeTone(Channel::Right, 44100, 880, 500);
        QCOMPARE(int(reinterpret_cast<const std::int16_t*>(r.constData())[2 * 1000]), 0);   // left silent
    }

    // Surface Pro 7: the sound card only takes 32-bit float.
    void audioToneFloat()
    {
        const QByteArray d = makeTone(Channel::Right, 48000, 660, 1000, SampleType::Float);
        QCOMPARE(d.size(), qsizetype(48000 * 2 * 4));   // 1 s, 2 channels, 4 bytes
        const auto* s = reinterpret_cast<const float*>(d.constData());
        float maxLeft = 0, maxRight = 0;
        for (int i = 0; i < 48000; ++i) {
            maxLeft = std::max(maxLeft, std::abs(s[2 * i]));
            maxRight = std::max(maxRight, std::abs(s[2 * i + 1]));
        }
        QVERIFY(maxRight > 0.3f);      // clearly audible (half of full scale 1.0)
        QVERIFY(maxRight <= 0.5f);     // no clipping
        QCOMPARE(maxLeft, 0.0f);       // the other side is silent
        QCOMPARE(s[1], 0.0f);          // fade in: starts at 0, no click
    }

    void audioAnswers()
    {
        QCOMPARE(audioResult(Heard::Left, Heard::Right).status, Status::Pass);
        QCOMPARE(audioResult(Heard::Both, Heard::Right).status, Status::Warn);    // mono speaker
        QCOMPARE(audioResult(Heard::Both, Heard::Both).status, Status::Warn);
        QCOMPARE(audioResult(Heard::Right, Heard::Left).status, Status::Fail);    // swapped
        QCOMPARE(audioResult(Heard::Left, Heard::Nothing).status, Status::Fail);  // right dead
        QCOMPARE(audioResult(Heard::Nothing, Heard::Both).status, Status::Fail);  // fail beats warn
        QVERIFY(audioResult(Heard::Right, Heard::Left).summary.contains("swapped"));
        QCOMPARE(audioSkipped().status, Status::Skipped);
    }

    void smartctlBatchScript()
    {
        const std::vector<ScannedDevice> devs{{"/dev/sda", "sat"}, {"/dev/nvme0", "nvme"}};
        const QString script = buildBatchScript("/opt/my tools/smartctl", devs);
        QCOMPARE(script.count('\n'), 1);   // one line per disk
        QVERIFY(script.contains("'/opt/my tools/smartctl' '--json' '-a' '-d' 'sat' '/dev/sda'"));
        QVERIFY(script.contains(QString(kBatchMarker) + "$?"));

        QVERIFY(isSafeShellWord("/dev/nvme0"));
        QVERIFY(isSafeShellWord("sntasmedia/sat"));
        QVERIFY(!isSafeShellWord("/dev/sda; rm -rf /"));
        QVERIFY(!isSafeShellWord("$(reboot)"));
        QVERIFY(!isSafeShellWord(""));
        // An unsafe word refuses the whole script (outputs are matched by position).
        QVERIFY(buildBatchScript("/usr/bin/smartctl", {{"/dev/sda", "sat"}, {"/dev/x y", ""}}).isEmpty());
    }

    void smartctlBatchOutput()
    {
        const std::vector<ScannedDevice> devs{{"/dev/sda", "sat"}, {"/dev/nvme0", "nvme"}};
        QByteArray out = sample("smartctl_ata_worn.json");
        out += QByteArray(kBatchMarker) + "0\n";
        out += sample("smartctl_nvme_ok.json");
        out += QByteArray(kBatchMarker) + "4\n";
        const auto runs = parseBatchOutput(out, devs);
        QCOMPARE(runs.size(), size_t(2));
        QCOMPARE(runs[0].device, QString("/dev/sda"));
        QCOMPARE(runs[0].exitCode, 0);
        QCOMPARE(runs[1].exitCode, 4);
        QCOMPARE(DiskTest::evaluateDisk(runs[0], loaded()).status, Status::Fail);   // the worn disk
        QCOMPARE(DiskTest::evaluateDisk(runs[1], loaded()).status, Status::Pass);

        // Batch stopped after the first disk: the second one is an Error, not lost.
        const QByteArray cut = sample("smartctl_ata_worn.json") + QByteArray(kBatchMarker) + "0\n";
        const auto partial = parseBatchOutput(cut, devs);
        QCOMPARE(partial.size(), size_t(2));
        QVERIFY(!partial[1].error.isEmpty());
        QCOMPARE(DiskTest::evaluateDisk(partial[1], loaded()).status, Status::Error);
    }

    void cameraFrames()
    {
        const int w = 64, h = 48, stride = 72;   // stride > width: padding must be skipped
        std::vector<unsigned char> img(size_t(stride) * h, 255);   // padding = white

        std::fill(img.begin(), img.end(), 0);
        for (int y = 0; y < h; ++y)
            std::fill_n(img.begin() + y * stride + w, stride - w, 255);   // white padding only
        FrameStats s = lumaStats(img.data(), w, h, stride);
        QCOMPARE(s.mean, 0.0);
        QCOMPARE(classifyFrame(s), FrameKind::Black);

        for (int y = 0; y < h; ++y)
            std::fill_n(img.begin() + y * stride, w, 128);
        QCOMPARE(classifyFrame(lumaStats(img.data(), w, h, stride)), FrameKind::Flat);   // all grey

        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                img[size_t(y * stride + x)] = static_cast<unsigned char>((x * 4 + y * 2) % 256);
        s = lumaStats(img.data(), w, h, stride);
        QCOMPARE(classifyFrame(s), FrameKind::Good);   // a real scene has detail

        QCOMPARE(lumaStats(nullptr, w, h, stride).mean, 0.0);   // no crash
    }

    void cameraOutcomes()
    {
        const FrameStats s{120, 40};
        QCOMPARE(cameraResult(CameraOutcome::Good, "cam", s).status, Status::Pass);
        QCOMPARE(cameraResult(CameraOutcome::Broken, "cam", s).status, Status::Fail);
        QCOMPARE(cameraResult(CameraOutcome::Skipped, "cam", s).status, Status::Skipped);
        QCOMPARE(cameraResult(CameraOutcome::NoCamera, {}, {}).status, Status::Skipped);
        QCOMPARE(cameraResult(CameraOutcome::NoFrames, "cam", s).status, Status::Error);
        const TestResult e = cameraResult(CameraOutcome::CameraError, "cam", s, "busy");
        QCOMPARE(e.status, Status::Error);
        QVERIFY(e.details.contains("busy"));
    }

    void chassisMapping()
    {
        using FF = FormFactor;
        QCOMPARE(SystemInfoTest::formFactorFromChassis(3), FF::Desktop);
        QCOMPARE(SystemInfoTest::formFactorFromChassis(35), FF::Desktop);
        QCOMPARE(SystemInfoTest::formFactorFromChassis(10), FF::Laptop);
        QCOMPARE(SystemInfoTest::formFactorFromChassis(31), FF::Laptop);
        QCOMPARE(SystemInfoTest::formFactorFromChassis(2), FF::Unknown);
        QCOMPARE(SystemInfoTest::formFactorFromChassis(0), FF::Unknown);
    }

    // One SMBIOS structure: header (type, length, handle), the fields after
    // the header, then the strings, each ending with '\0', plus a final '\0'.
    static QByteArray smbiosStructure(quint8 type, const QByteArray& fields, const QList<QByteArray>& strings)
    {
        QByteArray s;
        s.append(char(type));
        s.append(char(4 + fields.size()));
        s.append(2, '\0');                     // handle, not used
        s.append(fields);
        for (const QByteArray& str : strings)
            s.append(str).append('\0');
        if (strings.isEmpty())
            s.append('\0');
        s.append('\0');
        return s;
    }

    // Hand-made table in the shape Windows gives (GetSystemFirmwareTable):
    // BIOS (type 0), System (type 1), Enclosure (type 3), End (type 127).
    static QByteArray smbiosTable()
    {
        return smbiosStructure(0, QByteArray("\x01\x02\x00\x00\x03", 5), {"LENOVO", "N1QET98W", "12/01/2023"})
             // manufacturer 1, product 2, version 3, serial 4, then UUID etc.
             + smbiosStructure(1, QByteArray("\x01\x02\x03\x04", 4) + QByteArray(17, '\0'),
                               {"LENOVO", "20HD000MUS ", "ThinkPad T470", "PF0ABCDE"})
             // manufacturer 1, type 0x8A = 10 (Notebook) + bit 7 (has a lock)
             + smbiosStructure(3, QByteArray("\x01\x8A\x00\x00", 4), {"LENOVO"})
             + smbiosStructure(127, {}, {});
    }

    void smbiosParsing()
    {
        const SmbiosIds ids = parseSmbios(smbiosTable());
        QVERIFY(ids.hasSystem);
        QCOMPARE(ids.vendor, QString("LENOVO"));
        QCOMPARE(ids.model, QString("20HD000MUS"));      // trimmed, from type 1 and not type 0
        QCOMPARE(ids.serial, QString("PF0ABCDE"));
        QCOMPARE(ids.chassisType, 10);
    }

    void smbiosBrokenTableIsNotACrash()
    {
        // Cut in the middle of the enclosure: the system part is still read.
        const QByteArray table = smbiosTable();
        const SmbiosIds cut = parseSmbios(table.left(table.indexOf(QByteArray("LENOVO\0\0", 8))));
        QVERIFY(cut.hasSystem);
        QCOMPARE(cut.model, QString("20HD000MUS"));
        QCOMPARE(cut.chassisType, 2);                    // unknown, not a guess

        QVERIFY(!parseSmbios({}).hasSystem);
        QVERIFY(!parseSmbios(QByteArray(3, '\x01')).hasSystem);

        // String number 0 = "no string"; a number past the strings = empty too.
        const SmbiosIds noSerial = parseSmbios(
            smbiosStructure(1, QByteArray("\x01\x02\x00\x09", 4), {"HP", "ZBook Fury 17 G7"}));
        QCOMPARE(noSerial.model, QString("ZBook Fury 17 G7"));
        QVERIFY(noSerial.serial.isEmpty());

        // A length below the 4-byte header would loop forever: stop instead.
        QVERIFY(!parseSmbios(QByteArray("\x01\x02\x00\x00\x00\x00", 6)).hasSystem);
    }

    void systemInfoComplete()
    {
        const TestResult r = SystemInfoTest::evaluate(fullSystem());
        QCOMPARE(r.status, Status::Pass);
        QVERIFY(r.summary.contains("ThinkPad T480"));
        QVERIFY(r.summary.contains("8.0 GiB"));
        QVERIFY(r.summary.contains("Laptop"));
        QVERIFY(r.summary.contains("S/N PF1ABCDE"));   // shown in the panel: the technician may copy it
        QVERIFY(r.details.contains("PF1ABCDE"));
    }

    void systemInfoMissingSerialIsOptional()
    {
        SystemInfo s = fullSystem();
        s.serial.clear();
        s.serialError = "Permission denied";
        const TestResult r = SystemInfoTest::evaluate(s);
        QCOMPARE(r.status, Status::Pass);   // serial optional (Linux: root only)
        QVERIFY(r.summary.contains("serial unknown"));
        QVERIFY(r.details.contains("Permission denied"));
        QVERIFY(r.details.contains("root"));
    }

    void systemInfoMissingDataIsError()
    {
        SystemInfo s = fullSystem();
        s.cpu.clear();
        QCOMPARE(SystemInfoTest::evaluate(s).status, Status::Error);
        s = fullSystem();
        s.ramBytes = 0;
        QCOMPARE(SystemInfoTest::evaluate(s).status, Status::Error);
        s = fullSystem();
        s.error = "GetSystemFirmwareTable(RSMB) failed (size 0, error 1)";
        QCOMPARE(SystemInfoTest::evaluate(s).status, Status::Error);
    }

    // Automatic first, then interactive, then "runs last", whatever the add() order.
    void runnerOrdersTests()
    {
        TestRunner runner;
        auto last = std::make_unique<FakeTest>(false, true);
        auto user = std::make_unique<FakeTest>(true, false);
        auto automatic = std::make_unique<FakeTest>(false, false);
        const ITest* l = last.get();
        const ITest* u = user.get();
        const ITest* a = automatic.get();
        runner.add(std::move(last));
        runner.add(std::move(user));
        runner.add(std::move(automatic));
        QCOMPARE(&runner.test(0), a);
        QCOMPARE(&runner.test(1), u);
        QCOMPARE(&runner.test(2), l);
    }

    // One test at a time: the CPU load starts only after the interactive tests.
    void runnerRunsLastTestAfterInteractive()
    {
        TestRunner runner;
        auto last = std::make_unique<FakeTest>(false, true);
        auto user = std::make_unique<FakeTest>(true, false);
        FakeTest* l = last.get();
        FakeTest* u = user.get();
        runner.add(std::move(last));
        runner.add(std::move(user));
        QSignalSpy all(&runner, &TestRunner::allFinished);

        runner.startAll();
        QCOMPARE(u->runs, 1);
        QCOMPARE(l->runs, 0);              // not during the interactive test
        u->answer(Status::Pass);
        QCOMPARE(l->runs, 1);
        QCOMPARE(all.count(), 0);
        l->answer(Status::Warn);
        l->answer(Status::Fail);           // duplicate: ignored
        QCOMPARE(all.count(), 1);
        QVERIFY(!runner.isRunning());
    }

    // "Run this test again": only that test runs, then allFinished as usual.
    void runnerStartsOneTest()
    {
        TestRunner runner;
        auto first = std::make_unique<FakeTest>(false, false);
        auto second = std::make_unique<FakeTest>(false, false);
        auto third = std::make_unique<FakeTest>(true, false);
        FakeTest* a = first.get();
        FakeTest* b = second.get();
        FakeTest* c = third.get();
        runner.add(std::move(first));
        runner.add(std::move(second));
        runner.add(std::move(third));
        QSignalSpy finished(&runner, &TestRunner::testFinished);
        QSignalSpy all(&runner, &TestRunner::allFinished);

        runner.startOne(1);
        QCOMPARE(a->runs, 0);
        QCOMPARE(b->runs, 1);
        QVERIFY(runner.isRunning());
        runner.startOne(0);                // busy: ignored
        QCOMPARE(a->runs, 0);
        b->answer(Status::Warn);
        QCOMPARE(c->runs, 0);              // the sequence stops after that test
        QCOMPARE(all.count(), 1);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toInt(), 1);
        QVERIFY(!runner.isRunning());

        runner.startOne(3);                // no such test: ignored
        QVERIFY(!runner.isRunning());
        runner.startAll();                 // a full run still runs everything
        a->answer(Status::Pass);
        b->answer(Status::Pass);
        c->answer(Status::Pass);
        QCOMPARE(all.count(), 2);
    }

    void deliverySheetReads_data() { sheetFiles(); }
    void deliverySheetReads()
    {
        QFETCH(QString, file);
        XlsxEditor sheet;
        QString error;
        QVERIFY2(sheet.load(sample(file), &error), qPrintable(error));
        QVERIFY2(DeliverySheet::check(sheet, &error), qPrintable(error));
        const QStringList models = DeliverySheet::names(sheet, SheetList::Models);
        QCOMPARE(models.size(), 53);
        QCOMPARE(models.first(), QStringLiteral("Dell Latitude 7400"));
        QCOMPARE(models.last(), QStringLiteral("Samsung Galaxy Tab Active 2"));
        QCOMPARE(sheet.cell(checkTab(), "C12"), QStringLiteral("\u2611"));
        QCOMPARE(sheet.cell(checkTab(), "B14"), QStringLiteral("Clavier / Toetsenbord"));
    }

    // The point of editing in place: only the written cells change.
    void deliverySheetKeepsEverythingElse_data() { sheetFiles(); }
    void deliverySheetKeepsEverythingElse()
    {
        QFETCH(QString, file);
        const QByteArray original = sample(file);
        XlsxEditor sheet;
        QString error;
        QVERIFY2(sheet.load(original, &error), qPrintable(error));
        QVERIFY2(sheet.setCell(checkTab(), "C12", QStringLiteral("\u2610"), &error), qPrintable(error));  // text cell
        QVERIFY2(sheet.setCell(checkTab(), "C7", "10/10/2026", &error), qPrintable(error));          // empty cell
        QVERIFY2(sheet.setCell(checkTab(), "E12", "after D12", &error), qPrintable(error));          // no cell yet
        QVERIFY2(sheet.setCell(checkTab(), "B30", "new row", &error), qPrintable(error));            // no row yet
        QVERIFY2(sheet.setCell(checkTab(), "D12", "<85 % & \"ok\">", &error), qPrintable(error));  // XML escaping
        const QByteArray saved = sheet.save(&error);
        QVERIFY2(!saved.isEmpty(), qPrintable(error));

        XlsxEditor reread;
        QVERIFY2(reread.load(saved, &error), qPrintable(error));
        QCOMPARE(reread.cell(checkTab(), "C12"), QStringLiteral("\u2610"));
        QCOMPARE(reread.cell(checkTab(), "C7"), QStringLiteral("10/10/2026"));
        QCOMPARE(reread.cell(checkTab(), "E12"), QStringLiteral("after D12"));
        QCOMPARE(reread.cell(checkTab(), "B30"), QStringLiteral("new row"));
        QCOMPARE(reread.cell(checkTab(), "D12"), QStringLiteral("<85 % & \"ok\">"));
        QCOMPARE(reread.cell(checkTab(), "B12").trimmed(), QStringLiteral("Batterie / Batterij \n(avec/met XXX)"));

        const auto before = zipEntries(original);
        const auto after = zipEntries(saved);
        QCOMPARE(after.size(), before.size());
        for (size_t i = 0; i < before.size(); ++i) {
            QCOMPARE(after[i].first, before[i].first);   // same files, same order
            if (before[i].first != QStringLiteral("xl/worksheets/sheet1.xml")) {
                QVERIFY2(after[i].second == before[i].second, qPrintable(before[i].first));
                continue;
            }
            // The changed sheet: dropdowns, page setup and cell formats kept.
            const QByteArray& a = before[i].second;
            const QByteArray& b = after[i].second;
            QCOMPARE(b.count("dataValidation "), a.count("dataValidation "));
            QVERIFY(a.count("dataValidation ") >= 6);
            const auto pageSetup = [](const QByteArray& xml) { return xml.mid(xml.indexOf("<pageSetup"), 200); };
            QCOMPARE(pageSetup(b), pageSetup(a));
            QCOMPARE(cellStyle(b, "C12"), cellStyle(a, "C12"));
            QCOMPARE(cellStyle(b, "C7"), cellStyle(a, "C7"));
            QVERIFY(!cellStyle(a, "C12").isEmpty());
        }
    }

    void deliverySheetFill_data() { sheetFiles(); }
    void deliverySheetFill()
    {
        QFETCH(QString, file);
        XlsxEditor sheet;
        QString error;
        QVERIFY2(sheet.load(sample(file), &error), qPrintable(error));
        std::map<SheetRow, TestResult> results;
        results[SheetRow::Battery] = {Status::Pass, "Health 85% of design capacity", "", "85 %"};
        results[SheetRow::Keyboard] = {Status::Fail, "Not working: F5", "", ""};
        results[SheetRow::Audio] = {Status::Warn, "no stereo separation", "", ""};
        results[SheetRow::Camera] = {Status::Skipped, "No camera found", "", ""};
        results[SheetRow::Disk] = {Status::Error, "Could not list the disks (see details)", "", ""};
        QVERIFY(DeliverySheet::currentShop(sheet).isEmpty());
        QVERIFY2(DeliverySheet::fill(sheet, QDate(2026, 10, 10), "Dell Latitude 7420", "Ixelles", results, &error),
                 qPrintable(error));

        XlsxEditor reread;
        QVERIFY2(reread.load(sheet.save(&error), &error), qPrintable(error));
        const auto at = [&](const char* ref) { return reread.cell(checkTab(), ref); };
        QCOMPARE(at("C4"), QStringLiteral("Ixelles"));
        QCOMPARE(DeliverySheet::currentShop(reread), QStringLiteral("Ixelles"));
        QCOMPARE(at("C5"), QStringLiteral("Dell Latitude 7420"));
        QCOMPARE(at("C25"), QStringLiteral("10/10/2026"));   // the operator's date
        QCOMPARE(at("C7"), QStringLiteral("10/10/2026"));    // the header date was empty
        QCOMPARE(at("C12"), QStringLiteral("\u2611"));
        QCOMPARE(at("D12"), QStringLiteral("85 %"));
        QCOMPARE(at("C13"), QStringLiteral("\u2611"));   // ERROR: not written either
        QVERIFY(at("D13").isEmpty());
        QCOMPARE(at("C14"), QStringLiteral("\u2611"));   // FAIL: not written, the sheet's own box stays
        QVERIFY(at("D14").isEmpty());
        QCOMPARE(at("C15"), QStringLiteral("\u2611"));   // WARN: ticked, with the reason
        QCOMPARE(at("D15"), QStringLiteral("no stereo separation"));
        QCOMPARE(at("C16"), QStringLiteral("\u2610"));
        QCOMPARE(at("D16"), QStringLiteral("No camera found"));

        // Filled again another day: the header date stays, the operator's date follows.
        QVERIFY2(DeliverySheet::fill(reread, QDate(2026, 10, 12), "", "", {}, &error), qPrintable(error));
        QCOMPARE(at("C7"), QStringLiteral("10/10/2026"));
        QCOMPARE(at("C25"), QStringLiteral("12/10/2026"));
        QCOMPARE(at("C4"), QStringLiteral("Ixelles"));   // no shop given: left as it was
    }

    void deliverySheetModels_data() { sheetFiles(); }
    void deliverySheetModels()
    {
        QFETCH(QString, file);
        XlsxEditor sheet;
        QString error;
        QVERIFY2(sheet.load(sample(file), &error), qPrintable(error));
        const QStringList models = DeliverySheet::names(sheet, SheetList::Models);
        QCOMPARE(DeliverySheet::findName(models, "  dell   LATITUDE 7420 "), QStringLiteral("Dell Latitude 7420"));
        QVERIFY(DeliverySheet::findName(models, "Lenovo T480").isEmpty());
        QVERIFY(DeliverySheet::findName(models, "").isEmpty());

        QVERIFY2(DeliverySheet::addName(sheet, SheetList::Models, "Lenovo  T480", &error), qPrintable(error));
        XlsxEditor reread;
        QVERIFY2(reread.load(sheet.save(&error), &error), qPrintable(error));
        const QStringList after = DeliverySheet::names(reread, SheetList::Models);
        QCOMPARE(after.size(), models.size() + 1);
        QVERIFY(after.contains("Lenovo T480"));
        QCOMPARE(after, DeliverySheet::sorted(after));   // the whole column is sorted
        QCOMPARE(reread.cell("Models", "A1"), after.first());
    }

    void deliverySheetShops_data() { sheetFiles(); }
    void deliverySheetShops()
    {
        QFETCH(QString, file);
        XlsxEditor sheet;
        QString error;
        QVERIFY2(sheet.load(sample(file), &error), qPrintable(error));
        const QStringList shops = DeliverySheet::names(sheet, SheetList::Shops);
        QVERIFY(shops.contains("Ixelles"));

        // A new shop with an accent, and one at the end of the alphabet.
        QVERIFY2(DeliverySheet::addName(sheet, SheetList::Shops, "Zottegem", &error), qPrintable(error));
        QVERIFY2(DeliverySheet::addName(sheet, SheetList::Shops, "\u00c9caussinnes", &error), qPrintable(error));
        XlsxEditor reread;
        QVERIFY2(reread.load(sheet.save(&error), &error), qPrintable(error));
        const QStringList after = DeliverySheet::names(reread, SheetList::Shops);
        QCOMPARE(after.size(), shops.size() + 2);
        QCOMPARE(after, DeliverySheet::sorted(after));
        QCOMPARE(after.last(), QStringLiteral("Zottegem"));
        // Models untouched.
        QCOMPARE(DeliverySheet::names(reread, SheetList::Models), DeliverySheet::names(sheet, SheetList::Models));
    }

    void deliverySheetMissingNames()
    {
        const QStringList fr{"Ixelles", "Li\u00e8ge", "Uccle"};
        const QStringList nl{"ixelles", "Leuven Diestsestraat", "Vilvoorde  Leuvensestraat", "Leuven Diestsestraat"};
        // Case and spaces ignored, each name once, spelling of the source kept (spaces tidied).
        QCOMPARE(DeliverySheet::missingNames(fr, nl),
                 (QStringList{"Leuven Diestsestraat", "Vilvoorde Leuvensestraat"}));
        QCOMPARE(DeliverySheet::missingNames(nl, fr), (QStringList{"Li\u00e8ge", "Uccle"}));
        QVERIFY(DeliverySheet::missingNames(fr, fr).isEmpty());
    }

    // Oxfam's French and Dutch sheets did not hold the same shops: merged.
    void deliverySheetMergeLists()
    {
        XlsxEditor fr, nl;
        QString error;
        QVERIFY2(fr.load(sample("delivery_sheet_fr.xlsx"), &error), qPrintable(error));
        QVERIFY2(nl.load(sample("delivery_sheet_nl.xlsx"), &error), qPrintable(error));
        QVERIFY2(DeliverySheet::addName(nl, SheetList::Shops, "Leuven Diestsestraat", &error), qPrintable(error));
        QVERIFY2(DeliverySheet::addName(fr, SheetList::Models, "Lenovo T480", &error), qPrintable(error));

        for (const SheetList list : {SheetList::Models, SheetList::Shops}) {
            const QStringList missing = DeliverySheet::missingNames(DeliverySheet::names(fr, list),
                                                                    DeliverySheet::names(nl, list));
            QVERIFY2(DeliverySheet::addNames(fr, list, missing, &error), qPrintable(error));
            QVERIFY2(DeliverySheet::addNames(nl, list, DeliverySheet::missingNames(DeliverySheet::names(nl, list),
                                                                                   DeliverySheet::names(fr, list)),
                                             &error),
                     qPrintable(error));
        }
        XlsxEditor reread;
        QVERIFY2(reread.load(fr.save(&error), &error), qPrintable(error));
        const QStringList shops = DeliverySheet::names(reread, SheetList::Shops);
        QVERIFY(shops.contains("Leuven Diestsestraat"));
        QCOMPARE(shops, DeliverySheet::sorted(shops));
        QVERIFY(DeliverySheet::names(nl, SheetList::Models).contains("Lenovo T480"));
        for (const SheetList list : {SheetList::Models, SheetList::Shops})
            QCOMPARE(DeliverySheet::names(fr, list).size(), DeliverySheet::names(nl, list).size());
    }

    void deliverySheetSorting()
    {
        // Case and accents ignored: E and \u00c9 together, "brugge" between B and C.
        QCOMPARE(DeliverySheet::sorted({"Fl\u00e9ron", "brugge", "Ciney", "\u00c9caussinnes", "Eupen", "Ath"}),
                 (QStringList{"Ath", "brugge", "Ciney", "\u00c9caussinnes", "Eupen", "Fl\u00e9ron"}));
    }

    void deliverySheetLanguage()
    {
        const QString fr = DeliverySheet::fileNames().at(0);
        const QString nl = DeliverySheet::fileNames().at(1);
        QCOMPARE(DeliverySheet::fileNamesFor("fr"), (QStringList{fr, nl}));
        QCOMPARE(DeliverySheet::fileNamesFor("nl"), (QStringList{nl, fr}));   // both always looked for
        QCOMPARE(DeliverySheet::fileNamesFor("en"), (QStringList{fr, nl}));
        QCOMPARE(DeliverySheet::fileName("nl"), nl);
        QVERIFY(DeliverySheet::fileName("en").isEmpty());
    }

    void xlsxRejectsOtherFiles()
    {
        XlsxEditor sheet;
        QString error;
        QVERIFY(!sheet.load("not a zip file", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!sheet.setCell(checkTab(), "C5", "x", &error));   // nothing loaded
        QVERIFY(!sheet.setCell(checkTab(), "5C", "x", &error));   // bad reference
    }

    void verdictRules()
    {
        QCOMPARE(computeVerdict({Status::Pass, Status::Skipped}).level, Status::Pass);
        QCOMPARE(computeVerdict({Status::Pass, Status::Warn}).level, Status::Warn);
        QCOMPARE(computeVerdict({Status::Warn, Status::Error}).level, Status::Error);
        QCOMPARE(computeVerdict({Status::Error, Status::Fail}).level, Status::Fail);
    }
};

// GUILESS, not APPLESS: TestRunner starts QTimers, which need a QCoreApplication.
QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
