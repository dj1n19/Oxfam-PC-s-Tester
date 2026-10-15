#include "report/RunReport.h"

#include <QSaveFile>

#include "core/ProgramFolder.h"

QString formatRunReport(const RunReport& report)
{
    QString text = QStringLiteral("Lazy Djin's Tester - %1\nPC: %2\nVerdict: %3\n")
                       .arg(report.when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                            report.model.isEmpty() ? QStringLiteral("unknown") : report.model,
                            report.verdict);
    for (const RunReportRow& row : report.rows) {
        text += QStringLiteral("\n==== [%1] %2 ====\n%3\n\n%4\n")
                    .arg(statusLabel(row.result.status), row.test, row.result.summary,
                         row.result.details.trimmed());
    }
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
    *path = folderAroundProgram().absoluteFilePath(runReportFileName(report.when, report.model));
    // Text: \r\n on Windows (Notepad). QSaveFile: never a half-written file.
    QSaveFile f(*path);
    const QByteArray data = formatRunReport(report).toUtf8();
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || f.write(data) != data.size() || !f.commit()) {
        *error = f.errorString();
        return false;
    }
    return true;
}
