#include "ui/interactive/CameraTest.h"

#include <QCameraDevice>
#include <QMediaDevices>

#include "ui/interactive/CameraDialog.h"

void CameraTest::run()
{
    m_runs.clear();
    // The default camera first (the front one on tablets), then the others.
    const QCameraDevice first = QMediaDevices::defaultVideoInput();
    if (first.isNull()) {
        emit finished(cameraResult(CameraOutcome::NoCamera, {}, {}));
        return;
    }
    m_cameras = {first};
    for (const QCameraDevice& d : QMediaDevices::videoInputs())
        if (d.id() != first.id())
            m_cameras << d;
    m_total = int(m_cameras.size());
    testNext();
}

void CameraTest::testNext()
{
    if (m_cameras.isEmpty()) {
        emit finished(combineCameraResults(m_runs));
        return;
    }
    const QCameraDevice device = m_cameras.takeFirst();
    const int number = m_total - int(m_cameras.size());   // 1-based

    // No parent: the dialog deletes itself on close (WA_DeleteOnClose).
    auto* dialog = new CameraDialog(device, number, m_total);
    dialog->setWindowModality(Qt::ApplicationModal);
    connect(dialog, &CameraDialog::outcome, this, [this, device](const TestResult& r) {
        m_runs.push_back({device.description(), r});
        // Queued: the dialog that just answered closes first (and releases its
        // camera) before the next one opens.
        QMetaObject::invokeMethod(this, &CameraTest::testNext, Qt::QueuedConnection);
    });
    dialog->show();
}
