#include "tests/common/PowerShell.h"

#include <QProcess>

PowerShellResult runPowerShell(const QString& script, int timeoutMs)
{
    PowerShellResult r;

    // Stop: any cmdlet error ends the script with exit code 1.
    // SilentlyContinue for progress: otherwise progress bars come out on
    // stderr as CLIXML noise when there is no console.
    // UTF-8 output: redirected output otherwise uses the old console code
    // page (CP850 on French Windows) and accented device names break the JSON.
    const QString full = QStringLiteral("$ErrorActionPreference = 'Stop'; "
                                        "$ProgressPreference = 'SilentlyContinue'; "
                                        "[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; ") + script;

    // -EncodedCommand takes base64 of UTF-16LE text. This avoids every
    // quoting problem of passing a script as one command-line argument.
    // QString is UTF-16 in memory, and Windows is little-endian.
    const QByteArray utf16(reinterpret_cast<const char*>(full.utf16()),
                           full.size() * qsizetype(sizeof(char16_t)));

    QProcess ps;
    ps.start(QStringLiteral("powershell.exe"),
             {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
              "-EncodedCommand", QString::fromLatin1(utf16.toBase64())});
    if (!ps.waitForStarted(5000)) {
        r.error = QStringLiteral("Could not start PowerShell");
        return r;
    }
    if (!ps.waitForFinished(timeoutMs)) {
        ps.kill();
        ps.waitForFinished();
        r.error = QStringLiteral("PowerShell timed out after %1 s").arg(timeoutMs / 1000);
        return r;
    }
    if (ps.exitStatus() != QProcess::NormalExit || ps.exitCode() != 0) {
        r.error = QStringLiteral("PowerShell failed (exit code %1): %2")
                      .arg(ps.exitCode())
                      .arg(QString::fromLocal8Bit(ps.readAllStandardError()).trimmed());
        return r;
    }
    r.out = ps.readAllStandardOutput().trimmed();
    return r;
}
