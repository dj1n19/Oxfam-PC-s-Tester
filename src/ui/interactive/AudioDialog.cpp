#include "ui/interactive/AudioDialog.h"

#include <QAudioSink>
#include <QAudioSource>
#include <QCloseEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "ui/Theme.h"

namespace {
constexpr int kToneMs = 750;
constexpr int kLeftHz = 440;    // two different pitches: easier to tell the
constexpr int kRightHz = 660;   // two tones apart if the technician replays
constexpr int kMicHelpMs = 8000;   // nothing heard by then: say what to check

// The microphone format: mono 48 kHz, 16-bit, else float; else what the
// device prefers if it is one of those two. An invalid format = refused.
QAudioFormat micFormat(const QAudioDevice& device)
{
    QAudioFormat f;
    f.setSampleRate(48000);
    f.setChannelCount(1);
    for (QAudioFormat::SampleFormat s : {QAudioFormat::Int16, QAudioFormat::Float}) {
        f.setSampleFormat(s);
        if (device.isFormatSupported(f))
            return f;
    }
    const QAudioFormat pref = device.preferredFormat();
    if (pref.sampleFormat() == QAudioFormat::Int16 || pref.sampleFormat() == QAudioFormat::Float)
        return pref;
    return {};
}
} // namespace

AudioDialog::AudioDialog(const QAudioDevice& device, const QAudioFormat& format, QWidget* parent)
    : QDialog(parent), m_device(device), m_format(format)
{
    setWindowTitle(tr("Audio test"));
    setAttribute(Qt::WA_DeleteOnClose);   // Qt frees the dialog when it closes
    setMinimumWidth(480);

    auto* top = new QVBoxLayout(this);

    // ---- Step 1: speakers ----
    m_speakerPage = new QWidget(this);
    auto* speakers = new QVBoxLayout(m_speakerPage);
    speakers->setContentsMargins(0, 0, 0, 0);
    auto* help = new QLabel(tr("Turn the volume up. A short tone plays on ONE side only.\n"
                               "Where did you hear it?"), m_speakerPage);
    help->setWordWrap(true);
    speakers->addWidget(help);
    m_prompt = new QLabel(m_speakerPage);
    m_prompt->setStyleSheet(QStringLiteral("font-size:16px; font-weight:bold;"));
    m_prompt->setText(tr("Tone 1 of 2"));
    speakers->addWidget(m_prompt);

    auto* grid = new QGridLayout;
    const struct { QString text; Heard heard; int row, col; } choices[] = {
        {tr("Left"), Heard::Left, 0, 0},
        {tr("Right"), Heard::Right, 0, 1},
        {tr("Both sides"), Heard::Both, 1, 0},
        {tr("Nothing"), Heard::Nothing, 1, 1},
    };
    for (const auto& c : choices) {
        auto* b = new QPushButton(c.text, m_speakerPage);
        b->setMinimumHeight(48);
        b->setAutoDefault(false);   // Enter must not answer "Left" by accident
        grid->addWidget(b, c.row, c.col);
        const Heard heard = c.heard;
        connect(b, &QPushButton::clicked, this, [this, heard] { answer(heard); });
    }
    speakers->addLayout(grid);

    auto* again = new QPushButton(tr("Play again"), m_speakerPage);
    auto* skip = new QPushButton(tr("Skip (no speakers)"), m_speakerPage);
    again->setAutoDefault(false);
    skip->setAutoDefault(false);
    auto* row = new QHBoxLayout;
    row->addWidget(again);
    row->addWidget(skip);
    speakers->addLayout(row);
    connect(again, &QPushButton::clicked, this, &AudioDialog::play);
    connect(skip, &QPushButton::clicked, this, [this] { speakersDone(audioSkipped()); });
    top->addWidget(m_speakerPage);

    // ---- Step 2: microphone (shown after the speakers) ----
    m_micPage = new QWidget(this);
    auto* mic = new QVBoxLayout(m_micPage);
    mic->setContentsMargins(0, 0, 0, 0);
    auto* micHelp = new QLabel(tr("Microphone: speak or clap your hands near the PC.\n"
                                  "The test ends by itself when a sound is heard."), m_micPage);
    micHelp->setWordWrap(true);
    mic->addWidget(micHelp);
    m_level = new QProgressBar(m_micPage);
    m_level->setRange(0, 60);   // -60..0 dBFS: quiet mics still move the bar
    m_level->setTextVisible(false);
    m_level->setMinimumHeight(28);
    mic->addWidget(m_level);
    m_micPrompt = new QLabel(m_micPage);
    m_micPrompt->setWordWrap(true);
    m_micPrompt->setStyleSheet(QStringLiteral("font-weight:bold; color:%1;").arg(Theme::statusColor(Status::Warn).name()));
    m_micPrompt->hide();
    mic->addWidget(m_micPrompt);
    auto* micBroken = new QPushButton(tr("Microphone does not work"), m_micPage);
    auto* micSkip = new QPushButton(tr("Skip"), m_micPage);
    auto* micRow = new QHBoxLayout;
    for (QPushButton* b : {micBroken, micSkip}) {
        b->setAutoDefault(false);
        b->setMinimumHeight(40);
        micRow->addWidget(b);
    }
    mic->addLayout(micRow);
    connect(micBroken, &QPushButton::clicked, this, [this] { micDone(MicOutcome::Broken); });
    connect(micSkip, &QPushButton::clicked, this, [this] { micDone(MicOutcome::Skipped); });
    m_micPage->hide();
    top->addWidget(m_micPage);

    m_sink = std::make_unique<QAudioSink>(m_device, m_format);
    // Small delay so the window is visible before the first tone.
    QTimer::singleShot(400, this, &AudioDialog::play);
}

AudioDialog::~AudioDialog()
{
    if (m_sink)
        m_sink->stop();   // the sink reads m_buffer: stop it before the buffer dies
    if (m_source)
        m_source->stop();
}

void AudioDialog::play()
{
    if (m_speakersDone)
        return;
    // The side is not shown: the technician must not know it in advance.
    m_prompt->setText(tr("Tone %1 of 2").arg(m_step + 1));

    m_sink->stop();
    m_buffer.close();
    const Channel ch = m_step == 0 ? Channel::Left : Channel::Right;
    const SampleType type = m_format.sampleFormat() == QAudioFormat::Float ? SampleType::Float : SampleType::Int16;
    m_buffer.setData(makeTone(ch, m_format.sampleRate(), m_step == 0 ? kLeftHz : kRightHz, kToneMs, type));
    m_buffer.open(QIODevice::ReadOnly);
    m_sink->start(&m_buffer);

    if (m_sink->error() != QAudio::NoError) {
        TestResult r;
        r.status = Status::Error;
        r.summary = tr("Could not play the sound (see details)");
        r.details = QStringLiteral("QAudioSink error %1 on device '%2'")
                        .arg(int(m_sink->error())).arg(m_device.description());
        speakersDone(r);
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
    speakersDone(r);
}

void AudioDialog::speakersDone(const TestResult& speakers)
{
    if (m_speakersDone)
        return;
    m_speakersDone = true;
    m_speakers = speakers;
    m_sink->stop();
    m_speakerPage->hide();
    m_micPage->show();
    startMic();
}

void AudioDialog::startMic()
{
    m_micDevice = QMediaDevices::defaultAudioInput();
    if (m_micDevice.isNull()) {
        micDone(MicOutcome::NoDevice);
        return;
    }
    m_micFormat = micFormat(m_micDevice);
    if (!m_micFormat.isValid()) {
        const QAudioFormat pref = m_micDevice.preferredFormat();
        micDone(MicOutcome::Error,
                QStringLiteral("No usable format (48 kHz mono 16-bit or float refused; preferred: %1 Hz, %2 channel(s), "
                               "sample format %3)")
                    .arg(pref.sampleRate()).arg(pref.channelCount()).arg(int(pref.sampleFormat())));
        return;
    }
    m_source = std::make_unique<QAudioSource>(m_micDevice, m_micFormat);
    m_micIo = m_source->start();   // "pull" mode: we read what arrived
    if (!m_micIo || m_source->error() != QAudio::NoError) {
        micDone(MicOutcome::Error, QStringLiteral("QAudioSource error %1").arg(int(m_source->error())));
        return;
    }
    connect(m_micIo, &QIODevice::readyRead, this, &AudioDialog::readMic);
    m_micClock.start();
    QTimer::singleShot(kMicHelpMs, this, [this] {
        if (m_done)
            return;
        // Windows gives silence, not an error, when desktop apps may not use
        // the microphone.
        m_micPrompt->setText(tr("No sound heard yet. Check that the microphone is not muted (key or Settings > "
                                "Privacy > Microphone). If the bar never moves, click \"Microphone does not work\"."));
        m_micPrompt->show();
    });
}

void AudioDialog::readMic()
{
    if (m_done || !m_micIo)
        return;
    const QByteArray chunk = m_micIo->readAll();
    const int bytesPerSecond = m_micFormat.bytesForDuration(1'000'000);
    if (chunk.isEmpty() || bytesPerSecond <= 0)
        return;
    const double peak = peakLevel(chunk, m_micFormat.sampleFormat() == QAudioFormat::Float ? SampleType::Float
                                                                                          : SampleType::Int16);
    m_tracker.add(peak, int(qint64(chunk.size()) * 1000 / bytesPerSecond));
    m_level->setValue(int(60 + toDbfs(peak)));   // -60 dBFS or less = empty bar
    if (m_tracker.heard())
        micDone(MicOutcome::Heard);
}

void AudioDialog::micDone(MicOutcome mic, const QString& error)
{
    if (m_done)
        return;
    m_done = true;
    if (m_source)
        m_source->stop();
    const QString name = m_micDevice.isNull() ? QString() : m_micDevice.description();
    emit outcome(audioWithMic(m_speakers, micResult(mic, m_tracker, name, error)));
    close();
}

void AudioDialog::closeEvent(QCloseEvent* e)
{
    // Window closed with the title bar X: the test still needs an answer.
    if (!m_done) {
        if (!m_speakersDone) {
            m_speakersDone = true;
            m_speakers = audioSkipped();
        }
        micDone(MicOutcome::Skipped);
    }
    e->accept();
}
