#pragma once
#include <vector>
#include <QCameraDevice>
#include <QList>
#include "core/ITest.h"
#include "tests/camera/FrameCheck.h"

// Interactive test, but it asks the technician only when the picture is
// ambiguous. Every camera is tested, one dialog after the other (tablets:
// front and rear), except infrared ones. Logic in tests/camera/FrameCheck (oxcore), dialog in CameraDialog.
class CameraTest : public ITest {
    Q_OBJECT
public:
    using ITest::ITest;

    QString name() const override { return tr("Camera"); }
    QString id() const override { return QStringLiteral("camera"); }
    bool needsUser() const override { return true; }
    void run() override;
    void cancel() override;   // STOP: no next camera

private:
    void testNext();
    TestResult withLeftOut(TestResult r) const;   // names the IR cameras in the details

    QList<QCameraDevice> m_cameras;   // still to test
    std::vector<CameraRun> m_runs;    // done
    QStringList m_leftOut;            // infrared cameras, not tested
    int m_total = 0;
};
