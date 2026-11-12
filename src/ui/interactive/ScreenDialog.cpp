#include "ui/interactive/ScreenDialog.h"

#include <functional>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDevice>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QPointingDevice>
#include <QScreen>
#include <QStringList>
#include <QTimer>
#include <QTouchEvent>
#include <QVBoxLayout>

#include "tests/screen/ScreenCheck.h"
#include "tests/screen/TouchCheck.h"
#include "ui/Theme.h"

// The full-screen colour, a top-level window with no Qt parent. It used to be a
// child window of the dialog, but GNOME (Wayland) keeps a modal dialog above
// everything of its application, so the dialog stayed in front of the colours.
// Now the dialog hides itself while the colours are shown (which also lifts its
// modality, so this window gets the keys) and comes back afterwards.
// No text and no cursor on it: they would hide the very pixels being checked.
class PatternWindow : public QWidget {
public:
    explicit PatternWindow(std::function<void(bool)> closed)
        : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint), m_closed(std::move(closed))
    {
        setCursor(Qt::BlankCursor);
    }

    void start(QScreen* screen)
    {
        m_index = 0;
        if (screen)
            setGeometry(screen->geometry());   // full screen on THAT screen
        showFullScreen();
        raise();
        activateWindow();
        setFocus();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const ScreenPattern& p = screenPatterns()[size_t(m_index)];
        QPainter(this).fillRect(rect(), QColor(p.red, p.green, p.blue));
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->isAutoRepeat())   // a key held down must not race through the colours
            return;
        switch (e->key()) {
        case Qt::Key_Space: case Qt::Key_Right: case Qt::Key_Down: case Qt::Key_PageDown:
        case Qt::Key_Return: case Qt::Key_Enter:
            step(+1);
            break;
        case Qt::Key_Left: case Qt::Key_Up: case Qt::Key_PageUp: case Qt::Key_Backspace:
            step(-1);
            break;
        case Qt::Key_Escape:
            leave(false);
            break;
        default:
            break;
        }
    }

    // A tap on a touch screen (Surface) comes as a left click.
    void mousePressEvent(QMouseEvent* e) override { step(e->button() == Qt::RightButton ? -1 : +1); }

    // Alt+F4: same as Esc.
    void closeEvent(QCloseEvent* e) override
    {
        e->accept();
        m_closed(false);
    }

private:
    void step(int delta)
    {
        const int next = m_index + delta;
        if (next < 0)
            return;
        if (next >= int(screenPatterns().size())) {
            leave(true);
            return;
        }
        m_index = next;
        update();
    }

    void leave(bool seenAll)
    {
        hide();
        m_closed(seenAll);
    }

    std::function<void(bool)> m_closed;
    int m_index = 0;
};

// The touch grid, full screen, same top-level way as PatternWindow. First
// kGhostWaitMs with a countdown: nobody touches, any finger then is a ghost
// touch. Then the grid: every square a finger reaches turns green.
// Only real touch screen events count: Qt also turns touches into mouse
// clicks, and a mouse or touchpad must not pass this test.
class TouchWindow : public QWidget {
public:
    // done(true): every square touched. done(false): Esc, Alt+F4, or no new
    // square for kTouchIdleMs (a tablet without keyboard has no Esc).
    explicit TouchWindow(std::function<void(bool)> done)
        : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint), m_done(std::move(done))
    {
        setAttribute(Qt::WA_AcceptTouchEvents);
        m_tick.setInterval(1000);
        connect(&m_tick, &QTimer::timeout, this, [this] {
            if (--m_secondsLeft > 0) {
                update();
                return;
            }
            m_tick.stop();
            m_phase = Phase::Grid;
            m_idle.start();   // nothing at all touched: also ends after kTouchIdleMs
            update();
        });
        m_idle.setSingleShot(true);
        m_idle.setInterval(kTouchIdleMs);
        connect(&m_idle, &QTimer::timeout, this, [this] { leave(false); });
    }

    void start(QScreen* screen, const QString& device)
    {
        const QSizeF mm = screen ? screen->physicalSize() : QSizeF();
        m_run = TouchRun{TouchGrid::forScreen(mm.width(), mm.height()), 0, 0, device};
        m_phase = Phase::Waiting;
        m_secondsLeft = kGhostWaitMs / 1000;
        if (screen)
            setGeometry(screen->geometry());
        showFullScreen();
        raise();
        activateWindow();
        setFocus();
        m_tick.start();
        update();
    }

    const TouchRun& run() const { return m_run; }

    void stop()   // the dialog finished: no callback
    {
        m_phase = Phase::Off;
        m_tick.stop();
        m_idle.stop();
        hide();
    }

protected:
    bool event(QEvent* e) override
    {
        switch (e->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
            onTouch(*static_cast<QTouchEvent*>(e));
            return true;   // accepted: Qt makes no mouse click from it
        default:
            return QWidget::event(e);
        }
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), Theme::background());
        QFont big = font();
        big.setPointSizeF(big.pointSizeF() * 2.5);
        big.setBold(true);
        p.setFont(big);
        if (m_phase == Phase::Waiting) {
            p.setPen(Theme::statusColor(Status::Warn));
            p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap,
                       QCoreApplication::translate("ScreenDialog", "Do not touch the screen... %1").arg(m_secondsLeft));
            return;
        }
        const TouchGrid& g = m_run.grid;
        const double w = double(width()) / g.cols();
        const double h = double(height()) / g.rows();
        for (int r = 0; r < g.rows(); ++r) {
            for (int c = 0; c < g.cols(); ++c) {
                const QRectF cell(c * w, r * h, w, h);
                if (g.isTouched(c, r))
                    p.fillRect(cell, Theme::statusColor(Status::Pass));
                p.setPen(Theme::dimText());
                p.drawRect(cell);
            }
        }
        if (g.touchedCount() == 0) {   // the hint goes away with the first square
            p.setPen(Theme::text());
            p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap,
                       QCoreApplication::translate("ScreenDialog", "Swipe a finger over every square"));
        }
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Escape)
            leave(false);
    }

    // Alt+F4: same as Esc.
    void closeEvent(QCloseEvent* e) override
    {
        e->accept();
        if (m_phase != Phase::Off) {
            stop();
            m_done(false);
        }
    }

private:
    enum class Phase { Off, Waiting, Grid };

    void onTouch(const QTouchEvent& e)
    {
        // A touchpad can send touch events too (gestures): not the screen.
        if (!e.device() || e.device()->type() != QInputDevice::DeviceType::TouchScreen)
            return;
        int down = 0;
        bool changed = false;
        for (const QEventPoint& pt : e.points()) {
            if (pt.state() == QEventPoint::Released)
                continue;
            ++down;
            if (m_phase == Phase::Waiting) {
                if (pt.state() == QEventPoint::Pressed)
                    ++m_run.ghostPoints;
            } else if (m_phase == Phase::Grid
                       && m_run.grid.touch(pt.position().x() / width(), pt.position().y() / height())) {
                changed = true;
            }
        }
        m_run.maxPoints = std::max(m_run.maxPoints, down);
        if (!changed)
            return;
        m_idle.start();   // a new square: the 10 s start again
        update();
        if (m_run.grid.complete())
            leave(true);
    }

    void leave(bool complete)
    {
        if (m_phase == Phase::Off)
            return;
        stop();
        m_done(complete);
    }

    std::function<void(bool)> m_done;
    TouchRun m_run;
    Phase m_phase = Phase::Off;
    int m_secondsLeft = 0;
    QTimer m_tick;   // countdown before the grid
    QTimer m_idle;   // no new square for a while: back to the dialog
};

namespace {
// The touch screens the system sees, by name. Empty: no touch part.
QString touchScreens()
{
    QStringList names;
    for (const QInputDevice* d : QInputDevice::devices()) {
        if (d->type() == QInputDevice::DeviceType::TouchScreen)
            names << d->name();
    }
    return names.join(QStringLiteral(", "));
}
} // namespace

ScreenDialog::ScreenDialog(QScreen* screen, QWidget* parent)
    : QDialog(parent), m_screen(screen), m_touchDevice(touchScreens())
{
    setWindowTitle(tr("Screen test"));
    setAttribute(Qt::WA_DeleteOnClose);   // Qt frees the dialog (and its windows) when it closes
    setMinimumWidth(520);
    auto* top = new QVBoxLayout(this);

    // ---- Page 1: colours ----
    m_colourPage = new QWidget(this);
    auto* colours = new QVBoxLayout(m_colourPage);
    colours->setContentsMargins(0, 0, 0, 0);
    auto* help = new QLabel(tr("%1 plain colours will fill the whole screen, one after the other.\n"
                               "Look for dead or bright dots, stains, lines and darker zones.\n\n"
                               "Next colour: click, tap or Space. Back: left arrow. Stop: Esc.")
                                .arg(screenPatterns().size()),
                            m_colourPage);
    help->setWordWrap(true);
    colours->addWidget(help);

    m_show = new QPushButton(tr("Show the colours"), m_colourPage);
    m_show->setMinimumHeight(48);
    m_show->setDefault(true);   // Enter or Space starts: no mouse needed
    colours->addWidget(m_show);
    connect(m_show, &QPushButton::clicked, this, &ScreenDialog::showColours);

    m_prompt = new QLabel(m_colourPage);
    m_prompt->setStyleSheet(QStringLiteral("font-size:16px; font-weight:bold;"));
    m_prompt->setWordWrap(true);
    colours->addWidget(m_prompt);

    auto* grid = new QGridLayout;
    const struct { QString text; ScreenAnswer answer; int row, col; } choices[] = {
        {tr("No defect"), ScreenAnswer::Ok, 0, 0},
        {tr("Small defect\n(dead pixel, light stain)"), ScreenAnswer::SmallDefect, 0, 1},
        {tr("Broken\n(lines, dead zone, big stain, flicker)"), ScreenAnswer::Broken, 0, 2},
    };
    for (const auto& c : choices) {
        auto* b = new QPushButton(c.text, m_colourPage);
        b->setMinimumHeight(56);
        b->setAutoDefault(false);
        b->setEnabled(false);   // not before every colour was seen
        grid->addWidget(b, c.row, c.col);
        m_answers << b;
        const ScreenAnswer answer = c.answer;
        connect(b, &QPushButton::clicked, this, [this, answer] { screenAnswered(screenResult(answer)); });
    }
    colours->addLayout(grid);

    auto* skip = new QPushButton(tr("Skip"), m_colourPage);
    skip->setAutoDefault(false);
    auto* skipRow = new QHBoxLayout;
    skipRow->addStretch();
    skipRow->addWidget(skip);
    colours->addLayout(skipRow);
    connect(skip, &QPushButton::clicked, this, [this] { finish(screenSkipped()); });
    top->addWidget(m_colourPage);

    // ---- Page 2: touch (only with a touch screen) ----
    m_touchPage = new QWidget(this);
    auto* touch = new QVBoxLayout(m_touchPage);
    touch->setContentsMargins(0, 0, 0, 0);
    auto* touchHelp = new QLabel(tr("Touch screen. After Start, do NOT touch the screen for %1 s (check for touches "
                                    "nobody made). Then swipe a finger over every square until all are green.\n\n"
                                    "At the edges, tap the squares: a swipe from outside the screen opens system "
                                    "menus.\nStop: Esc, or touch no new square for %2 s.")
                                     .arg(kGhostWaitMs / 1000)
                                     .arg(kTouchIdleMs / 1000),
                                 m_touchPage);
    touchHelp->setWordWrap(true);
    touch->addWidget(touchHelp);

    m_touchStart = new QPushButton(tr("Start the touch test"), m_touchPage);
    m_touchStart->setMinimumHeight(48);
    touch->addWidget(m_touchStart);
    connect(m_touchStart, &QPushButton::clicked, this, &ScreenDialog::showTouch);

    m_touchPrompt = new QLabel(m_touchPage);
    m_touchPrompt->setWordWrap(true);
    m_touchPrompt->setStyleSheet(QStringLiteral("font-weight:bold; color:%1;").arg(Theme::statusColor(Status::Warn).name()));
    m_touchPrompt->hide();
    touch->addWidget(m_touchPrompt);

    m_touchBroken = new QPushButton(tr("Touch does not work properly"), m_touchPage);
    auto* touchSkip = new QPushButton(tr("Skip (touch)"), m_touchPage);
    auto* touchRow = new QHBoxLayout;
    for (QPushButton* b : {m_touchBroken, touchSkip}) {
        b->setAutoDefault(false);
        b->setMinimumHeight(40);
        touchRow->addWidget(b);
    }
    m_touchBroken->setEnabled(false);   // not before one try
    touch->addLayout(touchRow);
    connect(m_touchBroken, &QPushButton::clicked, this,
            [this] { finish(touchAnswer(int(TouchOutcome::ZoneDead))); });
    connect(touchSkip, &QPushButton::clicked, this, [this] { finish(touchAnswer(int(TouchOutcome::Skipped))); });
    m_touchPage->hide();
    top->addWidget(m_touchPage);

    // On both pages. The runner stops first, so the "skipped" answer of
    // close() is ignored.
    auto* stop = new QPushButton(tr("Stop all tests"), this);
    stop->setAutoDefault(false);
    auto* stopRow = new QHBoxLayout;
    stopRow->addStretch();
    stopRow->addWidget(stop);
    top->addLayout(stopRow);
    connect(stop, &QPushButton::clicked, this, [this] {
        emit stopAll();
        close();
    });

    m_patterns = std::make_unique<PatternWindow>([this](bool seenAll) { coloursClosed(seenAll); });
    m_touch = std::make_unique<TouchWindow>([this](bool complete) { touchClosed(complete); });
}

ScreenDialog::~ScreenDialog() = default;

void ScreenDialog::showColours()
{
    if (m_done)
        return;
    hide();   // hide, not close: the dialog stays alive and waits for the answer
    m_patterns->start(m_screen);
}

void ScreenDialog::coloursClosed(bool seenAll)
{
    if (m_done)   // the colour window also closes when the dialog does
        return;
    show();   // back, modal again
    raise();
    activateWindow();
    if (seenAll) {
        m_prompt->setText(tr("What did you see?"));
        for (QPushButton* b : m_answers)
            b->setEnabled(true);
        m_show->setText(tr("See again"));
        m_show->setDefault(false);   // Enter must not restart by accident
        m_show->setAutoDefault(false);
    } else if (!m_answers.first()->isEnabled()) {
        m_prompt->setText(tr("Stopped before the last colour: show them again, or Skip."));
    }
}

void ScreenDialog::screenAnswered(const TestResult& screen)
{
    if (m_touchDevice.isEmpty()) {   // no touch screen: the row is the colours alone
        finish(screen);
        return;
    }
    m_screenResult = screen;
    m_onTouchPage = true;
    m_colourPage->hide();
    m_touchPage->show();
    m_touchStart->setDefault(true);
    m_touchStart->setFocus();
}

void ScreenDialog::showTouch()
{
    if (m_done)
        return;
    hide();   // as for the colours
    m_touch->start(m_screen, m_touchDevice);
}

void ScreenDialog::touchClosed(bool complete)
{
    if (m_done)
        return;
    const TouchRun& run = m_touch->run();
    if (complete && run.ghostPoints == 0) {   // the normal case: no question
        finish(touchAnswer(int(TouchOutcome::AllTouched)));
        return;
    }
    show();
    raise();
    activateWindow();
    m_touchBroken->setEnabled(true);
    m_touchStart->setText(tr("Again"));
    if (run.ghostPoints > 0)
        m_touchPrompt->setText(tr("The screen reported %1 touch(es) while nobody should touch it (ghost touches). "
                                  "If a hand touched it, press Again; if not, the touch screen is faulty.")
                                   .arg(run.ghostPoints));
    else
        m_touchPrompt->setText(tr("%1 of %2 squares touched. A square that never turns green is a dead zone: "
                                  "press Again to retry, or report it.")
                                   .arg(run.grid.touchedCount())
                                   .arg(run.grid.cellCount()));
    m_touchPrompt->show();
}

TestResult ScreenDialog::touchAnswer(int outcome) const
{
    return screenWithTouch(m_screenResult, touchResult(TouchOutcome(outcome), m_touch->run()));
}

void ScreenDialog::closeEvent(QCloseEvent* e)
{
    // Window closed with the title bar X: the test still needs an answer.
    // On the touch page the colours were judged: keep that answer.
    if (!m_done)
        finish(m_onTouchPage ? touchAnswer(int(TouchOutcome::Skipped)) : screenSkipped());
    e->accept();
}

void ScreenDialog::finish(TestResult result)
{
    if (m_done)
        return;
    m_done = true;
    m_patterns->hide();
    m_touch->stop();
    if (m_screen)
        result.details += QStringLiteral("\nScreen: %1, %2x%3 (logical pixels)")
                              .arg(m_screen->name())
                              .arg(m_screen->geometry().width())
                              .arg(m_screen->geometry().height());
    emit outcome(result);
    close();
}
