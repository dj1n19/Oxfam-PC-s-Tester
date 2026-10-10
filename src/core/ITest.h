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
    // Long automatic test that needs nobody (CPU load): the runner starts it
    // and goes on with the next tests (the interactive ones) without waiting.
    virtual bool runsInBackground() const { return false; }
    virtual void run() = 0;

signals:
    void finished(const TestResult& result);
};
