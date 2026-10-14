#include "ui/DeliveryAction.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QSaveFile>
#include <QStandardPaths>

#include "delivery/XlsxEditor.h"

namespace {

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

// Where the program "lives" for the technician: the folder holding the program
// folder (Windows), or the folder holding the .AppImage file (Linux). Inside an
// AppImage, applicationDirPath() is a read-only mount that disappears on exit;
// the AppImage runtime gives the real file in $APPIMAGE.
QDir folderAroundProgram()
{
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty())
        return QFileInfo(appImage).absoluteDir();
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    return dir;
}

// The copy to share goes next to the program (Documents in production, the
// USB stick when testing). The previous one becomes "<name> (backup).xlsx".
bool writeSharedCopy(const QString& fileName, const QByteArray& data, QString* path, QString* error)
{
    const QDir dir = folderAroundProgram();
    *path = dir.absoluteFilePath(fileName);
    if (QFile::exists(*path)) {
        const QString backup = dir.absoluteFilePath(QFileInfo(fileName).completeBaseName()
                                                    + QStringLiteral(" (backup).xlsx"));
        QFile::remove(backup);
        if (!QFile::copy(*path, backup)) {
            *error = QStringLiteral("Cannot make the backup %1").arg(backup);
            return false;
        }
    }
    return writeFile(*path, data, error);
}

} // namespace

void DeliveryAction::run(QWidget* parent, const DeliveryInput& input)
{
    const QString title = tr("Delivery sheet");

    // 1. The sheet is always put on the Desktop before the tests.
    const QDir desktop(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    const QStringList names = DeliverySheet::fileNames();
    QString path;
    for (const QString& name : names) {
        if (desktop.exists(name)) {
            path = desktop.absoluteFilePath(name);
            break;
        }
    }
    if (path.isEmpty()) {
        QMessageBox::warning(parent, title,
                             tr("No delivery sheet on the Desktop (%1).\nCopy \"%2\" or \"%3\" there, then click again.")
                                 .arg(QDir::toNativeSeparators(desktop.path()), names.value(0), names.value(1)));
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
    QFile file(path);
    XlsxEditor sheet;
    QString error;
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
    } else if (sheet.load(file.readAll(), &error)) {
        DeliverySheet::check(sheet, &error);
    }
    file.close();
    if (!error.isEmpty()) {
        QMessageBox::warning(parent, title, tr("The delivery sheet cannot be read:\n%1").arg(error));
        return;
    }

    // 3. The model must be one of the dropdown's list. When the PC's own
    // name is not in it (Lenovo reports "20HD..."), the technician picks it.
    const QStringList models = DeliverySheet::models(sheet);
    QString model = DeliverySheet::findModel(models, input.systemModel);
    QString copyPath;
    if (model.isEmpty()) {
        QInputDialog dialog(parent);
        dialog.setWindowTitle(title);
        dialog.setLabelText(tr("This PC reports \"%1\", which is not in the model list.\n"
                               "Choose its model in the list, or type the name of a new model:")
                                .arg(input.systemModel));
        dialog.setComboBoxItems(models);
        dialog.setComboBoxEditable(true);
        dialog.setTextValue(input.systemModel);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const QString chosen = dialog.textValue().simplified();
        model = DeliverySheet::findModel(models, chosen);
        if (model.isEmpty() && !chosen.isEmpty()
            && QMessageBox::question(parent, title,
                                     tr("Add \"%1\" to the model list?\n\nA copy of the sheet with the new model, "
                                        "without the results of this PC, will be saved next to the program "
                                        "folder, to share with the other technicians.").arg(chosen))
                   == QMessageBox::Yes) {
            if (!DeliverySheet::addModel(sheet, chosen, &error)) {
                QMessageBox::warning(parent, title, tr("The model could not be added:\n%1").arg(error));
                return;
            }
            model = chosen;
            // Saved now, before the results are written into the sheet.
            const QByteArray clean = sheet.save(&error);
            if (clean.isEmpty() || !writeSharedCopy(info.fileName(), clean, &copyPath, &error)) {
                QMessageBox::warning(parent, title,
                                     tr("The copy for the other technicians could not be saved:\n%1").arg(error));
                copyPath.clear();
            }
        }
        // A new model not added: the model cell is left for the technician.
    }

    // 4. Fill and write.
    QByteArray filled;
    if (DeliverySheet::fill(sheet, QDate::currentDate(), model, input.results, &error))
        filled = sheet.save(&error);
    if (filled.isEmpty() || !writeFile(path, filled, &error)) {
        QMessageBox::warning(parent, title,
                             tr("The delivery sheet could not be written. Is it open in an office program?\n%1")
                                 .arg(error));
        return;
    }
    QString done = tr("Delivery sheet filled:\n%1\n\nOpen it to complete the other checks.")
                       .arg(QDir::toNativeSeparators(path));
    if (!copyPath.isEmpty())
        done += QStringLiteral("\n\n")
              + tr("Copy with the new model, for the other technicians:\n%1").arg(QDir::toNativeSeparators(copyPath));
    QMessageBox::information(parent, title, done);
}
