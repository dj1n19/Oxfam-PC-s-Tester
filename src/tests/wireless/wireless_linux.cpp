#include "tests/wireless/WirelessInfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {

QString readText(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

// rfkill = the kernel's on/off switches for radios: "soft" = turned off by
// software (airplane mode, a setting), "hard" = a physical switch or key.
// A laptop may have two entries for one radio (the vendor's and the
// driver's): blocked if any of them is.
bool rfkillBlocked(const QString& type, QStringList* raw)
{
    const QDir root(QStringLiteral("/sys/class/rfkill"));
    bool blocked = false;
    for (const QString& entry : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name)) {
        const QString dir = root.filePath(entry);
        if (readText(dir + "/type") != type)
            continue;
        const QString soft = readText(dir + "/soft");
        const QString hard = readText(dir + "/hard");
        *raw << QStringLiteral("rfkill %1 (%2): soft=%3 hard=%4").arg(entry, readText(dir + "/name"), soft, hard);
        if (soft == QLatin1String("1") || hard == QLatin1String("1"))
            blocked = true;
    }
    return blocked;
}

// Runs nmcli and returns its output; *ok = false if it failed (why in *raw).
QString nmcli(const QString& program, const QStringList& args, bool* ok, QStringList* raw)
{
    QProcess p;
    p.start(program, args);
    *ok = false;
    if (!p.waitForFinished(20000)) {
        p.kill();
        p.waitForFinished(1000);
        *raw << QStringLiteral("nmcli %1: no answer in 20 s").arg(args.join(' '));
        return {};
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        *raw << QStringLiteral("nmcli %1: exit %2: %3")
                    .arg(args.join(' ')).arg(p.exitCode()).arg(QString::fromUtf8(p.readAllStandardError()).trimmed());
        return {};
    }
    *ok = true;
    return QString::fromUtf8(p.readAllStandardOutput());
}

} // namespace

WifiInfo readWifi()
{
    WifiInfo info;
    QStringList raw;
    // A network interface with a "wireless" folder is a Wi-Fi adapter whose
    // driver is loaded.
    const QDir net(QStringLiteral("/sys/class/net"));
    for (const QString& iface : net.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name)) {
        if (!QFileInfo::exists(net.filePath(iface) + "/wireless") && !QFileInfo::exists(net.filePath(iface) + "/phy80211"))
            continue;
        info.present = true;
        const QString driver = QFileInfo(net.filePath(iface) + "/device/driver").symLinkTarget().section('/', -1);
        info.name = driver.isEmpty() ? iface : driver;
        raw << QStringLiteral("interface %1, driver %2").arg(iface, driver.isEmpty() ? QStringLiteral("?") : driver);
        break;   // first adapter only
    }
    if (!info.present) {
        info.raw = QStringLiteral("no interface with /wireless in /sys/class/net");
        return info;
    }
    info.radioOff = rfkillBlocked(QStringLiteral("wlan"), &raw);

    // Counting networks needs NetworkManager (Mint, Ubuntu...). Without it
    // (iwd, a bare system) the adapter is checked, the networks are not.
    const QString program = QStandardPaths::findExecutable(QStringLiteral("nmcli"));
    if (program.isEmpty()) {
        raw << QStringLiteral("nmcli not found: networks not counted");
    } else if (!info.radioOff) {
        bool ok = false;
        const QString radio = nmcli(program, {"radio", "wifi"}, &ok, &raw).trimmed();
        raw << QStringLiteral("nmcli radio wifi: %1").arg(radio);
        if (ok && radio == QLatin1String("disabled")) {
            info.radioOff = true;
        } else if (ok) {
            // -t: one SSID per line, hidden networks as empty lines.
            QString out = nmcli(program, {"-t", "-f", "SSID", "device", "wifi", "list", "--rescan", "auto"}, &ok, &raw);
            if (ok) {
                if (out.endsWith('\n'))
                    out.chop(1);
                info.counted = true;
                if (!out.isEmpty())
                    info.ssids = out.split('\n');
                raw << QStringLiteral("nmcli: %1 access points").arg(info.ssids.size());
            }
        }
    }
    info.raw = raw.join('\n');
    return info;
}

BluetoothInfo readBluetooth()
{
    BluetoothInfo info;
    QStringList raw;
    const QDir root(QStringLiteral("/sys/class/bluetooth"));
    for (const QString& hci : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name)) {
        if (!hci.startsWith(QLatin1String("hci")) || hci.contains(':'))
            continue;   // hci0:256 = a connection, not an adapter
        info.present = true;
        const QString driver = QFileInfo(root.filePath(hci) + "/device/driver").symLinkTarget().section('/', -1);
        info.name = driver.isEmpty() ? hci : driver;
        raw << QStringLiteral("adapter %1, driver %2").arg(hci, driver.isEmpty() ? QStringLiteral("?") : driver);
        break;
    }
    if (!info.present) {
        info.raw = QStringLiteral("no hci adapter in /sys/class/bluetooth");
        return info;
    }
    info.radioOff = rfkillBlocked(QStringLiteral("bluetooth"), &raw);
    info.raw = raw.join('\n');
    return info;
}
