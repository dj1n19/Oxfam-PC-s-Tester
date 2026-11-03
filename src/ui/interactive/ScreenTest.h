#pragma once
#include "core/ITest.h"

// Interactive test: plain colours fill the screen, the technician says what
// they saw. Logic in tests/screen/ScreenCheck (oxcore), window in ScreenDialog.
class ScreenTest : public ITest {
    Q_OBJECT
public:
    using ITest::ITest;

    QString name() const override { return tr("Screen"); }
    QString id() const override { return QStringLiteral("screen"); }
    bool needsUser() const override { return true; }
    void run() override;
};
