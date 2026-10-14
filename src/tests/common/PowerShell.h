#pragma once
#include <QByteArray>
#include <QString>

class QProcess;

// Windows only: included by *_win.cpp files, implemented in powershell_win.cpp.

struct PowerShellResult {
    QString error;    // non-empty = did not start, timed out, or exit code != 0
    QByteArray out;   // standard output, trimmed
};

// Runs a PowerShell script and waits for it (blocking: call it from a worker
// thread). The script is sent with -EncodedCommand, so it may contain quotes.
// Errors are made fatal: a failing cmdlet gives an error, not partial output.
PowerShellResult runPowerShell(const QString& script, int timeoutMs = 30000);

// Starts a script with the same settings, without waiting: for a long script
// that prints one line at a time (the CPU test's readings). The caller reads
// the lines, waits and checks the exit code. False = PowerShell did not start.
bool startPowerShell(QProcess& ps, const QString& script);
