#include "ui/interactive/CameraTest.h"

#include <QCameraDevice>
#include <QMediaDevices>

#include "ui/interactive/CameraDialog.h"

void CameraTest::run()
{
    m_runs.clear();
    m_cameras.clear();
    m_leftOut.clear();
    // The default camera first (the front one on tablets), then the others.
    QList<QCameraDevice> all;
    const QCameraDevice first = QMediaDevices::defaultVideoInput();
    if (!first.isNull())
        all << first;
    for (const QCameraDevice& d : QMediaDevices::videoInputs())
        if (d.id() != first.id())
            all << d;
    for (const QCameraDevice& d : all) {
        if (isInfraredCamera(d.description()))
            m_leftOut << d.description();
        else
            m_cameras << d;
    }
    if (m_cameras.isEmpty()) {
        emit finished(withLeftOut(cameraResult(CameraOutcome::NoCamera, {}, {})));
        return;
    }
    m_total = int(m_cameras.size());
    testNext();
}

TestResult CameraTest::withLeftOut(TestResult r) const
{
    for (const QString& name : m_leftOut)
        r.details += QStringLiteral("\n\nNot tested (infrared, Windows Hello): %1").arg(name);
    return r;
}

void CameraTest::testNext()
{
    if (m_cameras.isEmpty()) {
        emit finished(withLeftOut(combineCameraResults(m_runs)));
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
