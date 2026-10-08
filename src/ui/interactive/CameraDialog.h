#pragma once
#include <QCamera>
#include <QCameraDevice>
#include <QDialog>
#include <QElapsedTimer>
#include <QMediaCaptureSession>
#include <QTimer>
#include <QVideoSink>
#include "tests/camera/FrameCheck.h"

class QLabel;
class QVideoFrame;

// Live preview + automatic frame check. The technician is asked only when
// the picture stays black or uniform (shutter closed? dead sensor?).
class CameraDialog : public QDialog {
    Q_OBJECT
public:
    explicit CameraDialog(const QCameraDevice& device, QWidget* parent = nullptr);
    ~CameraDialog() override;

signals:
    void outcome(const TestResult& result);   // emitted exactly once

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void onFrame(const QVideoFrame& frame);
    void onNoFrameTimeout();
    void finish(CameraOutcome o, const QString& error = {});

    // C++ destroys members in REVERSE order: the session (which links
    // camera -> sink) goes first, then the camera, then the sink. Safer than
    // the opposite; the destructor also unlinks them explicitly.
    QCameraDevice m_device;
    QVideoSink m_sink;
    QCamera m_camera;
    QMediaCaptureSession m_session;
    QTimer m_noFrameTimer;       // camera opened but silent
    QElapsedTimer m_lastAnalysis;
    QElapsedTimer m_badSince;    // how long the picture has been black/flat
    QLabel* m_preview = nullptr;
    QLabel* m_prompt = nullptr;
    FrameStats m_last;
    int m_goodFrames = 0;
    bool m_gotFrame = false;
    bool m_done = false;
};
