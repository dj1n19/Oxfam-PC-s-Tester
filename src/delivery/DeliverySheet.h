#pragma once
#include <map>
#include <QDate>
#include <QStringList>
#include "core/TestResult.h"
#include "delivery/XlsxEditor.h"

// Oxfam's delivery sheet ("Fiche Pre Livraison" / "Pre- LeveringsFiche", same
// layout in both languages): which cells the app fills. Everything else
// (order number, shop, manual checks, operator) is left to the technician.

// The checks of the sheet that a test answers.
enum class SheetRow { Battery, Disk, Keyboard, Audio, Camera };

namespace DeliverySheet {

// French and Dutch file names, looked for on the Desktop.
QStringList fileNames();

// False (and error) if the file is not the delivery sheet (tabs missing).
bool check(const XlsxEditor& sheet, QString* error);

// The model list used by the dropdown of the "model" cell.
QStringList models(const XlsxEditor& sheet);

// The list's spelling of name (case and spaces ignored), or "" if absent.
QString findModel(const QStringList& models, const QString& name);

// Adds a model at the end of the list (the dropdown takes the whole column).
bool addModel(XlsxEditor& sheet, const QString& model, QString* error);

// Writes the date, the model (if not empty) and the box + comment of each
// row that has a result. PASS/WARN tick the box, SKIPPED leaves it empty
// with the reason. FAIL and ERROR are not written (developer's choice: such
// a PC is not delivered as is), so the row keeps what the sheet had.
bool fill(XlsxEditor& sheet, const QDate& date, const QString& model,
          const std::map<SheetRow, TestResult>& results, QString* error);

} // namespace DeliverySheet
