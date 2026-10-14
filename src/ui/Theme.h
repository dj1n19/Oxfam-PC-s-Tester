#pragma once
#include <QColor>
#include <QIcon>
#include "core/TestResult.h"

class QApplication;

// The app's one look: a dark "old terminal" theme, phosphor green on black.
// It is fixed (Fusion style + our own palette), so it looks the same on every
// Windows and Linux PC, and a light or dark desktop theme cannot break it.
// Every colour of the UI comes from here.
namespace Theme {

// Style, palette, monospace font and the few rules a palette cannot express
// (button borders, table headers). Call once, before any window is created.
void apply(QApplication& app);

QColor background();   // windows
QColor surface();      // tables, text boxes, keyboard keys
QColor text();         // phosphor green: normal text, borders, accent
QColor dimText();      // secondary text, optional keys, disabled

QColor statusColor(Status s);
QColor runningColor();
QColor waitingColor();

// Small icon drawn in code (no image files). Its shape differs per status
// (check, !, cross, ?, dash) so it can be read without seeing colours.
QIcon statusIcon(Status s);
QIcon runningIcon();

} // namespace Theme
