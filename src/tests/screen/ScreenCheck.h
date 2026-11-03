#pragma once
#include <vector>
#include <QString>
#include "core/TestResult.h"

// Logic of the screen test (the full-screen window is in src/ui/interactive/).
// Plain colours fill the whole screen one after the other: a dead pixel or a
// stain shows against a uniform colour, not over a wallpaper or windows.

// One full-screen colour, 0-255 per channel (no QColor: oxcore has no Qt Gui).
struct ScreenPattern {
    int red = 0;
    int green = 0;
    int blue = 0;
    QString name;   // English, for the details
};

// The colours in the order they are shown (why each one: ScreenCheck.cpp).
const std::vector<ScreenPattern>& screenPatterns();

// What the technician saw. A small defect does not stop the sale: it is a
// note for the buyer (developer's choice 2026-11-03).
enum class ScreenAnswer { Ok, SmallDefect, Broken };

TestResult screenResult(ScreenAnswer answer);
TestResult screenSkipped();
