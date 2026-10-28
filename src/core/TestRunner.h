#pragma once
#include <memory>
#include <vector>
#include <QObject>
#include "core/ITest.h"

// Runs the tests one after another. Automatic tests are placed before
// interactive ones, so the technician can walk away at the start; tests with
// runsLast() (CPU load) come after the interactive ones.
class TestRunner : public QObject {
    Q_OBJECT
public:
    explicit TestRunner(QObject* parent = nullptr);

    void add(std::unique_ptr<ITest> test);   // the runner owns the test
    int count() const { return static_cast<int>(m_tests.size()); }
    const ITest& test(int index) const { return *m_tests[static_cast<size_t>(index)]; }

    // Runs these tests, in the runner's order (the order given does not
    // matter; unknown indexes are ignored). Same signals for every start.
    void start(std::vector<int> indexes);
    void startAll();
    void startOne(int index);   // that test alone
    bool isRunning() const { return m_running; }

signals:
    void testStarted(int index);
    void testFinished(int index, const TestResult& result);
    void allFinished();   // every test answered

private:
    void runNext();
    void onTestFinished(ITest* sender, const TestResult& result);
    void finish(int index, const TestResult& result);

    std::vector<std::unique_ptr<ITest>> m_tests;
    std::vector<bool> m_waiting;   // per test: started, no answer yet
    std::vector<int> m_queue;      // the tests of this start, in order
    size_t m_next = 0;             // position in m_queue of the next test
    int m_current = -1;            // the test running now
    int m_run = 0;                 // +1 per START: old watchdogs are ignored
    bool m_running = false;
};
