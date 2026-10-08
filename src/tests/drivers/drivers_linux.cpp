#include "tests/drivers/DriverInfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {

QString readText(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

} // namespace

// Reads sysfs directly: no dependency on lspci. A PCI device whose
// "driver" symlink is missing has no kernel driver bound to it.
DriverInfo readDrivers()
{
    DriverInfo info;
    const QDir root(QStringLiteral("/sys/bus/pci/devices"));
    const QStringList pciSlots = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name);
    if (pciSlots.isEmpty()) {
        info.error = QStringLiteral("No PCI device found in %1").arg(root.path());
        return info;
    }

    QStringList lines;
    for (const QString& slot : pciSlots) {
        const QString dir = root.filePath(slot);
        bool ok = false;
        const unsigned cls = readText(dir + "/class").toUInt(&ok, 16);   // "0x020000"
        const QString ids = QStringLiteral("%1:%2").arg(readText(dir + "/vendor").mid(2),
                                                        readText(dir + "/device").mid(2));
        const QFileInfo driverLink(dir + "/driver");
        const QString driver = driverLink.exists() ? QFileInfo(driverLink.symLinkTarget()).fileName()
                                                   : QStringLiteral("-");
        lines << QStringLiteral("%1 class=%2 [%3] driver=%4")
                     .arg(slot, readText(dir + "/class"), ids, driver);

        if (ok && !driverLink.exists() && pciClassMatters(cls))
            info.problems.push_back({QStringLiteral("%1 [%2]").arg(pciClassName(cls), ids), slot, -1});
    }
    info.raw = lines.join('\n');
    return info;
}
