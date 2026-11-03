#pragma once
#include <QByteArray>
#include "core/TestResult.h"

// Logic of the audio test (the dialog that plays the sound is in
// src/ui/interactive/). A tone is played on ONE channel; the technician
// says where it was heard. Asking "where" (not just "did you hear it")
// also catches swapped channels and dead speakers.

enum class Channel { Left, Right };
enum class Heard { Left, Right, Both, Nothing };
// Sample format the sound card takes. Most take 16-bit; some only take
// 32-bit float (Surface Pro 7, Realtek SST, 2026-10-15).
enum class SampleType { Int16, Float };

// Sine tone on one channel, silence on the other: 16-bit signed or 32-bit
// float, native byte order, stereo interleaved (L R L R ...). Short fade
// in/out so the speaker does not click. Pure function, unit tested.
QByteArray makeTone(Channel channel, int sampleRate, int frequencyHz, int durationMs,
                    SampleType type = SampleType::Int16);

TestResult audioResult(Heard leftTone, Heard rightTone);
TestResult audioSkipped();

// ---- Microphone (same row: Audio = speakers + microphone) ----

// Loudest sample of a block of interleaved samples, 0..1 (1 = full scale).
// Pure function, unit tested.
double peakLevel(const QByteArray& samples, SampleType type);

// A dead microphone gives zeros or a faint hiss. Any real sound (a voice, a
// clap, the shop around) is louder than kMicLoudPeak; heard for kMicHeardMs
// in total = the microphone works. First guesses, to tune in the field (M5).
inline constexpr double kMicLoudPeak = 0.05;   // about -26 dBFS
inline constexpr int kMicHeardMs = 300;

// Adds up how long the microphone heard a clear sound, block by block.
class MicTracker {
public:
    void add(double peak, int ms);
    bool heard() const { return m_loudMs >= kMicHeardMs; }
    double maxPeak() const { return m_maxPeak; }
    int loudMs() const { return m_loudMs; }
    int totalMs() const { return m_totalMs; }

private:
    double m_maxPeak = 0;
    int m_loudMs = 0;
    int m_totalMs = 0;
};

// 0..1 -> dBFS (0 = full scale), floored at -90 (digital silence).
double toDbfs(double peak);

enum class MicOutcome {
    Heard,      // clear sound seen: automatic Pass
    Broken,     // technician: nothing moves
    Skipped,    // technician skipped
    NoDevice,   // no audio input at all
    Error,      // could not be opened / format refused
};
TestResult micResult(MicOutcome outcome, const MicTracker& tracker, const QString& device,
                     const QString& error = {});
// One row: both parts in the summary, the worse status wins.
TestResult audioWithMic(const TestResult& speakers, const TestResult& mic);
