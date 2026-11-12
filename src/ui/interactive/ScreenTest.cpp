#include "ui/interactive/ScreenTest.h"

#include <QApplication>
#include <QScreen>
#include <QWidget>

#include "ui/interactive/ScreenDialog.h"

void ScreenTest::run()
{
    // The screen the main window is on: the built-in one, even when an
    // external monitor is the primary screen.
    const QWidget* window = QApplication::activeWindow();
    QScreen* screen = window ? window->screen() : QGuiApplication::primaryScreen();

    // No parent: the dialog deletes itself on close (WA_DeleteOnClose).
    auto* dialog = new ScreenDialog(screen);
    dialog->setWindowModality(Qt::ApplicationModal);
    connect(dialog, &ScreenDialog::outcome, this, &ITest::finished);
    connect(dialog, &ScreenDialog::stopAll, this, &ITest::stopAllRequested);
    dialog->show();
}
