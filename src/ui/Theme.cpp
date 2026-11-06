#include "ui/Theme.h"

#include <map>
#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>

namespace {

constexpr int kIconSize = 32;   // drawn large, Qt scales it down to the row height

// Pen and canvas shared by every icon.
QPixmap canvas()
{
    QPixmap pm(kIconSize, kIconSize);
    pm.fill(Qt::transparent);
    return pm;
}

QPen pen(const QColor& c)
{
    return QPen(c, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

QIcon drawIcon(Status s)
{
    QPixmap pm = canvas();
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor c = Theme::statusColor(s);
    p.setPen(pen(c));
    switch (s) {
    case Status::Pass:   // check mark
        p.drawPolyline(QPolygonF({{6, 17}, {13, 24}, {26, 8}}));
        break;
    case Status::Warn: { // triangle with "!"
        QPainterPath tri;
        tri.moveTo(16, 4);
        tri.lineTo(29, 27);
        tri.lineTo(3, 27);
        tri.closeSubpath();
        p.setPen(QPen(c, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(tri);
        p.drawLine(16, 12, 16, 19);
        p.drawPoint(16, 23);
        break;
    }
    case Status::Fail:   // cross
        p.drawLine(8, 8, 24, 24);
        p.drawLine(24, 8, 8, 24);
        break;
    case Status::Error: { // "?" in a circle: the test could not tell
        p.setPen(QPen(c, 3));
        p.drawEllipse(QRectF(3, 3, 26, 26));
        QFont f = p.font();
        f.setPixelSize(20);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRectF(3, 3, 26, 26), Qt::AlignCenter, QStringLiteral("?"));
        break;
    }
    case Status::Skipped: // dash: nothing to say
        p.drawLine(8, 16, 24, 16);
        break;
    }
    return QIcon(pm);
}

} // namespace

namespace Theme {

// Colours of the logo (djinn.png): night blue = its outline, royal and sky
// blue = its body, ice = its highlights, gold = the lamp (START only).
QColor background() { return QColor(0x0b, 0x1a, 0x33); }
QColor surface()    { return QColor(0x07, 0x12, 0x26); }
QColor text()       { return QColor(0xd6, 0xf0, 0xff); }   // ice, a bit lighter than the logo's: long text
QColor dimText()    { return QColor(0x6f, 0x9c, 0xc8); }

QColor statusColor(Status s)
{
    switch (s) {
    case Status::Pass:    return QColor(0x5e, 0xe0, 0x8a);   // green
    case Status::Warn:    return QColor(0xff, 0xb3, 0x47);   // amber: not the lamp's gold of START
    case Status::Fail:    return QColor(0xff, 0x6b, 0x6b);   // coral red
    case Status::Error:   return QColor(0xd5, 0x9b, 0xff);   // violet: "could not test", not "bad"
    case Status::Skipped: return QColor(0x8f, 0xa3, 0xbf);   // blue grey
    }
    return text();
}

QColor runningColor() { return QColor(0x7f, 0xf3, 0xff); }   // cyan, the djinn's magic
QColor waitingColor() { return dimText(); }

QIcon statusIcon(Status s)
{
    // Drawn once per status: tables ask for them on every row update.
    static std::map<Status, QIcon> cache;
    auto it = cache.find(s);
    if (it == cache.end())
        it = cache.emplace(s, drawIcon(s)).first;
    return it->second;
}

QIcon runningIcon()
{
    static const QIcon icon = [] {   // "play" triangle
        QPixmap pm = canvas();
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(runningColor());
        p.drawPolygon(QPolygonF({{9, 6}, {26, 16}, {9, 26}}));
        return QIcon(pm);
    }();
    return icon;
}

QFont monoFont()
{
    // Fonts that already exist on the PCs (no font file to ship): Windows has
    // Consolas, Linux Mint DejaVu, Arch often only Liberation.
    QFont font;
    font.setFamilies({QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono"),
                      QStringLiteral("Liberation Mono"), QStringLiteral("Ubuntu Mono"),
                      QStringLiteral("Noto Sans Mono"), QStringLiteral("Courier New")});
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(10);
    return font;
}

void apply(QApplication& app)
{
    // Fusion draws every widget from the palette below, the same way on
    // every OS. The native Windows style would ignore part of it.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette pal;
    const QColor button(0x12, 0x30, 0x5a);
    pal.setColor(QPalette::Window, background());
    pal.setColor(QPalette::WindowText, text());
    pal.setColor(QPalette::Base, surface());
    pal.setColor(QPalette::AlternateBase, QColor(0x0d, 0x20, 0x3d));
    pal.setColor(QPalette::Text, text());
    pal.setColor(QPalette::Button, button);
    pal.setColor(QPalette::ButtonText, text());
    pal.setColor(QPalette::BrightText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, button);
    pal.setColor(QPalette::ToolTipText, text());
    // Dark selection: the coloured status text must stay readable on it.
    pal.setColor(QPalette::Highlight, QColor(0x1e, 0x4a, 0x85));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::PlaceholderText, dimText());
    pal.setColor(QPalette::Link, runningColor());
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        pal.setColor(QPalette::Disabled, role, dimText());
    app.setPalette(pal);

    // A normal UI font, easier to read than monospace for labels and
    // summaries; raw output keeps monoFont() (MainWindow's details pane).
    QFont font;
    font.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans"),
                      QStringLiteral("Liberation Sans")});
    font.setStyleHint(QFont::SansSerif);
    font.setPointSize(10);
    app.setFont(font);

    // What the palette cannot say: borders, hover, the gold START, tick boxes.
    app.setStyleSheet(QStringLiteral(
        "QPushButton { border: 1px solid %1; border-radius: 6px; padding: 6px 14px; }"
        "QPushButton:hover { background: #1e4a85; }"
        "QPushButton:pressed { background: %1; color: #ffffff; }"
        "QPushButton:disabled { border-color: %2; }"
        // START = the djinn's lamp: gold, the one button to press.
        "QPushButton#startButton { font-size: 16pt; font-weight: bold; background: %3; color: %4;"
        "  border: 2px solid #d9770f; }"
        "QPushButton#startButton:hover { background: #f0b93a; }"
        "QPushButton#startButton:disabled { background: #5a4a22; color: #2a2a2a; border-color: #5a4a22; }"
        "QLabel#pcInfo { border: 1px solid %1; border-radius: 6px; padding: 6px; font-size: 11pt;"
        "  background: #10264a; }"
        "QComboBox QAbstractItemView { border: 1px solid %1; }"
        "QTableWidget, QPlainTextEdit { border: 1px solid %2; border-radius: 4px; gridline-color: #17305a; }"
        "QHeaderView::section { background: #10264a; color: #a9e4ff; border: none;"
        "  border-bottom: 1px solid %1; padding: 4px; font-weight: bold; }"
        // Tick boxes of the test list: the palette's empty box was invisible
        // on the dark table. Ticked = filled.
        "QTableWidget::indicator { width: 12px; height: 12px; border: 1px solid #5fb8ff; border-radius: 2px;"
        "  background: %5; }"
        "QTableWidget::indicator:checked { background: #5fb8ff; }"
        // Microphone level bar: the palette's Highlight is too dark here.
        "QProgressBar { border: 1px solid %2; background: %5; }"
        "QProgressBar::chunk { background: #5fb8ff; }")
                          .arg(QStringLiteral("#1e6fd9"), QStringLiteral("#24406a"), QStringLiteral("#e0a526"),
                               background().name(), surface().name()));
}

} // namespace Theme
