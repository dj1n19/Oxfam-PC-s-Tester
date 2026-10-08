#pragma once
#include <QString>
#include "core/TestResult.h"

// Logic of the camera test (the dialog with the preview is in
// src/ui/interactive/). It works on plain grey bytes because oxcore may not
// use QImage (QtGui): the dialog converts each frame to Grayscale8.

struct FrameStats {
    double mean = 0;     // brightness 0..255
    double stddev = 0;   // amount of detail; ~0 = uniform picture
};

// Samples every 4th pixel of every 4th line: plenty for a verdict, cheap
// enough to run several times per second.
FrameStats lumaStats(const unsigned char* pixels, int width, int height, int bytesPerLine);

enum class FrameKind {
    Good,    // has detail: the camera shows something real
    Black,   // dark and uniform: shutter closed, lens covered, or dead sensor
    Flat,    // uniform but not dark (all grey/green...): broken sensor or driver
};
FrameKind classifyFrame(const FrameStats& s);

enum class CameraOutcome {
    Good,        // real image seen: automatic Pass
    Broken,      // technician: picture stays black/wrong
    Skipped,     // technician skipped
    NoCamera,    // no video input at all
    NoFrames,    // camera opened but never sent a picture
    CameraError, // Qt reported an error
};
TestResult cameraResult(CameraOutcome outcome, const QString& device,
                        const FrameStats& last, const QString& error = {});
