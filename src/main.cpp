#include <memory>
#include <QApplication>
#include <QAudioDevice>
#include <QFile>
#include <QIcon>
#include <QMediaDevices>
#include <QMessageBox>
#include <QTimer>

#include "core/ProgramFolder.h"
#include "core/TestRunner.h"
#include "core/Thresholds.h"
#include "tests/battery/BatteryTest.h"
#include "tests/cputemp/CpuTempTest.h"
#include "tests/disk/DiskTest.h"
#include "tests/drivers/DriversTest.h"
#include "tests/license/LicenseTest.h"
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
    QApplication::setApplicationVersion(QStringLiteral(OX_APP_VERSION));   // set by CMake (APP_VERSION)
    // The djinn, in every size we have: Qt picks the closest one for the title bar,
    // the taskbar and Alt+Tab. Set on the application, so every dialog gets it too.
    QIcon icon;
    for (const int size : {16, 24, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/icons/djinn-%1.png").arg(size));
    icon.addFile(QStringLiteral(":/icons/djinn.png"));   // the 32 px master
    QApplication::setWindowIcon(icon);
    Theme::apply(app);               // before any window, the start-up warning included
    setLanguage(systemLanguage());   // before any text is shown; the window has a selector

    // Config files live in <root>/config/, shared by Windows and Linux (see ProgramFolder.h).
    Thresholds thresholds;
    QString error;
    if (!thresholds.loadFromFile(configFile(QStringLiteral("thresholds.json")), &error)) {
        QMessageBox::warning(nullptr, QObject::tr("Configuration"),
                             QObject::tr("thresholds.json not loaded:\n%1\n\n"
                                         "Tests needing thresholds will report ERROR. "
                                         "Copy the whole \"Lazy Djin's Tester\" folder again: "
                                         "thresholds.json must be in its config folder.").arg(error));
    }

    // If keyboard_layout.json is missing, the keyboard test reports ERROR when
    // it runs (no pop-up at start).
    QFile layoutFile(configFile(QStringLiteral("keyboard_layout.json")));
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

    // Start Qt Multimedia now, while the technician gets ready. Its first use
    // loads the FFmpeg backend: ~5 s from a USB stick on the shop's laptops
    // (Windows, 2026-10-15), and the UI froze between the keyboard and audio
    // tests. It must start on the UI thread; the short delay lets the window
    // paint first, so the program does not look dead at launch.
    QTimer::singleShot(300, [] { QMediaDevices::audioOutputs(); });
    return app.exec();
}
