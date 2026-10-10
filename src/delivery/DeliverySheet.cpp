#include "delivery/DeliverySheet.h"

namespace {

// Tab names, the same in both files. é keeps this source ASCII (MSVC C4819).
const QString kCheckSheet = QStringLiteral("Double Vérification PC");
const QString kModelSheet = QStringLiteral("Models");

const QString kModelCell = QStringLiteral("C5");
const QString kDateCell = QStringLiteral("C7");

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

} // namespace

namespace DeliverySheet {

QStringList fileNames()
{
    return {QStringLiteral("Fiche Pré Livraison.xlsx"), QStringLiteral("Pre- LeveringsFiche.xlsx")};
}

bool check(const XlsxEditor& sheet, QString* error)
{
    // The title cell exists in both languages: a cheap proof it is our sheet.
    if (sheet.cell(kCheckSheet, QStringLiteral("A1")).isEmpty() || models(sheet).isEmpty()) {
        *error = QStringLiteral("Not the delivery sheet: tab '%1' or model list missing").arg(kCheckSheet);
        return false;
    }
    return true;
}

QStringList models(const XlsxEditor& sheet)
{
    QStringList list;
    for (const auto& [row, text] : sheet.column(kModelSheet, QStringLiteral("A")))
        list << text;
    return list;
}

QString findModel(const QStringList& models, const QString& name)
{
    const QString wanted = normalized(name);
    if (wanted.isEmpty())
        return QString();
    for (const QString& m : models) {
        if (normalized(m) == wanted)
            return m;
    }
    return QString();
}

bool addModel(XlsxEditor& sheet, const QString& model, QString* error)
{
    const std::map<int, QString> list = sheet.column(kModelSheet, QStringLiteral("A"));
    const int next = list.empty() ? 1 : list.rbegin()->first + 1;
    return sheet.setCell(kModelSheet, QStringLiteral("A%1").arg(next), model.simplified(), error);
}

bool fill(XlsxEditor& sheet, const QDate& date, const QString& model,
          const std::map<SheetRow, TestResult>& results, QString* error)
{
    // Text, not an Excel date number: shown the same in every office program.
    if (!sheet.setCell(kCheckSheet, kDateCell, date.toString(QStringLiteral("dd/MM/yyyy")), error))
        return false;
    if (!model.isEmpty() && !sheet.setCell(kCheckSheet, kModelCell, model, error))
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
