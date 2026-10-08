#include <memory>
#include <QApplication>
#include <QMessageBox>

#include "core/TestRunner.h"
#include "core/Thresholds.h"
#include "tests/battery/BatteryTest.h"
#include "tests/disk/DiskTest.h"
#include "tests/drivers/DriversTest.h"
#include "tests/license/LicenseTest.h"
#include "tests/sysinfo/SystemInfoTest.h"
#include "ui/MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Oxfam PC Tester"));

    // thresholds.json sits next to the executable (portable app)
    Thresholds thresholds;
    QString error;
    const QString path = QApplication::applicationDirPath() + QStringLiteral("/thresholds.json");
    if (!thresholds.loadFromFile(path, &error)) {
        QMessageBox::warning(nullptr, QObject::tr("Configuration"),
                             QObject::tr("thresholds.json not loaded:\n%1\n\n"
                                         "Tests needing thresholds will report ERROR.").arg(error));
    }

    // Composition root: the only place that knows every concrete test.
    TestRunner runner;                       // declared before the window: outlives it
    runner.add(std::make_unique<SystemInfoTest>());   // first: identifies the PC
    runner.add(std::make_unique<BatteryTest>(thresholds));
    runner.add(std::make_unique<DiskTest>(thresholds));
    runner.add(std::make_unique<DriversTest>());
    runner.add(std::make_unique<LicenseTest>());

    MainWindow window(runner);
    window.show();
    return app.exec();
}
