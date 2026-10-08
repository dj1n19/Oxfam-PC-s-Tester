#include "ui/interactive/AudioDialog.h"

#include <QAudioSink>
#include <QCloseEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int kToneMs = 750;
constexpr int kLeftHz = 440;    // two different pitches: easier to tell the
constexpr int kRightHz = 660;   // two tones apart if the technician replays
}

AudioDialog::AudioDialog(const QAudioDevice& device, const QAudioFormat& format, QWidget* parent)
    : QDialog(parent), m_device(device), m_format(format)
{
    setWindowTitle(tr("Audio test"));
    setAttribute(Qt::WA_DeleteOnClose);   // Qt frees the dialog when it closes
    setMinimumWidth(480);

    auto* top = new QVBoxLayout(this);
    auto* help = new QLabel(tr("Turn the volume up. A short tone plays on ONE side only.\n"
                               "Where did you hear it?"), this);
    help->setWordWrap(true);
    top->addWidget(help);
    m_prompt = new QLabel(this);
    m_prompt->setStyleSheet(QStringLiteral("font-size:16px; font-weight:bold;"));
    m_prompt->setText(tr("Tone 1 of 2"));
    top->addWidget(m_prompt);

    auto* grid = new QGridLayout;
    const struct { QString text; Heard heard; int row, col; } choices[] = {
        {tr("Left"), Heard::Left, 0, 0},
        {tr("Right"), Heard::Right, 0, 1},
        {tr("Both sides"), Heard::Both, 1, 0},
        {tr("Nothing"), Heard::Nothing, 1, 1},
    };
    for (const auto& c : choices) {
        auto* b = new QPushButton(c.text, this);
        b->setMinimumHeight(48);
        b->setAutoDefault(false);   // Enter must not answer "Left" by accident
        grid->addWidget(b, c.row, c.col);
        const Heard heard = c.heard;
        connect(b, &QPushButton::clicked, this, [this, heard] { answer(heard); });
    }
    top->addLayout(grid);

    auto* again = new QPushButton(tr("Play again"), this);
    auto* skip = new QPushButton(tr("Skip (no speakers)"), this);
    again->setAutoDefault(false);
    skip->setAutoDefault(false);
    auto* row = new QHBoxLayout;
    row->addWidget(again);
    row->addWidget(skip);
    top->addLayout(row);
    connect(again, &QPushButton::clicked, this, &AudioDialog::play);
    connect(skip, &QPushButton::clicked, this, [this] { finish(audioSkipped()); });

    m_sink = std::make_unique<QAudioSink>(m_device, m_format);
    // Small delay so the window is visible before the first tone.
    QTimer::singleShot(400, this, &AudioDialog::play);
}

AudioDialog::~AudioDialog()
{
    if (m_sink)
        m_sink->stop();   // the sink reads m_buffer: stop it before the buffer dies
}

void AudioDialog::play()
{
    if (m_done)
        return;
    // The side is not shown: the technician must not know it in advance.
    m_prompt->setText(tr("Tone %1 of 2").arg(m_step + 1));

    m_sink->stop();
    m_buffer.close();
    const Channel ch = m_step == 0 ? Channel::Left : Channel::Right;
    m_buffer.setData(makeTone(ch, m_format.sampleRate(), m_step == 0 ? kLeftHz : kRightHz, kToneMs));
    m_buffer.open(QIODevice::ReadOnly);
    m_sink->start(&m_buffer);

    if (m_sink->error() != QAudio::NoError) {
        TestResult r;
        r.status = Status::Error;
        r.summary = QStringLiteral("Could not play sound");
        r.details = QStringLiteral("QAudioSink error %1 on device '%2'")
                        .arg(int(m_sink->error())).arg(m_device.description());
        finish(r);
    }
}

void AudioDialog::answer(Heard heard)
{
    if (m_step == 0) {
        m_leftAnswer = heard;
        m_step = 1;
        play();
        return;
    }
    TestResult r = audioResult(m_leftAnswer, heard);
    r.details += QStringLiteral("\nDevice: %1").arg(m_device.description());
    finish(r);
}

void AudioDialog::closeEvent(QCloseEvent* e)
{
    // Window closed with the title bar X: the test still needs an answer.
    if (!m_done)
        finish(audioSkipped());
    e->accept();
}

void AudioDialog::finish(const TestResult& result)
{
    if (m_done)
        return;
    m_done = true;
    m_sink->stop();
    emit outcome(result);
    close();
}
