#include <memory>
#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QMessageBox>

#include "core/TestRunner.h"
#include "core/Thresholds.h"
#include "tests/battery/BatteryTest.h"
#include "tests/cputemp/CpuTempTest.h"
#include "tests/disk/DiskTest.h"
#include "tests/drivers/DriversTest.h"
#include "tests/license/LicenseTest.h"
#include "tests/sysinfo/SystemInfoTest.h"
#include "ui/Language.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "ui/interactive/AudioTest.h"
#include "ui/interactive/CameraTest.h"
#include "ui/interactive/KeyboardTest.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Lazy Djin's Tester"));
    // The djinn, in every size we have: Qt picks the closest one for the title bar,
    // the taskbar and Alt+Tab. Set on the application, so every dialog gets it too.
    QIcon icon;
    for (const int size : {16, 24, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/icons/djinn-%1.png").arg(size));
    icon.addFile(QStringLiteral(":/icons/djinn.png"));   // the 32 px master
    QApplication::setWindowIcon(icon);
    Theme::apply(app);               // before any window, the start-up warning included
    setLanguage(systemLanguage());   // before any text is shown; the window has a selector

    // thresholds.json sits next to the executable (portable app)
    Thresholds thresholds;
    QString error;
    const QString path = QApplication::applicationDirPath() + QStringLiteral("/thresholds.json");
    if (!thresholds.loadFromFile(path, &error)) {
        QMessageBox::warning(nullptr, QObject::tr("Configuration"),
                             QObject::tr("thresholds.json not loaded:\n%1\n\n"
                                         "Tests needing thresholds will report ERROR. "
                                         "Copy the whole program folder again: thresholds.json "
                                         "must be next to the program.").arg(error));
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
    // add() returns the test's address: it tells the window which result
    // goes to which row of the delivery sheet.
    TestRunner runner;                       // declared before the window: outlives it
    const auto add = [&runner](std::unique_ptr<ITest> test) {
        const ITest* t = test.get();
        runner.add(std::move(test));
        return t;
    };
    DeliveryLinks delivery;
    delivery.systemInfo = add(std::make_unique<SystemInfoTest>());   // first: identifies the PC
    delivery.rows[add(std::make_unique<BatteryTest>(thresholds))] = SheetRow::Battery;
    delivery.rows[add(std::make_unique<DiskTest>(thresholds))] = SheetRow::Disk;
    add(std::make_unique<DriversTest>());
    add(std::make_unique<LicenseTest>());
    delivery.rows[add(std::make_unique<KeyboardTest>(keyLayout))] = SheetRow::Keyboard;   // interactive: after the automatic ones, in this order
    delivery.rows[add(std::make_unique<AudioTest>())] = SheetRow::Audio;
    delivery.rows[add(std::make_unique<CameraTest>())] = SheetRow::Camera;
    add(std::make_unique<CpuTempTest>(thresholds));   // last: ~30 s of full load would make the dialogs lag

    MainWindow window(runner, delivery);
    window.show();
    return app.exec();
}
