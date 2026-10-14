#include "core/TestRunner.h"

#include <algorithm>
#include <QTimer>

namespace {
constexpr int kAutoTestTimeoutMs = 60'000;

// Placement order: automatic, then interactive, then "runs last".
int group(const ITest& t)
{
    if (t.runsLast())
        return 2;
    return t.needsUser() ? 1 : 0;
}
}

TestRunner::TestRunner(QObject* parent) : QObject(parent)
{
}

void TestRunner::add(std::unique_ptr<ITest> test)
{
    ITest* raw = test.get();
    connect(raw, &ITest::finished, this,
            [this, raw](const TestResult& r) { onTestFinished(raw, r); });

    // Insert before the first test of a later group: the order of add()
    // is kept inside each group.
    const auto pos = std::find_if(
        m_tests.begin(), m_tests.end(),
        [&](const std::unique_ptr<ITest>& t) { return group(*t) > group(*raw); });
    m_tests.insert(pos, std::move(test));
    m_waiting.push_back(false);
}

void TestRunner::startAll()
{
    if (m_running)
        return;
    m_running = true;
    ++m_run;
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

    const int index = m_current;
    ITest& t = *m_tests[static_cast<size_t>(index)];
    m_waiting[static_cast<size_t>(index)] = true;
    emit testStarted(index);
    if (!t.needsUser()) {
        // One watchdog per automatic test. A timer from an older run, or for
        // a test that already answered, does nothing.
        QTimer::singleShot(kAutoTestTimeoutMs, this, [this, index, run = m_run] {
            if (run != m_run || !m_waiting[static_cast<size_t>(index)])
                return;
            TestResult r;
            r.status = Status::Error;
            r.summary = tr("Test did not answer within %1 s: press START again").arg(kAutoTestTimeoutMs / 1000);
            r.details = QStringLiteral("Timeout after %1 s").arg(kAutoTestTimeoutMs / 1000);
            finish(index, r);
        });
    }
    t.run();   // may emit finished() synchronously or later
}

void TestRunner::onTestFinished(ITest* sender, const TestResult& result)
{
    const auto it = std::find_if(m_tests.begin(), m_tests.end(),
                                 [sender](const std::unique_ptr<ITest>& t) { return t.get() == sender; });
    const auto index = static_cast<int>(it - m_tests.begin());
    // Ignore late or duplicate answers (e.g. from a test already timed out).
    if (!m_running || it == m_tests.end() || !m_waiting[static_cast<size_t>(index)])
        return;
    finish(index, result);
}

void TestRunner::finish(int index, const TestResult& result)
{
    m_waiting[static_cast<size_t>(index)] = false;
    emit testFinished(index, result);
    if (index == m_current)
        runNext();
}
