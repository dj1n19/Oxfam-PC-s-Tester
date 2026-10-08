#include "tests/camera/FrameCheck.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr int kStep = 4;
// Below this much variation the picture is considered uniform. A real scene,
// even a plain wall, has noise and shading well above it.
constexpr double kFlatStddev = 6.0;
constexpr double kDarkMean = 20.0;
}

FrameStats lumaStats(const unsigned char* pixels, int width, int height, int bytesPerLine)
{
    FrameStats s;
    if (!pixels || width <= 0 || height <= 0)
        return s;

    double sum = 0, sumSquares = 0;
    long count = 0;
    for (int y = 0; y < height; y += kStep) {
        const unsigned char* line = pixels + static_cast<long>(y) * bytesPerLine;
        for (int x = 0; x < width; x += kStep) {
            const double v = line[x];
            sum += v;
            sumSquares += v * v;
            ++count;
        }
    }
    s.mean = sum / count;
    s.stddev = std::sqrt(std::max(0.0, sumSquares / count - s.mean * s.mean));
    return s;
}

FrameKind classifyFrame(const FrameStats& s)
{
    if (s.stddev >= kFlatStddev)
        return FrameKind::Good;
    return s.mean < kDarkMean ? FrameKind::Black : FrameKind::Flat;
}

TestResult cameraResult(CameraOutcome outcome, const QString& device,
                        const FrameStats& last, const QString& error)
{
    TestResult r;
    r.details = QStringLiteral("Device: %1\nLast frame: brightness %2, detail (std dev) %3")
                    .arg(device.isEmpty() ? QStringLiteral("-") : device)
                    .arg(last.mean, 0, 'f', 1).arg(last.stddev, 0, 'f', 1);
    if (!error.isEmpty())
        r.details += QStringLiteral("\nError: ") + error;

    switch (outcome) {
    case CameraOutcome::Good:
        r.status = Status::Pass;
        r.summary = QStringLiteral("Camera gives a picture");
        break;
    case CameraOutcome::Broken:
        r.status = Status::Fail;
        r.summary = QStringLiteral("Camera picture black or wrong (confirmed by technician)");
        break;
    case CameraOutcome::Skipped:
        r.status = Status::Skipped;
        r.summary = QStringLiteral("Skipped by the technician");
        break;
    case CameraOutcome::NoCamera:
        // Desktops usually have no camera: not an error of the PC.
        r.status = Status::Skipped;
        r.summary = QStringLiteral("No camera found (desktop? camera disabled in BIOS?)");
        break;
    case CameraOutcome::NoFrames:
        r.status = Status::Error;
        r.summary = QStringLiteral("Camera gives no picture (used by another app? privacy setting?)");
        break;
    case CameraOutcome::CameraError:
        r.status = Status::Error;
        r.summary = QStringLiteral("Camera error: %1").arg(error);
        break;
    }
    return r;
}
