#pragma once
#include <QObject>
#include <QProcess>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
// RtspStreamer
//
// Manages a single FFmpeg process that:
//   1. Reads a video file in an infinite loop (-stream_loop -1)
//   2. Re-encodes (or copies) to H.264/AAC
//   3. Publishes to the local MediaMTX RTSP server
//
// The publisher URL is:  rtsp://pubUser:pubPass@127.0.0.1:rtspPort/rtspPath
// ─────────────────────────────────────────────────────────────────────────────
class RtspStreamer : public QObject
{
    Q_OBJECT
public:
    explicit RtspStreamer(QObject *parent = nullptr);
    ~RtspStreamer() override;

    void setFfmpegPath(const QString &path);        // default: "ffmpeg"
    void setVideoFile(const QString &path);
    void setRtspTarget(const QString &url);         // full rtsp:// publisher URL
    void setForceReencode(bool force);              // force libx264 even for H264 input

    bool start();
    void stop();
    bool isRunning() const;
    QString lastError() const;

signals:
    void logMessage(const QString &msg);
    void started();
    void stopped();
    void restartingStream();

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onReadyReadStdErr();

private:
    QStringList buildArguments() const;

    QProcess  *m_process      = nullptr;
    QString    m_ffmpegPath   = "ffmpeg";
    QString    m_videoFile;
    QString    m_rtspTarget;
    bool       m_forceEncode  = false;
    bool       m_stopping     = false;
    QString    m_lastError;
};
