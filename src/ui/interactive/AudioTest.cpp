#include "ui/interactive/AudioTest.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QMediaDevices>

#include "ui/interactive/AudioDialog.h"

void AudioTest::run()
{
    TestResult error;
    error.status = Status::Error;

    // No output at all: missing driver, or (Linux) no sound server reachable.
    // PipeWire/PulseAudio run in the user's session, so an app started with
    // sudo cannot reach them and sees no device at all.
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        error.summary = QStringLiteral("No audio output found (sound driver? or started with sudo?)");
        error.details = QStringLiteral(
            "QMediaDevices found no default audio output. Possible causes:\n"
            "- the sound driver is missing: see the Drivers test;\n"
            "- Linux: the program was started with sudo/as root. The sound server "
            "(PipeWire/PulseAudio) belongs to the user session: start it as the normal user.");
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
