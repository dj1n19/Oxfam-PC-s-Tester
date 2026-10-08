#include <QtTest>

#include "core/Thresholds.h"
#include "core/Verdict.h"
#include "tests/battery/BatteryTest.h"
#include "tests/disk/DiskTest.h"
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
