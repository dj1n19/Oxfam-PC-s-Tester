#pragma once
#include <QDir>

// Where the program "lives" for the technician: the folder holding the program
// folder (Windows: Documents in production, the USB stick when testing), or the
// folder holding the .AppImage file (Linux). Files made for people (delivery
// sheet copy, run reports) go there, never inside the program folder.
QDir folderAroundProgram();
