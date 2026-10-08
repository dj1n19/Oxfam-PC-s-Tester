#pragma once
#include <QByteArray>
#include "core/TestResult.h"

// Logic of the audio test (the dialog that plays the sound is in
// src/ui/interactive/). A tone is played on ONE channel; the technician
// says where it was heard. Asking "where" (not just "did you hear it")
// also catches swapped channels and dead speakers.

enum class Channel { Left, Right };
enum class Heard { Left, Right, Both, Nothing };

// Sine tone on one channel, silence on the other: 16-bit signed,
// little-endian, stereo interleaved (L R L R ...). Short fade in/out so the
// speaker does not click. Pure function, unit tested.
QByteArray makeTone(Channel channel, int sampleRate, int frequencyHz, int durationMs);

TestResult audioResult(Heard leftTone, Heard rightTone);
TestResult audioSkipped();
