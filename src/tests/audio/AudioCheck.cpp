#include "tests/audio/AudioCheck.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <QStringList>

QByteArray makeTone(Channel channel, int sampleRate, int frequencyHz, int durationMs)
{
    const qsizetype frames = qsizetype(sampleRate) * durationMs / 1000;
    const qsizetype fadeFrames = sampleRate / 50;   // 20 ms
    const double amplitude = 0.5 * INT16_MAX;       // half scale: loud enough, no clipping

    QByteArray data(frames * 2 * qsizetype(sizeof(std::int16_t)), '\0');   // 2 channels
    auto* out = reinterpret_cast<std::int16_t*>(data.data());
    for (qsizetype i = 0; i < frames; ++i) {
        double gain = 1.0;
        if (i < fadeFrames)
            gain = double(i) / double(fadeFrames);
        else if (i > frames - fadeFrames)
            gain = double(frames - i) / double(fadeFrames);

        const double t = double(i) / sampleRate;
        const auto sample = static_cast<std::int16_t>(
            amplitude * gain * std::sin(2.0 * std::numbers::pi * frequencyHz * t));
        out[2 * i]     = channel == Channel::Left  ? sample : std::int16_t(0);
        out[2 * i + 1] = channel == Channel::Right ? sample : std::int16_t(0);
    }
    return data;
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
    if (leftTone == Heard::Nothing)  fails << QStringLiteral("left silent");
    if (rightTone == Heard::Nothing) fails << QStringLiteral("right silent");
    if (leftTone == Heard::Right || rightTone == Heard::Left)
        fails << QStringLiteral("left/right swapped");
    // Sound on both sides for a one-channel tone: mono speaker or wrong
    // mixing. It still works, so a note rather than a repair.
    if (leftTone == Heard::Both || rightTone == Heard::Both)
        warns << QStringLiteral("no stereo separation (mono speaker?)");

    if (!fails.isEmpty()) {
        r.status = Status::Fail;
        r.summary = fails.join(", ");
    } else if (!warns.isEmpty()) {
        r.status = Status::Warn;
        r.summary = warns.join(", ");
    } else {
        r.status = Status::Pass;
        r.summary = QStringLiteral("Left and right speakers work");
    }
    return r;
}

TestResult audioSkipped()
{
    TestResult r;
    r.status = Status::Skipped;
    r.summary = QStringLiteral("Skipped by the technician (no speakers?)");
    return r;
}
