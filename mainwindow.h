#pragma once
#include <QMainWindow>
#include <QString>
#include <QUuid>

class QLineEdit;
class QComboBox;
class QSpinBox;
class QPushButton;
class QLabel;
class QTextEdit;
class QCheckBox;
class QGroupBox;

class MediaMtxManager;
class RtspStreamer;
class WsDiscovery;
class OnvifServer;

// ─────────────────────────────────────────────────────────────────────────────
// MainWindow – IP Camera Simulator UI
// ─────────────────────────────────────────────────────────────────────────────
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onBrowseVideo();
    void onBrowseFfmpeg();
    void onBrowseMediaMtx();
    void onStart();
    void onStop();
    void onCameraTypeChanged(int index);
    void appendLog(const QString &msg);
    void onShowAbout();

private:
    void buildUi();
    void buildStreaming();
    void buildStatus();
    void updateUrlDisplay();
    bool validateInputs();
    void setRunning(bool running);
    QString getPublisherUrl() const;

    // ── UI Widgets ────────────────────────────────────────────────────────────
    // Video
    QLineEdit   *m_videoPathEdit;
    QPushButton *m_browseVideoBtn;

    // Tools
    QLineEdit   *m_ffmpegPathEdit;
    QPushButton *m_browseFfmpegBtn;
    QLineEdit   *m_mediaMtxPathEdit;
    QPushButton *m_browseMediaMtxBtn;

    // Camera config
    QComboBox   *m_cameraTypeCombo;
    QLineEdit   *m_usernameEdit;
    QLineEdit   *m_passwordEdit;
    QPushButton *m_showPassBtn;

    // Ports
    QSpinBox    *m_rtspPortSpin;
    QSpinBox    *m_onvifPortSpin;

    // Options
    QCheckBox   *m_forceEncodeCheck;

    // Control
    QPushButton *m_startBtn;
    QPushButton *m_stopBtn;

    // Status display
    QLabel      *m_rtspUrlLabel;
    QLabel      *m_onvifUrlLabel;
    QLabel      *m_statusIndicator;
    QTextEdit   *m_logEdit;

    // ── Backend components ────────────────────────────────────────────────────
    MediaMtxManager *m_mediaMtx  = nullptr;
    RtspStreamer     *m_streamer  = nullptr;
    WsDiscovery      *m_discovery = nullptr;
    OnvifServer      *m_onvif     = nullptr;

    QString m_deviceUuid;
    bool    m_running = false;
};
