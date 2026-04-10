#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
// OnvifServer
//
// A minimal ONVIF-compliant HTTP/SOAP server that satisfies the subset of
// the ONVIF Device (tds) and Media (trt) service required by most NVR
// software for initial device discovery and stream connection.
//
// Endpoints served:
//   POST /onvif/device_service   → GetSystemDateAndTime, GetCapabilities,
//                                  GetDeviceInformation, GetNetworkInterfaces,
//                                  GetServices, GetScopes
//   POST /onvif/media_service    → GetProfiles, GetVideoSources, GetStreamUri,
//                                  GetVideoEncoderConfigurations
//
// Authentication: WS-Security UsernameToken (PasswordText or PasswordDigest).
// ─────────────────────────────────────────────────────────────────────────────

struct OnvifConfig {
    QString ip;
    quint16 httpPort    = 8080;
    quint16 rtspPort    = 8554;
    QString rtspPathFmt;          // QString::arg(%1=user, %2=pass, %3=ip, %4=port)
    QString subStreamFmt;
    QString username;
    QString password;
    QString cameraType;
    QString deviceUuid;
    QString vendor;
    QString model;
    QString hardware;
};

class OnvifServer : public QObject
{
    Q_OBJECT
public:
    explicit OnvifServer(QObject *parent = nullptr);
    ~OnvifServer() override;

    bool start(const OnvifConfig &cfg);
    void stop();
    bool isRunning() const;

    QString deviceServiceUrl() const;

signals:
    void logMessage(const QString &msg);

private slots:
    void onNewConnection();

private:
    QTcpServer  *m_server = nullptr;
    OnvifConfig  m_cfg;

    void         handleConnection(QTcpSocket *sock);
    QByteArray   processRequest(const QString &path, const QByteArray &soapBody);

    // Authentication
    bool         verifyCredentials(const QString &soapEnvelope) const;

    // SOAP response builders
    QByteArray   deviceService(const QString &body);
    QByteArray   mediaService(const QString &body);

    QString      wrapSoap(const QString &body) const;
    QString      soapFault(const QString &reason) const;
    QString      soapAuthFault() const;

    // Device service responses
    QString      rspGetSystemDateAndTime() const;
    QString      rspGetCapabilities() const;
    QString      rspGetDeviceInformation() const;
    QString      rspGetNetworkInterfaces() const;
    QString      rspGetServices() const;
    QString      rspGetScopes() const;

    // Media service responses
    QString      rspGetProfiles() const;
    QString      rspGetVideoSources() const;
    QString      rspGetStreamUri() const;
    QString      rspGetVideoEncoderConfigurations() const;

    // Helpers
    QString      mainStreamUrl() const;
    QString      subStreamUrl() const;
};
