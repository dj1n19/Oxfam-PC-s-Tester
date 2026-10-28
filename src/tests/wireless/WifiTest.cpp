#include "tests/wireless/WifiTest.h"

#include <QSet>
#include <QtConcurrent>

WifiTest::WifiTest(QObject* parent) : ITest(parent)
{
    connect(&m_watcher, &QFutureWatcher<std::pair<WifiInfo, FormFactor>>::finished, this, [this] {
        const auto& [info, form] = m_watcher.result();
        emit finished(evaluate(info, form));
    });
}

void WifiTest::run()
{
    // The chassis type comes from the same probe as the "This PC" panel:
    // a few file reads or one firmware table, quick enough to read again.
    m_watcher.setFuture(QtConcurrent::run([] {
        return std::make_pair(readWifi(), SystemInfoTest::formFactorFromChassis(readSystemInfo().chassisType));
    }));
}

TestResult WifiTest::evaluate(const WifiInfo& info, FormFactor form)
{
    TestResult r;
    r.details = info.raw;

    if (!info.error.isEmpty()) {
        r.status = Status::Error;
        r.summary = tr("Could not read the Wi-Fi adapter (see details)");
        r.details = info.error + QLatin1Char('\n') + info.raw;
        return r;
    }
    if (!info.present) {
        // Many desktops have no Wi-Fi; a laptop always should.
        if (form == FormFactor::Laptop) {
            r.status = Status::Fail;
            r.summary = tr("No Wi-Fi adapter found: card missing or its driver not installed");
        } else {
            r.status = Status::Skipped;
            r.summary = tr("No Wi-Fi adapter (desktop?)");
        }
        return r;
    }
    if (info.radioOff) {
        r.status = Status::Warn;
        r.summary = tr("Wi-Fi is off (key, switch or airplane mode): turn it on and run this test again");
        return r;
    }
    if (!info.counted) {
        // The adapter and its driver are checked; the antenna is not.
        r.status = Status::Pass;
        r.summary = tr("Wi-Fi adapter on (networks not counted: NetworkManager missing)");
        return r;
    }

    // One access point = one line: count each name once. Hidden networks
    // have no name: each counts on its own.
    QSet<QString> names;
    int hidden = 0;
    for (const QString& ssid : info.ssids) {
        if (ssid.isEmpty())
            ++hidden;
        else
            names.insert(ssid);
    }
    const int networks = int(names.size()) + hidden;
    r.details = QStringLiteral("Networks seen: %1\n%2").arg(networks).arg(info.raw);
    if (networks == 0) {
        r.status = Status::Warn;
        r.summary = tr("Wi-Fi on but no network seen: antenna cable unplugged? (check where a network is known to be)");
        return r;
    }
    r.status = Status::Pass;
    r.summary = tr("Wi-Fi works: %1 networks seen").arg(networks);
    return r;
}
