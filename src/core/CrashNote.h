#pragma once
#include <cstddef>
#include <QString>

// Crash note (developer's request 2026-11-13: on a Lenovo L13 the program
// vanished during the CPU test, without any message or run report).
// When the program crashes, it writes "<date> CRASH.txt" in logs/: version,
// test running, what went wrong (Windows exception code / Linux signal, the
// module and offset) and the call stack. Windows still records the crash in
// its own Event Viewer afterwards: the handler only writes, then lets go.
//
// A crash handler runs in a broken program (memory may be corrupted), so it
// does as little as possible: no Qt, no allocation, fixed buffers, plain
// system calls. Everything it needs is prepared here beforehand.

// Installs the handlers (crashnote_win.cpp / crashnote_linux.cpp). logsFolder
// is created only if a crash happens. Call once, after QApplication exists.
void installCrashNote(const QString& logsFolder);

// What the program is doing now ("cpu_load (CPU under load)"); empty = no test.
// Called on the UI thread when a test starts and when the run ends.
void setCrashContext(const QString& text);

// --- For crashnote_win.cpp / crashnote_linux.cpp only (CrashNote.cpp) ---

// Qt fatal messages and uncaught C++ exceptions (both end in abort()):
// their text is kept for the note.
void installCrashNoteCommon();

// Appends printf-style text to buffer (size bytes, used so far); never
// overflows, always 0-terminated.
void crashAppend(char* buffer, std::size_t size, std::size_t& used, const char* format, ...);

// First lines of every note: version, test running, last fatal message.
void crashAppendHeader(char* buffer, std::size_t size, std::size_t& used, const char* newline);
