#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include <QtTest>

#include "core/ProgramFolder.h"
#include "core/TestRunner.h"
#include "core/TestChoice.h"
#include "core/Thresholds.h"
#include "core/Verdict.h"
#include "delivery/DeliverySheet.h"
#include "delivery/XlsxEditor.h"
#include "report/RunReport.h"
#include "tests/battery/BatteryTest.h"
#include "tests/battery/ChargeTest.h"
#include "tests/wireless/BluetoothTest.h"
#include "tests/wireless/WifiTest.h"
#include "tests/disk/DiskTest.h"
#include "tests/drivers/DriversTest.h"
#include "tests/license/LicenseTest.h"
#include "tests/screen/ScreenCheck.h"
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
    QString id() const override { return QStringLiteral("fake"); }
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
            "nvme_temperature_c":       {"warn": 70, "fail": 80},
            "disk_read_mb_s":           {"warn": 100, "fail": 20},
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

    void batteryNearlyEmptyIsNoted()
    {
        // Surface Pro 2026-11-04: 79 % read at 13 % charge, 57 % once charged.
        BatteryInfo b = battery(40000);
        b.chargePct = 13;
        TestResult r = BatteryTest::evaluate(b, loaded());
        QCOMPARE(r.status, Status::Pass);   // the status is unchanged: only a note
        QVERIFY(r.details.startsWith("Note: battery nearly empty (13 %)"));
        b.chargePct = 50;
        QVERIFY(!BatteryTest::evaluate(b, loaded()).details.contains("nearly empty"));
        b.chargePct = -1;   // unknown charge: no note
        QVERIFY(!BatteryTest::evaluate(b, loaded()).details.contains("nearly empty"));
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

    static BatteryInfo charger(int acOnline, ChargeState state, double pct, double watts = 0)
    {
        BatteryInfo b = battery(40000);
        b.acOnline = acOnline;
        b.state = state;
        b.chargePct = pct;
        b.rateW = watts;
        return b;
    }

    void chargeStates()
    {
        TestResult r = ChargeTest::evaluate(charger(1, ChargeState::Charging, 42, 31.5));
        QCOMPARE(r.status, Status::Pass);
        QCOMPARE(r.summary, QString("Charging at 31.5 W (42 %)"));
        QCOMPARE(ChargeTest::evaluate(charger(1, ChargeState::Charging, 42)).summary, QString("Charging (42 %)"));

        // Not seen = socket, cable or charger (or not plugged in: the text says so).
        QCOMPARE(ChargeTest::evaluate(charger(0, ChargeState::Discharging, 60)).status, Status::Fail);
        // The OS cannot tell: never a Pass.
        QCOMPARE(ChargeTest::evaluate(charger(-1, ChargeState::Unknown, 60)).status, Status::Error);

        // Seen but not charging: the charger works; why it does not charge decides.
        QCOMPARE(ChargeTest::evaluate(charger(1, ChargeState::Full, 100)).status, Status::Pass);
        QCOMPARE(ChargeTest::evaluate(charger(1, ChargeState::NotCharging, 97)).status, Status::Pass);
        r = ChargeTest::evaluate(charger(1, ChargeState::NotCharging, 80));   // charge limit
        QCOMPARE(r.status, Status::Pass);
        QVERIFY(r.summary.contains("limit"));
        r = ChargeTest::evaluate(charger(1, ChargeState::NotCharging, 0));    // pre-charge (ThinkPad 13)
        QCOMPARE(r.status, Status::Warn);
        QVERIFY(r.summary.contains("10 minutes"));
        r = ChargeTest::evaluate(charger(1, ChargeState::Discharging, 50));   // charger too weak
        QCOMPARE(r.status, Status::Warn);

        // Charging, but almost nothing goes in (Surface Pro 0.3 W at 41 %, 2026-11-06).
        r = ChargeTest::evaluate(charger(1, ChargeState::Charging, 41, 0.3));
        QCOMPARE(r.status, Status::Warn);
        QVERIFY(r.summary.startsWith("Charging very slowly at 0.3 W (41 %)"));
        // Nearly full: the charge slows down on purpose (0.8 W at 98 % seen).
        QCOMPARE(ChargeTest::evaluate(charger(1, ChargeState::Charging, 98, 0.8)).status, Status::Pass);
        QCOMPARE(ChargeTest::evaluate(charger(1, ChargeState::Charging, 85, 1.8)).status, Status::Pass);
        // Lowest real rates under 80 % stay a Pass.
        QCOMPARE(ChargeTest::evaluate(charger(1, ChargeState::Charging, 62, 13.7)).status, Status::Pass);

        BatteryInfo desktop;
        QCOMPARE(ChargeTest::evaluate(desktop).status, Status::Skipped);
        BatteryInfo broken;
        broken.error = "boom";
        QCOMPARE(ChargeTest::evaluate(broken).status, Status::Error);
    }

    void wifiStates()
    {
        WifiInfo none;
        QCOMPARE(WifiTest::evaluate(none, FormFactor::Laptop).status, Status::Fail);
        QCOMPARE(WifiTest::evaluate(none, FormFactor::Desktop).status, Status::Skipped);

        WifiInfo w;
        w.present = true;
        w.radioOff = true;
        QCOMPARE(WifiTest::evaluate(w, FormFactor::Laptop).status, Status::Warn);
        w.radioOff = false;
        QCOMPARE(WifiTest::evaluate(w, FormFactor::Laptop).status, Status::Pass);   // not counted
        w.counted = true;
        QCOMPARE(WifiTest::evaluate(w, FormFactor::Laptop).status, Status::Warn);   // antenna?

        // One line per access point: names once each, hidden networks each.
        w.ssids = {"Oxfam", "Oxfam", "proximus-1234", "", ""};
        const TestResult r = WifiTest::evaluate(w, FormFactor::Laptop);
        QCOMPARE(r.status, Status::Pass);
        QCOMPARE(r.summary, QString("Wi-Fi works: 4 networks seen"));

        WifiInfo broken;
        broken.error = "boom";
        QCOMPARE(WifiTest::evaluate(broken, FormFactor::Laptop).status, Status::Error);
    }

    void bluetoothStates()
    {
        BluetoothInfo none;
        QCOMPARE(BluetoothTest::evaluate(none, FormFactor::Laptop).status, Status::Warn);   // older models
        QCOMPARE(BluetoothTest::evaluate(none, FormFactor::Unknown).status, Status::Skipped);
        BluetoothInfo b;
        b.present = true;
        b.name = "btusb";
        QCOMPARE(BluetoothTest::evaluate(b, FormFactor::Laptop).summary, QString("Bluetooth adapter found: btusb"));
        b.radioOff = true;
        QCOMPARE(BluetoothTest::evaluate(b, FormFactor::Laptop).status, Status::Warn);
        BluetoothInfo broken;
        broken.error = "boom";
        QCOMPARE(BluetoothTest::evaluate(broken, FormFactor::Laptop).status, Status::Error);
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
            << "/dev/test PM9A1 NVMe Samsung 256GB 256 GB: SMART OK, 6% worn, 31 C, speed not measured, 1480 h";
        QTest::newRow("Dell 7430 WD SN810") << "smartctl_real_wd_sn810.json"
            << "/dev/test PC SN810 NVMe WDC 512GB 512 GB: SMART OK, 21% worn, 26 C, speed not measured, 2868 h";
        QTest::newRow("Lenovo 20M6 Intel") << "smartctl_real_intel_ssdpekkf256g8l.json"
            << "/dev/test INTEL SSDPEKKF256G8L 256 GB: SMART OK, 6% worn, 42 C, speed not measured, 14621 h";
        QTest::newRow("Lenovo 20M6 Samsung MZVLW") << "smartctl_real_samsung_mzvlw256.json"
            << "/dev/test SAMSUNG MZVLW256HEHP-000L7 256 GB: SMART OK, 5% worn, 37 C, speed not measured, 3860 h";
        QTest::newRow("Surface Pro 7 SK Hynix") << "smartctl_real_skhynix_hfb1m8.json"
            << "/dev/test HFB1M8MO331C0MR 256 GB: SMART OK, 25% worn, 46 C, speed not measured, 2585 h";
        QTest::newRow("Surface Pro 7 Toshiba (2026-10-27)") << "smartctl_real_toshiba_kbg40zpz256g.json"
            << "/dev/test KBG40ZPZ256G TOSHIBA MEMORY 256 GB: SMART OK, 13% worn, 46 C, speed not measured, 12674 h";
        QTest::newRow("HP ZBook WD SN730") << "smartctl_real_wd_sn730.json"
            << "/dev/test WDC PC SN730 SDBPNTY-1T00-1006 1024 GB: SMART OK, 1% worn, 29 C, speed not measured, 3307 h";
        QTest::newRow("HP ZBook Micron") << "smartctl_real_micron_mtfdkba1t0tfh.json"
            << "/dev/test MTFDKBA1T0TFH-1BC1AABHA 1024 GB: SMART OK, 3% worn, 22 C, speed not measured, 2454 h";
        // Field 2026-10-28 to 11-03 (Windows 10/11, and the AppImage's static smartctl on Mint).
        QTest::newRow("Surface Pro 2017 Samsung KUS") << "smartctl_real_samsung_kus030202m.json"
            << "/dev/test SAMSUNG KUS030202M-B000 256 GB: SMART OK, 3% worn, 45 C, speed not measured, 2565 h";
        QTest::newRow("Surface Pro 2017 Intel") << "smartctl_real_intel_ssdpebkf256g7.json"
            << "/dev/test INTEL SSDPEBKF256G7 256 GB: SMART OK, 5% worn, 35 C, speed not measured, 7863 h";
        QTest::newRow("Surface Pro 2017 SK Hynix BC501") << "smartctl_real_skhynix_bc501.json"
            << "/dev/test Skhynix BC501 NVMe 256GB 256 GB: SMART OK, 1% worn, 36 C, speed not measured, 1101 h";
        QTest::newRow("Lenovo 20NS Samsung MZVLB HAHQ") << "smartctl_real_samsung_mzvlb256hahq.json"
            << "/dev/test SAMSUNG MZVLB256HAHQ-000L7 256 GB: SMART OK, 4% worn, 32 C, speed not measured, 6541 h";
        QTest::newRow("Lenovo 20NS Samsung MZVLB HBHQ") << "smartctl_real_samsung_mzvlb256hbhq_l7.json"
            << "/dev/test SAMSUNG MZVLB256HBHQ-000L7 256 GB: SMART OK, 4% worn, 33 C, speed not measured, 3865 h";
        QTest::newRow("HP EliteBook 845 Samsung MZVLB") << "smartctl_real_samsung_mzvlb256hbhq_h1.json"
            << "/dev/test SAMSUNG MZVLB256HBHQ-000H1 256 GB: SMART OK, 6% worn, 34 C, speed not measured, 3787 h";
        QTest::newRow("Lenovo 20J2 Samsung MZVLW, Mint") << "smartctl_real_samsung_mzvlw256_linux.json"
            << "/dev/test SAMSUNG MZVLW256HEHP-000L7 256 GB: SMART OK, 2% worn, 28 C, speed not measured, 4175 h";
        // Field 2026-11-04/06 (Windows 10/11).
        QTest::newRow("Surface Pro 4 Toshiba") << "smartctl_real_toshiba_thnsn5256gpu7.json"
            << "/dev/test THNSN5256GPU7 TOSHIBA 256 GB: SMART OK, 4% worn, 33 C, speed not measured, 3909 h";
        // Worn a lot, still under the 80 % warning.
        QTest::newRow("Surface Pro 7 SK Hynix 512 GB") << "smartctl_real_skhynix_hfb1m8mh_512.json"
            << "/dev/test HFB1M8MH331C0MR 512 GB: SMART OK, 61% worn, 31 C, speed not measured, 3358 h";
        // Was a WARN with the old common limit (55 C); NVMe limit is 70 C since 2026-11-06.
        QTest::newRow("Surface Pro 2017 Samsung KUS at 59 C") << "smartctl_real_samsung_kus030202m_59c.json"
            << "/dev/test SAMSUNG KUS030202M-B000 256 GB: SMART OK, 2% worn, 59 C, speed not measured, 2359 h";
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
                 QString("/dev/test SanDisk Cruzer 16 GB: SMART OK, 0 C, speed not measured"));

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

    void diskTemperatureLimitsDependOnTheKind()
    {
        // NVMe: 59 C seen on a Surface Pro at rest (2026-11-06) is fine, 72 C is not.
        SmartctlRun nvme = smart("smartctl_nvme_ok.json", 0);
        nvme.json.replace("\"current\": 36", "\"current\": 59");
        QCOMPARE(DiskTest::evaluateDisk(nvme, loaded()).status, Status::Pass);
        nvme.json.replace("\"current\": 59", "\"current\": 72");
        TestResult r = DiskTest::evaluateDisk(nvme, loaded());
        QCOMPARE(r.status, Status::Warn);
        QVERIFY2(r.summary.contains("72 C (WARN)"), qPrintable(r.summary));

        // A hard disk keeps the lower limits: 59 C is already a warning.
        SmartctlRun ata = smart("smartctl_ata_worn.json", 0);
        ata.json.replace("\"current\": 41", "\"current\": 59");
        r = DiskTest::evaluateDisk(ata, loaded());
        QVERIFY2(r.summary.contains("59 C (WARN)"), qPrintable(r.summary));
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

    // ThinkPad L390 (Windows, 2026-10-28): smartctl --scan gave the NVMe disk
    // as /dev/sda and /dev/nvme0. One disk, one entry in the summary.
    void diskNamedTwiceCountsOnce()
    {
        DiskScan scan;
        scan.disks = {smart("smartctl_real_samsung_mzvlw256.json", 0), smart("smartctl_real_samsung_mzvlw256.json", 0),
                      smart("smartctl_real_wd_sn730.json", 0)};   // same anonymised serial, other model: kept
        scan.disks[0].device = "/dev/sda";
        scan.disks[1].device = "/dev/nvme0";
        const TestResult r = DiskTest::evaluate(scan, loaded());
        QCOMPARE(r.summary.count("MZVLW256"), 1);
        QVERIFY(r.summary.contains("SN730"));
        QVERIFY(r.details.contains("/dev/nvme0: same disk as /dev/sda"));
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
        info.drive = "C:";
        info.bitlocker = 2;   // off
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

    void bitlockerOffIsPass()
    {
        LicenseInfo info = windowsLicense(1, true);
        info.bitlocker = 0;   // not encryptable: nothing to turn off either
        const TestResult r = LicenseTest::evaluate(info);
        QCOMPARE(r.status, Status::Pass);
        QVERIFY(r.summary.endsWith("| BitLocker off"));
        QVERIFY(r.details.contains("BitLockerProtection = 0"));
    }

    void bitlockerOnIsWarn()
    {
        for (int code : {1, 3, 5}) {
            LicenseInfo info = windowsLicense(1, true);
            info.bitlocker = code;
            const TestResult r = LicenseTest::evaluate(info);
            QCOMPARE(r.status, Status::Warn);
            QVERIFY(r.summary.contains("BitLocker ON: turn it off"));
            QVERIFY(r.summary.startsWith("Activated"));   // the licence part is still shown
        }
    }

    void bitlockerWaitingForActivationIsPass()
    {
        // Code 8 on 3 reinstalled Surface Pro (2026-11-06): no key protector.
        LicenseInfo info = windowsLicense(1, true);
        info.bitlocker = 8;
        const TestResult r = LicenseTest::evaluate(info);
        QCOMPARE(r.status, Status::Pass);
        QVERIFY(r.summary.contains("waiting for activation"));
        QVERIFY(r.details.contains("manage-bde -off"));
    }

    void bitlockerUnknownCodeIsWarnNotPass()
    {
        LicenseInfo info = windowsLicense(1, true);
        info.bitlocker = 6;
        const TestResult r = LicenseTest::evaluate(info);
        QCOMPARE(r.status, Status::Warn);
        QVERIFY(r.summary.contains("code 6"));
    }

    void bitlockerUnreadableIsError()
    {
        LicenseInfo info = windowsLicense(1, true);
        info.bitlocker = -1;
        info.bitlockerError = "GetInt32(BitLockerProtection) on C:\\ failed (0x80070490)";
        const TestResult r = LicenseTest::evaluate(info);
        QCOMPARE(r.status, Status::Error);
        QVERIFY(r.details.contains("0x80070490"));
        // A licence failure and a BitLocker warning: the Error wins.
        LicenseInfo both;
        both.error = "SLOpen failed (0xc004f075)";
        both.bitlocker = 1;
        QCOMPARE(LicenseTest::evaluate(both).status, Status::Error);
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
        QVERIFY(rest.raw.contains("ACPI zones (not used, not the CPU): none"));

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

    // Real runs of the app on Windows. Since 2026-10-27 the ACPI zones are
    // never the CPU temperature there: only the speed is judged.
    void cpuRealWindowsRuns()
    {
        const Thresholds t = loaded();

        // ThinkPad L380 (2026-10-14): the zone stays at 77.1 C under load, the
        // speed is live and settles at ~150 %.
        CpuLoadRun lenovo = speedRun({184, 160, 151, 150, 148, 150});
        lenovo.idle = windowsCpuReading({{"ACPI\\ThermalZone\\THM0_0", 350.2}}, 67, false);
        QVERIFY(!lenovo.idle.hasTemp);                                  // listed, not used
        QVERIFY(lenovo.idle.raw.contains("THM0_0 = 350.2 K"));
        QCOMPARE(lenovo.idle.speedPct, 67.0);
        const TestResult l = CpuTempTest::evaluate(lenovo, t);
        QCOMPARE(l.status, Status::Pass);
        QCOMPARE(l.summary, QString("Speed 150 % of base"));

        // Latitude 7420, 3rd load in a row on battery: turbo budget spent, under base speed.
        CpuLoadRun dell = speedRun({266, 266, 210, 106, 82, 86, 82, 91, 83, 84, 93, 82, 81}, true);
        const TestResult d = CpuTempTest::evaluate(dell, t);
        QCOMPARE(d.status, Status::Warn);   // on battery: never Fail
        QVERIFY(d.summary.contains("83 %"));
        QVERIFY(d.summary.contains("charger"));

        // ThinkPad 20J2 (2026-10-27): zone at 74 C at rest, FALLING under load.
        // Before, it was taken for the CPU on some PCs ("Peak 72 C"): no more.
        CpuLoadRun thinkpad = speedRun({120, 120, 120, 119, 109, 120, 120});
        thinkpad.idle = windowsCpuReading({{"\\_TZ.TZ00", 347.05}}, 110, false);
        thinkpad.samples = {73.9, 69.9, 67.9, 65.9};   // ignored: no temperature at rest
        const TestResult tp = CpuTempTest::evaluate(thinkpad, t);
        QCOMPARE(tp.status, Status::Pass);
        QVERIFY(!tp.summary.contains("C under load"));
        QVERIFY(tp.details.contains("TZ00 = 347.05 K"));   // still in the details
    }

    // The "not the CPU" rule still guards Linux sensors. Values of the
    // Surface Pro 7 zone (+1 and +3 C under full load), as if from hwmon.
    void cpuTempBarelyReacting()
    {
        const Thresholds t = loaded();
        CpuLoadRun run = cpuRun(48.9, {48.9, 48.9, 48.9, 49.9});
        QVERIFY(CpuTempTest::evaluate(run, t).summary.contains("barely reacted"));
        run = cpuRun(29.9, {29.9, 30.9, 31.9, 32.9});
        QCOMPARE(CpuTempTest::evaluate(run, t).status, Status::Skipped);
        run = cpuRun(52, {60, 68, 72});   // Arch, coretemp: +20 C
        QCOMPARE(CpuTempTest::evaluate(run, t).status, Status::Pass);
    }

    void thermalZones()
    {
        const CpuReading r = windowsCpuReading({{"\\_TZ.TZ00", 313.2}, {"\\_TZ.THM0", 333.2}}, 0, false);
        QVERIFY(!r.hasTemp);
        QVERIFY(r.raw.contains("\\_TZ.THM0 = 333.2 K"));   // listed in the details
        QVERIFY(r.raw.contains("not used"));
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
        QCOMPARE(audioResult(Heard::Left, Heard::Left).status, Status::Fail);     // one wrong side
        QCOMPARE(audioResult(Heard::Left, Heard::Left).summary, QString("right tone heard on the left"));
        QCOMPARE(audioResult(Heard::Right, Heard::Right).summary, QString("left tone heard on the right"));
        QCOMPARE(audioSkipped().status, Status::Skipped);
    }

    void smartctlBatchScript()
    {
        const std::vector<ScannedDevice> devs{{"/dev/sda", "sat"}, {"/dev/nvme0", "nvme"}};
        const QString script = buildBatchScript("/opt/my tools/smartctl", devs);
        QCOMPARE(script.count('\n'), 1);   // one line per disk
        QVERIFY(script.contains("'/opt/my tools/smartctl' '--json' '-a' '-d' 'sat' '/dev/sda'"));
        QVERIFY(script.contains(QString(kBatchMarker) + "$?"));
        // Speed read before smartctl, dd's lines marked; the NVMe controller read as its namespace.
        QVERIFY(script.contains("LC_ALL=C timeout '-s' 'USR1' '-k' '1' '8' 'dd' 'if=/dev/sda' 'of=/dev/null'"));
        QVERIFY(script.contains("'if=/dev/nvme0n1'"));
        QVERIFY(script.contains(QString("2>&1 | sed 's/^/%1/'; ").arg(kDdPrefix)));

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
        out += QByteArray(kBatchMarker) + "0\n";   // sda: no dd line (dd missing?) = not measured
        for (const QByteArray& line : sample("dd_linux_nvme.txt").split('\n'))
            if (!line.isEmpty())
                out += QByteArray(kDdPrefix) + line + '\n';
        out += sample("smartctl_nvme_ok.json");
        out += QByteArray(kBatchMarker) + "4\n";
        const auto runs = parseBatchOutput(out, devs);
        QCOMPARE(runs.size(), size_t(2));
        QCOMPARE(runs[0].device, QString("/dev/sda"));
        QCOMPARE(runs[0].exitCode, 0);
        QCOMPARE(runs[1].exitCode, 4);
        QCOMPARE(DiskTest::evaluateDisk(runs[0], loaded()).status, Status::Fail);   // the worn disk
        QCOMPARE(DiskTest::evaluateDisk(runs[1], loaded()).status, Status::Pass);
        QCOMPARE(runs[0].readMBps, -1.0);
        QCOMPARE(qRound(runs[1].readMBps), 1394);
        QVERIFY(runs[1].json.startsWith('{'));   // dd's lines kept out of the JSON
        QVERIFY(runs[1].speedRaw.contains("copied, 0.385132 s"));

        // Batch stopped after the first disk: the second one is an Error, not lost.
        const QByteArray cut = sample("smartctl_ata_worn.json") + QByteArray(kBatchMarker) + "0\n";
        const auto partial = parseBatchOutput(cut, devs);
        QCOMPARE(partial.size(), size_t(2));
        QVERIFY(!partial[1].error.isEmpty());
        QCOMPARE(DiskTest::evaluateDisk(partial[1], loaded()).status, Status::Error);
    }

    void ddSpeed()
    {
        // Real dd reports (Arch, 2026-11-03): read with iflag=direct, stopped
        // by timeout's SIGUSR1 (report printed twice), no root.
        QCOMPARE(qRound(parseDdSpeed(sample("dd_linux_nvme.txt"))), 1394);   // 536870912 B / 0.385132 s
        QCOMPARE(qRound(parseDdSpeed(sample("dd_linux_stopped.txt"))), 49882);
        QCOMPARE(parseDdSpeed(sample("dd_linux_denied.txt")), -1.0);
        QCOMPARE(parseDdSpeed("1048576 bytes (1.0 MB, 1.0 MiB) copied, 0.01 s, 105 MB/s"), -1.0);   // too little
        QCOMPARE(parseDdSpeed(""), -1.0);

        QCOMPARE(linuxBlockDevice("/dev/nvme0"), QString("/dev/nvme0n1"));
        QCOMPARE(linuxBlockDevice("/dev/nvme12"), QString("/dev/nvme12n1"));
        QCOMPARE(linuxBlockDevice("/dev/nvme0n1"), QString("/dev/nvme0n1"));
        QCOMPARE(linuxBlockDevice("/dev/sda"), QString("/dev/sda"));
    }

    void diskSpeedJudged()
    {
        SmartctlRun run = smart("smartctl_nvme_ok.json", 0);
        run.readMBps = 1393.98;
        run.speedRaw = "\\\\.\\PhysicalDrive0: 512 MiB read in 0.385 s (unbuffered, sequential)";
        TestResult r = DiskTest::evaluateDisk(run, loaded());
        QCOMPARE(r.status, Status::Pass);
        QVERIFY(r.summary.contains(", 1394 MB/s"));
        QVERIFY(r.details.contains("Read speed: \\\\.\\PhysicalDrive0"));

        run.readMBps = 60;   // old HDD or a SATA link at 1.5 Gb/s
        r = DiskTest::evaluateDisk(run, loaded());
        QCOMPARE(r.status, Status::Warn);
        QVERIFY(r.summary.contains("60 MB/s (WARN)"));

        run.readMBps = 12;   // dying disk
        QCOMPARE(DiskTest::evaluateDisk(run, loaded()).status, Status::Fail);

        // Not measured: said, not judged (SMART still decides).
        run.readMBps = -1;
        r = DiskTest::evaluateDisk(run, loaded());
        QCOMPARE(r.status, Status::Pass);
        QVERIFY(r.summary.contains("speed not measured"));
    }

    void cameraSeveral()
    {
        const FrameStats s{120, 40};
        const TestResult good = cameraResult(CameraOutcome::Good, "Front", s);
        const TestResult broken = cameraResult(CameraOutcome::Broken, "Rear", s);
        const TestResult skipped = cameraResult(CameraOutcome::Skipped, "IR", s);

        // One camera: unchanged (laptops).
        const TestResult one = combineCameraResults({{"Integrated Camera", good}});
        QCOMPARE(one.summary, good.summary);
        QCOMPARE(one.status, Status::Pass);

        // Tablet: front OK, rear broken -> Fail, both named.
        const TestResult two = combineCameraResults({{"Microsoft Camera Front", good}, {"Microsoft Camera Rear", broken}});
        QCOMPARE(two.status, Status::Fail);
        QVERIFY(two.summary.startsWith("Microsoft Camera Front: Camera gives a picture | Microsoft Camera Rear: "));
        QVERIFY(two.details.contains("=== Microsoft Camera Rear ==="));

        // A skipped camera (IR?) never hides a pass or a fault.
        QCOMPARE(combineCameraResults({{"Front", good}, {"IR", skipped}}).status, Status::Pass);
        QCOMPARE(combineCameraResults({{"IR", skipped}, {"Rear", broken}}).status, Status::Fail);
        QCOMPARE(combineCameraResults({{"A", skipped}, {"B", skipped}}).status, Status::Skipped);
    }

        void micLevels()
    {
        // 16-bit: one sample at half scale among quiet ones.
        QByteArray pcm(8, '\0');
        const qint16 half = 16384, quiet = -100;
        std::memcpy(pcm.data() + 2, &half, 2);
        std::memcpy(pcm.data() + 4, &quiet, 2);
        QCOMPARE(peakLevel(pcm, SampleType::Int16), 0.5);
        QCOMPARE(peakLevel(QByteArray(64, '\0'), SampleType::Int16), 0.0);   // dead mic: zeros
        QCOMPARE(peakLevel(QByteArray(3, '\x7f'), SampleType::Float), 0.0);  // too short for one float
        QByteArray f(8, '\0');
        const float a = -0.25f, b = 2.0f;   // clipped to full scale
        std::memcpy(f.data(), &a, 4);
        QCOMPARE(peakLevel(f, SampleType::Float), 0.25);
        std::memcpy(f.data() + 4, &b, 4);
        QCOMPARE(peakLevel(f, SampleType::Float), 1.0);

        QCOMPARE(toDbfs(1.0), 0.0);
        QCOMPARE(qRound(toDbfs(0.05)), -26);
        QCOMPARE(toDbfs(0), -90.0);

        // Hiss never counts; 300 ms of voice does, in several bits.
        MicTracker t;
        for (int i = 0; i < 100; ++i)
            t.add(0.01, 50);
        QVERIFY(!t.heard());
        t.add(0.3, 200);
        QVERIFY(!t.heard());
        t.add(0.06, 100);
        QVERIFY(t.heard());
        QCOMPARE(t.totalMs(), 5300);

        QCOMPARE(micResult(MicOutcome::Heard, t, "Mic").status, Status::Pass);
        QCOMPARE(micResult(MicOutcome::Broken, t, "Mic").status, Status::Fail);
        QCOMPARE(micResult(MicOutcome::NoDevice, {}, {}).status, Status::Skipped);
        QCOMPARE(micResult(MicOutcome::Error, {}, "Mic", "format").status, Status::Error);
        QVERIFY(micResult(MicOutcome::Heard, t, "Mic").details.contains("Loudest peak -10.5 dBFS"));

        // One Audio row: speakers OK + mic broken = Fail, both said.
        const TestResult row = audioWithMic(audioResult(Heard::Left, Heard::Right), micResult(MicOutcome::Broken, t, "Mic"));
        QCOMPARE(row.status, Status::Fail);
        QCOMPARE(row.summary, QString("Left and right speakers work | microphone hears nothing (confirmed by technician)"));
        // Mic skipped never hides the speakers' result.
        QCOMPARE(audioWithMic(audioResult(Heard::Left, Heard::Right), micResult(MicOutcome::Skipped, t, "Mic")).status,
                 Status::Pass);
    }

        void screenAnswers()
    {
        QCOMPARE(int(screenPatterns().size()), 4);   // developer's choice: black, white, grey, red
        QCOMPARE(screenPatterns().front().name, QString("black"));
        QCOMPARE(screenResult(ScreenAnswer::Ok).status, Status::Pass);
        QCOMPARE(screenResult(ScreenAnswer::SmallDefect).status, Status::Warn);   // sell with a note
        QCOMPARE(screenResult(ScreenAnswer::Broken).status, Status::Fail);
        QVERIFY(screenResult(ScreenAnswer::Broken).details.contains("black, white, grey, red"));
        QCOMPARE(screenSkipped().status, Status::Skipped);
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

    void cameraInfrared()
    {
        QVERIFY(isInfraredCamera("Microsoft IR Camera Front"));   // Surface Pro, real name
        QVERIFY(isInfraredCamera("Integrated IR Camera"));
        QVERIFY(isInfraredCamera("Infrared Camera"));
        QVERIFY(!isInfraredCamera("Microsoft Camera Front"));
        QVERIFY(!isInfraredCamera("Microsoft Camera Rear"));
        QVERIFY(!isInfraredCamera("Integrated Camera"));
        QVERIFY(!isInfraredCamera("Mirror Cam"));   // "ir" inside a word
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
        QVERIFY(!r.summary.contains("Laptop"));          // form factor: report only
        QVERIFY(r.details.contains("Form factor: Laptop"));
        QVERIFY(r.summary.contains("S/N PF1ABCDE"));   // shown in the panel: the technician may copy it
        QVERIFY(r.details.contains("PF1ABCDE"));
        QCOMPARE(r.summary.count('\n'), 1);            // two lines: identity, then hardware
    }

    void systemInfoExtras()
    {
        SystemInfo s = fullSystem();
        s.cores = 4;
        s.threads = 8;
        s.os = "Windows 11 Pro 24H2 (build 26100.2033)";
        s.screenMm = QSizeF(309, 174);         // 14.0" laptop panel
        s.screenPixels = QSize(1920, 1080);
        const QString summary = SystemInfoTest::evaluate(s).summary;
        QVERIFY(summary.contains("i5-8350U CPU @ 1.70GHz (4 cores / 8 threads)"));
        QVERIFY(summary.contains("Windows 11 Pro 24H2"));
        QVERIFY(summary.contains("screen 14.0\" 1920x1080"));

        // A driver that gives no (or an absurd) size: the resolution alone.
        s.screenMm = QSizeF(16, 9);
        QVERIFY(SystemInfoTest::evaluate(s).summary.contains("screen 1920x1080"));
        // Nothing known: nothing shown, still a pass (these are extras).
        s.os.clear();
        s.cores = s.threads = 0;
        s.screenPixels = QSize();
        const TestResult bare = SystemInfoTest::evaluate(s);
        QCOMPARE(bare.status, Status::Pass);
        QVERIFY(!bare.summary.contains("screen"));
        QVERIFY(!bare.summary.contains("cores"));
    }

    void windowsVersionNames()
    {
        // The registry still says "Windows 10" on Windows 11: the build decides.
        QCOMPARE(windowsVersionName("Windows 10 Pro", "24H2", 26100, 2033),
                 QString("Windows 11 Pro 24H2 (build 26100.2033)"));
        QCOMPARE(windowsVersionName("Windows 10 Pro", "22H2", 19045, 5011),
                 QString("Windows 10 Pro 22H2 (build 19045.5011)"));
        QCOMPARE(windowsVersionName("Windows 10 Home", "", 17763, 0), QString("Windows 10 Home (build 17763)"));
        QVERIFY(windowsVersionName("", "", 0, 0).isEmpty());
    }

    void drmModes()
    {
        QCOMPARE(parseDrmMode("1920x1080"), QSize(1920, 1080));
        QCOMPARE(parseDrmMode("1920x1080i\n"), QSize(1920, 1080));
        QVERIFY(parseDrmMode("").isEmpty());
        QVERIFY(parseDrmMode("0x0").isEmpty());
        QVERIFY(parseDrmMode("garbage").isEmpty());
    }

    void cpuinfoCores()
    {
        // 2 cores with Hyper-Threading (processors 0-3), the shape of /proc/cpuinfo.
        const QString cpuinfo =
            "processor\t: 0\nphysical id\t: 0\ncore id\t\t: 0\n\n"
            "processor\t: 1\nphysical id\t: 0\ncore id\t\t: 1\n\n"
            "processor\t: 2\nphysical id\t: 0\ncore id\t\t: 0\n\n"
            "processor\t: 3\nphysical id\t: 0\ncore id\t\t: 1\n";
        int cores = -1, threads = -1;
        countCpuCores(cpuinfo, &cores, &threads);
        QCOMPARE(cores, 2);
        QCOMPARE(threads, 4);
        // No "core id" (some virtual machines): cores unknown.
        countCpuCores("processor\t: 0\nmodel name\t: QEMU\n", &cores, &threads);
        QCOMPARE(cores, 0);
        QCOMPARE(threads, 1);
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

    void testChoiceFile()
    {
        TestChoice choice;
        QString error;
        QVERIFY2(choice.loadFromJson(R"({"camera": false, "battery": true})", &error), qPrintable(error));
        QVERIFY(!choice.isTicked("camera"));
        QVERIFY(choice.isTicked("battery"));
        QVERIFY(choice.isTicked("wifi"));   // not in the file: ticked

        QVERIFY(!choice.loadFromJson(R"({"camera": "no"})", &error));   // not guessed
        QVERIFY(error.contains("camera"));
        QVERIFY(choice.isTicked("camera"));                            // nothing kept
        QVERIFY(!choice.loadFromJson("{ broken", &error));
        QVERIFY(!choice.loadFromJson("[true]", &error));
    }

    // What "Remember these choices" writes: readable, in order, read back the same.
    void testChoiceWritten()
    {
        const QByteArray json = TestChoice::toJson({{"battery", true}, {"camera", false}, {"audio", true}});
        QCOMPARE(json, QByteArray("{\n    \"battery\": true,\n    \"camera\": false,\n    \"audio\": true\n}\n"));
        TestChoice choice;
        QString error;
        QVERIFY2(choice.loadFromJson(json, &error), qPrintable(error));
        QVERIFY(!choice.isTicked("camera"));
        QVERIFY(choice.isTicked("audio"));
    }

    // The file shipped in config/ names every test of main.cpp, all ticked.
    void testChoiceShippedFile()
    {
        QFile f(QFINDTESTDATA("../config/tests.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        TestChoice choice;
        QString error;
        QVERIFY2(choice.loadFromJson(f.readAll(), &error), qPrintable(error));
        QCOMPARE(int(choice.ticked.size()), 12);
        for (const auto& [id, on] : choice.ticked)
            QVERIFY2(on, qPrintable(id));
    }

    // START with only some boxes ticked: those run, in the runner's order.
    void runnerStartsChosenTests()
    {
        TestRunner runner;
        std::vector<FakeTest*> t;
        for (int i = 0; i < 3; ++i) {
            auto test = std::make_unique<FakeTest>(false, false);
            t.push_back(test.get());
            runner.add(std::move(test));
        }
        QSignalSpy started(&runner, &TestRunner::testStarted);
        QSignalSpy all(&runner, &TestRunner::allFinished);

        runner.start({2, 0, 7, 0});        // order given and duplicates do not matter; 7 does not exist
        QCOMPARE(t[0]->runs, 1);
        t[0]->answer(Status::Pass);
        QCOMPARE(t[1]->runs, 0);           // not chosen
        QCOMPARE(t[2]->runs, 1);
        t[2]->answer(Status::Pass);
        QCOMPARE(all.count(), 1);
        QCOMPARE(started.count(), 2);
        QCOMPARE(started.at(1).at(0).toInt(), 2);

        runner.start({});                  // nothing chosen: nothing starts
        QVERIFY(!runner.isRunning());
        QCOMPARE(all.count(), 1);
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
        QVERIFY(DeliverySheet::currentOxfamNumber(sheet).isEmpty());
        QCOMPARE(sheet.cell(checkTab(), "C21"), QStringLiteral("\u2610"));   // touch screen: unticked in the blank sheet
        const QString blankC16 = sheet.cell(checkTab(), "C16");
        const QString blankD16 = sheet.cell(checkTab(), "D16");
        QVERIFY2(DeliverySheet::fill(sheet, QDate(2026, 10, 10), "Dell Latitude 7420", "Ixelles", "012345", results,
                                     true, &error),
                 qPrintable(error));

        XlsxEditor reread;
        QVERIFY2(reread.load(sheet.save(&error), &error), qPrintable(error));
        const auto at = [&](const char* ref) { return reread.cell(checkTab(), ref); };
        QCOMPARE(at("C4"), QStringLiteral("Ixelles"));
        QCOMPARE(DeliverySheet::currentShop(reread), QStringLiteral("Ixelles"));
        QCOMPARE(at("C5"), QStringLiteral("Dell Latitude 7420"));
        QCOMPARE(at("C6"), QStringLiteral("012345"));   // text: the leading 0 stays
        QCOMPARE(DeliverySheet::currentOxfamNumber(reread), QStringLiteral("012345"));
        QCOMPARE(at("C25"), QStringLiteral("10/10/2026"));   // the operator's date
        QCOMPARE(at("C7"), QStringLiteral("10/10/2026"));    // the header date was empty
        QCOMPARE(at("C12"), QStringLiteral("\u2611"));
        QCOMPARE(at("D12"), QStringLiteral("85 %"));
        QCOMPARE(at("C13"), QStringLiteral("\u2611"));   // ERROR: not written either
        QVERIFY(at("D13").isEmpty());
        // FAIL and WARN: ticked, key value only (none for these rows), never the summary.
        QCOMPARE(at("C14"), QStringLiteral("\u2611"));
        QVERIFY(at("D14").isEmpty());
        QCOMPARE(at("C15"), QStringLiteral("\u2611"));
        QVERIFY(at("D15").isEmpty());
        QCOMPARE(at("C16"), blankC16);   // SKIPPED: not written, never unticked
        QCOMPARE(at("D16"), blankD16);   // and never the summary
        QCOMPARE(at("C21"), QStringLiteral("\u2611"));   // touch screen seen

        // A worn battery (FAIL or WARN): ticked, the percentage alone.
        std::map<SheetRow, TestResult> worn;
        worn[SheetRow::Battery] = {Status::Fail, "Health 45% of design capacity (FAIL)", "", "45 %"};
        QVERIFY2(DeliverySheet::fill(reread, QDate(2026, 10, 11), "", "", "", worn, false, &error), qPrintable(error));
        QCOMPARE(at("C12"), QStringLiteral("\u2611"));
        QCOMPARE(at("D12"), QStringLiteral("45 %"));
        QCOMPARE(at("C21"), QStringLiteral("\u2611"));   // no touch screen seen: left as it was

        // Filled again another day: the header date stays, the operator's date follows.
        QVERIFY2(DeliverySheet::fill(reread, QDate(2026, 10, 12), "", "", "", {}, false, &error), qPrintable(error));
        QCOMPARE(at("C7"), QStringLiteral("10/10/2026"));
        QCOMPARE(at("C25"), QStringLiteral("12/10/2026"));
        QCOMPARE(at("C4"), QStringLiteral("Ixelles"));   // no shop given: left as it was
        QCOMPARE(at("C6"), QStringLiteral("012345"));    // no number given: left as it was
    }

    void oxfamNumberFormat()
    {
        QVERIFY(DeliverySheet::isOxfamNumber("123456"));
        QVERIFY(DeliverySheet::isOxfamNumber("000123"));
        QVERIFY(!DeliverySheet::isOxfamNumber("12345"));
        QVERIFY(!DeliverySheet::isOxfamNumber("1234567"));
        QVERIFY(!DeliverySheet::isOxfamNumber("12 456"));
        QVERIFY(!DeliverySheet::isOxfamNumber("12345a"));
        QVERIFY(!DeliverySheet::isOxfamNumber(""));
        QVERIFY(!DeliverySheet::isOxfamNumber("123456\n"));   // "^...$" alone lets a final newline through
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

    // No name to add: the list is still written back sorted (Oxfam's model
    // list came unsorted).
    void deliverySheetSortsOnly()
    {
        XlsxEditor sheet;
        QString error;
        QVERIFY2(sheet.load(sample("delivery_sheet_fr.xlsx"), &error), qPrintable(error));
        const QStringList before = DeliverySheet::names(sheet, SheetList::Models);
        QVERIFY2(DeliverySheet::addNames(sheet, SheetList::Models, {}, &error), qPrintable(error));
        const QStringList after = DeliverySheet::names(sheet, SheetList::Models);
        QCOMPARE(after, DeliverySheet::sorted(before));
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

        // Only some tests ran: said in the text, never a bare "OK FOR SALE".
        const Verdict partial = computeVerdict({Status::Pass}, 8);
        QCOMPARE(partial.text, QString("PARTIAL (1 of 8 tests) - OK FOR SALE"));
        QCOMPARE(partial.level, Status::Warn);
        QCOMPARE(computeVerdict({Status::Fail}, 8).level, Status::Fail);
        QCOMPARE(computeVerdict({Status::Pass, Status::Pass}, 2).text, QString("OK FOR SALE"));
    }
};

// GUILESS, not APPLESS: TestRunner starts QTimers, which need a QCoreApplication.
QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
