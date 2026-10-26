#include "core/ProgramFolder.h"

#include <QCoreApplication>
#include <QFileInfo>

// Inside an AppImage, applicationDirPath() is a read-only mount that disappears
// on exit; the AppImage runtime gives the real file in $APPIMAGE.
QDir folderAroundProgram()
{
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const QString programFolder = appImage.isEmpty() ? QCoreApplication::applicationDirPath()
                                                     : QFileInfo(appImage).absolutePath();
    // Path arithmetic, not QDir::cdUp(): cdUp() checks the disk and does not
    // move if the parent cannot be read.
    return QDir(QDir::cleanPath(programFolder + QStringLiteral("/..")));
}

QString rootSubfolder(const QString& name, QString* error)
{
    const QDir root = folderAroundProgram();
    if (!root.mkpath(name)) {
        *error = QStringLiteral("Cannot create %1").arg(root.absoluteFilePath(name));
        return QString();
    }
    return root.absoluteFilePath(name);
}

QString configFile(const QString& file)
{
    return folderAroundProgram().absoluteFilePath(QStringLiteral("config/") + file);
}
