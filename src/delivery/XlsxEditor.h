#pragma once
#include <map>
#include <QByteArray>
#include <QString>
#include <QStringList>

// Changes cell texts of an .xlsx file "in place".
//
// An .xlsx file is a zip of XML files: one per sheet, plus styles, dropdown
// lists, page setup... General libraries (QXlsx) read everything into objects
// and write a new file: what they do not understand is lost (the delivery
// sheet lost all its dropdowns). Here only the XML of a changed sheet is
// rewritten, and inside it only the changed cells: every other byte stays as
// LibreOffice or OnlyOffice saved it.
class XlsxEditor {
public:
    // xlsx = the whole file. False (and error) if it is not a readable .xlsx.
    bool load(const QByteArray& xlsx, QString* error);

    // Text of a cell, "" if empty or missing. sheet = tab name, ref = "C12".
    QString cell(const QString& sheet, const QString& ref) const;

    // Non-empty cells of a column ("A"), by row number.
    std::map<int, QString> column(const QString& sheet, const QString& letters) const;

    // Writes text in a cell, keeping its format (borders, font).
    // Empty text clears the value. Creates the cell or the row if needed.
    bool setCell(const QString& sheet, const QString& ref, const QString& text, QString* error);

    // The whole file with the changes, or empty (and error) on failure.
    QByteArray save(QString* error) const;

private:
    QByteArray entry(const QString& path) const;   // current XML of a zip entry
    QString sheetPath(const QString& sheet) const; // "xl/worksheets/sheet1.xml", "" if unknown

    QByteArray m_zip;                               // the file as loaded
    std::map<QString, QString> m_sheets;            // tab name -> zip path
    QStringList m_sharedStrings;                    // texts that cells refer to by number
    std::map<QString, QByteArray> m_changed;        // zip path -> rewritten XML
};
