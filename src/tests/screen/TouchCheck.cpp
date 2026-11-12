#include "tests/screen/TouchCheck.h"

#include <algorithm>
#include <cmath>
#include <QCoreApplication>
#include <QStringList>

TouchGrid::TouchGrid(int cols, int rows)
    : m_cols(std::max(1, cols)), m_rows(std::max(1, rows)),
      m_touched(static_cast<size_t>(m_cols * m_rows), false)
{
}

TouchGrid TouchGrid::forScreen(double widthMm, double heightMm)
{
    if (widthMm <= 0 || heightMm <= 0)
        return TouchGrid(12, 8);
    // 4 to 24 squares per side: a wrong physical size (a TV, a bad EDID)
    // still gives a usable grid.
    const auto count = [](double mm) { return std::clamp(int(std::lround(mm / kTouchCellMm)), 4, 24); };
    return TouchGrid(count(widthMm), count(heightMm));
}

bool TouchGrid::touch(double x, double y)
{
    if (!(x >= 0 && x <= 1 && y >= 0 && y <= 1))   // also rejects NaN
        return false;
    // x == 1 (right edge) belongs to the last square, not to one after it.
    const int col = std::min(int(x * m_cols), m_cols - 1);
    const int row = std::min(int(y * m_rows), m_rows - 1);
    const auto index = static_cast<size_t>(row * m_cols + col);
    if (m_touched[index])
        return false;
    m_touched[index] = true;
    return true;
}

bool TouchGrid::isTouched(int col, int row) const
{
    if (col < 0 || col >= m_cols || row < 0 || row >= m_rows)
        return false;
    return m_touched[static_cast<size_t>(row * m_cols + col)];
}

int TouchGrid::touchedCount() const
{
    return int(std::count(m_touched.begin(), m_touched.end(), true));
}

QString TouchGrid::map() const
{
    QStringList lines;
    for (int r = 0; r < m_rows; ++r) {
        QString line;
        for (int c = 0; c < m_cols; ++c)
            line += isTouched(c, r) ? QLatin1Char('#') : QLatin1Char('.');
        lines << line;
    }
    return lines.join(QLatin1Char('\n'));
}

TestResult touchResult(TouchOutcome outcome, const TouchRun& run)
{
    TestResult r;
    const int touched = run.grid.touchedCount();
    const int total = run.grid.cellCount();
    switch (outcome) {
    case TouchOutcome::AllTouched:
        r.status = Status::Pass;
        r.summary = QCoreApplication::translate("ScreenTest", "touch OK");
        break;
    case TouchOutcome::ZoneDead:
        r.status = Status::Fail;
        r.summary = QCoreApplication::translate("ScreenTest", "touch: a zone does not respond (%1 of %2 squares)")
                        .arg(touched).arg(total);
        break;
    case TouchOutcome::Skipped:
        r.status = Status::Skipped;
        r.summary = QCoreApplication::translate("ScreenTest", "touch skipped");
        break;
    }
    // Ghost touches make a tablet unusable, whatever the grid gave. The
    // technician could retry ("Again") if a hand touched it by mistake.
    if (run.ghostPoints > 0 && outcome != TouchOutcome::Skipped) {
        r.status = Status::Fail;
        r.summary = QCoreApplication::translate("ScreenTest", "touch: ghost touches (the screen reacted while nobody touched it)");
    }
    r.details = QStringLiteral("Touch screen: %1\nGrid %2 x %3 squares, %4 touched (# = touched, . = never)\n%5\n"
                               "Ghost touches (%6 s without touching): %7\nMost fingers at once: %8")
                    .arg(run.device.isEmpty() ? QStringLiteral("(name unknown)") : run.device)
                    .arg(run.grid.cols()).arg(run.grid.rows()).arg(touched)
                    .arg(run.grid.map())
                    .arg(kGhostWaitMs / 1000).arg(run.ghostPoints).arg(run.maxPoints);
    return r;
}

TestResult screenWithTouch(const TestResult& screen, const TestResult& touch)
{
    TestResult r;
    r.status = worse(screen.status, touch.status);
    r.summary = screen.summary + QStringLiteral(" | ") + touch.summary;
    r.details = screen.details + QStringLiteral("\n\n") + touch.details;
    r.note = screen.note;
    return r;
}
