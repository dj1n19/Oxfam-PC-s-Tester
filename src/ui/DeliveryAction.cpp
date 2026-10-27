#include "ui/DeliveryAction.h"

#include <map>

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QSaveFile>
#include <QStandardPaths>

#include "core/ProgramFolder.h"
#include "delivery/XlsxEditor.h"
#include "ui/Language.h"

namespace {

const QString kSheetsFolder = QStringLiteral("delivery-sheets");

constexpr SheetList kLists[] = {SheetList::Models, SheetList::Shops};

// LibreOffice and OpenOffice leave ".~lock.<name>#" next to an open file,
// some programs "~$<name>". They do not stop us from writing, but whoever
// saves last overwrites the other: better to warn.
bool looksOpen(const QFileInfo& file)
{
    const QDir dir = file.dir();
    return dir.exists(QStringLiteral(".~lock.%1#").arg(file.fileName()))
        || dir.exists(QStringLiteral("~$%1").arg(file.fileName()));
}

// QSaveFile writes a temporary file and renames it at the end: a crash or a
// full disk never leaves a half-written sheet.
bool writeFile(const QString& path, const QByteArray& data, QString* error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        *error = f.errorString();
        return false;
    }
    return true;
}

// Writes dir/fileName; the previous version becomes the hidden
// ".<name> (backup).xlsx" (one backup, replaced each time).
bool writeWithBackup(const QDir& dir, const QString& fileName, const QByteArray& data, QString* error)
{
    const QString path = dir.absoluteFilePath(fileName);
    if (QFile::exists(path)) {
        const QString backup = dir.absoluteFilePath(QStringLiteral(".%1 (backup).xlsx")
                                                        .arg(QFileInfo(fileName).completeBaseName()));
        QFile::remove(backup);
        if (!QFile::copy(path, backup)) {
            *error = QStringLiteral("Cannot make the backup %1").arg(backup);
            return false;
        }
        hideFile(backup);   // cosmetic: a visible backup is not a failure
    }
    return writeFile(path, data, error);
}

bool readSheet(const QString& path, XlsxEditor* sheet, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    return sheet->load(file.readAll(), error) && DeliverySheet::check(*sheet, error);
}

// Adds to target's lists the names that source has and target lacks.
// *changed tells whether anything was added.
bool addMissingNames(XlsxEditor& target, const XlsxEditor& source, bool* changed, QString* error)
{
    for (const SheetList list : kLists) {
        const QStringList missing =
            DeliverySheet::missingNames(DeliverySheet::names(target, list), DeliverySheet::names(source, list));
        if (missing.isEmpty())
            continue;
        if (!DeliverySheet::addNames(target, list, missing, error))
            return false;
        *changed = true;
    }
    return true;
}

} // namespace

void DeliveryAction::run(QWidget* parent, const DeliveryInput& input)
{
    const QString title = tr("Delivery sheet");
    const QString language = systemLanguage();
    QString error;

    // 1. The sheet on the Desktop, in either language (a technician may put
    // the Dutch one on a French PC). Both there: the system's language wins.
    const QDir desktop(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    QString path;
    for (const QString& name : DeliverySheet::fileNamesFor(language)) {
        if (desktop.exists(name)) {
            path = desktop.absoluteFilePath(name);
            break;
        }
    }

    // None: copy the one of the system's language from delivery-sheets/.
    QString copiedFrom;
    QString sheetsError;
    const QString sheetsPath = rootSubfolder(kSheetsFolder, &sheetsError);
    const QDir sheets(sheetsPath);
    if (path.isEmpty() && !sheetsPath.isEmpty()) {
        QStringList available;
        for (const QString& name : DeliverySheet::fileNames()) {
            if (sheets.exists(name))
                available << name;
        }
        QString chosen = available.size() == 1 ? available.first() : QString();
        if (available.size() == 2) {
            chosen = DeliverySheet::fileName(language);
            if (chosen.isEmpty()) {   // system neither French nor Dutch: ask
                bool ok = false;
                const QStringList languages{QStringLiteral("Fran\u00e7ais"), QStringLiteral("Nederlands")};
                const QString picked = QInputDialog::getItem(
                    parent, title, tr("Which delivery sheet should be copied to the Desktop?"), languages, 0,
                    false, &ok);
                if (!ok)
                    return;
                chosen = DeliverySheet::fileNames().at(languages.indexOf(picked) == 1 ? 1 : 0);
            }
        }
        if (!chosen.isEmpty()) {
            const QString target = desktop.absoluteFilePath(chosen);
            if (!QFile::copy(sheets.absoluteFilePath(chosen), target)) {
                QMessageBox::warning(parent, title, tr("The delivery sheet could not be copied to the Desktop:\n%1")
                                                        .arg(QDir::toNativeSeparators(target)));
                return;
            }
            path = target;
            copiedFrom = sheets.absoluteFilePath(chosen);
        }
    }
    if (path.isEmpty()) {
        QMessageBox::warning(parent, title,
                             tr("No delivery sheet on the Desktop (%1) nor in the \"%2\" folder (%3).\n"
                                "Put \"%4\" or \"%5\" in one of them, then click again.")
                                 .arg(QDir::toNativeSeparators(desktop.path()), kSheetsFolder,
                                      QDir::toNativeSeparators(sheetsPath.isEmpty() ? sheetsError : sheetsPath),
                                      DeliverySheet::fileNames().value(0), DeliverySheet::fileNames().value(1)));
        return;
    }
    const QFileInfo info(path);
    if (looksOpen(info)
        && QMessageBox::question(parent, title,
                                 tr("The delivery sheet seems to be open in an office program. Close it first: "
                                    "otherwise saving it there will erase these results.\n\nWrite anyway?"))
               != QMessageBox::Yes)
        return;

    // 2. Read it.
    XlsxEditor sheet;
    if (!readSheet(path, &sheet, &error)) {
        QMessageBox::warning(parent, title, tr("The delivery sheet cannot be read:\n%1").arg(error));
        return;
    }

    // 2b. The sheets of delivery-sheets/ (French and Dutch). Their lists and
    // this sheet's are merged: Oxfam's two files did not hold the same shops
    // (the Dutch one had Leuven and Vilvoorde), and the dialogs below should
    // offer every name.
    std::map<QString, XlsxEditor> shared;   // file name -> sheet
    QStringList problems;
    if (sheetsPath.isEmpty()) {
        problems << sheetsError;
    } else {
        for (const QString& name : DeliverySheet::fileNames()) {
            if (!sheets.exists(name))
                continue;
            QString why;
            if (!readSheet(sheets.absoluteFilePath(name), &shared[name], &why)) {
                problems << QStringLiteral("%1: %2").arg(name, why);
                shared.erase(name);
            }
        }
    }
    for (const auto& [name, other] : shared) {
        bool changed = false;
        if (!addMissingNames(sheet, other, &changed, &error)) {
            QMessageBox::warning(parent, title,
                                 tr("The model and shop lists could not be completed from \"%1\":\n%2")
                                     .arg(name, error));
            return;
        }
    }

    // 3. The model must be one of the dropdown's list. When the PC's own
    // name is not in it (Lenovo reports "20HD..."), the technician picks it.
    bool addedName = false;
    const QStringList models = DeliverySheet::names(sheet, SheetList::Models);
    QString model = DeliverySheet::findName(models, input.systemModel);
    if (model.isEmpty()) {
        QInputDialog dialog(parent);
        dialog.setWindowTitle(title);
        dialog.setLabelText(tr("This PC reports \"%1\", which is not in the model list.\n"
                               "Choose its model in the list, or type the name of a new model:")
                                .arg(input.systemModel));
        dialog.setComboBoxItems(DeliverySheet::sorted(models));
        dialog.setComboBoxEditable(true);
        dialog.setTextValue(input.systemModel);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const QString chosen = dialog.textValue().simplified();
        model = DeliverySheet::findName(models, chosen);
        if (model.isEmpty() && !chosen.isEmpty()
            && QMessageBox::question(parent, title,
                                     tr("Add \"%1\" to the model list?\n\nThe sheets of the \"%2\" folder get it "
                                        "too, without the results of this PC, to share with the other technicians.")
                                         .arg(chosen, kSheetsFolder))
                   == QMessageBox::Yes) {
            if (!DeliverySheet::addName(sheet, SheetList::Models, chosen, &error)) {
                QMessageBox::warning(parent, title, tr("The model could not be added:\n%1").arg(error));
                return;
            }
            model = chosen;
            addedName = true;
        }
        // A new model not added: the model cell is left for the technician.
    }

    // 4. The shop: the PC cannot know it. The shop already on the sheet is
    // offered; otherwise the field starts empty (no shop chosen by mistake).
    QString shop;
    const QStringList shops = DeliverySheet::names(sheet, SheetList::Shops);
    if (!shops.isEmpty()) {
        QInputDialog dialog(parent);
        dialog.setWindowTitle(title);
        dialog.setLabelText(tr("Which shop is this PC for?\nChoose it in the list, or type the name of a new shop:"));
        dialog.setComboBoxItems(DeliverySheet::sorted(shops));
        dialog.setComboBoxEditable(true);
        dialog.setTextValue(DeliverySheet::findName(shops, DeliverySheet::currentShop(sheet)));
        if (dialog.exec() != QDialog::Accepted)
            return;
        const QString chosen = dialog.textValue().simplified();
        shop = DeliverySheet::findName(shops, chosen);
        if (shop.isEmpty() && !chosen.isEmpty()
            && QMessageBox::question(parent, title,
                                     tr("Add \"%1\" to the shop list?\n\nThe sheets of the \"%2\" folder get it "
                                        "too, to share with the other technicians.")
                                         .arg(chosen, kSheetsFolder))
                   == QMessageBox::Yes) {
            if (!DeliverySheet::addName(sheet, SheetList::Shops, chosen, &error)) {
                QMessageBox::warning(parent, title, tr("The shop could not be added:\n%1").arg(error));
                return;
            }
            shop = chosen;
            addedName = true;
        }
        // A new shop not added: the shop cell is left for the technician.
    }

    // 5. Each sheet of delivery-sheets/ gets the names it lacks: new ones, or
    // names only the other language had. Written (with a backup) only if
    // something was added. When the Desktop sheet's language is missing there
    // and a name was added, it is saved there, before the results go in.
    QStringList updated;
    for (auto& [name, other] : shared) {
        bool changed = false;
        QString why;
        bool ok = addMissingNames(other, sheet, &changed, &why);
        if (ok && changed) {
            const QByteArray data = other.save(&why);
            ok = !data.isEmpty() && writeWithBackup(sheets, name, data, &why);
        }
        if (!ok)
            problems << QStringLiteral("%1: %2").arg(name, why);
        else if (changed)
            updated << sheets.absoluteFilePath(name);
    }
    if (addedName && !sheetsPath.isEmpty() && !sheets.exists(info.fileName())) {
        QString why;
        const QByteArray clean = sheet.save(&why);
        if (!clean.isEmpty() && writeWithBackup(sheets, info.fileName(), clean, &why))
            updated << sheets.absoluteFilePath(info.fileName());
        else
            problems << QStringLiteral("%1: %2").arg(info.fileName(), why);
    }
    // Only worth a dialog when the technician changed something: a folder
    // that cannot be created is otherwise none of their business.
    if (!problems.isEmpty() && (addedName || !updated.isEmpty()))
        QMessageBox::warning(parent, title,
                             tr("The sheets for the other technicians could not all be updated:\n%1")
                                 .arg(problems.join(QLatin1Char('\n'))));

    // 6. Fill and write.
    QByteArray filled;
    if (DeliverySheet::fill(sheet, QDate::currentDate(), model, shop, input.results, &error))
        filled = sheet.save(&error);
    if (filled.isEmpty() || !writeFile(path, filled, &error)) {
        QMessageBox::warning(parent, title,
                             tr("The delivery sheet could not be written. Is it open in an office program?\n%1")
                                 .arg(error));
        return;
    }
    QString done = tr("Delivery sheet filled:\n%1\n\nOpen it to complete the other checks.")
                       .arg(QDir::toNativeSeparators(path));
    if (!copiedFrom.isEmpty())
        done += QStringLiteral("\n\n")
              + tr("It was copied to the Desktop from:\n%1").arg(QDir::toNativeSeparators(copiedFrom));
    if (!updated.isEmpty()) {
        QStringList shown;
        for (const QString& p : updated)
            shown << QDir::toNativeSeparators(p);
        done += QStringLiteral("\n\n")
              + tr("Sheets with the new names, for the other technicians:\n%1").arg(shown.join(QLatin1Char('\n')));
    }
    QMessageBox::information(parent, title, done);
}
