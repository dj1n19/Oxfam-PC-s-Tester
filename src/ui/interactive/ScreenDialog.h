#pragma once
#include <QDialog>
#include <QList>
#include <QPointer>
#include "tests/screen/ScreenCheck.h"

class QLabel;
class QPushButton;
class QScreen;
class PatternWindow;

// Explains the test, shows the full-screen colours (PatternWindow), then asks
// what was seen. The answers only unlock once every colour was shown.
class ScreenDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScreenDialog(QScreen* screen, QWidget* parent = nullptr);

signals:
    void outcome(const TestResult& result);   // emitted exactly once

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void showColours();
    void coloursClosed(bool seenAll);
    void finish(TestResult result);

    QPointer<QScreen> m_screen;          // a screen can be unplugged meanwhile
    PatternWindow* m_patterns = nullptr; // child window, owned by this dialog
    QLabel* m_prompt = nullptr;
    QPushButton* m_show = nullptr;
    QList<QPushButton*> m_answers;
    bool m_done = false;
};
