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
    virtual bool needsUser() const { return false; }   // interactive test?
    // Automatic test placed after the interactive ones (CPU load): it would
    // make the keyboard and audio dialogs lag if it ran during them.
    virtual bool runsLast() const { return false; }
    // Automatic tests only: the runner gives up (Error) after this long.
    virtual int timeoutMs() const { return 60'000; }
    virtual void run() = 0;

signals:
    void finished(const TestResult& result);
};
