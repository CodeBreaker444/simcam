#include "rtspstreamer.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTimer>
#include <QDebug>

// ─────────────────────────────────────────────────────────────────────────────
RtspStreamer::RtspStreamer(QObject *parent) : QObject(parent) {}

RtspStreamer::~RtspStreamer()
{
    m_stopping = true;
    stop();
}

void RtspStreamer::setFfmpegPath(const QString &p)    { m_ffmpegPath  = p.isEmpty() ? "ffmpeg" : p; }
void RtspStreamer::setVideoFile(const QString &p)     { m_videoFile   = p; }
void RtspStreamer::setRtspTarget(const QString &url)  { m_rtspTarget  = url; }
void RtspStreamer::setForceReencode(bool f)           { m_forceEncode = f; }
bool RtspStreamer::isRunning() const                  { return m_process && m_process->state() == QProcess::Running; }
QString RtspStreamer::lastError() const               { return m_lastError; }

// ─────────────────────────────────────────────────────────────────────────────
bool RtspStreamer::start()
{
    if (isRunning()) return true;

    if (m_videoFile.isEmpty()) {
        m_lastError = "No video file selected.";
        return false;
    }
    if (!QFileInfo::exists(m_videoFile)) {
        m_lastError = "Video file not found: " + m_videoFile;
        return false;
    }
    if (m_rtspTarget.isEmpty()) {
        m_lastError = "No RTSP target URL set.";
        return false;
    }

    m_stopping = false;
    m_process  = new QProcess(this);

    // Merge stderr into stdout (FFmpeg logs to stderr)
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process, &QProcess::started, this, &RtspStreamer::onProcessStarted);
    connect(m_process, &QProcess::readyReadStandardError, this, &RtspStreamer::onReadyReadStdErr);
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RtspStreamer::onProcessFinished);

    const QStringList args = buildArguments();
    emit logMessage("[FFmpeg] " + m_ffmpegPath + " " + args.join(' '));

    m_process->start(m_ffmpegPath, args);
    if (!m_process->waitForStarted(5000)) {
        m_lastError = "Failed to start FFmpeg: " + m_process->errorString()
                      + "\nMake sure ffmpeg.exe is on PATH or set its path.";
        delete m_process;
        m_process = nullptr;
        return false;
    }
    return true;
}

void RtspStreamer::stop()
{
    m_stopping = true;
    if (!m_process) return;

    // Send 'q' to FFmpeg's stdin for a clean stop, then kill if needed.
    m_process->write("q\n");
    if (!m_process->waitForFinished(4000)) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    delete m_process;
    m_process = nullptr;
    emit stopped();
    emit logMessage("[FFmpeg] Process stopped.");
}

// ─────────────────────────────────────────────────────────────────────────────
QStringList RtspStreamer::buildArguments() const
{
    QStringList args;

    // Input: infinite loop, realtime pacing
    args << "-stream_loop" << "-1"
         << "-re"
         << "-i" << m_videoFile;

    // Video encoding
    if (m_forceEncode) {
        args << "-c:v"    << "libx264"
             << "-preset" << "ultrafast"
             << "-tune"   << "zerolatency"
             << "-b:v"    << "2000k"
             << "-g"      << "50"          // keyframe every 2 s at 25fps
             << "-sc_threshold" << "0";
    } else {
        // Try stream copy first; if codec is not H264 the user should enable force re-encode.
        args << "-c:v" << "copy";
    }

    // Audio encoding
    args << "-c:a" << "aac"
         << "-b:a" << "128k";

    // RTSP output
    args << "-f"               << "rtsp"
         << "-rtsp_transport"  << "tcp"
         << m_rtspTarget;

    return args;
}

// ─────────────────────────────────────────────────────────────────────────────
void RtspStreamer::onProcessStarted()
{
    emit started();
    emit logMessage("[FFmpeg] Stream publishing started → " + m_rtspTarget);
}

void RtspStreamer::onProcessFinished(int exitCode, QProcess::ExitStatus)
{
    delete m_process;
    m_process = nullptr;

    if (m_stopping) {
        emit stopped();
        return;
    }

    // Unexpected exit – restart after a short delay
    emit logMessage("[FFmpeg] Process exited (" + QString::number(exitCode) +
                   "). Restarting in 3 seconds...");
    emit restartingStream();

    QTimer::singleShot(3000, this, [this]() {
        if (!m_stopping) start();
    });
}

void RtspStreamer::onReadyReadStdErr()
{
    if (!m_process) return;
    const QString text = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
    // Only forward lines that look like errors or progress markers
    for (const QString &line : text.split('\n')) {
        const QString t = line.trimmed();
        if (t.isEmpty()) continue;
        // Suppress very verbose frame lines to keep the log readable
        if (t.startsWith("frame=") || t.startsWith("size=")) continue;
        emit logMessage("[FFmpeg] " + t);
    }
}
