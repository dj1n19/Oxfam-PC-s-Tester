#include "tests/audio/AudioCheck.h"

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <QCoreApplication>
#include <QStringList>

namespace {

// One sample type at a time: fullScale is the largest value it can hold
// (32767 for 16-bit, 1.0 for float).
template <typename Sample>
QByteArray toneData(Channel channel, int sampleRate, int frequencyHz, int durationMs, double fullScale)
{
    const qsizetype frames = qsizetype(sampleRate) * durationMs / 1000;
    const qsizetype fadeFrames = sampleRate / 50;   // 20 ms
    const double amplitude = 0.5 * fullScale;       // half scale: loud enough, no clipping

    QByteArray data(frames * 2 * qsizetype(sizeof(Sample)), '\0');   // 2 channels
    auto* out = reinterpret_cast<Sample*>(data.data());
    for (qsizetype i = 0; i < frames; ++i) {
        double gain = 1.0;
        if (i < fadeFrames)
            gain = double(i) / double(fadeFrames);
        else if (i > frames - fadeFrames)
            gain = double(frames - i) / double(fadeFrames);

        const double t = double(i) / sampleRate;
        const auto sample = static_cast<Sample>(
            amplitude * gain * std::sin(2.0 * std::numbers::pi * frequencyHz * t));
        out[2 * i]     = channel == Channel::Left  ? sample : Sample(0);
        out[2 * i + 1] = channel == Channel::Right ? sample : Sample(0);
    }
    return data;
}

} // namespace

QByteArray makeTone(Channel channel, int sampleRate, int frequencyHz, int durationMs, SampleType type)
{
    if (type == SampleType::Float)
        return toneData<float>(channel, sampleRate, frequencyHz, durationMs, 1.0);
    return toneData<std::int16_t>(channel, sampleRate, frequencyHz, durationMs, INT16_MAX);
}

namespace {

QString heardText(Heard h)
{
    switch (h) {
    case Heard::Left:    return QStringLiteral("left");
    case Heard::Right:   return QStringLiteral("right");
    case Heard::Both:    return QStringLiteral("both sides");
    case Heard::Nothing: return QStringLiteral("nothing");
    }
    return QStringLiteral("?");
}

} // namespace

TestResult audioResult(Heard leftTone, Heard rightTone)
{
    TestResult r;
    r.details = QStringLiteral("Tone on LEFT channel: heard %1\nTone on RIGHT channel: heard %2")
                    .arg(heardText(leftTone), heardText(rightTone));

    QStringList fails, warns;
    if (leftTone == Heard::Nothing)  fails << QCoreApplication::translate("AudioTest", "left silent");
    if (rightTone == Heard::Nothing) fails << QCoreApplication::translate("AudioTest", "right silent");
    // "Swapped" only when both tones came from the wrong side: the field reports
    // (2026-10-28, 2026-11-02) had "heard left" twice, which is one wrong side.
    if (leftTone == Heard::Right && rightTone == Heard::Left) {
        fails << QCoreApplication::translate("AudioTest", "left/right swapped");
    } else {
        if (leftTone == Heard::Right)
            fails << QCoreApplication::translate("AudioTest", "left tone heard on the right");
        if (rightTone == Heard::Left)
            fails << QCoreApplication::translate("AudioTest", "right tone heard on the left");
    }
    // Sound on both sides for a one-channel tone: mono speaker or wrong
    // mixing. It still works, so a note rather than a repair.
    if (leftTone == Heard::Both || rightTone == Heard::Both)
        warns << QCoreApplication::translate("AudioTest", "no stereo separation (mono speaker?)");

    if (!fails.isEmpty()) {
        r.status = Status::Fail;
        r.summary = fails.join(", ");
    } else if (!warns.isEmpty()) {
        r.status = Status::Warn;
        r.summary = warns.join(", ");
    } else {
        r.status = Status::Pass;
        r.summary = QCoreApplication::translate("AudioTest", "Left and right speakers work");
    }
    return r;
}

TestResult audioSkipped()
{
    TestResult r;
    r.status = Status::Skipped;
    r.summary = QCoreApplication::translate("AudioTest", "Skipped by the technician (no speakers?)");
    return r;
}

double peakLevel(const QByteArray& samples, SampleType type)
{
    double peak = 0;
    // memcpy, not a cast: the bytes of a QByteArray need not be aligned.
    if (type == SampleType::Int16) {
        for (qsizetype i = 0; i + 1 < samples.size(); i += 2) {
            std::int16_t v = 0;
            std::memcpy(&v, samples.constData() + i, sizeof v);
            peak = std::max(peak, std::abs(double(v)) / 32768.0);
        }
    } else {
        for (qsizetype i = 0; i + 3 < samples.size(); i += 4) {
            float v = 0;
            std::memcpy(&v, samples.constData() + i, sizeof v);
            if (std::isfinite(v))
                peak = std::max(peak, std::abs(double(v)));
        }
    }
    return std::min(peak, 1.0);
}

void MicTracker::add(double peak, int ms)
{
    m_maxPeak = std::max(m_maxPeak, peak);
    m_totalMs += ms;
    if (peak >= kMicLoudPeak)
        m_loudMs += ms;
}

double toDbfs(double peak)
{
    return peak > 0 ? std::max(-90.0, 20.0 * std::log10(peak)) : -90.0;
}

TestResult micResult(MicOutcome outcome, const MicTracker& t, const QString& device, const QString& error)
{
    TestResult r;
    r.details = QStringLiteral("Microphone: %1\nLoudest peak %2 dBFS; above %3 dBFS for %4 ms of %5 ms (needs %6 ms)")
                    .arg(device.isEmpty() ? QStringLiteral("-") : device)
                    .arg(toDbfs(t.maxPeak()), 0, 'f', 1)
                    .arg(toDbfs(kMicLoudPeak), 0, 'f', 1)
                    .arg(t.loudMs()).arg(t.totalMs()).arg(kMicHeardMs);
    if (!error.isEmpty())
        r.details += QStringLiteral("\nError: ") + error;

    switch (outcome) {
    case MicOutcome::Heard:
        r.status = Status::Pass;
        r.summary = QCoreApplication::translate("AudioTest", "microphone works");
        break;
    case MicOutcome::Broken:
        r.status = Status::Fail;
        r.summary = QCoreApplication::translate("AudioTest", "microphone hears nothing (confirmed by technician)");
        break;
    case MicOutcome::Skipped:
        r.status = Status::Skipped;
        r.summary = QCoreApplication::translate("AudioTest", "microphone skipped");
        break;
    case MicOutcome::NoDevice:
        // Desktops usually have none. On a laptop the Drivers row tells more.
        r.status = Status::Skipped;
        r.summary = QCoreApplication::translate("AudioTest", "no microphone found (desktop?)");
        break;
    case MicOutcome::Error:
        r.status = Status::Error;
        r.summary = QCoreApplication::translate("AudioTest", "microphone could not be opened (see details)");
        break;
    }
    return r;
}

TestResult audioWithMic(const TestResult& speakers, const TestResult& mic)
{
    TestResult r;
    r.status = worse(speakers.status, mic.status);
    r.summary = speakers.summary + QStringLiteral(" | ") + mic.summary;
    r.details = speakers.details + QStringLiteral("\n\n") + mic.details;
    r.note = speakers.note;
    return r;
}
