#include "ui/Language.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace {

QString g_current = QStringLiteral("en");

// Created once and owned by the application object (Qt parent-child).
// They are kept alive and reloaded, because Qt only reads translators
// that are installed.
QTranslator* appTranslator()
{
    static QTranslator* t = new QTranslator(QCoreApplication::instance());
    return t;
}

QTranslator* qtTranslator()
{
    static QTranslator* t = new QTranslator(QCoreApplication::instance());
    return t;
}

} // namespace

QString systemLanguage()
{
    // A Belgian system is fr_BE or nl_BE: only the language part matters.
    switch (QLocale::system().language()) {
    case QLocale::French: return QStringLiteral("fr");
    case QLocale::Dutch:  return QStringLiteral("nl");
    default:              return QStringLiteral("en");
    }
}

void setLanguage(const QString& code)
{
    QCoreApplication::removeTranslator(appTranslator());
    QCoreApplication::removeTranslator(qtTranslator());
    g_current = QStringLiteral("en");
    if (code == QLatin1String("en"))
        return;

    // Our .qm files are compiled into the executable (qt_add_translations
    // in CMakeLists.txt): nothing to ship next to it.
    if (!appTranslator()->load(QStringLiteral(":/i18n/oxfam-tester_%1").arg(code)))
        return;   // unknown language: stay in English
    QCoreApplication::installTranslator(appTranslator());
    g_current = code;

    // Qt's own texts ("Cancel", "OK"...). windeployqt copies them into
    // translations/ next to the .exe. Missing = those few stay English.
    if (qtTranslator()->load(QStringLiteral("qt_%1").arg(code),
                             QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(qtTranslator());
}

QString currentLanguage()
{
    return g_current;
}
