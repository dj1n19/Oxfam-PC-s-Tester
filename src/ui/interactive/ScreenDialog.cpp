#include "ui/interactive/ScreenDialog.h"

#include <functional>
#include <QCloseEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

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

ScreenDialog::ScreenDialog(QScreen* screen, QWidget* parent) : QDialog(parent), m_screen(screen)
{
    setWindowTitle(tr("Screen test"));
    setAttribute(Qt::WA_DeleteOnClose);   // Qt frees the dialog (and the colour window) when it closes
    setMinimumWidth(520);

    auto* top = new QVBoxLayout(this);
    auto* help = new QLabel(tr("%1 plain colours will fill the whole screen, one after the other.\n"
                               "Look for dead or bright dots, stains, lines and darker zones.\n\n"
                               "Next colour: click, tap or Space. Back: left arrow. Stop: Esc.")
                                .arg(screenPatterns().size()),
                            this);
    help->setWordWrap(true);
    top->addWidget(help);

    m_show = new QPushButton(tr("Show the colours"), this);
    m_show->setMinimumHeight(48);
    m_show->setDefault(true);   // Enter or Space starts: no mouse needed
    top->addWidget(m_show);
    connect(m_show, &QPushButton::clicked, this, &ScreenDialog::showColours);

    m_prompt = new QLabel(this);
    m_prompt->setStyleSheet(QStringLiteral("font-size:16px; font-weight:bold;"));
    m_prompt->setWordWrap(true);
    top->addWidget(m_prompt);

    auto* grid = new QGridLayout;
    const struct { QString text; ScreenAnswer answer; int row, col; } choices[] = {
        {tr("No defect"), ScreenAnswer::Ok, 0, 0},
        {tr("Small defect\n(dead pixel, light stain)"), ScreenAnswer::SmallDefect, 0, 1},
        {tr("Broken\n(lines, dead zone, big stain, flicker)"), ScreenAnswer::Broken, 0, 2},
    };
    for (const auto& c : choices) {
        auto* b = new QPushButton(c.text, this);
        b->setMinimumHeight(56);
        b->setAutoDefault(false);
        b->setEnabled(false);   // not before every colour was seen
        grid->addWidget(b, c.row, c.col);
        m_answers << b;
        const ScreenAnswer answer = c.answer;
        connect(b, &QPushButton::clicked, this, [this, answer] { finish(screenResult(answer)); });
    }
    top->addLayout(grid);

    auto* skip = new QPushButton(tr("Skip"), this);
    skip->setAutoDefault(false);
    auto* stop = new QPushButton(tr("Stop all tests"), this);
    stop->setAutoDefault(false);
    auto* row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(skip);
    row->addWidget(stop);
    top->addLayout(row);
    connect(skip, &QPushButton::clicked, this, [this] { finish(screenSkipped()); });
    // The runner stops first, so the "skipped" answer of close() is ignored.
    connect(stop, &QPushButton::clicked, this, [this] {
        emit stopAll();
        close();
    });

    m_patterns = std::make_unique<PatternWindow>([this](bool seenAll) { coloursClosed(seenAll); });
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

void ScreenDialog::closeEvent(QCloseEvent* e)
{
    // Window closed with the title bar X: the test still needs an answer.
    if (!m_done)
        finish(screenSkipped());
    e->accept();
}

void ScreenDialog::finish(TestResult result)
{
    if (m_done)
        return;
    m_done = true;
    m_patterns->hide();
    if (m_screen)
        result.details += QStringLiteral("\nScreen: %1, %2x%3 (logical pixels)")
                              .arg(m_screen->name())
                              .arg(m_screen->geometry().width())
                              .arg(m_screen->geometry().height());
    emit outcome(result);
    close();
}
