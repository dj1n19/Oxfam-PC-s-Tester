#include "tests/disk/DiskInfo.h"

// The program already runs as administrator (manifest): call smartctl directly.
std::vector<SmartctlRun> readDevices(const QString& smartctlPath, const std::vector<ScannedDevice>& devices)
{
    std::vector<SmartctlRun> runs;
    for (const ScannedDevice& dev : devices)
        runs.push_back(runSmartctl(smartctlPath, dev));
    return runs;
}
