#pragma once
#include "core/ITest.h"

// Interactive test, but it asks the technician only when the picture is
// ambiguous. Logic in tests/camera/FrameCheck (oxcore), dialog in CameraDialog.
class CameraTest : public ITest {
    Q_OBJECT
public:
    using ITest::ITest;

    QString name() const override { return QStringLiteral("Camera"); }
    bool needsUser() const override { return true; }
    void run() override;
};
