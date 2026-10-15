#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>
#include <QtTest>

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

    void cpuWindowsParsing()
    {
        // Real output, at rest on battery: no ACPI zone, battery as an object.
        const CpuReading rest = parseWindowsCpu(sample("cpu_win_latitude7420_rest_battery.json"));
        QVERIFY(rest.error.isEmpty());
        QVERIFY(!rest.hasTemp);
        QVERIFY(rest.hasSpeed);
        QCOMPARE(rest.speedPct, 88.0);
        QVERIFY(rest.onBattery);

        const CpuReading load = parseWindowsCpu(sample("cpu_win_latitude7420_load.json"));
        QCOMPARE(load.speedPct, 241.0);
        QVERIFY(!load.onBattery);   // BatteryStatus 2 = on the charger

        // A counter that is not ready gives 0: not a speed.
        QVERIFY(!parseWindowsCpu(R"({"zones":[],"cpu":{"PercentProcessorPerformance":0}})").hasSpeed);
    }

    void runReportText()
    {
        RunReport report;
        report.when = QDateTime(QDate(2026, 10, 15), QTime(14, 32, 5));
        report.model = "Dell Latitude 7420";
        report.verdict = "SELL WITH NOTE - see warnings";
        TestResult cpu;
        cpu.status = Status::Warn;
        cpu.summary = "Slowed to 83 % of base speed on battery";
        cpu.details = "Load: 8 threads, 30 s\nIdle speed: 123 % of base\n";
        report.rows.push_back({"CPU temperature", cpu});
        report.rows.push_back({"Camera", TestResult{}});   // never run: Skipped, empty

        const QString text = formatRunReport(report);
        QVERIFY(text.startsWith("Lazy Djin's Tester - 2026-10-15 14:32:05\nPC: Dell Latitude 7420\n"
                                "Verdict: SELL WITH NOTE - see warnings\n"));
        QVERIFY(text.contains("\n==== [WARN] CPU temperature ====\nSlowed to 83 % of base speed on battery\n\n"
                              "Load: 8 threads, 30 s\nIdle speed: 123 % of base\n"));   // details as in the pane
        QVERIFY(text.contains("==== [SKIPPED] Camera ===="));
        report.model.clear();
        QVERIFY(formatRunReport(report).contains("PC: unknown\n"));
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
        // the speed is live and settles at ~150 %.
        CpuLoadRun lenovo;
        lenovo.idle = parseWindowsCpu(sample("cpu_win_l380_rest.json"));
        QVERIFY(lenovo.idle.hasTemp);
        QCOMPARE(lenovo.idle.celsius, 3502 / 10.0 - 273.15);
        QCOMPARE(lenovo.idle.speedPct, 67.0);
        lenovo.samples.assign(6, lenovo.idle.celsius);
        lenovo.speeds = {184, 160, 151, 150, 148, 150};
        const TestResult l = CpuTempTest::evaluate(lenovo, t);
        QCOMPARE(l.status, Status::Pass);   // a deaf sensor does not hide a good speed
        QVERIFY(l.summary.contains("did not react"));
        QVERIFY(l.summary.contains("150 %"));

        // Latitude 7420, 3rd load in a row on battery: turbo budget spent, under base speed.
        CpuLoadRun dell = speedRun({266, 266, 210, 106, 82, 86, 82, 91, 83, 84, 93, 82, 81}, true);
        const TestResult d = CpuTempTest::evaluate(dell, t);
        QCOMPARE(d.status, Status::Warn);   // on battery: never Fail
        QVERIFY(d.summary.contains("83 %"));
        QVERIFY(d.summary.contains("charger"));
    }

    void acpiZonesParsing()
    {
        const CpuReading r = parseWindowsCpu(sample("acpi_zones.json"));
        QVERIFY(r.error.isEmpty());
        QVERIFY(r.hasTemp);
        QCOMPARE(qRound(r.celsius), 60);   // hottest zone, 3332 = 60.05 C; the 0 K zone is ignored
        QCOMPARE(r.source, QString("ACPI\\ThermalZone\\THM0_0"));

        const CpuReading none = parseWindowsCpu(sample("acpi_unsupported.json"));
        QVERIFY(none.error.isEmpty());
        QVERIFY(!none.hasTemp);
        QVERIFY(!none.hasSpeed);   // older script: no "cpu" key
        QVERIFY(!parseWindowsCpu("not json").error.isEmpty());
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

    void systemInfoComplete()
    {
        const TestResult r = SystemInfoTest::evaluate(fullSystem());
        QCOMPARE(r.status, Status::Pass);
        QVERIFY(r.summary.contains("ThinkPad T480"));
        QVERIFY(r.summary.contains("8.0 GiB"));
        QVERIFY(r.summary.contains("Laptop"));
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
        s.error = "PowerShell timed out";
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
        const QStringList models = DeliverySheet::models(sheet);
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
        QVERIFY2(DeliverySheet::fill(sheet, QDate(2026, 10, 10), "Dell Latitude 7420", results, &error),
                 qPrintable(error));

        XlsxEditor reread;
        QVERIFY2(reread.load(sheet.save(&error), &error), qPrintable(error));
        const auto at = [&](const char* ref) { return reread.cell(checkTab(), ref); };
        QCOMPARE(at("C5"), QStringLiteral("Dell Latitude 7420"));
        QCOMPARE(at("C7"), QStringLiteral("10/10/2026"));
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
    }

    void deliverySheetModels_data() { sheetFiles(); }
    void deliverySheetModels()
    {
        QFETCH(QString, file);
        XlsxEditor sheet;
        QString error;
        QVERIFY2(sheet.load(sample(file), &error), qPrintable(error));
        const QStringList models = DeliverySheet::models(sheet);
        QCOMPARE(DeliverySheet::findModel(models, "  dell   LATITUDE 7420 "), QStringLiteral("Dell Latitude 7420"));
        QVERIFY(DeliverySheet::findModel(models, "Lenovo T480").isEmpty());
        QVERIFY(DeliverySheet::findModel(models, "").isEmpty());

        QVERIFY2(DeliverySheet::addModel(sheet, "Lenovo  T480", &error), qPrintable(error));
        XlsxEditor reread;
        QVERIFY2(reread.load(sheet.save(&error), &error), qPrintable(error));
        const QStringList after = DeliverySheet::models(reread);
        QCOMPARE(after.size(), 54);
        QCOMPARE(after.last(), QStringLiteral("Lenovo T480"));
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
