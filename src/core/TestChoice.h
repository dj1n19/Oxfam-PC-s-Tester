#pragma once
#include <map>
#include <QByteArray>
#include <QString>

// config/tests.json: which tests are ticked when the program starts, edited
// by each technician. Shape: { "battery": true, "camera": false, ... }, keys
// = ITest::id(). A test missing from the file is ticked: a new test of a
// newer version is not hidden by an older file.
struct TestChoice {
    std::map<QString, bool> ticked;

    // False (and error) if the text is not a JSON object of true/false
    // values; the choice is then left empty (everything ticked).
    bool loadFromJson(const QByteArray& json, QString* error);
    bool isTicked(const QString& id) const;
};
