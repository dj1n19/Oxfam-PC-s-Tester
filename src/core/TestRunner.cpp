#include "core/TestRunner.h"

#include <algorithm>

namespace {
constexpr int kAutoTestTimeoutMs = 60'000;
}

TestRunner::TestRunner(QObject* parent) : QObject(parent)
{
    m_watchdog.setSingleShot(true);
    connect(&m_watchdog, &QTimer::timeout, this, &TestRunner::onTimeout);
}

void TestRunner::add(std::unique_ptr<ITest> test)
{
    ITest* raw = test.get();
    connect(raw, &ITest::finished, this,
            [this, raw](const TestResult& r) { onTestFinished(raw, r); });

    if (test->needsUser()) {
        m_tests.push_back(std::move(test));
    } else {
        const auto firstInteractive = std::find_if(
            m_tests.begin(), m_tests.end(),
            [](const std::unique_ptr<ITest>& t) { return t->needsUser(); });
        m_tests.insert(firstInteractive, std::move(test));
    }
}

void TestRunner::startAll()
{
    if (m_running)
        return;
    m_running = true;
    m_current = -1;
    runNext();
}

void TestRunner::runNext()
{
    ++m_current;
    if (m_current >= count()) {
        m_running = false;
        emit allFinished();
        return;
    }

    ITest& t = *m_tests[static_cast<size_t>(m_current)];
    emit testStarted(m_current);
    if (!t.needsUser())
        m_watchdog.start(kAutoTestTimeoutMs);
    t.run();   // may emit finished() synchronously or later
}

void TestRunner::onTestFinished(ITest* sender, const TestResult& result)
{
    // Ignore late or duplicate answers (e.g. from a test already timed out).
    if (!m_running || sender != m_tests[static_cast<size_t>(m_current)].get())
        return;
    finishCurrent(result);
}

void TestRunner::onTimeout()
{
    if (!m_running)
        return;
    TestResult r;
    r.status = Status::Error;
    r.summary = QStringLiteral("Test did not answer in time");
    r.details = QStringLiteral("Timeout after %1 s").arg(kAutoTestTimeoutMs / 1000);
    finishCurrent(r);
}

void TestRunner::finishCurrent(const TestResult& result)
{
    m_watchdog.stop();
    emit testFinished(m_current, result);
    runNext();
}
