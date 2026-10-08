#include "tests/disk/DiskTest.h"

#include <optional>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStringList>
#include <QtConcurrent>

namespace {

// smartctl exit code bits (man smartctl, RETURN VALUES)
constexpr int kBitCommandLine = 0x01;   // bad arguments
constexpr int kBitOpenFailed  = 0x02;   // device open failed: usually no root/admin

// Order used to combine several disks into one row.
int severity(Status s)
{
    switch (s) {
    case Status::Fail:    return 4;
    case Status::Error:   return 3;
    case Status::Warn:    return 2;
    case Status::Pass:    return 1;
    case Status::Skipped: return 0;
    }
    return 3;
}

Status worse(Status a, Status b) { return severity(a) >= severity(b) ? a : b; }

// 1234567 -> "1234567" (QString::arg(double) would give "1.23457e+06")
QString whole(double v) { return QString::number(v, 'f', 0); }

// Raw value of an ATA attribute, e.g. id 5 = Reallocated_Sector_Ct.
std::optional<double> ataRaw(const QJsonObject& root, int id)
{
    const QJsonArray table = root["ata_smart_attributes"].toObject()["table"].toArray();
    for (const QJsonValue& v : table) {
        const QJsonObject a = v.toObject();
        if (a["id"].toInt() == id)
            return a["raw"].toObject()["value"].toDouble();
    }
    return std::nullopt;
}

std::optional<double> number(const QJsonValue& v)
{
    return v.isDouble() ? std::optional<double>(v.toDouble()) : std::nullopt;
}

} // namespace

DiskTest::DiskTest(Thresholds thresholds, QObject* parent)
    : ITest(parent), m_thresholds(std::move(thresholds))
{
    connect(&m_watcher, &QFutureWatcher<DiskScan>::finished, this, [this] {
        emit finished(evaluate(m_watcher.result(), m_thresholds));
    });
}

void DiskTest::run()
{
    // Bundled copy first (portable app), then the system one (Arch: smartmontools).
    // findExecutable() adds ".exe" on Windows by itself.
    const QString tools = QCoreApplication::applicationDirPath() + QStringLiteral("/tools");
    QString smartctl = QStandardPaths::findExecutable(QStringLiteral("smartctl"), {tools});
    if (smartctl.isEmpty())
        smartctl = QStandardPaths::findExecutable(QStringLiteral("smartctl"));

    m_watcher.setFuture(QtConcurrent::run(&readDisks, smartctl));
}

TestResult DiskTest::evaluateDisk(const SmartctlRun& run, const Thresholds& thresholds)
{
    TestResult r;
    const QString header = QStringLiteral("=== %1 (-d %2), smartctl exit code %3 ===")
                               .arg(run.device, run.type).arg(run.exitCode);
    r.details = header + QLatin1Char('\n') + QString::fromUtf8(run.json);

    if (!run.error.isEmpty()) {
        r.status = Status::Error;
        r.summary = QStringLiteral("%1: could not run smartctl").arg(run.device);
        r.details = header + QLatin1Char('\n') + run.error;
        return r;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(run.json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        r.status = Status::Error;
        r.summary = QStringLiteral("%1: unreadable smartctl output").arg(run.device);
        return r;
    }
    const QJsonObject root = doc.object();

    // smartctl explains its problems in smartctl.messages[].string
    QStringList messages;
    for (const QJsonValue& m : root["smartctl"].toObject()["messages"].toArray())
        messages << m.toObject()["string"].toString();

    if (run.exitCode & (kBitCommandLine | kBitOpenFailed)) {
        r.status = Status::Error;
        r.summary = QStringLiteral("%1: cannot open disk (run as root/administrator?)").arg(run.device);
        r.details = header + QStringLiteral("\nsmartctl could not open the device. Reading SMART "
                                            "data usually needs root (Linux) or administrator (Windows).\n")
                    + messages.join('\n') + QStringLiteral("\n\n") + QString::fromUtf8(run.json);
        return r;
    }

    // Name shown in the table: "/dev/sda Samsung SSD 860 EVO 500 GB"
    QString label = run.device;
    const QString model = root["model_name"].toString();
    if (!model.isEmpty())
        label += QLatin1Char(' ') + model;
    double bytes = root["user_capacity"].toObject()["bytes"].toDouble();
    if (bytes <= 0)
        bytes = root["nvme_total_capacity"].toDouble();
    if (bytes > 0)
        label += QStringLiteral(" %1 GB").arg(bytes / 1e9, 0, 'f', 0);   // decimal GB, as on the label

    const QJsonValue passed = root["smart_status"].toObject()["passed"];
    if (!passed.isBool()) {
        // USB sticks and card readers have no SMART: not a fault of the PC.
        const QJsonValue available = root["smart_support"].toObject()["available"];
        r.status = (available.isBool() && !available.toBool()) ? Status::Skipped : Status::Error;
        r.summary = r.status == Status::Skipped
                        ? QStringLiteral("%1: no SMART (USB stick or card reader?)").arg(label)
                        : QStringLiteral("%1: no SMART health status").arg(label);
        return r;
    }

    Status status = passed.toBool() ? Status::Pass : Status::Fail;
    QStringList parts;
    parts << (passed.toBool() ? QStringLiteral("SMART OK") : QStringLiteral("SMART FAILED"));

    // Each check: threshold key, value (if the disk reports it), text.
    struct Check { const char* key; std::optional<double> value; QString text; };
    const QJsonObject nvme = root["nvme_smart_health_information_log"].toObject();
    const std::optional<double> realloc = ataRaw(root, 5);
    const std::optional<double> pending = ataRaw(root, 197);
    const std::optional<double> used = number(nvme["percentage_used"]);
    const std::optional<double> temp = number(root["temperature"].toObject()["current"]);
    const Check checks[] = {
        {"disk_reallocated_sectors", realloc, QStringLiteral("%1 reallocated").arg(whole(realloc.value_or(0)))},
        {"disk_pending_sectors",     pending, QStringLiteral("%1 pending").arg(whole(pending.value_or(0)))},
        {"nvme_percentage_used",     used,    QStringLiteral("%1% worn").arg(whole(used.value_or(0)))},
        {"disk_temperature_c",       temp,    QStringLiteral("%1 C").arg(whole(temp.value_or(0)))},
    };
    for (const Check& c : checks) {
        if (!c.value)
            continue;   // not reported by this kind of disk (e.g. no sectors on NVMe)
        const QString key = QString::fromLatin1(c.key);
        // Without a rule we would silently show green: refuse instead.
        if (!thresholds.has(key)) {
            status = worse(status, Status::Error);
            parts << QStringLiteral("no threshold %1").arg(key);
            continue;
        }
        const Status s = thresholds.evaluate(key, *c.value);
        status = worse(status, s);
        parts << (s == Status::Pass ? c.text : QStringLiteral("%1 (%2)").arg(c.text, statusLabel(s)));
    }

    // Power-on hours: information only, no threshold yet (to agree with colleagues, M5).
    if (const auto hours = number(root["power_on_time"].toObject()["hours"]))
        parts << QStringLiteral("%1 h").arg(whole(*hours));

    r.status = status;
    r.summary = QStringLiteral("%1: %2").arg(label, parts.join(", "));
    return r;
}

TestResult DiskTest::evaluate(const DiskScan& scan, const Thresholds& thresholds)
{
    TestResult r;
    if (!scan.error.isEmpty()) {
        r.status = Status::Error;
        r.summary = QStringLiteral("Could not read disks");
        r.details = scan.error + QStringLiteral("\n\n") + QString::fromUtf8(scan.scanJson);
        return r;
    }
    if (scan.disks.empty()) {
        r.status = Status::Error;
        r.summary = QStringLiteral("No disk found");
        r.details = QStringLiteral("smartctl --scan found nothing:\n") + QString::fromUtf8(scan.scanJson);
        return r;
    }

    Status status = Status::Skipped;
    QStringList summaries, details;
    for (const SmartctlRun& d : scan.disks) {
        const TestResult one = evaluateDisk(d, thresholds);
        status = worse(status, one.status);
        summaries << one.summary;
        details << one.details;
    }
    // Every disk skipped = we checked nothing: that must not look like a pass.
    if (status == Status::Skipped) {
        status = Status::Error;
        summaries.prepend(QStringLiteral("No disk with SMART data"));
    }

    r.status = status;
    r.summary = summaries.join(QStringLiteral(" | "));
    r.details = details.join(QStringLiteral("\n\n"));
    return r;
}
