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

QColor background() { return QColor(0x0a, 0x0f, 0x0a); }
QColor surface()    { return QColor(0x05, 0x08, 0x05); }
QColor text()       { return QColor(0x33, 0xff, 0x66); }
QColor dimText()    { return QColor(0x2e, 0x7d, 0x45); }

QColor statusColor(Status s)
{
    switch (s) {
    case Status::Pass:    return text();
    case Status::Warn:    return QColor(0xff, 0xc4, 0x00);   // amber
    case Status::Fail:    return QColor(0xff, 0x4d, 0x4d);   // red
    case Status::Error:   return QColor(0xe0, 0x70, 0xff);   // magenta: "could not test", not "bad"
    case Status::Skipped: return QColor(0x80, 0x90, 0x80);   // grey
    }
    return text();
}

QColor runningColor() { return QColor(0x4d, 0xd0, 0xe1); }   // cyan
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

void apply(QApplication& app)
{
    // Fusion draws every widget from the palette below, the same way on
    // every OS. The native Windows style would ignore part of it.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette pal;
    const QColor button(0x0f, 0x1f, 0x12);
    pal.setColor(QPalette::Window, background());
    pal.setColor(QPalette::WindowText, text());
    pal.setColor(QPalette::Base, surface());
    pal.setColor(QPalette::AlternateBase, QColor(0x0c, 0x14, 0x0c));
    pal.setColor(QPalette::Text, text());
    pal.setColor(QPalette::Button, button);
    pal.setColor(QPalette::ButtonText, text());
    pal.setColor(QPalette::BrightText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, button);
    pal.setColor(QPalette::ToolTipText, text());
    // Dark selection: the coloured status text must stay readable on it.
    pal.setColor(QPalette::Highlight, QColor(0x13, 0x3d, 0x1e));
    pal.setColor(QPalette::HighlightedText, QColor(0xc8, 0xff, 0xd4));
    pal.setColor(QPalette::PlaceholderText, dimText());
    pal.setColor(QPalette::Link, runningColor());
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        pal.setColor(QPalette::Disabled, role, dimText());
    app.setPalette(pal);

    // Monospace like a terminal. A list of fonts that already exist on the
    // PCs (no font file to ship): Windows has Consolas, Linux Mint DejaVu.
    QFont font;
    font.setFamilies({QStringLiteral("Consolas"), QStringLiteral("DejaVu Sans Mono"),
                      QStringLiteral("Liberation Mono"), QStringLiteral("Ubuntu Mono"),
                      QStringLiteral("Noto Sans Mono"), QStringLiteral("Courier New")});
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(10);
    app.setFont(font);

    // What the palette cannot say: green borders, hover, a bigger START.
    app.setStyleSheet(QStringLiteral(
        "QPushButton { border: 1px solid %1; border-radius: 2px; padding: 6px 14px; }"
        "QPushButton:hover { background: #163a1d; }"
        "QPushButton:pressed { background: %1; color: #000000; }"
        "QPushButton:disabled { border-color: %2; }"
        "QPushButton#startButton { font-size: 16pt; font-weight: bold; }"
        "QLabel#pcInfo { border: 1px solid %2; padding: 6px; font-size: 11pt; }"
        "QComboBox QAbstractItemView { border: 1px solid %1; }"
        "QTableWidget, QPlainTextEdit { border: 1px solid %2; gridline-color: #13301a; }"
        "QHeaderView::section { background: #0f1a10; color: %1; border: none;"
        "  border-bottom: 1px solid %2; padding: 4px; font-weight: bold; }")
                          .arg(text().name(), dimText().name()));
}

} // namespace Theme
