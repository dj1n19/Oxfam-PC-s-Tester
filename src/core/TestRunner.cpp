#include "core/TestRunner.h"

#include <algorithm>
#include <QTimer>

namespace {
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

void TestRunner::start(std::vector<int> indexes)
{
    if (m_running)
        return;
    std::sort(indexes.begin(), indexes.end());
    indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
    std::erase_if(indexes, [this](int i) { return i < 0 || i >= count(); });
    if (indexes.empty())
        return;
    m_running = true;
    ++m_run;
    m_queue = std::move(indexes);
    m_next = 0;
    runNext();
}

void TestRunner::startAll()
{
    std::vector<int> all(static_cast<size_t>(count()));
    for (int i = 0; i < count(); ++i)
        all[static_cast<size_t>(i)] = i;
    start(std::move(all));
}

void TestRunner::startOne(int index)
{
    start({index});
}

void TestRunner::runNext()
{
    if (m_next >= m_queue.size()) {
        m_running = false;
        m_current = -1;
        emit allFinished();
        return;
    }

    m_current = m_queue[m_next++];
    const int index = m_current;
    ITest& t = *m_tests[static_cast<size_t>(index)];
    m_waiting[static_cast<size_t>(index)] = true;
    emit testStarted(index);
    if (!t.needsUser()) {
        // One watchdog per automatic test. A timer from an older run, or for
        // a test that already answered, does nothing.
        const int timeoutMs = t.timeoutMs();
        QTimer::singleShot(timeoutMs, this, [this, index, timeoutMs, run = m_run] {
            if (run != m_run || !m_waiting[static_cast<size_t>(index)])
                return;
            TestResult r;
            r.status = Status::Error;
            r.summary = tr("Test did not answer within %1 s: press START again").arg(timeoutMs / 1000);
            r.details = QStringLiteral("Timeout after %1 s").arg(timeoutMs / 1000);
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
