#include "ui/interactive/AudioTest.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QMediaDevices>

#include "ui/interactive/AudioDialog.h"

void AudioTest::run()
{
    TestResult error;
    error.status = Status::Error;

    // No output at all usually means a missing driver: the test cannot run.
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        error.summary = QStringLiteral("No audio output device");
        error.details = QStringLiteral("QMediaDevices found no default audio output "
                                       "(driver missing? see the Drivers test).");
        emit finished(error);
        return;
    }

    // makeTone() writes 16-bit stereo; 48 kHz is what nearly every device takes.
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format)) {
        const QAudioFormat pref = device.preferredFormat();
        error.summary = QStringLiteral("Audio device refuses 48 kHz 16-bit stereo");
        error.details = QStringLiteral("Device: %1\nPreferred: %2 Hz, %3 channel(s), sample format %4")
                            .arg(device.description())
                            .arg(pref.sampleRate()).arg(pref.channelCount())
                            .arg(int(pref.sampleFormat()));
        emit finished(error);
        return;
    }

    // No parent: the dialog deletes itself on close (WA_DeleteOnClose).
    auto* dialog = new AudioDialog(device, format);
    dialog->setWindowModality(Qt::ApplicationModal);
    connect(dialog, &AudioDialog::outcome, this, &ITest::finished);
    dialog->show();
}
