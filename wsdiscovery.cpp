#include "wsdiscovery.h"

#include <QNetworkDatagram>
#include <QHostAddress>
#include <QUuid>
#include <QDateTime>
#include <QRegularExpression>
#include <QDebug>

// ─────────────────────────────────────────────────────────────────────────────
WsDiscovery::WsDiscovery(QObject *parent) : QObject(parent) {}

WsDiscovery::~WsDiscovery() { stop(); }

void WsDiscovery::setDeviceUuid(const QString &u)       { m_uuid        = u; }
void WsDiscovery::setOnvifServiceUrl(const QString &u)  { m_serviceUrl  = u; }
void WsDiscovery::setDeviceName(const QString &n)       { m_deviceName  = n; }
bool WsDiscovery::isRunning() const                     { return m_running; }

// ─────────────────────────────────────────────────────────────────────────────
bool WsDiscovery::start()
{
    if (m_running) return true;

    m_socket = new QUdpSocket(this);
    m_socket->setSocketOption(QAbstractSocket::MulticastTtlOption, 4);

    // Bind to the WSD port, shared so multiple processes can coexist.
    if (!m_socket->bind(QHostAddress::AnyIPv4, MCAST_PORT,
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit logMessage("[WS-Discovery] Bind failed: " + m_socket->errorString());
        delete m_socket;
        m_socket = nullptr;
        return false;
    }

    if (!m_socket->joinMulticastGroup(QHostAddress(QLatin1String(MCAST_ADDR)))) {
        emit logMessage("[WS-Discovery] Multicast join failed: " + m_socket->errorString()
                       + " (non-fatal, direct queries will still work)");
    }

    connect(m_socket, &QUdpSocket::readyRead, this, &WsDiscovery::onReadyRead);
    m_running = true;

    emit logMessage("[WS-Discovery] Listening on 239.255.255.250:3702");
    sendHello();
    return true;
}

void WsDiscovery::stop()
{
    if (!m_running || !m_socket) return;
    sendBye();
    m_socket->leaveMulticastGroup(QHostAddress(QLatin1String(MCAST_ADDR)));
    m_socket->close();
    delete m_socket;
    m_socket  = nullptr;
    m_running = false;
    emit logMessage("[WS-Discovery] Stopped.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────
QString WsDiscovery::buildEndpointBlock() const
{
    return QString(
        "<wsa:EndpointReference>"
        "<wsa:Address>urn:uuid:%1</wsa:Address>"
        "</wsa:EndpointReference>"
    ).arg(m_uuid);
}

QString WsDiscovery::scopes() const
{
    return QString(
        "onvif://www.onvif.org/type/video_encoder "
        "onvif://www.onvif.org/hardware/%1 "
        "onvif://www.onvif.org/name/%2 "
        "onvif://www.onvif.org/location/"
    ).arg(m_deviceName, m_deviceName);
}

QString WsDiscovery::buildEnvelope(const QString &action,
                                    const QString &msgId,
                                    const QString &relatesTo,
                                    const QString &body) const
{
    QString header = QString(
        "<wsa:MessageID>urn:uuid:%1</wsa:MessageID>"
        "<wsa:To>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</wsa:To>"
        "<wsa:Action>%2</wsa:Action>"
    ).arg(msgId, action);

    if (!relatesTo.isEmpty())
        header += "<wsa:RelatesTo>" + relatesTo + "</wsa:RelatesTo>";

    return QString(
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<SOAP-ENV:Envelope)"
        R"( xmlns:SOAP-ENV="http://www.w3.org/2003/05/soap-envelope")"
        R"( xmlns:wsa="http://schemas.xmlsoap.org/ws/2004/08/addressing")"
        R"( xmlns:wsd="http://schemas.xmlsoap.org/ws/2005/04/discovery")"
        R"( xmlns:dn="http://www.onvif.org/ver10/network/wsdl">)"
        "<SOAP-ENV:Header>%1</SOAP-ENV:Header>"
        "<SOAP-ENV:Body>%2</SOAP-ENV:Body>"
        "</SOAP-ENV:Envelope>"
    ).arg(header, body);
}

// ─────────────────────────────────────────────────────────────────────────────
void WsDiscovery::sendHello()
{
    const QString msgId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString body = QString(
        "<wsd:Hello>"
        "%1"
        "<wsd:Types>dn:NetworkVideoTransmitter</wsd:Types>"
        "<wsd:Scopes>%2</wsd:Scopes>"
        "<wsd:XAddrs>%3</wsd:XAddrs>"
        "<wsd:MetadataVersion>1</wsd:MetadataVersion>"
        "</wsd:Hello>"
    ).arg(buildEndpointBlock(), scopes(), m_serviceUrl);

    const QString envelope = buildEnvelope(
        "http://schemas.xmlsoap.org/ws/2005/04/discovery/Hello",
        msgId, {}, body);

    m_socket->writeDatagram(envelope.toUtf8(),
                            QHostAddress(QLatin1String(MCAST_ADDR)), MCAST_PORT);
}

void WsDiscovery::sendBye()
{
    const QString msgId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString body = QString(
        "<wsd:Bye>"
        "%1"
        "<wsd:Types>dn:NetworkVideoTransmitter</wsd:Types>"
        "<wsd:Scopes>%2</wsd:Scopes>"
        "</wsd:Bye>"
    ).arg(buildEndpointBlock(), scopes());

    const QString envelope = buildEnvelope(
        "http://schemas.xmlsoap.org/ws/2005/04/discovery/Bye",
        msgId, {}, body);

    m_socket->writeDatagram(envelope.toUtf8(),
                            QHostAddress(QLatin1String(MCAST_ADDR)), MCAST_PORT);
}

void WsDiscovery::sendProbeMatch(const QHostAddress &dest, quint16 port,
                                  const QString &relatesTo)
{
    const QString msgId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString body = QString(
        "<wsd:ProbeMatches>"
        "<wsd:ProbeMatch>"
        "%1"
        "<wsd:Types>dn:NetworkVideoTransmitter</wsd:Types>"
        "<wsd:Scopes>%2</wsd:Scopes>"
        "<wsd:XAddrs>%3</wsd:XAddrs>"
        "<wsd:MetadataVersion>1</wsd:MetadataVersion>"
        "</wsd:ProbeMatch>"
        "</wsd:ProbeMatches>"
    ).arg(buildEndpointBlock(), scopes(), m_serviceUrl);

    const QString envelope = buildEnvelope(
        "http://schemas.xmlsoap.org/ws/2005/04/discovery/ProbeMatches",
        msgId, relatesTo, body);

    m_socket->writeDatagram(envelope.toUtf8(), dest, port);
}

// ─────────────────────────────────────────────────────────────────────────────
void WsDiscovery::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_socket->receiveDatagram();
        const QString data = QString::fromUtf8(dg.data());

        // Only respond to Probe messages (not our own Hello/Bye or other traffic)
        if (!data.contains(QLatin1String("Probe")) ||
             data.contains(QLatin1String("ProbeMatch")))
            continue;

        // Extract MessageID to put in RelatesTo
        QString messageId;
        static const QRegularExpression rxId(R"(<[^>]*:?MessageID[^>]*>(.*?)<\/[^>]*:?MessageID>)");
        const auto m = rxId.match(data);
        if (m.hasMatch()) messageId = m.captured(1);

        emit logMessage("[WS-Discovery] Probe from " + dg.senderAddress().toString()
                       + "  → sending ProbeMatch");
        sendProbeMatch(dg.senderAddress(), static_cast<quint16>(dg.senderPort()), messageId);
    }
}
