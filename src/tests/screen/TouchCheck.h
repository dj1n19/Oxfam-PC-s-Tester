#pragma once
#include <vector>
#include <QString>
#include "core/TestResult.h"

// Logic of the touch screen check, part of the "Screen" row (the full-screen
// window is in src/ui/interactive/ScreenDialog.cpp). The screen is cut into
// squares; the technician swipes a finger until every square was touched.
// A square never reached = dead zone (damaged digitizer, cracked glass).

// About the size of a fingertip swipe: small enough to find a dead stripe,
// big enough to fill in ~20 s (Surface Pro 7: 10 x 7 squares).
constexpr double kTouchCellMm = 25.0;

// Without any new square for this long, the window closes and the dialog
// asks: a tablet without keyboard has no Esc, and a dead zone never fills.
constexpr int kTouchIdleMs = 10'000;

// Before the grid: nobody touches the screen for this long. Any touch seen
// then is a ghost touch (a known Surface defect: the screen "taps" by itself).
constexpr int kGhostWaitMs = 3'000;

class TouchGrid {
public:
    TouchGrid(int cols, int rows);
    // Squares of about kTouchCellMm for a screen of that physical size; an
    // unknown size (0, some Wayland setups) gives 12 x 8.
    static TouchGrid forScreen(double widthMm, double heightMm);

    int cols() const { return m_cols; }
    int rows() const { return m_rows; }
    // x and y from 0 to 1 across the screen. True if that square is new.
    // Only the touched point counts, never the line between two points: a
    // fast swipe across a dead stripe would otherwise "fill" it.
    bool touch(double x, double y);
    bool isTouched(int col, int row) const;
    int touchedCount() const;
    int cellCount() const { return m_cols * m_rows; }
    bool complete() const { return touchedCount() == cellCount(); }
    // One text line per row: '#' touched, '.' never touched (for the details).
    QString map() const;

private:
    int m_cols;
    int m_rows;
    std::vector<bool> m_touched;
};

// What happened in the touch window.
struct TouchRun {
    TouchGrid grid{1, 1};
    int ghostPoints = 0;   // fingers seen while nobody should touch
    int maxPoints = 0;     // most fingers seen at the same time
    QString device;        // touch screen name(s), for the details
};

// AllTouched: every square came (Pass unless ghost touches).
// ZoneDead: the technician says a zone does not respond (Fail).
// Skipped: the technician skipped the touch part.
enum class TouchOutcome { AllTouched, ZoneDead, Skipped };

TestResult touchResult(TouchOutcome outcome, const TouchRun& run);

// The "Screen" row of a touch PC: "screen | touch", the worse wins.
TestResult screenWithTouch(const TestResult& screen, const TestResult& touch);
