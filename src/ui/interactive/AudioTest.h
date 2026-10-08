#pragma once
#include "core/ITest.h"

// Interactive test: tone on the left, then on the right speaker.
// Logic in tests/audio/AudioCheck (oxcore), dialog in AudioDialog.
class AudioTest : public ITest {
    Q_OBJECT
public:
    using ITest::ITest;

    QString name() const override { return QStringLiteral("Audio"); }
    bool needsUser() const override { return true; }
    void run() override;
};
