#include "tests/sysinfo/SystemInfo.h"

#include <set>
#include <utility>

#include <QStringList>

QString windowsVersionName(const QString& productName, const QString& displayVersion, int build, int ubr)
{
    QString name = productName.simplified();
    if (build >= 22000 && name.startsWith(QLatin1String("Windows 10")))
        name.replace(0, 10, QStringLiteral("Windows 11"));
    if (!displayVersion.isEmpty())
        name += QLatin1Char(' ') + displayVersion;
    if (build > 0) {
        const QString number = ubr > 0 ? QStringLiteral("%1.%2").arg(build).arg(ubr) : QString::number(build);
        name += QStringLiteral(" (build %1)").arg(number);
    }
    return name.trimmed();
}

// One block per logical processor: "processor : 3", "physical id : 0",
// "core id : 1"... A core = one (physical id, core id) pair; with
// Hyper-Threading two processors share it.
void countCpuCores(const QString& cpuinfo, int* cores, int* threads)
{
    std::set<std::pair<QString, QString>> physicalCores;
    int processors = 0;
    QString physicalId;
    QString coreId;
    bool hasCoreId = false;
    const auto endBlock = [&] {
        if (hasCoreId)
            physicalCores.insert({physicalId, coreId});
        physicalId.clear();
        coreId.clear();
        hasCoreId = false;
    };
    for (const QString& line : cpuinfo.split(QLatin1Char('\n'))) {
        const QString key = line.section(QLatin1Char(':'), 0, 0).trimmed();
        const QString value = line.section(QLatin1Char(':'), 1).trimmed();
        if (key.isEmpty() && value.isEmpty()) {
            endBlock();
        } else if (key == QLatin1String("processor")) {
            ++processors;
        } else if (key == QLatin1String("physical id")) {
            physicalId = value;
        } else if (key == QLatin1String("core id")) {
            coreId = value;
            hasCoreId = true;
        }
    }
    endBlock();
    *threads = processors;
    *cores = int(physicalCores.size());   // 0 without "core id" (some VMs, ARM)
}
