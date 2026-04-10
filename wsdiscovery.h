#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
// WsDiscovery
//
// Implements WS-Discovery (DPWS) on UDP multicast 239.255.255.250:3702.
// Listens for Probe messages targeting NetworkVideoTransmitter and replies
// with ProbeMatches pointing at the ONVIF device service URL.
//
// Also sends a Hello announcement when started and Bye when stopped so that
// NVRs detect the camera automatically without explicit scanning.
// ─────────────────────────────────────────────────────────────────────────────
class WsDiscovery : public QObject
{
    Q_OBJECT
public:
    explicit WsDiscovery(QObject *parent = nullptr);
    ~WsDiscovery() override;

    void setDeviceUuid(const QString &uuid);           // "xxxxxxxx-xxxx-..."
    void setOnvifServiceUrl(const QString &url);        // "http://ip:port/onvif/device_service"
    void setDeviceName(const QString &name);

    bool start();
    void stop();
    bool isRunning() const;

signals:
    void logMessage(const QString &msg);

private slots:
    void onReadyRead();

private:
    void sendHello();
    void sendBye();
    void sendProbeMatch(const QHostAddress &dest, quint16 port,
                        const QString &relatesTo);

    QString buildEnvelope(const QString &action, const QString &msgId,
                          const QString &relatesTo, const QString &body) const;
    QString buildEndpointBlock() const;
    QString scopes() const;

    QUdpSocket *m_socket   = nullptr;
    QString     m_uuid;
    QString     m_serviceUrl;
    QString     m_deviceName = "IPCameraSimulator";
    bool        m_running   = false;

    static constexpr const char* MCAST_ADDR = "239.255.255.250";
    static constexpr quint16     MCAST_PORT = 3702;
};
