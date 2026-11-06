#pragma once
#include <QColor>
#include <QFont>
#include <QIcon>
#include "core/TestResult.h"

class QApplication;

// The app's one look, the "Djinn" theme: the colours of the logo (night blue,
// ice blue text, a gold START button). It replaced the green "old terminal"
// theme on 2026-11-06 (a colleague did not like it). It is fixed (Fusion style
// + our own palette), so it looks the same on every Windows and Linux PC, and
// a light or dark desktop theme cannot break it. Every colour of the UI comes
// from here.
namespace Theme {

// Style, palette, UI font and the few rules a palette cannot express
// (button borders, table headers). Call once, before any window is created.
void apply(QApplication& app);

QColor background();   // windows
QColor surface();      // tables, text boxes, keyboard keys
QColor text();         // ice blue: normal text
QColor dimText();      // secondary text, optional keys, disabled

// For raw tool output (details pane): columns line up only in a monospace font.
QFont monoFont();

QColor statusColor(Status s);
QColor runningColor();
QColor waitingColor();

// Small icon drawn in code (no image files). Its shape differs per status
// (check, !, cross, ?, dash) so it can be read without seeing colours.
QIcon statusIcon(Status s);
QIcon runningIcon();

} // namespace Theme
