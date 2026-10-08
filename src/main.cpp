#include <memory>
#include <QApplication>
#include <QFile>
#include <QMessageBox>

#include "core/TestRunner.h"
#include "core/Thresholds.h"
#include "tests/battery/BatteryTest.h"
#include "tests/disk/DiskTest.h"
#include "tests/drivers/DriversTest.h"
#include "tests/license/LicenseTest.h"
#include "tests/sysinfo/SystemInfoTest.h"
#include "ui/MainWindow.h"
#include "ui/interactive/KeyboardTest.h"

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

    // keyboard_layout.json also sits next to the executable. If it is missing,
    // the keyboard test reports ERROR when it runs (no pop-up at start).
    QFile layoutFile(QApplication::applicationDirPath() + QStringLiteral("/keyboard_layout.json"));
    KeyLayout keyLayout;
    if (layoutFile.open(QIODevice::ReadOnly))
        keyLayout = parseKeyLayout(layoutFile.readAll());
    else
        keyLayout.error = QStringLiteral("Cannot open %1: %2").arg(layoutFile.fileName(), layoutFile.errorString());

    // Composition root: the only place that knows every concrete test.
    TestRunner runner;                       // declared before the window: outlives it
    runner.add(std::make_unique<SystemInfoTest>());   // first: identifies the PC
    runner.add(std::make_unique<BatteryTest>(thresholds));
    runner.add(std::make_unique<DiskTest>(thresholds));
    runner.add(std::make_unique<DriversTest>());
    runner.add(std::make_unique<LicenseTest>());
    runner.add(std::make_unique<KeyboardTest>(keyLayout));   // interactive: runs last

    MainWindow window(runner);
    window.show();
    return app.exec();
}
