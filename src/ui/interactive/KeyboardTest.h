#pragma once
#include "core/ITest.h"
#include "tests/keyboard/KeyLayout.h"

// Interactive test: lives in the UI layer because it shows a dialog.
// Its logic (layout, scan codes, result) is KeyTracker, in oxcore.
class KeyboardTest : public ITest {
    Q_OBJECT
public:
    explicit KeyboardTest(KeyLayout layout, QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("Keyboard"); }
    bool needsUser() const override { return true; }
    void run() override;

private:
    KeyLayout m_layout;
};
