#include "ui/interactive/CameraDialog.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVideoFrame>

namespace {
constexpr int kAnalyseEveryMs = 250;   // converting frames is not free
constexpr int kGoodFramesForPass = 4;  // about 1 s of real picture
constexpr int kBadPromptMs = 3000;     // webcams start dark while adjusting exposure
constexpr int kNoFrameMs = 6000;
}

CameraDialog::CameraDialog(const QCameraDevice& device, QWidget* parent)
    : QDialog(parent), m_device(device), m_camera(device)
{
    setWindowTitle(tr("Camera test"));
    setAttribute(Qt::WA_DeleteOnClose);   // Qt frees the dialog when it closes

    auto* top = new QVBoxLayout(this);
    auto* help = new QLabel(tr("Look at the camera. The test ends by itself when a picture is seen."), this);
    help->setWordWrap(true);
    top->addWidget(help);

    m_preview = new QLabel(this);
    m_preview->setFixedSize(480, 360);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet(QStringLiteral("background:#000000; color:#ffffff;"));
    m_preview->setText(tr("Starting camera..."));
    top->addWidget(m_preview);

    m_prompt = new QLabel(this);
    m_prompt->setWordWrap(true);
    m_prompt->setStyleSheet(QStringLiteral("font-weight:bold; color:#b71c1c;"));
    m_prompt->hide();
    top->addWidget(m_prompt);

    auto* broken = new QPushButton(tr("Camera is broken"), this);
    auto* skip = new QPushButton(tr("Skip"), this);
    auto* row = new QHBoxLayout;
    for (QPushButton* b : {broken, skip}) {
        b->setAutoDefault(false);
        b->setMinimumHeight(40);
        row->addWidget(b);
    }
    top->addLayout(row);
    connect(broken, &QPushButton::clicked, this, [this] { finish(CameraOutcome::Broken); });
    connect(skip, &QPushButton::clicked, this, [this] { finish(CameraOutcome::Skipped); });

    m_session.setCamera(&m_camera);
    m_session.setVideoOutput(&m_sink);   // frames come to us, not to a widget
    connect(&m_sink, &QVideoSink::videoFrameChanged, this, &CameraDialog::onFrame);
    connect(&m_camera, &QCamera::errorOccurred, this,
            [this](QCamera::Error e, const QString& text) {
                if (e != QCamera::NoError)
                    finish(CameraOutcome::CameraError, text);
            });

    m_noFrameTimer.setSingleShot(true);
    connect(&m_noFrameTimer, &QTimer::timeout, this, &CameraDialog::onNoFrameTimeout);
    m_noFrameTimer.start(kNoFrameMs);
    m_camera.start();
}

CameraDialog::~CameraDialog()
{
    // Stop and unlink explicitly, before any member is destroyed, so no frame
    // can arrive in a half-destroyed dialog (see the member order in the .h).
    disconnect(&m_sink, nullptr, this, nullptr);
    m_camera.stop();   // also turns the camera LED off
    m_session.setVideoOutput(nullptr);
    m_session.setCamera(nullptr);
}

void CameraDialog::onFrame(const QVideoFrame& frame)
{
    if (m_done || !frame.isValid())
        return;
    if (!m_gotFrame) {
        m_gotFrame = true;
        m_noFrameTimer.stop();
    }
    if (m_lastAnalysis.isValid() && m_lastAnalysis.elapsed() < kAnalyseEveryMs)
        return;
    m_lastAnalysis.start();

    const QImage image = frame.toImage();
    if (image.isNull())
        return;
    m_preview->setPixmap(QPixmap::fromImage(
        image.scaled(m_preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));

    const QImage grey = image.convertToFormat(QImage::Format_Grayscale8);
    m_last = lumaStats(grey.constBits(), grey.width(), grey.height(),
                       static_cast<int>(grey.bytesPerLine()));

    if (classifyFrame(m_last) == FrameKind::Good) {
        m_badSince.invalidate();
        m_prompt->hide();
        if (++m_goodFrames >= kGoodFramesForPass)
            finish(CameraOutcome::Good);
        return;
    }

    // Black or uniform: only ask once it has lasted a while.
    m_goodFrames = 0;
    if (!m_badSince.isValid())
        m_badSince.start();
    if (m_badSince.elapsed() >= kBadPromptMs && m_prompt->isHidden()) {
        m_prompt->setText(tr("The picture stays black or uniform. Open the privacy shutter or "
                             "uncover the lens. If it does not change, click \"Camera is broken\"."));
        m_prompt->show();
    }
}

void CameraDialog::onNoFrameTimeout()
{
    finish(CameraOutcome::NoFrames);
}

void CameraDialog::closeEvent(QCloseEvent* e)
{
    // Window closed with the title bar X: the test still needs an answer.
    if (!m_done)
        finish(CameraOutcome::Skipped);
    e->accept();
}

void CameraDialog::finish(CameraOutcome o, const QString& error)
{
    if (m_done)
        return;
    m_done = true;
    m_noFrameTimer.stop();
    m_camera.stop();
    emit outcome(cameraResult(o, m_device.description(), m_last, error));
    close();
}
