#include "core/ProgramFolder.h"

#include <QCoreApplication>
#include <QFileInfo>

// Inside an AppImage, applicationDirPath() is a read-only mount that disappears
// on exit; the AppImage runtime gives the real file in $APPIMAGE.
QDir folderAroundProgram()
{
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty())
        return QFileInfo(appImage).absoluteDir();
    QDir dir(QCoreApplication::applicationDirPath());
    dir.cdUp();
    return dir;
}
