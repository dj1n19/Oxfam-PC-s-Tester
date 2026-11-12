#pragma once
#include <QDialog>
#include <QList>
#include <QPointer>
#include <memory>
#include "core/TestResult.h"

class QLabel;
class QPushButton;
class QScreen;
class QWidget;
class PatternWindow;
class TouchWindow;

// Explains the test, shows the full-screen colours (PatternWindow), then asks
// what was seen. The answers only unlock once every colour was shown.
// On a PC with a touch screen a second page follows: the touch grid
// (TouchWindow), in the same "Screen" row (developer's choice 2026-11-12).
// While a full-screen window is shown the dialog is hidden (see showColours()).
class ScreenDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScreenDialog(QScreen* screen, QWidget* parent = nullptr);
    ~ScreenDialog() override;   // in the .cpp: the windows are complete only there

signals:
    void outcome(const TestResult& result);   // emitted exactly once
    void stopAll();   // "Stop all tests": emitted just before the dialog closes

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void showColours();
    void coloursClosed(bool seenAll);
    void screenAnswered(const TestResult& screen);   // colours judged: touch page, or the end
    void showTouch();
    void touchClosed(bool complete);
    TestResult touchAnswer(int outcome) const;       // TouchOutcome, combined with the screen part
    void finish(TestResult result);

    QPointer<QScreen> m_screen;          // a screen can be unplugged meanwhile
    std::unique_ptr<PatternWindow> m_patterns;   // own top-level windows, no Qt parent
    std::unique_ptr<TouchWindow> m_touch;
    QString m_touchDevice;               // touch screen name(s); empty = no touch part
    TestResult m_screenResult;           // the colours' answer, kept during the touch part

    QWidget* m_colourPage = nullptr;
    QLabel* m_prompt = nullptr;
    QPushButton* m_show = nullptr;
    QList<QPushButton*> m_answers;

    QWidget* m_touchPage = nullptr;
    QLabel* m_touchPrompt = nullptr;
    QPushButton* m_touchStart = nullptr;
    QPushButton* m_touchBroken = nullptr;
    bool m_onTouchPage = false;
    bool m_done = false;
};
