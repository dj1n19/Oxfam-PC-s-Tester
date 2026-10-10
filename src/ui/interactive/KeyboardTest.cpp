#include "ui/interactive/KeyboardTest.h"

#include "ui/interactive/KeyboardDialog.h"

KeyboardTest::KeyboardTest(KeyLayout layout, QObject* parent)
    : ITest(parent), m_layout(std::move(layout))
{
}

void KeyboardTest::run()
{
    if (!m_layout.error.isEmpty()) {
        TestResult r;
        r.status = Status::Error;
        r.summary = tr("keyboard_layout.json missing or broken: copy the whole program folder again");
        r.details = m_layout.error;
        emit finished(r);
        return;
    }

    // No parent: the dialog deletes itself on close (WA_DeleteOnClose).
    // Application-modal so the main window cannot be used meanwhile.
    auto* dialog = new KeyboardDialog(m_layout);
    dialog->setWindowModality(Qt::ApplicationModal);
    connect(dialog, &KeyboardDialog::outcome, this, &ITest::finished);
    dialog->show();
    dialog->activateWindow();
    dialog->setFocus();
}
