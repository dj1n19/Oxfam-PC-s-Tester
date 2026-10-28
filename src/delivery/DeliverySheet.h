#pragma once
#include <map>
#include <QDate>
#include <QStringList>
#include "core/TestResult.h"
#include "delivery/XlsxEditor.h"

// Oxfam's delivery sheet ("Fiche Pre Livraison" / "Pre- LeveringsFiche", same
// layout in both languages): which cells the app fills. Everything else
// (order number, manual checks, operator) is left to the technician.

// The checks of the sheet that a test answers.
enum class SheetRow { Battery, Disk, Keyboard, Audio, Camera };

// The lists behind the sheet's dropdowns, one tab each.
enum class SheetList { Models, Shops };

namespace DeliverySheet {

// French and Dutch file names, in that order.
QStringList fileNames();

// The two file names, the system language's first ("nl": Dutch first,
// anything else: French first). Both are always looked for: a technician
// may put the Dutch sheet on a French PC.
QStringList fileNamesFor(const QString& systemLanguage);

// The file name of one language: "fr" or "nl", "" for any other.
QString fileName(const QString& language);

// False (and error) if the file is not the delivery sheet (tabs missing).
bool check(const XlsxEditor& sheet, QString* error);

// A list as it is in its tab (model list, shop list).
QStringList names(const XlsxEditor& sheet, SheetList list);

// Alphabetical order, ignoring case and accents ("Ecaussinnes" and
// "\u00c9caussinnes" sort together). Pure, the same on every system.
QStringList sorted(QStringList names);

// The list's spelling of name (case and spaces ignored), or "" if absent.
QString findName(const QStringList& names, const QString& name);

// The names of wanted that have does not hold (case and spaces ignored),
// each once. Used to keep the French and Dutch lists the same.
QStringList missingNames(const QStringList& have, const QStringList& wanted);

// Adds names and writes the whole column back sorted, so the dropdown is
// alphabetical too (it takes the whole column).
bool addNames(XlsxEditor& sheet, SheetList list, const QStringList& names, QString* error);
bool addName(XlsxEditor& sheet, SheetList list, const QString& name, QString* error);

// The shop the sheet already holds ("" if none).
QString currentShop(const XlsxEditor& sheet);

// Oxfam's inventory number of the PC: exactly 6 digits (developer's rule).
bool isOxfamNumber(const QString& text);
// The number the sheet already holds, "" if none or not 6 digits.
QString currentOxfamNumber(const XlsxEditor& sheet);

// Writes the operator's date (always), the sheet's date (only if empty: it
// may be the order date), the model, the shop and the Oxfam number (each if
// not empty; the number as text, so a leading 0 stays) and the box
// + comment of each row that has a result. PASS/WARN tick the box, SKIPPED leaves it empty
// with the reason. FAIL and ERROR are not written (developer's choice: such
// a PC is not delivered as is), so the row keeps what the sheet had.
bool fill(XlsxEditor& sheet, const QDate& date, const QString& model, const QString& shop,
          const QString& oxfamNumber, const std::map<SheetRow, TestResult>& results, QString* error);

} // namespace DeliverySheet
