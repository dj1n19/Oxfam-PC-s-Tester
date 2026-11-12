#pragma once
#include <memory>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QBuffer>
#include <QDialog>
#include <QElapsedTimer>
#include <QPointer>
#include "tests/audio/AudioCheck.h"

class QAudioSink;
class QAudioSource;
class QIODevice;
class QLabel;
class QProgressBar;
class QWidget;

// Two steps in one dialog (one Audio row):
// 1. speakers: a tone on the left, then on the right channel, "where was it heard?"
//    (audioResult(), oxcore);
// 2. microphone: a live level bar; a clear sound = automatic Pass (MicTracker, oxcore).
class AudioDialog : public QDialog {
    Q_OBJECT
public:
    AudioDialog(const QAudioDevice& device, const QAudioFormat& format, QWidget* parent = nullptr);
    ~AudioDialog() override;

signals:
    void outcome(const TestResult& result);   // emitted exactly once
    void stopAll();   // "Stop all tests": emitted just before the dialog closes

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void play();
    void answer(Heard heard);
    void speakersDone(const TestResult& speakers);
    void startMic();
    void readMic();
    void micDone(MicOutcome mic, const QString& error = {});

    QAudioDevice m_device;
    QAudioFormat m_format;
    std::unique_ptr<QAudioSink> m_sink;
    QBuffer m_buffer;            // the tone being played
    QWidget* m_speakerPage = nullptr;
    QLabel* m_prompt = nullptr;
    int m_step = 0;              // 0 = left tone, 1 = right tone
    Heard m_leftAnswer = Heard::Nothing;

    // Microphone
    QWidget* m_micPage = nullptr;
    QProgressBar* m_level = nullptr;
    QLabel* m_micPrompt = nullptr;
    QAudioDevice m_micDevice;
    QAudioFormat m_micFormat;
    std::unique_ptr<QAudioSource> m_source;
    QPointer<QIODevice> m_micIo;  // owned by m_source
    MicTracker m_tracker;
    QElapsedTimer m_micClock;

    bool m_speakersDone = false;
    TestResult m_speakers;
    bool m_done = false;
};
