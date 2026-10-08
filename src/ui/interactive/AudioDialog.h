#pragma once
#include <memory>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QBuffer>
#include <QDialog>
#include "tests/audio/AudioCheck.h"

class QAudioSink;
class QLabel;

// Plays a tone on the left, then on the right channel, and asks where it
// was heard. The result comes from audioResult() (oxcore).
class AudioDialog : public QDialog {
    Q_OBJECT
public:
    AudioDialog(const QAudioDevice& device, const QAudioFormat& format, QWidget* parent = nullptr);
    ~AudioDialog() override;

signals:
    void outcome(const TestResult& result);   // emitted exactly once

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void play();
    void answer(Heard heard);
    void finish(const TestResult& result);

    QAudioDevice m_device;
    QAudioFormat m_format;
    std::unique_ptr<QAudioSink> m_sink;
    QBuffer m_buffer;            // the tone being played
    QLabel* m_prompt = nullptr;
    int m_step = 0;              // 0 = left tone, 1 = right tone
    Heard m_leftAnswer = Heard::Nothing;
    bool m_done = false;
};
