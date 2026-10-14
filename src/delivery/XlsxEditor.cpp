#include "delivery/XlsxEditor.h"

#include <vector>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include "miniz.h"

namespace {

// Attribute names are QLatin1String, not u"...": QXmlStreamAttributes::value() only
// accepts u"..." since Qt 6.6, and the Linux CI builds with Qt 6.4.
const QString kRelNs = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships");

// miniz is a C library: this frees its state on every return path (RAII).
struct Zip {
    mz_zip_archive z{};
    Zip() = default;
    Zip(const Zip&) = delete;
    Zip& operator=(const Zip&) = delete;
    ~Zip() { mz_zip_end(&z); }
};

QString zipError(mz_zip_archive* z)
{
    return QString::fromLatin1(mz_zip_get_error_string(mz_zip_get_last_error(z)));
}

// One file of the zip. False if it is missing or unreadable.
bool readEntry(const QByteArray& zip, const QString& path, QByteArray* out)
{
    Zip r;
    if (!mz_zip_reader_init_mem(&r.z, zip.constData(), size_t(zip.size()), 0))
        return false;
    const int index = mz_zip_reader_locate_file(&r.z, path.toUtf8().constData(), nullptr, 0);
    if (index < 0)
        return false;
    size_t size = 0;
    void* data = mz_zip_reader_extract_to_heap(&r.z, mz_uint(index), &size, 0);
    if (!data)
        return false;
    *out = QByteArray(static_cast<const char*>(data), qsizetype(size));
    mz_free(data);
    return true;
}

struct CellRef {
    int col = 0;   // A = 1
    int row = 0;
};

// "C12" -> column 3, row 12. False for anything else.
bool parseRef(const QString& ref, CellRef* out)
{
    static const QRegularExpression re(QStringLiteral("^([A-Z]{1,3})([1-9][0-9]{0,6})$"));
    const QRegularExpressionMatch m = re.match(ref);
    if (!m.hasMatch())
        return false;
    int col = 0;
    for (QChar c : m.captured(1))
        col = col * 26 + (c.unicode() - 'A' + 1);
    out->col = col;
    out->row = m.captured(2).toInt();
    return true;
}

int columnNumber(const QString& letters)
{
    CellRef r;
    return parseRef(letters + QLatin1Char('1'), &r) ? r.col : 0;
}

// Where the rows and cells are in a sheet's XML text (character offsets into
// the QString). QXmlStreamReader finds them, so attribute order, quotes and
// spacing do not matter: LibreOffice and OnlyOffice write them differently.
struct CellPos {
    CellRef ref;
    qsizetype start = 0, end = 0;   // the whole <c ...>...</c>
    QString style;                  // s="..." (format), kept when rewriting
    QString text;
};
struct RowPos {
    int number = 0;
    qsizetype start = 0, openEnd = 0, closeStart = 0;
    bool selfClosing = false;       // <row r="5"/>
    std::vector<CellPos> cells;
};
struct SheetPos {
    qsizetype dataOpenEnd = -1, dataCloseStart = 0;   // inside <sheetData>
    bool dataSelfClosing = false;
    std::vector<RowPos> rows;
};

bool scanSheet(const QString& xml, const QStringList& shared, SheetPos* out, QString* error)
{
    QXmlStreamReader x(xml);
    bool inData = false, inValue = false, inPhonetic = false;
    QString type, value;
    while (true) {
        const qsizetype before = x.characterOffset();   // where the next token starts
        const QXmlStreamReader::TokenType token = x.readNext();
        if (token == QXmlStreamReader::EndDocument || token == QXmlStreamReader::Invalid)
            break;
        const qsizetype after = x.characterOffset();
        if (token == QXmlStreamReader::StartElement) {
            // A prefixed main namespace (<x:c>) is valid XML but would need the
            // same prefix on the cells we add: refuse instead of guessing.
            if (!x.prefix().isEmpty() && (x.name() == u"sheetData" || x.name() == u"row" || x.name() == u"c")) {
                *error = QStringLiteral("Unsupported sheet XML (prefixed elements)");
                return false;
            }
            if (x.name() == u"sheetData") {
                inData = true;
                out->dataOpenEnd = after;
                out->dataSelfClosing = xml.mid(after - 2, 2) == u"/>";
            } else if (inData && x.name() == u"row") {
                bool ok = false;
                RowPos row;
                row.number = x.attributes().value(QLatin1String("r")).toInt(&ok);
                if (!ok) {
                    *error = QStringLiteral("Row without a number");
                    return false;
                }
                row.start = before;
                row.openEnd = after;
                row.selfClosing = xml.mid(after - 2, 2) == u"/>";
                out->rows.push_back(row);
            } else if (inData && x.name() == u"c" && !out->rows.empty()) {
                CellPos cell;
                if (!parseRef(x.attributes().value(QLatin1String("r")).toString(), &cell.ref)
                    || cell.ref.row != out->rows.back().number) {
                    *error = QStringLiteral("Cell without a valid reference in row %1").arg(out->rows.back().number);
                    return false;
                }
                cell.start = before;
                cell.style = x.attributes().value(QLatin1String("s")).toString();
                type = x.attributes().value(QLatin1String("t")).toString();
                value.clear();
                out->rows.back().cells.push_back(cell);
            } else if (x.name() == u"rPh") {
                inPhonetic = true;   // pronunciation guide (Japanese): not the text
            } else if ((x.name() == u"v" || x.name() == u"t") && !inPhonetic) {
                inValue = true;
            }
        } else if (token == QXmlStreamReader::Characters) {
            if (inValue)
                value += x.text();
        } else if (token == QXmlStreamReader::EndElement) {
            if (x.name() == u"sheetData") {
                inData = false;
                out->dataCloseStart = before;
            } else if (inData && x.name() == u"row") {
                out->rows.back().closeStart = before;
            } else if (inData && x.name() == u"c" && !out->rows.empty()) {
                CellPos& cell = out->rows.back().cells.back();
                cell.end = after;
                // t="s": the cell holds the number of a shared string.
                cell.text = type == u"s" ? shared.value(value.toInt()) : value;
            } else if (x.name() == u"rPh") {
                inPhonetic = false;
            } else if (x.name() == u"v" || x.name() == u"t") {
                inValue = false;
            }
        }
    }
    if (x.hasError()) {
        *error = QStringLiteral("Invalid sheet XML: %1").arg(x.errorString());
        return false;
    }
    if (out->dataOpenEnd < 0) {
        *error = QStringLiteral("Sheet has no <sheetData>");
        return false;
    }
    return true;
}

const CellPos* findCell(const SheetPos& pos, const CellRef& ref)
{
    for (const RowPos& row : pos.rows) {
        if (row.number != ref.row)
            continue;
        for (const CellPos& c : row.cells) {
            if (c.ref.col == ref.col)
                return &c;
        }
    }
    return nullptr;
}

QString cellXml(const QString& ref, const QString& style, const QString& text)
{
    QString xml = QStringLiteral("<c r=\"%1\"").arg(ref);
    if (!style.isEmpty())
        xml += QStringLiteral(" s=\"%1\"").arg(style.toHtmlEscaped());
    if (text.isEmpty())
        return xml + QStringLiteral("/>");
    // Inline string: the text is stored in the cell itself, so the shared
    // strings file (used by all the other cells) does not have to change.
    return xml + QStringLiteral(" t=\"inlineStr\"><is><t xml:space=\"preserve\">%1</t></is></c>")
                     .arg(text.toHtmlEscaped());
}

} // namespace

bool XlsxEditor::load(const QByteArray& xlsx, QString* error)
{
    m_zip = xlsx;
    m_sheets.clear();
    m_sharedStrings.clear();
    m_changed.clear();

    // Tab names are in workbook.xml; the file of each tab is found through
    // an id in workbook.xml.rels (the files are not always named in order).
    QByteArray workbook, rels;
    if (!readEntry(m_zip, QStringLiteral("xl/workbook.xml"), &workbook)
        || !readEntry(m_zip, QStringLiteral("xl/_rels/workbook.xml.rels"), &rels)) {
        *error = QStringLiteral("Not an .xlsx file (no xl/workbook.xml)");
        return false;
    }
    std::map<QString, QString> targets;   // relationship id -> file
    QXmlStreamReader r(rels);
    while (!r.atEnd()) {
        if (r.readNext() == QXmlStreamReader::StartElement && r.name() == u"Relationship") {
            // Relative to xl/ ("worksheets/sheet1.xml"), or absolute ("/xl/...").
            QString target = r.attributes().value(QLatin1String("Target")).toString();
            target = target.startsWith(QLatin1Char('/')) ? target.mid(1) : QStringLiteral("xl/") + target;
            targets[r.attributes().value(QLatin1String("Id")).toString()] = target;
        }
    }
    QXmlStreamReader w(workbook);
    while (!w.atEnd()) {
        if (w.readNext() == QXmlStreamReader::StartElement && w.name() == u"sheet") {
            const auto it = targets.find(w.attributes().value(kRelNs, QLatin1String("id")).toString());
            if (it != targets.end())
                m_sheets[w.attributes().value(QLatin1String("name")).toString()] = it->second;
        }
    }
    if (r.hasError() || w.hasError() || m_sheets.empty()) {
        *error = QStringLiteral("Unreadable workbook: %1").arg(r.hasError() ? r.errorString() : w.errorString());
        return false;
    }

    // Optional: a file with only numbers or inline texts has none.
    QByteArray shared;
    if (readEntry(m_zip, QStringLiteral("xl/sharedStrings.xml"), &shared)) {
        QXmlStreamReader s(shared);
        bool inPhonetic = false, inText = false;
        while (!s.atEnd()) {
            switch (s.readNext()) {
            case QXmlStreamReader::StartElement:
                if (s.name() == u"si")
                    m_sharedStrings.append(QString());
                else if (s.name() == u"rPh")
                    inPhonetic = true;
                else if (s.name() == u"t" && !inPhonetic)
                    inText = true;
                break;
            case QXmlStreamReader::EndElement:
                if (s.name() == u"rPh")
                    inPhonetic = false;
                else if (s.name() == u"t")
                    inText = false;
                break;
            case QXmlStreamReader::Characters:
                if (inText && !m_sharedStrings.isEmpty())
                    m_sharedStrings.last() += s.text();
                break;
            default:
                break;
            }
        }
        if (s.hasError()) {
            *error = QStringLiteral("Unreadable shared strings: %1").arg(s.errorString());
            return false;
        }
    }
    return true;
}

QString XlsxEditor::sheetPath(const QString& sheet) const
{
    const auto it = m_sheets.find(sheet);
    return it == m_sheets.end() ? QString() : it->second;
}

QByteArray XlsxEditor::entry(const QString& path) const
{
    const auto it = m_changed.find(path);
    if (it != m_changed.end())
        return it->second;
    QByteArray data;
    readEntry(m_zip, path, &data);
    return data;
}

QString XlsxEditor::cell(const QString& sheet, const QString& ref) const
{
    CellRef target;
    const QString path = sheetPath(sheet);
    SheetPos pos;
    QString error;
    if (path.isEmpty() || !parseRef(ref, &target)
        || !scanSheet(QString::fromUtf8(entry(path)), m_sharedStrings, &pos, &error))
        return QString();
    const CellPos* c = findCell(pos, target);
    return c ? c->text : QString();
}

std::map<int, QString> XlsxEditor::column(const QString& sheet, const QString& letters) const
{
    std::map<int, QString> cells;
    const int col = columnNumber(letters);
    const QString path = sheetPath(sheet);
    SheetPos pos;
    QString error;
    if (col == 0 || path.isEmpty() || !scanSheet(QString::fromUtf8(entry(path)), m_sharedStrings, &pos, &error))
        return cells;
    for (const RowPos& row : pos.rows) {
        for (const CellPos& c : row.cells) {
            if (c.ref.col == col && !c.text.isEmpty())
                cells[row.number] = c.text;
        }
    }
    return cells;
}

bool XlsxEditor::setCell(const QString& sheet, const QString& ref, const QString& text, QString* error)
{
    CellRef target;
    if (!parseRef(ref, &target)) {
        *error = QStringLiteral("Invalid cell reference '%1'").arg(ref);
        return false;
    }
    const QString path = sheetPath(sheet);
    if (path.isEmpty()) {
        *error = QStringLiteral("No sheet named '%1'").arg(sheet);
        return false;
    }
    QString xml = QString::fromUtf8(entry(path));
    SheetPos pos;
    if (!scanSheet(xml, m_sharedStrings, &pos, error))
        return false;

    // Rows and cells are stored in order (the format requires it): a new one
    // goes before the first one that comes after it.
    const RowPos* row = nullptr;
    for (const RowPos& r : pos.rows) {
        if (r.number == target.row)
            row = &r;
    }
    if (const CellPos* old = findCell(pos, target)) {
        xml.replace(old->start, old->end - old->start, cellXml(ref, old->style, text));
    } else if (row && row->selfClosing) {
        xml.replace(row->openEnd - 2, 2, QLatin1Char('>') + cellXml(ref, {}, text) + QStringLiteral("</row>"));
    } else if (row) {
        qsizetype at = row->closeStart;
        for (const CellPos& c : row->cells) {
            if (c.ref.col > target.col) {
                at = c.start;
                break;
            }
        }
        xml.insert(at, cellXml(ref, {}, text));
    } else {
        const QString newRow = QStringLiteral("<row r=\"%1\">%2</row>").arg(target.row).arg(cellXml(ref, {}, text));
        if (pos.dataSelfClosing) {
            xml.replace(pos.dataOpenEnd - 2, 2, QLatin1Char('>') + newRow + QStringLiteral("</sheetData>"));
        } else {
            qsizetype at = pos.dataCloseStart;
            for (const RowPos& r : pos.rows) {
                if (r.number > target.row) {
                    at = r.start;
                    break;
                }
            }
            xml.insert(at, newRow);
        }
    }

    // Never keep a broken sheet: read the result back before accepting it.
    SheetPos check;
    const CellPos* written = nullptr;
    if (!scanSheet(xml, m_sharedStrings, &check, error) || !(written = findCell(check, target))
        || written->text != text) {
        if (error->isEmpty())
            *error = QStringLiteral("Cell %1 could not be written").arg(ref);
        return false;
    }
    m_changed[path] = xml.toUtf8();
    return true;
}

QByteArray XlsxEditor::save(QString* error) const
{
    Zip in, out;
    if (!mz_zip_reader_init_mem(&in.z, m_zip.constData(), size_t(m_zip.size()), 0)) {
        *error = QStringLiteral("Cannot reopen the file: %1").arg(zipError(&in.z));
        return {};
    }
    if (!mz_zip_writer_init_heap(&out.z, 0, size_t(m_zip.size()))) {
        *error = QStringLiteral("Cannot create the new file: %1").arg(zipError(&out.z));
        return {};
    }
    // Same files, same order. Unchanged ones are copied compressed as they
    // are (not even unpacked): byte for byte what the office program wrote.
    const mz_uint count = mz_zip_reader_get_num_files(&in.z);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&in.z, i, &stat)) {
            *error = QStringLiteral("Cannot read file %1 of the zip: %2").arg(i).arg(zipError(&in.z));
            return {};
        }
        const auto it = m_changed.find(QString::fromUtf8(stat.m_filename));
        const bool ok = it == m_changed.end()
            ? mz_zip_writer_add_from_zip_reader(&out.z, &in.z, i)
            : mz_zip_writer_add_mem(&out.z, stat.m_filename, it->second.constData(),
                                    size_t(it->second.size()), MZ_DEFAULT_COMPRESSION);
        if (!ok) {
            *error = QStringLiteral("Cannot write %1: %2").arg(QString::fromUtf8(stat.m_filename), zipError(&out.z));
            return {};
        }
    }
    void* buffer = nullptr;
    size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&out.z, &buffer, &size)) {
        *error = QStringLiteral("Cannot finish the new file: %1").arg(zipError(&out.z));
        return {};
    }
    QByteArray result(static_cast<const char*>(buffer), qsizetype(size));
    mz_free(buffer);   // the finished buffer is ours to free (miniz.c)
    return result;
}
