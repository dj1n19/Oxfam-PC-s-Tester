#include "ui/interactive/CameraTest.h"

#include <QCameraDevice>
#include <QMediaDevices>

#include "ui/interactive/CameraDialog.h"

void CameraTest::run()
{
    const QCameraDevice device = QMediaDevices::defaultVideoInput();
    if (device.isNull()) {
        emit finished(cameraResult(CameraOutcome::NoCamera, {}, {}));
        return;
    }

    // No parent: the dialog deletes itself on close (WA_DeleteOnClose).
    auto* dialog = new CameraDialog(device);
    dialog->setWindowModality(Qt::ApplicationModal);
    connect(dialog, &CameraDialog::outcome, this, &ITest::finished);
    dialog->show();
}
