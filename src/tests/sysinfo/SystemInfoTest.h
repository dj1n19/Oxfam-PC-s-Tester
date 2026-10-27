#pragma once
#include <QCoreApplication>
#include "core/TestResult.h"
#include "tests/sysinfo/SystemInfo.h"

enum class FormFactor { Unknown, Desktop, Laptop };

// What the "This PC" panel above the test list shows. Not a test of the
// runner any more (developer's decision 2026-10-27): it identifies the PC,
// it does not check hardware, so it never counts in the verdict. Read once
// at launch (MainWindow, readSystemInfo() on a worker thread). The name stays:
// it is the translation context of its texts.
class SystemInfoTest {
    Q_DECLARE_TR_FUNCTIONS(SystemInfoTest)   // tr() without being a QObject
public:
    // Pure functions: easy to unit test without any hardware.
    static FormFactor formFactorFromChassis(int smbiosChassisType);
    // status: Pass, or Error if the probe failed or a value is missing.
    // summary: one line for the panel, note: "vendor model", details: raw values.
    static TestResult evaluate(const SystemInfo& info);
};
