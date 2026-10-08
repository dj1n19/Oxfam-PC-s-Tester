#pragma once
#include <QByteArray>
#include <QString>

// Windows only: included by *_win.cpp files, implemented in powershell_win.cpp.

struct PowerShellResult {
    QString error;    // non-empty = did not start, timed out, or exit code != 0
    QByteArray out;   // standard output, trimmed
};

// Runs a PowerShell script and waits for it (blocking: call it from a worker
// thread). The script is sent with -EncodedCommand, so it may contain quotes.
// Errors are made fatal: a failing cmdlet gives an error, not partial output.
PowerShellResult runPowerShell(const QString& script, int timeoutMs = 30000);
