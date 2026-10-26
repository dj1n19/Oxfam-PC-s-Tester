#pragma once
#include <QDir>
#include <QString>

// The root folder, as shared with the technicians:
//   Lazy Djin's Tester/
//     app/               the program (Windows files and the Linux AppImage)
//     config/            thresholds.json, keyboard_layout.json
//     logs/              a run report per run
//     delivery-sheets/   the delivery sheets to share (FR, NL)
// Root = the folder above the one holding the program: the .exe on Windows,
// the .AppImage FILE on Linux (inside, the program runs from a read-only
// mount). From build/ it is the repository, which has config/ too.
QDir folderAroundProgram();

// <root>/<name> ("logs", "delivery-sheets"), created if missing. Empty
// (and error) if it cannot be created (read-only stick...).
QString rootSubfolder(const QString& name, QString* error);

// <root>/config/<file>, to read. Not checked: the reader reports a missing file.
QString configFile(const QString& file);

// Makes a file hidden in the file manager. The name must already start with
// a dot (hidden on Linux); Windows also needs the "hidden" attribute
// (hidefile_win.cpp / hidefile_linux.cpp). False if Windows refused.
bool hideFile(const QString& path);
