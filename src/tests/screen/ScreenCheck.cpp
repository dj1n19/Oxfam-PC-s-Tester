#include "tests/screen/ScreenCheck.h"

#include <QCoreApplication>
#include <QStringList>

const std::vector<ScreenPattern>& screenPatterns()
{
    // 4 colours (developer's choice 2026-11-03: quick to go through).
    // Black: stuck or bright pixels, light leaking at the edges (backlight bleed).
    // White: dead pixels, stains, pressure marks, yellowed zones; a pixel with
    //        one dead sub-pixel shows as a coloured dot.
    // Grey:  uneven backlight (clouding) and faint stains that white can hide.
    // Red:   one plain colour, for dead red sub-pixels and colour faults.
    static const std::vector<ScreenPattern> patterns{
        {0, 0, 0, QStringLiteral("black")},
        {255, 255, 255, QStringLiteral("white")},
        {128, 128, 128, QStringLiteral("grey")},
        {255, 0, 0, QStringLiteral("red")},
    };
    return patterns;
}

namespace {
QString patternNames()
{
    QStringList names;
    for (const ScreenPattern& p : screenPatterns())
        names << p.name;
    return names.join(QStringLiteral(", "));
}
} // namespace

TestResult screenResult(ScreenAnswer answer)
{
    TestResult r;
    switch (answer) {
    case ScreenAnswer::Ok:
        r.status = Status::Pass;
        r.summary = QCoreApplication::translate("ScreenTest", "No defect seen");
        break;
    case ScreenAnswer::SmallDefect:
        r.status = Status::Warn;
        r.summary = QCoreApplication::translate("ScreenTest",
                                                "Small defect seen (dead pixel, light stain...): sell with a note");
        break;
    case ScreenAnswer::Broken:
        r.status = Status::Fail;
        r.summary = QCoreApplication::translate("ScreenTest",
                                                "Screen defect: lines, dead zone, big stain or flicker");
        break;
    }
    r.details = QStringLiteral("Colours shown full screen: %1\nTechnician's answer: %2")
                    .arg(patternNames(),
                         answer == ScreenAnswer::Ok            ? QStringLiteral("no defect")
                         : answer == ScreenAnswer::SmallDefect ? QStringLiteral("small defect")
                                                               : QStringLiteral("broken"));
    return r;
}

TestResult screenSkipped()
{
    TestResult r;
    r.status = Status::Skipped;
    r.summary = QCoreApplication::translate("ScreenTest", "Skipped by the technician");
    return r;
}
