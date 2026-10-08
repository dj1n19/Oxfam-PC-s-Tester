#pragma once
#include <QDialog>
#include "tests/keyboard/KeyLayout.h"

class QLabel;

// Draws the layout and lights up every key pressed. The logic lives in
// KeyTracker (oxcore); this class only turns key events into codes.
class KeyboardDialog : public QDialog {
    Q_OBJECT
public:
    explicit KeyboardDialog(const KeyLayout& layout, QWidget* parent = nullptr);

signals:
    void outcome(const TestResult& result);   // emitted exactly once

protected:
    bool event(QEvent* e) override;           // catches Tab, Esc... before Qt uses them
    void closeEvent(QCloseEvent* e) override;

private:
    void finish(KeyTracker::Outcome o);
    void updateStatus();

    KeyLayout m_layout;
    KeyTracker m_tracker;
    QLabel* m_status = nullptr;
    QWidget* m_view = nullptr;   // the drawn keyboard
    bool m_done = false;
};
