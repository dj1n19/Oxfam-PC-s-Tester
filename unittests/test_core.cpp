#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <QtTest>

#include "core/Thresholds.h"
#include "core/Verdict.h"
#include "tests/battery/BatteryTest.h"
#include "tests/disk/DiskTest.h"
#include "tests/drivers/DriversTest.h"
#include "tests/license/LicenseTest.h"
#include "tests/keyboard/KeyLayout.h"
#include "tests/audio/AudioCheck.h"
#include "tests/sysinfo/SystemInfoTest.h"

// Lets QCOMPARE print Status values on failure (found via ADL).
char *toString(Status s)
{
    return qstrdup(qPrintable(statusLabel(s)));
}

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
            "disk_temperature_c":       {"warn": 55, "fail": 65}
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

    // Golden samples live in unittests/samples/ (found via QFINDTESTDATA).
    static QByteArray sample(const QString& file)
    {
        QFile f(QFINDTESTDATA("samples/" + file));
        if (!f.open(QIODevice::ReadOnly))
            qFatal("Missing test sample %s", qPrintable(file));
        return f.readAll();
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

    // Real report from a Dell Latitude 7420, Windows 11 (UTF-8 BOM, CRLF).
    // <RuntimeEstimates> holds a second <DesignCapacity>: it must be ignored.
    void powercfgLatitude7420()
    {
        const BatteryInfo b = parseBatteryReport(sample("powercfg_latitude7420.xml"));
        QVERIFY2(b.error.isEmpty(), qPrintable(b.error));
        QVERIFY(b.present);
        QCOMPARE(b.name, QString("DELL 4M1JN18"));
        QCOMPARE(b.designCapacity, 61834.0);
        QCOMPARE(b.fullCapacity, 22359.0);
        QCOMPARE(b.cycles, -1);   // 0 = not reported
        QCOMPARE(BatteryTest::evaluate(b, loaded()).status, Status::Fail);   // 36 %
    }

    void powercfgDesktopIsSkipped()
    {
        const BatteryInfo b = parseBatteryReport(sample("powercfg_desktop.xml"));
        QVERIFY(b.error.isEmpty());
        QVERIFY(!b.present);
        QCOMPARE(BatteryTest::evaluate(b, loaded()).status, Status::Skipped);
    }

    void powercfgBadDataIsError()
    {
        QByteArray xml = sample("powercfg_latitude7420.xml");
        xml.replace("<DesignCapacity>61834</DesignCapacity>", "");
        QCOMPARE(BatteryTest::evaluate(parseBatteryReport(xml), loaded()).status, Status::Error);
        QCOMPARE(BatteryTest::evaluate(parseBatteryReport(""), loaded()).status, Status::Error);
        QCOMPARE(BatteryTest::evaluate(parseBatteryReport("not xml"), loaded()).status, Status::Error);
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

    // pnp_problems.json is hand-made from the PowerShell script's output
    // shape. Replace it with a real one from a PC with a driver problem.
    void driversWindowsProblems()
    {
        const DriverInfo info = parsePnpEntities(sample("pnp_problems.json"));
        QVERIFY2(info.error.isEmpty(), qPrintable(info.error));
        QCOMPARE(info.problems.size(), size_t(4));
        QCOMPARE(info.problems[1].name, QString("ACPI\\INT33A1\\1"));   // no name: DeviceID used

        const TestResult r = DriversTest::evaluate(info);
        QCOMPARE(r.status, Status::Warn);
        QVERIFY2(r.summary.startsWith("3 device(s)"), qPrintable(r.summary));   // code 45 ignored
        QVERIFY(r.summary.contains("no driver installed"));
        QVERIFY(r.summary.contains("disabled"));
        QVERIFY(!r.summary.contains("SanDisk"));
        QVERIFY(r.details.contains("SanDisk"));   // still visible in details
    }

    void driversWindowsNoProblem()
    {
        const DriverInfo info = parsePnpEntities("[]");
        QVERIFY(info.error.isEmpty());
        QCOMPARE(DriversTest::evaluate(info).status, Status::Pass);
    }

    void driversBadOutputIsError()
    {
        QCOMPARE(DriversTest::evaluate(parsePnpEntities("")).status, Status::Error);
        QCOMPARE(DriversTest::evaluate(parsePnpEntities("{\"Name\":1}")).status, Status::Error);
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

    // license_*.json are hand-made from the script's output shape.
    void licenseActivated()
    {
        const LicenseInfo info = parseLicenseJson(sample("license_activated.json"));
        QVERIFY2(info.error.isEmpty(), qPrintable(info.error));
        const TestResult r = LicenseTest::evaluate(info);
        QCOMPARE(r.status, Status::Pass);
        QVERIFY(r.summary.contains("Professional"));
        QVERIFY(r.summary.contains("OEM key in firmware: yes"));
    }

    void licenseNotActivatedIsWarn()
    {
        const TestResult r = LicenseTest::evaluate(parseLicenseJson(sample("license_notification.json")));
        QCOMPARE(r.status, Status::Warn);
        QVERIFY(r.summary.contains("NOT activated"));
        QVERIFY(r.details.startsWith("Hint"));   // firmware key exists
    }

    void licenseNoneIsWarn()
    {
        const LicenseInfo info = parseLicenseJson(sample("license_none.json"));
        QVERIFY2(info.error.isEmpty(), qPrintable(info.error));
        const TestResult r = LicenseTest::evaluate(info);
        QCOMPARE(r.status, Status::Warn);
        QVERIFY(r.summary.contains("OEM key in firmware: no"));
    }

    void licenseBadOutputIsError()
    {
        QCOMPARE(LicenseTest::evaluate(parseLicenseJson("")).status, Status::Error);
        QCOMPARE(LicenseTest::evaluate(parseLicenseJson("{}")).status, Status::Error);
        QByteArray noStatus = sample("license_activated.json");
        noStatus.replace("\"status\":1", "\"status\":null");
        QCOMPARE(LicenseTest::evaluate(parseLicenseJson(noStatus)).status, Status::Error);
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

    void scanCodesWindows()
    {
        QCOMPARE(scanCodeFromWindows(0x1E), 0x1E);
        QCOMPARE(scanCodeFromWindows(0x11D), 0x11D);         // extended bit kept
        QCOMPARE(scanCodeFromWindows(0x2000 | 0x38), 0x38);  // reserved bits dropped
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

    void systemInfoComplete()
    {
        const TestResult r = SystemInfoTest::evaluate(fullSystem());
        QCOMPARE(r.status, Status::Pass);
        QVERIFY(r.summary.contains("ThinkPad T480"));
        QVERIFY(r.summary.contains("8.0 GiB"));
        QVERIFY(r.summary.contains("Laptop"));
        QVERIFY(r.details.contains("PF1ABCDE"));
    }

    void systemInfoMissingSerialIsError()
    {
        SystemInfo s = fullSystem();
        s.serial.clear();
        s.serialError = "Permission denied";
        const TestResult r = SystemInfoTest::evaluate(s);
        QCOMPARE(r.status, Status::Error);
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
        s.error = "PowerShell timed out";
        QCOMPARE(SystemInfoTest::evaluate(s).status, Status::Error);
    }

    void verdictRules()
    {
        QCOMPARE(computeVerdict({Status::Pass, Status::Skipped}).level, Status::Pass);
        QCOMPARE(computeVerdict({Status::Pass, Status::Warn}).level, Status::Warn);
        QCOMPARE(computeVerdict({Status::Warn, Status::Error}).level, Status::Error);
        QCOMPARE(computeVerdict({Status::Error, Status::Fail}).level, Status::Fail);
    }
};

QTEST_APPLESS_MAIN(TestCore)
#include "test_core.moc"
