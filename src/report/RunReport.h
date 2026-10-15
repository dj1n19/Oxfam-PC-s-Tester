#pragma once
#include <vector>
#include <QDateTime>
#include <QString>
#include "core/TestResult.h"

// One run as a plain text file: what the details pane shows, for every test.
// Written after each run (testing on real PCs now; the base of the phase 2 report).
struct RunReportRow {
    QString test;        // the test's name, as in the table
    TestResult result;
};

struct RunReport {
    QDateTime when;
    QString model;       // "Dell Latitude 7420"; empty = unknown
    QString verdict;
    std::vector<RunReportRow> rows;
};

// Pure, unit tested.
QString formatRunReport(const RunReport& report);
// "2026-10-15 14-32-05 Dell Latitude 7420.txt": sorts by date, one file per run,
// and only characters that Windows and Linux both accept.
QString runReportFileName(const QDateTime& when, const QString& model);

// Writes the report into folderAroundProgram(). False + error if it could not.
bool writeRunReport(const RunReport& report, QString* path, QString* error);
