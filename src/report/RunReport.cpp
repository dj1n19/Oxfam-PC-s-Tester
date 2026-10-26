#include "report/RunReport.h"

#include <QSaveFile>

#include "core/ProgramFolder.h"

QString formatRunReport(const RunReport& report)
{
    QString text = QStringLiteral("Lazy Djin's Tester %1 - %2\nPC: %3\nVerdict: %4\n")
                       .arg(report.version.isEmpty() ? QStringLiteral("(unknown version)") : report.version,
                            report.when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                            report.model.isEmpty() ? QStringLiteral("unknown") : report.model,
                            report.verdict);
    for (const RunReportRow& row : report.rows) {
        text += QStringLiteral("\n==== [%1] %2 ====\n%3\n\n%4\n")
                    .arg(statusLabel(row.result.status), row.test, row.result.summary,
                         row.result.details.trimmed());
    }
    // Raw tool output from Windows (smartctl.exe) ends its lines with \r\n.
    // The file is written in text mode, which turns every \n into \r\n:
    // without this, raw lines ended in \r\r\n (an empty line after each in
    // some editors, run reports of 2026-10-15).
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    return text;
}

QString runReportFileName(const QDateTime& when, const QString& model)
{
    // Windows refuses \ / : * ? " < > | and control characters in a file
    // name, and trailing dots or spaces (removed by simplified()).
    QString safe;
    for (const QChar c : model) {
        const bool refused = c.unicode() < 0x20 || QStringLiteral("\\/:*?\"<>|").contains(c);
        safe += refused ? QLatin1Char('_') : c;
    }
    safe = safe.simplified();
    while (safe.endsWith(QLatin1Char('.')))
        safe.chop(1);
    if (safe.isEmpty())
        safe = QStringLiteral("unknown PC");
    return when.toString(QStringLiteral("yyyy-MM-dd HH-mm-ss ")) + safe + QStringLiteral(".txt");
}

bool writeRunReport(const RunReport& report, QString* path, QString* error)
{
    const QString logs = rootSubfolder(QStringLiteral("logs"), error);
    if (logs.isEmpty())
        return false;
    *path = QDir(logs).absoluteFilePath(runReportFileName(report.when, report.model));
    // Text: \r\n on Windows (Notepad). QSaveFile: never a half-written file.
    QSaveFile f(*path);
    const QByteArray data = formatRunReport(report).toUtf8();
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || f.write(data) != data.size() || !f.commit()) {
        *error = f.errorString();
        return false;
    }
    return true;
}
