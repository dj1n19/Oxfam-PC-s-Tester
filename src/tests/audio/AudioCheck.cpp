#include "tests/audio/AudioCheck.h"

#include <cmath>
#include <cstdint>
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
    if (leftTone == Heard::Right || rightTone == Heard::Left)
        fails << QCoreApplication::translate("AudioTest", "left/right swapped");
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
