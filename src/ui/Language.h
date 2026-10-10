#pragma once
#include <QString>

// Interface language: "fr", "nl" or "en" (English = the texts in the code).

// French or Dutch when the system uses it, otherwise English.
QString systemLanguage();

// Loads the translations of the app and of Qt itself (standard buttons).
// Qt then sends QEvent::LanguageChange to every widget, so open windows
// can update their texts. Texts already computed (test summaries) stay
// in the language they were made in.
void setLanguage(const QString& code);

QString currentLanguage();
