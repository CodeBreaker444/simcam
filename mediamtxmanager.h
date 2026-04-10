#pragma once
#include <QObject>
#include <QProcess>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
// MediaMtxManager
//
// Manages the lifecycle of mediamtx.exe (mediamtx on Linux/macOS).
// It generates a fresh mediamtx.yml config file on every Start() call,
// then spawns the process.  Stop() kills it cleanly.
//
// MediaMTX must be placed either:
//   • next to the application executable, OR
//   • at the path set via setMediaMtxPath().
//
// Download: https://github.com/bluenviron/mediamtx/releases
// ─────────────────────────────────────────────────────────────────────────────
class MediaMtxManager : public QObject
{
    Q_OBJECT
public:
    explicit MediaMtxManager(QObject *parent = nullptr);
    ~MediaMtxManager() override;

    // Path to mediamtx binary (auto-detected if empty).
    void setMediaMtxPath(const QString &path);

    // Configure before calling start().
    void setRtspPort(quint16 port);
    void setRtspPath(const QString &path);       // publish/read path in MediaMTX
    void setReadCredentials(const QString &user, const QString &pass);
    void setPublishCredentials(const QString &user, const QString &pass);

    bool start();
    void stop();

    bool isRunning() const;
    QString lastError() const;

signals:
    void logMessage(const QString &msg);
    void started();
    void stopped();

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onReadyReadStdOut();
    void onReadyReadStdErr();

private:
    QString generateConfig() const;
    QString findMediaMtxBinary() const;
    QString writeConfigFile() const;

    QProcess  *m_process       = nullptr;
    QString    m_mediaMtxPath;
    QString    m_configPath;
    quint16    m_rtspPort      = 8554;
    QString    m_rtspPath      = "live/main";
    QString    m_readUser;
    QString    m_readPass;
    QString    m_pubUser       = "publisher";
    QString    m_pubPass       = "pub_secret_123";
    QString    m_lastError;
};
