#pragma once
#include <memory>
#include <vector>
#include <QObject>
#include <QTimer>
#include "core/ITest.h"

// Runs the tests one after another. Automatic tests are always placed
// before interactive ones, so the technician can walk away at the start.
class TestRunner : public QObject {
    Q_OBJECT
public:
    explicit TestRunner(QObject* parent = nullptr);

    void add(std::unique_ptr<ITest> test);   // the runner owns the test
    int count() const { return static_cast<int>(m_tests.size()); }
    const ITest& test(int index) const { return *m_tests[static_cast<size_t>(index)]; }

    void startAll();
    bool isRunning() const { return m_running; }

signals:
    void testStarted(int index);
    void testFinished(int index, const TestResult& result);
    void allFinished();

private:
    void runNext();
    void onTestFinished(ITest* sender, const TestResult& result);
    void onTimeout();
    void finishCurrent(const TestResult& result);

    std::vector<std::unique_ptr<ITest>> m_tests;
    int m_current = -1;
    bool m_running = false;
    QTimer m_watchdog;   // an automatic test that never answers becomes Error
};
