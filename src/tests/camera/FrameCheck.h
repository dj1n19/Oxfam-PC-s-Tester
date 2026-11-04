#pragma once
#include <vector>
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

// Tablets have two cameras (front and rear): each is tested, in one row. With
// one camera its result is returned unchanged; with several, each summary is
// prefixed by its camera's name and the worse status wins (a skipped camera
// never hides another one's fault). Pure, unit tested.
struct CameraRun {
    QString name;        // QCameraDevice::description(): "Microsoft Camera Rear"
    TestResult result;
};
TestResult combineCameraResults(const std::vector<CameraRun>& runs);

// Infrared cameras (Windows Hello face login) are left out of the test
// (developer's decision 2026-11-04): Windows turns their IR light on only for
// Hello, so an app gets black frames (brightness 0.0, detail 0.0-0.2 on
// Surface Pro, 3 runs of 4) and cannot tell a working one from a dead one.
// Recognised by name. Pure, unit tested.
bool isInfraredCamera(const QString& name);
