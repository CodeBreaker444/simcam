#include "mainwindow.h"

#include "cameraprofile.h"
#include "mediamtxmanager.h"
#include "rtspstreamer.h"
#include "wsdiscovery.h"
#include "onvifserver.h"
#include "networkutils.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTextEdit>
#include <QEventLoop>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("SimCam •  ONVIF / RTSP");
    setMinimumSize(760, 860);
    resize(780, 920);

    // Stable device UUID across sessions (re-generated each run is fine for a simulator)
    m_deviceUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);

    buildUi();

    // ── Menu bar ──────────────────────────────────────────────────────────────
    QMenuBar *mb       = new QMenuBar(this);
    QMenu    *helpMenu = mb->addMenu("Help");
    QAction  *aboutAct = helpMenu->addAction("About SimCam…");
    connect(aboutAct, &QAction::triggered, this, &MainWindow::onShowAbout);
    setMenuBar(mb);

    // Pre-populate defaults
    m_usernameEdit->setText("admin");
    m_passwordEdit->setText("admin123");
    m_rtspPortSpin->setValue(8554);
    m_onvifPortSpin->setValue(8080);
}

MainWindow::~MainWindow()
{
    onStop();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    onStop();
    event->accept();
}

// ─────────────────────────────────────────────────────────────────────────────
// UI construction
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::buildUi()
{
    QWidget     *central = new QWidget(this);
    QVBoxLayout *root    = new QVBoxLayout(central);
    root->setSpacing(10);
    root->setContentsMargins(12, 12, 12, 12);
    setCentralWidget(central);

    // ── Section: Tools ────────────────────────────────────────────────────────
    {
        QGroupBox   *gb = new QGroupBox("Tool Paths", central);
        QFormLayout *fl = new QFormLayout(gb);

        auto makeFilePick = [&](QLineEdit *&edit, QPushButton *&btn, const QString &ph) {
            QWidget *row = new QWidget;
            QHBoxLayout *hl = new QHBoxLayout(row);
            hl->setContentsMargins(0,0,0,0);
            edit = new QLineEdit; edit->setPlaceholderText(ph);
            btn  = new QPushButton("…"); btn->setFixedWidth(32);
            hl->addWidget(edit); hl->addWidget(btn);
            return row;
        };

        fl->addRow("FFmpeg:",   makeFilePick(m_ffmpegPathEdit,   m_browseFfmpegBtn,   "ffmpeg  (auto-detected from PATH)"));
        {
            QLabel *lnk = new QLabel(
                R"(<a href="https://github.com/BtbN/FFmpeg-Builds/releases">Download FFmpeg for Windows (GitHub) →</a>)");
            lnk->setOpenExternalLinks(true);
            lnk->setStyleSheet("font-size:11px;");
            fl->addRow("", lnk);
        }

        fl->addRow("MediaMTX:", makeFilePick(m_mediaMtxPathEdit, m_browseMediaMtxBtn, "mediamtx.exe  (place next to app or browse)"));
        {
            QLabel *lnk = new QLabel(
                R"(<a href="https://github.com/bluenviron/mediamtx/releases">Download MediaMTX (GitHub) →</a>)");
            lnk->setOpenExternalLinks(true);
            lnk->setStyleSheet("font-size:11px;");
            fl->addRow("", lnk);
        }

        connect(m_browseFfmpegBtn,   &QPushButton::clicked, this, &MainWindow::onBrowseFfmpeg);
        connect(m_browseMediaMtxBtn, &QPushButton::clicked, this, &MainWindow::onBrowseMediaMtx);
        root->addWidget(gb);
    }

    // ── Section: Video Source ─────────────────────────────────────────────────
    {
        QGroupBox   *gb = new QGroupBox("Video Source", central);
        QHBoxLayout *hl = new QHBoxLayout(gb);
        m_videoPathEdit = new QLineEdit;
        m_videoPathEdit->setPlaceholderText("Select a video file to loop as the camera feed…");
        m_browseVideoBtn = new QPushButton("Browse…");
        hl->addWidget(m_videoPathEdit);
        hl->addWidget(m_browseVideoBtn);
        connect(m_browseVideoBtn, &QPushButton::clicked, this, &MainWindow::onBrowseVideo);
        root->addWidget(gb);
    }

    // ── Section: Camera Configuration ────────────────────────────────────────
    {
        QGroupBox   *gb = new QGroupBox("Camera Configuration", central);
        QFormLayout *fl = new QFormLayout(gb);

        // ── Camera type ───────────────────────────────────────────────────────
        m_cameraTypeCombo = new QComboBox;
        const auto profiles = cameraProfiles();
        for (auto it = profiles.cbegin(); it != profiles.cend(); ++it)
            m_cameraTypeCombo->addItem(it.key());
        m_cameraTypeCombo->setCurrentText("CPPlus");   // default brand

        // ── Credentials ───────────────────────────────────────────────────────
        m_usernameEdit = new QLineEdit;

        m_passwordEdit = new QLineEdit;
        m_passwordEdit->setEchoMode(QLineEdit::Password);

        m_showPassBtn = new QPushButton("\xf0\x9f\x91\x81");   // 👁 UTF-8
        m_showPassBtn->setFixedWidth(34);
        m_showPassBtn->setCheckable(true);
        m_showPassBtn->setFlat(false);
        m_showPassBtn->setToolTip("Show / hide password");
        m_showPassBtn->setStyleSheet(
            "QPushButton { font-size:14px; padding:0px; border:1px solid #bdbdbd; border-radius:3px; }"
            "QPushButton:checked { background:#e3f2fd; border-color:#1565c0; }");
        connect(m_showPassBtn, &QPushButton::toggled, this, [this](bool show) {
            m_passwordEdit->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
        });

        QWidget     *passRow = new QWidget;
        QHBoxLayout *passHl  = new QHBoxLayout(passRow);
        passHl->setContentsMargins(0, 0, 0, 0);
        passHl->setSpacing(4);
        passHl->addWidget(m_passwordEdit);
        passHl->addWidget(m_showPassBtn);

        // ── Ports / options ───────────────────────────────────────────────────
        m_rtspPortSpin  = new QSpinBox; m_rtspPortSpin->setRange(1024, 65535);
        m_onvifPortSpin = new QSpinBox; m_onvifPortSpin->setRange(1024, 65535);

        m_forceEncodeCheck = new QCheckBox("Force re-encode to H.264 (slower, needed for non-H264 files)");

        // ── Add rows in display order ─────────────────────────────────────────
        fl->addRow("Camera Type:",   m_cameraTypeCombo);
        fl->addRow("Username:",      m_usernameEdit);
        fl->addRow("Password:",      passRow);
        fl->addRow("RTSP Port:",     m_rtspPortSpin);
        fl->addRow("ONVIF Port:",    m_onvifPortSpin);
        fl->addRow("",               m_forceEncodeCheck);

        connect(m_cameraTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::onCameraTypeChanged);

        root->addWidget(gb);
    }

    // ── Control buttons ───────────────────────────────────────────────────────
    {
        QHBoxLayout *hl = new QHBoxLayout;
        m_startBtn = new QPushButton("▶  Start Simulator");
        m_stopBtn  = new QPushButton("■  Stop Simulator");
        m_startBtn->setFixedHeight(38);
        m_stopBtn->setFixedHeight(38);
        m_stopBtn->setEnabled(false);

        m_startBtn->setStyleSheet("QPushButton { background:#2e7d32; color:white; font-weight:bold; border-radius:4px; }"
                                  "QPushButton:hover { background:#388e3c; }"
                                  "QPushButton:disabled { background:#bdbdbd; }");
        m_stopBtn->setStyleSheet("QPushButton { background:#c62828; color:white; font-weight:bold; border-radius:4px; }"
                                 "QPushButton:hover { background:#d32f2f; }"
                                 "QPushButton:disabled { background:#bdbdbd; }");

        hl->addWidget(m_startBtn);
        hl->addWidget(m_stopBtn);
        connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStart);
        connect(m_stopBtn,  &QPushButton::clicked, this, &MainWindow::onStop);
        root->addLayout(hl);
    }

    // ── Status display ────────────────────────────────────────────────────────
    {
        QGroupBox   *gb = new QGroupBox("Active Stream URLs", central);
        QFormLayout *fl = new QFormLayout(gb);

        m_rtspUrlLabel  = new QLabel("—");
        m_onvifUrlLabel = new QLabel("—");
        m_statusIndicator = new QLabel("● Idle");
        m_statusIndicator->setStyleSheet("color:#757575; font-weight:bold;");

        m_rtspUrlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_onvifUrlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_rtspUrlLabel->setWordWrap(true);
        m_onvifUrlLabel->setWordWrap(true);

        fl->addRow("Status:",      m_statusIndicator);
        fl->addRow("RTSP Stream:", m_rtspUrlLabel);
        fl->addRow("ONVIF URL:",   m_onvifUrlLabel);
        root->addWidget(gb);
    }

    // ── Log output ────────────────────────────────────────────────────────────
    {
        QGroupBox   *gb = new QGroupBox("Log", central);
        QVBoxLayout *vl = new QVBoxLayout(gb);
        m_logEdit = new QTextEdit;
        m_logEdit->setReadOnly(true);
        m_logEdit->setFont(QFont("Consolas", 9));
        m_logEdit->setMinimumHeight(140);
        QPushButton *clearBtn = new QPushButton("Clear");
        clearBtn->setFixedWidth(60);
        connect(clearBtn, &QPushButton::clicked, m_logEdit, &QTextEdit::clear);
        QHBoxLayout *btnRow = new QHBoxLayout;
        btnRow->addStretch(); btnRow->addWidget(clearBtn);
        vl->addWidget(m_logEdit);
        vl->addLayout(btnRow);
        root->addWidget(gb, 1);
    }

    appendLog("IP Camera Simulator ready.  Select a video file and press Start.");
    appendLog("Requirements: ffmpeg.exe and mediamtx.exe must be accessible.");
}

// ─────────────────────────────────────────────────────────────────────────────
// File browse slots
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onBrowseVideo()
{
    const QString f = QFileDialog::getOpenFileName(
        this, "Select Video File", {},
        "Video Files (*.mp4 *.mkv *.avi *.mov *.ts *.h264 *.h265);;All Files (*)");
    if (!f.isEmpty()) m_videoPathEdit->setText(f);
}

void MainWindow::onBrowseFfmpeg()
{
    const QString f = QFileDialog::getOpenFileName(
        this, "Select FFmpeg Executable", {},
        "Executables (*.exe);;All Files (*)");
    if (!f.isEmpty()) m_ffmpegPathEdit->setText(f);
}

void MainWindow::onBrowseMediaMtx()
{
    const QString f = QFileDialog::getOpenFileName(
        this, "Select MediaMTX Executable", {},
        "Executables (*.exe);;All Files (*)");
    if (!f.isEmpty()) m_mediaMtxPathEdit->setText(f);
}

void MainWindow::onCameraTypeChanged(int /*index*/)
{
    updateUrlDisplay();
}

// ─────────────────────────────────────────────────────────────────────────────
// Start / Stop
// ─────────────────────────────────────────────────────────────────────────────
bool MainWindow::validateInputs()
{
    if (m_videoPathEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Missing Input", "Please select a video file.");
        return false;
    }
    if (m_usernameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Missing Input", "Username cannot be empty.");
        return false;
    }
    if (m_passwordEdit->text().isEmpty()) {
        QMessageBox::warning(this, "Missing Input", "Password cannot be empty.");
        return false;
    }
    return true;
}

void MainWindow::onStart()
{
    if (m_running || !validateInputs()) return;

    const QString localIp   = getLocalIPAddress();
    const quint16 rtspPort  = static_cast<quint16>(m_rtspPortSpin->value());
    const quint16 onvifPort = static_cast<quint16>(m_onvifPortSpin->value());
    const QString username  = m_usernameEdit->text().trimmed();
    const QString password  = m_passwordEdit->text();
    const QString camType   = m_cameraTypeCombo->currentText();
    const auto    profiles  = cameraProfiles();
    const CameraProfile &profile = profiles.value(camType, profiles.value("Generic"));

    // Internal publisher credentials (different from viewer credentials)
    const QString pubUser = "publisher";
    const QString pubPass = "pub_secret_123!";

    appendLog("──────────────────────────────────────");
    appendLog("Starting  ·  Camera: " + camType + "  ·  IP: " + localIp);

    // ── 1. MediaMTX ──────────────────────────────────────────────────────────
    m_mediaMtx = new MediaMtxManager(this);
    if (!m_mediaMtxPathEdit->text().isEmpty())
        m_mediaMtx->setMediaMtxPath(m_mediaMtxPathEdit->text().trimmed());
    m_mediaMtx->setRtspPort(rtspPort);
    m_mediaMtx->setRtspPath(profile.rtspInternalPath);
    m_mediaMtx->setReadCredentials(username, password);
    m_mediaMtx->setPublishCredentials(pubUser, pubPass);
    connect(m_mediaMtx, &MediaMtxManager::logMessage, this, &MainWindow::appendLog);

    if (!m_mediaMtx->start()) {
        QMessageBox::critical(this, "MediaMTX Error", m_mediaMtx->lastError());
        delete m_mediaMtx; m_mediaMtx = nullptr;
        return;
    }

    // Give MediaMTX a moment to initialise before FFmpeg connects
    { QEventLoop loop; QTimer::singleShot(800, &loop, &QEventLoop::quit); loop.exec(); }

    // ── 2. FFmpeg Streamer ───────────────────────────────────────────────────
    // Publisher URL uses internal credentials (no query string)
    const QString publishUrl = QString("rtsp://%1:%2@127.0.0.1:%3/%4")
                                   .arg(pubUser, pubPass)
                                   .arg(rtspPort)
                                   .arg(profile.rtspInternalPath);

    m_streamer = new RtspStreamer(this);
    if (!m_ffmpegPathEdit->text().isEmpty())
        m_streamer->setFfmpegPath(m_ffmpegPathEdit->text().trimmed());
    m_streamer->setVideoFile(m_videoPathEdit->text().trimmed());
    m_streamer->setRtspTarget(publishUrl);
    m_streamer->setForceReencode(m_forceEncodeCheck->isChecked());
    connect(m_streamer, &RtspStreamer::logMessage, this, &MainWindow::appendLog);

    if (!m_streamer->start()) {
        QMessageBox::critical(this, "FFmpeg Error", m_streamer->lastError());
        m_mediaMtx->stop();
        delete m_mediaMtx; m_mediaMtx = nullptr;
        delete m_streamer; m_streamer = nullptr;
        return;
    }

    // ── 3. ONVIF HTTP server ─────────────────────────────────────────────────
    OnvifConfig cfg;
    cfg.ip           = localIp;
    cfg.httpPort     = onvifPort;
    cfg.rtspPort     = rtspPort;
    cfg.rtspPathFmt  = profile.rtspExternalPathFmt;
    cfg.subStreamFmt = profile.subStreamPathFmt;
    cfg.username     = username;
    cfg.password     = password;
    cfg.cameraType   = camType;
    cfg.deviceUuid   = m_deviceUuid;
    cfg.vendor       = profile.vendor;
    cfg.model        = profile.model;
    cfg.hardware     = profile.hardware;

    m_onvif = new OnvifServer(this);
    connect(m_onvif, &OnvifServer::logMessage, this, &MainWindow::appendLog);
    if (!m_onvif->start(cfg)) {
        appendLog("WARNING: ONVIF server failed to start (port may be in use).");
    }

    // ── 4. WS-Discovery ──────────────────────────────────────────────────────
    m_discovery = new WsDiscovery(this);
    m_discovery->setDeviceUuid(m_deviceUuid);
    m_discovery->setOnvifServiceUrl(m_onvif->deviceServiceUrl());
    m_discovery->setDeviceName(profile.model);
    connect(m_discovery, &WsDiscovery::logMessage, this, &MainWindow::appendLog);
    if (!m_discovery->start()) {
        appendLog("WARNING: WS-Discovery failed (UDP port 3702 may be in use).");
    }

    setRunning(true);
    updateUrlDisplay();
}

void MainWindow::onStop()
{
    if (!m_running && !m_mediaMtx && !m_streamer) return;

    appendLog("Stopping simulator…");

    if (m_discovery) { m_discovery->stop(); delete m_discovery; m_discovery = nullptr; }
    if (m_onvif)     { m_onvif->stop();     delete m_onvif;     m_onvif     = nullptr; }
    if (m_streamer)  { m_streamer->stop();  delete m_streamer;  m_streamer  = nullptr; }
    if (m_mediaMtx)  { m_mediaMtx->stop(); delete m_mediaMtx;  m_mediaMtx  = nullptr; }

    setRunning(false);

    m_rtspUrlLabel->setText("—");
    m_onvifUrlLabel->setText("—");
    appendLog("Simulator stopped.");
    appendLog("──────────────────────────────────────");
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::setRunning(bool running)
{
    m_running = running;
    m_startBtn->setEnabled(!running);
    m_stopBtn->setEnabled(running);
    m_videoPathEdit->setEnabled(!running);
    m_browseVideoBtn->setEnabled(!running);
    m_cameraTypeCombo->setEnabled(!running);
    m_usernameEdit->setEnabled(!running);
    m_passwordEdit->setEnabled(!running);
    m_showPassBtn->setEnabled(!running);
    m_rtspPortSpin->setEnabled(!running);
    m_onvifPortSpin->setEnabled(!running);
    m_forceEncodeCheck->setEnabled(!running);

    if (running) {
        m_statusIndicator->setText("● Live");
        m_statusIndicator->setStyleSheet("color:#2e7d32; font-weight:bold;");
    } else {
        m_statusIndicator->setText("● Idle");
        m_statusIndicator->setStyleSheet("color:#757575; font-weight:bold;");
    }
}

void MainWindow::updateUrlDisplay()
{
    if (!m_running) return;

    const QString localIp  = getLocalIPAddress();
    const quint16 rtspPort = static_cast<quint16>(m_rtspPortSpin->value());
    const quint16 httpPort = static_cast<quint16>(m_onvifPortSpin->value());
    const QString user     = m_usernameEdit->text().trimmed();
    const QString pass     = m_passwordEdit->text();
    const QString camType  = m_cameraTypeCombo->currentText();
    const auto    profiles = cameraProfiles();
    const CameraProfile &p = profiles.value(camType, profiles.value("Generic"));

    const QString rtspUrl  = p.rtspExternalPathFmt
                               .arg(user, pass, localIp).arg(rtspPort);
    const QString onvifUrl = QString("http://%1:%2/onvif/device_service")
                               .arg(localIp).arg(httpPort);

    m_rtspUrlLabel->setText(rtspUrl);
    m_onvifUrlLabel->setText(onvifUrl);
}

void MainWindow::appendLog(const QString &msg)
{
    const QString ts  = QDateTime::currentDateTime().toString("hh:mm:ss");
    const QString line = "[" + ts + "]  " + msg;
    m_logEdit->append(line);
    // Auto-scroll
    m_logEdit->verticalScrollBar()->setValue(m_logEdit->verticalScrollBar()->maximum());
}

// ─────────────────────────────────────────────────────────────────────────────
// Help / About dialog
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::onShowAbout()
{
    QDialog dlg(this);
    dlg.setWindowTitle("About SimCam");
    dlg.setMinimumWidth(440);
    dlg.setMaximumWidth(440);

    QVBoxLayout *vl = new QVBoxLayout(&dlg);
    vl->setSpacing(8);
    vl->setContentsMargins(20, 20, 20, 20);

    QLabel *titleLbl = new QLabel("<span style='font-size:18px; font-weight:bold;'>SimCam</span>");
    QLabel *subtitleLbl = new QLabel("ONVIF-compliant IP Camera Simulator");
    subtitleLbl->setStyleSheet("color:#555555;");
    QLabel *versionLbl = new QLabel("Version 0.1");

    QFrame *sep1 = new QFrame; sep1->setFrameShape(QFrame::HLine); sep1->setFrameShadow(QFrame::Sunken);

    QLabel *descLbl = new QLabel(
        "Simulates a real IP camera by looping a local video file and "
        "publishing it as an authenticated RTSP stream. Supports full ONVIF "
        "device discovery so any NVR or VMS can find and connect to it "
        "automatically — no physical camera hardware required.");
    descLbl->setWordWrap(true);

    QFrame *sep2 = new QFrame; sep2->setFrameShape(QFrame::HLine); sep2->setFrameShadow(QFrame::Sunken);

    QLabel *repoLbl = new QLabel(
        "<b>GitHub:</b>  "
        "<a href='https://github.com/codebreaker444/simcam'>"
        "github.com/codebreaker444/simcam</a>");
    repoLbl->setOpenExternalLinks(true);
    repoLbl->setTextFormat(Qt::RichText);

    QLabel *depsLbl = new QLabel(
        "<b>Dependencies:</b><br>"
        "&nbsp;&nbsp;• <a href='https://github.com/BtbN/FFmpeg-Builds/releases'>FFmpeg</a>"
        " – video encoding &amp; RTSP publishing<br>"
        "&nbsp;&nbsp;• <a href='https://github.com/bluenviron/mediamtx/releases'>MediaMTX</a>"
        " – RTSP server<br>"
        "&nbsp;&nbsp;• <a href='https://www.qt.io/'>Qt 6</a> – UI framework");
    depsLbl->setOpenExternalLinks(true);
    depsLbl->setTextFormat(Qt::RichText);

    QPushButton *closeBtn = new QPushButton("Close");
    closeBtn->setFixedWidth(80);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);

    vl->addWidget(titleLbl);
    vl->addWidget(subtitleLbl);
    vl->addWidget(versionLbl);
    vl->addWidget(sep1);
    vl->addWidget(descLbl);
    vl->addWidget(sep2);
    vl->addWidget(repoLbl);
    vl->addSpacing(4);
    vl->addWidget(depsLbl);
    vl->addSpacing(8);
    vl->addLayout(btnRow);

    dlg.exec();
}