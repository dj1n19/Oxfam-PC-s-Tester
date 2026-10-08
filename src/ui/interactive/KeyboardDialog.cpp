#include "ui/interactive/KeyboardDialog.h"

#include <algorithm>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int kUnit = 52;      // pixels per key unit
constexpr int kMargin = 16;
constexpr int kSpacing = 4;

// Paints the layout; reads the tracker owned by the dialog.
class KeyboardView : public QWidget {
public:
    KeyboardView(const KeyLayout& layout, const KeyTracker& tracker, QWidget* parent)
        : QWidget(parent), m_layout(layout), m_tracker(tracker)
    {
        double widest = 0;
        for (const auto& row : m_layout.rows) {
            double w = 0;
            for (const Key& k : row)
                w += k.width;
            widest = std::max(widest, w);
        }
        setFixedSize(static_cast<int>(widest * kUnit) + 2 * kMargin,
                     static_cast<int>(m_layout.rows.size()) * kUnit + 2 * kMargin);
        setFocusPolicy(Qt::NoFocus);   // keys must reach the dialog
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        double y = kMargin;
        for (const auto& row : m_layout.rows) {
            double x = kMargin;
            for (const Key& k : row) {
                const double w = k.width * kUnit;
                if (k.code > 0) {
                    const QRectF r(x, y, w - kSpacing, kUnit - kSpacing);
                    p.setPen(QPen(QColor("#555555"), 1, k.required ? Qt::SolidLine : Qt::DashLine));
                    p.setBrush(m_tracker.isPressed(k.code) ? QColor("#81c784") : QColor("#ffffff"));
                    p.drawRoundedRect(r, 4, 4);
                    p.setPen(k.required ? QColor("#000000") : QColor("#777777"));
                    p.drawText(r, Qt::AlignCenter, k.label);
                }
                x += w;
            }
            y += kUnit;
        }
    }

private:
    const KeyLayout& m_layout;
    const KeyTracker& m_tracker;
};

} // namespace

KeyboardDialog::KeyboardDialog(const KeyLayout& layout, QWidget* parent)
    : QDialog(parent), m_layout(layout), m_tracker(layout)
{
    setWindowTitle(tr("Keyboard test"));
    setAttribute(Qt::WA_DeleteOnClose);   // Qt frees the dialog when it closes
    setFocusPolicy(Qt::StrongFocus);

    auto* top = new QVBoxLayout(this);
    auto* help = new QLabel(tr("Press every key once. Pressed keys turn green. "
                               "Dashed keys are optional (they may not exist on this keyboard). "
                               "Hold Fn if F1-F12 change brightness or volume instead."), this);
    help->setWordWrap(true);
    top->addWidget(help);
    m_status = new QLabel(this);
    m_status->setStyleSheet(QStringLiteral("font-size:16px; font-weight:bold;"));
    top->addWidget(m_status);

    m_view = new KeyboardView(m_layout, m_tracker, this);
    top->addWidget(m_view, 0, Qt::AlignHCenter);

    // NoFocus on the buttons: otherwise Space or Enter would "click" the
    // focused button instead of being tested.
    auto* buttons = new QHBoxLayout;
    auto* broken = new QPushButton(tr("A key does not work"), this);
    auto* skip = new QPushButton(tr("Skip (no keyboard)"), this);
    for (QPushButton* b : {broken, skip}) {
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumHeight(40);
        buttons->addWidget(b);
    }
    top->addLayout(buttons);
    connect(broken, &QPushButton::clicked, this, [this] { finish(KeyTracker::Outcome::KeyBroken); });
    connect(skip, &QPushButton::clicked, this, [this] { finish(KeyTracker::Outcome::Skipped); });

    updateStatus();
}

bool KeyboardDialog::event(QEvent* e)
{
    // Handled here rather than in keyPressEvent(): Qt uses Tab for focus and
    // Esc to close the dialog before keyPressEvent() would see them.
    // Release events count too: Windows sends Print Screen as a release only.
    if (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (!m_done && m_tracker.press(canonicalScanCode(ke->nativeScanCode()))) {
            m_view->update();
            updateStatus();
            if (m_tracker.allRequiredPressed())
                // Short pause so the technician sees the last key turn green.
                QTimer::singleShot(600, this, [this] { finish(KeyTracker::Outcome::AllPressed); });
        }
        return true;
    }
    if (e->type() == QEvent::ShortcutOverride) {   // Alt+letter, etc.: keep them for us
        e->accept();
        return true;
    }
    return QDialog::event(e);
}

void KeyboardDialog::closeEvent(QCloseEvent* e)
{
    // Window closed with the title bar X (or Alt+F4): the test still needs an answer.
    if (!m_done)
        finish(KeyTracker::Outcome::Skipped);
    e->accept();
}

void KeyboardDialog::finish(KeyTracker::Outcome o)
{
    if (m_done)
        return;
    m_done = true;
    emit outcome(m_tracker.result(o));
    close();
}

void KeyboardDialog::updateStatus()
{
    m_status->setText(tr("%1 / %2 keys done").arg(m_tracker.requiredPressedCount())
                                               .arg(m_tracker.requiredCount()));
}
