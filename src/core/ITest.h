#pragma once
#include <QObject>
#include "core/TestResult.h"

// Contract of every diagnostic test (Strategy pattern).
// run() may finish later: it MUST emit finished() exactly once.
class ITest : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~ITest() override = default;

    virtual QString name() const = 0;
    // Stable key, never translated: names the test in config/tests.json.
    virtual QString id() const = 0;
    virtual bool needsUser() const { return false; }   // interactive test?
    // Automatic test placed after the interactive ones (CPU load): it would
    // make the keyboard and audio dialogs lag if it ran during them.
    virtual bool runsLast() const { return false; }
    // Automatic tests only: the runner gives up (Error) after this long.
    virtual int timeoutMs() const { return 60'000; }
    virtual void run() = 0;
    // The technician pressed STOP during this test: stop what costs (CPU
    // load, next camera). finished() may still come; the runner ignores it.
    virtual void cancel() {}

signals:
    void finished(const TestResult& result);
    // "Stop all tests" in an interactive test's dialog (the main window's
    // STOP cannot be reached behind a modal dialog).
    void stopAllRequested();
};
