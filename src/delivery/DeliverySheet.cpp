#include "delivery/DeliverySheet.h"

#include <algorithm>

#include <QRegularExpression>

namespace {

// Tab names, the same in both files. é keeps this source ASCII (MSVC C4819).
const QString kCheckSheet = QStringLiteral("Double Vérification PC");
const QString kModelSheet = QStringLiteral("Models");
const QString kShopSheet = QStringLiteral("Shops");

const QString kShopCell = QStringLiteral("C4");
const QString kModelCell = QStringLiteral("C5");
const QString kOxfamNumberCell = QStringLiteral("C6");   // "Oxfam number"
const QString kDateCell = QStringLiteral("C7");           // "Date / Datum" of the header
const QString kOperatorDateCell = QStringLiteral("C25");  // date of the first operator line

const QString kTicked = QStringLiteral("☑");     // the sheet's own checkbox characters
const QString kUnticked = QStringLiteral("☐");

// Row of each check on the sheet: column C = box, D = comment.
int sheetRow(SheetRow row)
{
    switch (row) {
    case SheetRow::Battery:  return 12;
    case SheetRow::Disk:     return 13;
    case SheetRow::Keyboard: return 14;
    case SheetRow::Audio:    return 15;
    case SheetRow::Camera:   return 16;
    }
    return 0;
}

QString normalized(const QString& s)
{
    return s.simplified().toCaseFolded();
}

// Sorting key: accents removed (decomposed, then the marks dropped), case folded.
QString sortKey(const QString& s)
{
    QString key;
    for (const QChar c : normalized(s).normalized(QString::NormalizationForm_D)) {
        if (c.category() != QChar::Mark_NonSpacing)
            key += c;
    }
    return key;
}

QString listTab(SheetList list)
{
    return list == SheetList::Models ? kModelSheet : kShopSheet;
}

} // namespace

namespace DeliverySheet {

QStringList fileNames()
{
    return {QStringLiteral("Fiche Pré Livraison.xlsx"), QStringLiteral("Pre- LeveringsFiche.xlsx")};
}

QStringList fileNamesFor(const QString& systemLanguage)
{
    QStringList names = fileNames();
    if (systemLanguage == QLatin1String("nl"))
        names.swapItemsAt(0, 1);
    return names;
}

QString fileName(const QString& language)
{
    if (language == QLatin1String("fr"))
        return fileNames().at(0);
    if (language == QLatin1String("nl"))
        return fileNames().at(1);
    return QString();
}

bool check(const XlsxEditor& sheet, QString* error)
{
    // The title cell exists in both languages: a cheap proof it is our sheet.
    if (sheet.cell(kCheckSheet, QStringLiteral("A1")).isEmpty() || names(sheet, SheetList::Models).isEmpty()) {
        *error = QStringLiteral("Not the delivery sheet: tab '%1' or model list missing").arg(kCheckSheet);
        return false;
    }
    return true;
}

QStringList names(const XlsxEditor& sheet, SheetList list)
{
    QStringList result;
    for (const auto& [row, text] : sheet.column(listTab(list), QStringLiteral("A")))
        result << text;
    return result;
}

QStringList sorted(QStringList names)
{
    // std::sort with a fixed key: QCollator would follow the system's locale.
    std::stable_sort(names.begin(), names.end(), [](const QString& a, const QString& b) {
        const int byKey = QString::compare(sortKey(a), sortKey(b));
        return byKey != 0 ? byKey < 0 : QString::compare(a, b) < 0;
    });
    return names;
}

QString findName(const QStringList& names, const QString& name)
{
    const QString wanted = normalized(name);
    if (wanted.isEmpty())
        return QString();
    for (const QString& n : names) {
        if (normalized(n) == wanted)
            return n;
    }
    return QString();
}

QStringList missingNames(const QStringList& have, const QStringList& wanted)
{
    QStringList missing;
    for (const QString& name : wanted) {
        if (findName(have, name).isEmpty() && findName(missing, name).isEmpty())
            missing << name.simplified();
    }
    return missing;
}

bool addName(XlsxEditor& sheet, SheetList list, const QString& name, QString* error)
{
    return addNames(sheet, list, {name}, error);
}

bool addNames(XlsxEditor& sheet, SheetList list, const QStringList& names, QString* error)
{
    const QString tab = listTab(list);
    const std::map<int, QString> column = sheet.column(tab, QStringLiteral("A"));
    QStringList all;
    for (const auto& [row, text] : column)
        all << text;
    for (const QString& name : names)
        all << name.simplified();
    all = sorted(all);

    // Written back from the list's first row, one name per row.
    const int first = column.empty() ? 1 : column.begin()->first;
    for (int i = 0; i < all.size(); ++i) {
        if (!sheet.setCell(tab, QStringLiteral("A%1").arg(first + i), all.at(i), error))
            return false;
    }
    // A list with empty rows in it was longer: clear what is left below.
    for (const auto& [row, text] : column) {
        if (row >= first + all.size() && !sheet.setCell(tab, QStringLiteral("A%1").arg(row), QString(), error))
            return false;
    }
    return true;
}

QString currentShop(const XlsxEditor& sheet)
{
    return sheet.cell(kCheckSheet, kShopCell).simplified();
}

bool isOxfamNumber(const QString& text)
{
    // ^[0-9]{6}$, anchored on the whole text: a plain "$" also matches
    // before a final newline ("123456\n" would pass).
    static const QRegularExpression sixDigits(QRegularExpression::anchoredPattern(QStringLiteral("[0-9]{6}")));
    return sixDigits.match(text).hasMatch();
}

QString currentOxfamNumber(const XlsxEditor& sheet)
{
    const QString number = sheet.cell(kCheckSheet, kOxfamNumberCell).trimmed();
    return isOxfamNumber(number) ? number : QString();
}

bool fill(XlsxEditor& sheet, const QDate& date, const QString& model, const QString& shop,
          const QString& oxfamNumber, const std::map<SheetRow, TestResult>& results, QString* error)
{
    // Text, not an Excel date number: shown the same in every office program.
    const QString day = date.toString(QStringLiteral("dd/MM/yyyy"));
    if (!sheet.setCell(kCheckSheet, kOperatorDateCell, day, error))
        return false;
    // The header date may already be there (the order's): never replace it.
    if (sheet.cell(kCheckSheet, kDateCell).trimmed().isEmpty()
        && !sheet.setCell(kCheckSheet, kDateCell, day, error))
        return false;
    if (!model.isEmpty() && !sheet.setCell(kCheckSheet, kModelCell, model, error))
        return false;
    if (!shop.isEmpty() && !sheet.setCell(kCheckSheet, kShopCell, shop, error))
        return false;
    if (!oxfamNumber.isEmpty() && !sheet.setCell(kCheckSheet, kOxfamNumberCell, oxfamNumber, error))
        return false;

    for (const auto& [row, result] : results) {
        if (result.status == Status::Fail || result.status == Status::Error)
            continue;
        const bool ok = result.status == Status::Pass || result.status == Status::Warn;
        // PASS: the key value only (battery "85 %"). Otherwise the reason.
        const QString comment = result.status == Status::Pass ? result.note : result.summary;
        const int r = sheetRow(row);
        if (!sheet.setCell(kCheckSheet, QStringLiteral("C%1").arg(r), ok ? kTicked : kUnticked, error)
            || !sheet.setCell(kCheckSheet, QStringLiteral("D%1").arg(r), comment, error))
            return false;
    }
    return true;
}

} // namespace DeliverySheet
