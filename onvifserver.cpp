#include "onvifserver.h"

#include <QTcpSocket>
#include <QDateTime>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QUuid>
#include <QDebug>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers – HTTP/SOAP plumbing
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Build a minimal HTTP 200 response carrying a SOAP body.
QByteArray httpResponse(int status, const QByteArray &body,
                         const QByteArray &contentType = "application/soap+xml; charset=utf-8")
{
    const QByteArray statusText = (status == 200) ? "OK" :
                                  (status == 400) ? "Bad Request" :
                                  (status == 401) ? "Unauthorized" : "Internal Server Error";
    QByteArray resp;
    resp += "HTTP/1.1 " + QByteArray::number(status) + " " + statusText + "\r\n";
    resp += "Content-Type: "   + contentType + "\r\n";
    resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    resp += "Connection: close\r\n";
    resp += "\r\n";
    resp += body;
    return resp;
}

// Extract the value of a tag from XML.  Simple but adequate for ONVIF SOAP.
QString extractTag(const QString &xml, const QString &tag)
{
    const QRegularExpression rx(
        "<[^>]*:?" + QRegularExpression::escape(tag) + "[^>]*>(.*?)<\\/[^>]*:?" +
        QRegularExpression::escape(tag) + ">",
        QRegularExpression::DotMatchesEverythingOption);
    const auto m = rx.match(xml);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

// Check if xml contains a given keyword (case-insensitive tag name check).
bool soapContains(const QString &xml, const QString &keyword)
{
    return xml.contains(keyword, Qt::CaseInsensitive);
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
OnvifServer::OnvifServer(QObject *parent) : QObject(parent) {}

OnvifServer::~OnvifServer() { stop(); }

// ─────────────────────────────────────────────────────────────────────────────
bool OnvifServer::start(const OnvifConfig &cfg)
{
    stop();
    m_cfg = cfg;

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &OnvifServer::onNewConnection);

    if (!m_server->listen(QHostAddress::AnyIPv4, cfg.httpPort)) {
        emit logMessage("[ONVIF] Cannot listen on port " + QString::number(cfg.httpPort)
                       + ": " + m_server->errorString());
        delete m_server;
        m_server = nullptr;
        return false;
    }

    emit logMessage("[ONVIF] Device service at " + deviceServiceUrl());
    return true;
}

void OnvifServer::stop()
{
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
        emit logMessage("[ONVIF] Stopped.");
    }
}

bool OnvifServer::isRunning() const { return m_server && m_server->isListening(); }

QString OnvifServer::deviceServiceUrl() const
{
    return QString("http://%1:%2/onvif/device_service")
               .arg(m_cfg.ip).arg(m_cfg.httpPort);
}

// ─────────────────────────────────────────────────────────────────────────────
// New TCP connection – read the full HTTP request then respond.
// ─────────────────────────────────────────────────────────────────────────────
void OnvifServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *sock = m_server->nextPendingConnection();
        // Read-and-respond on connection's thread (GUI thread) – fine for low traffic.
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            handleConnection(sock);
        });
        connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
    }
}

void OnvifServer::handleConnection(QTcpSocket *sock)
{
    // Accumulate data until we have headers + full body
    const QByteArray data = sock->readAll();
    const QString text    = QString::fromUtf8(data);

    // Split HTTP headers from body
    const int sep = text.indexOf("\r\n\r\n");
    if (sep < 0) return; // incomplete – wait for more data (simplified: works for ONVIF sizes)

    const QString headers = text.left(sep);
    const QByteArray body = data.mid(sep + 4);

    // Extract request line
    const QString firstLine = headers.section('\n', 0, 0).trimmed();
    const QString method    = firstLine.section(' ', 0, 0);
    const QString path      = firstLine.section(' ', 1, 1);

    if (method != "POST") {
        sock->write(httpResponse(400, "Only POST is supported"));
        sock->disconnectFromHost();
        return;
    }

    emit logMessage("[ONVIF] " + method + " " + path);

    const QByteArray response = processRequest(path, body);
    sock->write(httpResponse(200, response));
    sock->disconnectFromHost();
}

// ─────────────────────────────────────────────────────────────────────────────
QByteArray OnvifServer::processRequest(const QString &path, const QByteArray &soapBody)
{
    const QString bodyStr = QString::fromUtf8(soapBody);

    // Authentication check (skip for GetSystemDateAndTime – required by spec)
    const bool needsAuth = !soapContains(bodyStr, "GetSystemDateAndTime");
    if (needsAuth && !m_cfg.username.isEmpty() && !verifyCredentials(bodyStr)) {
        return wrapSoap(soapAuthFault()).toUtf8();
    }

    if (path.contains("device"))  return deviceService(bodyStr);
    if (path.contains("media"))   return mediaService(bodyStr);

    // Fall-through: try both
    QByteArray r = deviceService(bodyStr);
    if (r.isEmpty()) r = mediaService(bodyStr);
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// WS-Security credential verification
// ─────────────────────────────────────────────────────────────────────────────
bool OnvifServer::verifyCredentials(const QString &envelope) const
{
    const QString xmlUser  = extractTag(envelope, "Username");
    const QString xmlPass  = extractTag(envelope, "Password");
    const QString xmlNonce = extractTag(envelope, "Nonce");
    const QString xmlCreated = extractTag(envelope, "Created");

    if (xmlUser.isEmpty()) return true; // no security header → allow (some clients skip it)

    if (xmlUser != m_cfg.username) return false;

    // Detect PasswordDigest vs PasswordText
    const bool isDigest = envelope.contains("PasswordDigest", Qt::CaseInsensitive);
    if (isDigest) {
        // Digest = Base64( SHA1( decoded(Nonce) + Created + Password ) )
        const QByteArray nonce   = QByteArray::fromBase64(xmlNonce.toUtf8());
        const QByteArray created = xmlCreated.toUtf8();
        const QByteArray pass    = m_cfg.password.toUtf8();
        const QByteArray raw     = nonce + created + pass;
        const QByteArray digest  = QCryptographicHash::hash(raw, QCryptographicHash::Sha1).toBase64();
        return digest == xmlPass.toUtf8();
    }

    // PasswordText
    return xmlPass == m_cfg.password;
}

// ─────────────────────────────────────────────────────────────────────────────
// SOAP envelope wrapper
// ─────────────────────────────────────────────────────────────────────────────
QString OnvifServer::wrapSoap(const QString &body) const
{
    return QString(
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<SOAP-ENV:Envelope)"
        R"( xmlns:SOAP-ENV="http://www.w3.org/2003/05/soap-envelope")"
        R"( xmlns:tt="http://www.onvif.org/ver10/schema")"
        R"( xmlns:tds="http://www.onvif.org/ver10/device/wsdl")"
        R"( xmlns:trt="http://www.onvif.org/ver10/media/wsdl">)"
        "<SOAP-ENV:Header/>"
        "<SOAP-ENV:Body>%1</SOAP-ENV:Body>"
        "</SOAP-ENV:Envelope>"
    ).arg(body);
}

QString OnvifServer::soapFault(const QString &reason) const
{
    return QString(
        "<SOAP-ENV:Fault>"
        "<SOAP-ENV:Code><SOAP-ENV:Value>SOAP-ENV:Sender</SOAP-ENV:Value></SOAP-ENV:Code>"
        "<SOAP-ENV:Reason><SOAP-ENV:Text xml:lang=\"en\">%1</SOAP-ENV:Text></SOAP-ENV:Reason>"
        "</SOAP-ENV:Fault>"
    ).arg(reason);
}

QString OnvifServer::soapAuthFault() const
{
    return soapFault("Authentication credentials not accepted.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Route device service actions
// ─────────────────────────────────────────────────────────────────────────────
QByteArray OnvifServer::deviceService(const QString &body)
{
    QString resp;
    if      (soapContains(body, "GetSystemDateAndTime"))       resp = rspGetSystemDateAndTime();
    else if (soapContains(body, "GetCapabilities"))            resp = rspGetCapabilities();
    else if (soapContains(body, "GetDeviceInformation"))       resp = rspGetDeviceInformation();
    else if (soapContains(body, "GetNetworkInterfaces"))       resp = rspGetNetworkInterfaces();
    else if (soapContains(body, "GetServices"))                resp = rspGetServices();
    else if (soapContains(body, "GetScopes"))                  resp = rspGetScopes();
    else return {};

    return wrapSoap(resp).toUtf8();
}

// Route media service actions
QByteArray OnvifServer::mediaService(const QString &body)
{
    QString resp;
    if      (soapContains(body, "GetProfiles"))                     resp = rspGetProfiles();
    else if (soapContains(body, "GetVideoSources"))                 resp = rspGetVideoSources();
    else if (soapContains(body, "GetStreamUri"))                    resp = rspGetStreamUri();
    else if (soapContains(body, "GetVideoEncoderConfigurations"))   resp = rspGetVideoEncoderConfigurations();
    else return wrapSoap(soapFault("Action not implemented.")).toUtf8();

    return wrapSoap(resp).toUtf8();
}

// ─────────────────────────────────────────────────────────────────────────────
// Stream URL helpers
// ─────────────────────────────────────────────────────────────────────────────
QString OnvifServer::mainStreamUrl() const
{
    return m_cfg.rtspPathFmt
               .arg(m_cfg.username, m_cfg.password, m_cfg.ip)
               .arg(m_cfg.rtspPort);
}

QString OnvifServer::subStreamUrl() const
{
    return m_cfg.subStreamFmt
               .arg(m_cfg.username, m_cfg.password, m_cfg.ip)
               .arg(m_cfg.rtspPort);
}

// ─────────────────────────────────────────────────────────────────────────────
// ── Device Service Responses ─────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
QString OnvifServer::rspGetSystemDateAndTime() const
{
    const QDateTime utc = QDateTime::currentDateTimeUtc();
    return QString(
        "<tds:GetSystemDateAndTimeResponse>"
        "<tds:SystemDateAndTime>"
        "<tt:DateTimeType>NTP</tt:DateTimeType>"
        "<tt:DaylightSavings>false</tt:DaylightSavings>"
        "<tt:TimeZone><tt:TZ>UTC</tt:TZ></tt:TimeZone>"
        "<tt:UTCDateTime>"
        "<tt:Time>"
        "<tt:Hour>%1</tt:Hour><tt:Minute>%2</tt:Minute><tt:Second>%3</tt:Second>"
        "</tt:Time>"
        "<tt:Date>"
        "<tt:Year>%4</tt:Year><tt:Month>%5</tt:Month><tt:Day>%6</tt:Day>"
        "</tt:Date>"
        "</tt:UTCDateTime>"
        "</tds:SystemDateAndTime>"
        "</tds:GetSystemDateAndTimeResponse>"
    ).arg(utc.time().hour()).arg(utc.time().minute()).arg(utc.time().second())
     .arg(utc.date().year()).arg(utc.date().month()).arg(utc.date().day());
}

QString OnvifServer::rspGetCapabilities() const
{
    const QString base = QString("http://%1:%2").arg(m_cfg.ip).arg(m_cfg.httpPort);
    return QString(
        "<tds:GetCapabilitiesResponse>"
        "<tds:Capabilities>"
        "<tt:Device>"
        "<tt:XAddr>%1/onvif/device_service</tt:XAddr>"
        "<tt:Network>"
        "<tt:IPFilter>false</tt:IPFilter>"
        "<tt:ZeroConfiguration>false</tt:ZeroConfiguration>"
        "<tt:IPVersion6>false</tt:IPVersion6>"
        "<tt:DynDNS>false</tt:DynDNS>"
        "</tt:Network>"
        "<tt:System>"
        "<tt:DiscoveryResolve>false</tt:DiscoveryResolve>"
        "<tt:DiscoveryBye>true</tt:DiscoveryBye>"
        "<tt:RemoteDiscovery>false</tt:RemoteDiscovery>"
        "<tt:SystemBackup>false</tt:SystemBackup>"
        "<tt:SystemLogging>false</tt:SystemLogging>"
        "<tt:FirmwareUpgrade>false</tt:FirmwareUpgrade>"
        "<tt:SupportedVersions><tt:Major>2</tt:Major><tt:Minor>0</tt:Minor></tt:SupportedVersions>"
        "</tt:System>"
        "<tt:Security>"
        "<tt:TLS1.1>false</tt:TLS1.1>"
        "<tt:TLS1.2>false</tt:TLS1.2>"
        "<tt:OnboardKeyGeneration>false</tt:OnboardKeyGeneration>"
        "<tt:AccessPolicyConfig>false</tt:AccessPolicyConfig>"
        "<tt:X.509Token>false</tt:X.509Token>"
        "<tt:SAMLToken>false</tt:SAMLToken>"
        "<tt:KerberosToken>false</tt:KerberosToken>"
        "<tt:RELToken>false</tt:RELToken>"
        "</tt:Security>"
        "</tt:Device>"
        "<tt:Media>"
        "<tt:XAddr>%1/onvif/media_service</tt:XAddr>"
        "<tt:StreamingCapabilities>"
        "<tt:RTPMulticast>false</tt:RTPMulticast>"
        "<tt:RTP_TCP>true</tt:RTP_TCP>"
        "<tt:RTP_RTSP_TCP>true</tt:RTP_RTSP_TCP>"
        "</tt:StreamingCapabilities>"
        "</tt:Media>"
        "</tds:Capabilities>"
        "</tds:GetCapabilitiesResponse>"
    ).arg(base);
}

QString OnvifServer::rspGetDeviceInformation() const
{
    return QString(
        "<tds:GetDeviceInformationResponse>"
        "<tds:Manufacturer>%1</tds:Manufacturer>"
        "<tds:Model>%2</tds:Model>"
        "<tds:FirmwareVersion>1.0.0</tds:FirmwareVersion>"
        "<tds:SerialNumber>IPCAMSIM-00001</tds:SerialNumber>"
        "<tds:HardwareId>%3</tds:HardwareId>"
        "</tds:GetDeviceInformationResponse>"
    ).arg(m_cfg.vendor, m_cfg.model, m_cfg.hardware);
}

QString OnvifServer::rspGetNetworkInterfaces() const
{
    return QString(
        "<tds:GetNetworkInterfacesResponse>"
        "<tds:NetworkInterfaces token=\"eth0\">"
        "<tt:Enabled>true</tt:Enabled>"
        "<tt:Info>"
        "<tt:Name>eth0</tt:Name>"
        "<tt:HwAddress>00:00:00:00:00:01</tt:HwAddress>"
        "<tt:MTU>1500</tt:MTU>"
        "</tt:Info>"
        "<tt:IPv4>"
        "<tt:Enabled>true</tt:Enabled>"
        "<tt:Config>"
        "<tt:Manual>"
        "<tt:Address>%1</tt:Address>"
        "<tt:PrefixLength>24</tt:PrefixLength>"
        "</tt:Manual>"
        "<tt:DHCP>false</tt:DHCP>"
        "</tt:Config>"
        "</tt:IPv4>"
        "</tds:NetworkInterfaces>"
        "</tds:GetNetworkInterfacesResponse>"
    ).arg(m_cfg.ip);
}

QString OnvifServer::rspGetServices() const
{
    const QString base = QString("http://%1:%2").arg(m_cfg.ip).arg(m_cfg.httpPort);
    return QString(
        "<tds:GetServicesResponse>"
        "<tds:Service>"
        "<tds:Namespace>http://www.onvif.org/ver10/device/wsdl</tds:Namespace>"
        "<tds:XAddr>%1/onvif/device_service</tds:XAddr>"
        "<tds:Version><tt:Major>2</tt:Major><tt:Minor>0</tt:Minor></tds:Version>"
        "</tds:Service>"
        "<tds:Service>"
        "<tds:Namespace>http://www.onvif.org/ver10/media/wsdl</tds:Namespace>"
        "<tds:XAddr>%1/onvif/media_service</tds:XAddr>"
        "<tds:Version><tt:Major>2</tt:Major><tt:Minor>0</tt:Minor></tds:Version>"
        "</tds:Service>"
        "</tds:GetServicesResponse>"
    ).arg(base);
}

QString OnvifServer::rspGetScopes() const
{
    return QString(
        "<tds:GetScopesResponse>"
        "<tds:Scopes>"
        "<tt:ScopeDef>Fixed</tt:ScopeDef>"
        "<tt:ScopeItem>onvif://www.onvif.org/type/video_encoder</tt:ScopeItem>"
        "</tds:Scopes>"
        "<tds:Scopes>"
        "<tt:ScopeDef>Fixed</tt:ScopeDef>"
        "<tt:ScopeItem>onvif://www.onvif.org/hardware/%1</tt:ScopeItem>"
        "</tds:Scopes>"
        "<tds:Scopes>"
        "<tt:ScopeDef>Fixed</tt:ScopeDef>"
        "<tt:ScopeItem>onvif://www.onvif.org/name/%2</tt:ScopeItem>"
        "</tds:Scopes>"
        "</tds:GetScopesResponse>"
    ).arg(m_cfg.model, m_cfg.vendor);
}

// ─────────────────────────────────────────────────────────────────────────────
// ── Media Service Responses ──────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
QString OnvifServer::rspGetProfiles() const
{
    const auto videoEncCfg = [](const QString &tok, const QString &name) {
        return QString(
            "<tt:VideoEncoderConfiguration token=\"%1\">"
            "<tt:Name>%2</tt:Name>"
            "<tt:UseCount>1</tt:UseCount>"
            "<tt:Encoding>H264</tt:Encoding>"
            "<tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>"
            "<tt:Quality>5</tt:Quality>"
            "<tt:RateControl>"
            "<tt:FrameRateLimit>25</tt:FrameRateLimit>"
            "<tt:EncodingInterval>1</tt:EncodingInterval>"
            "<tt:BitrateLimit>4096</tt:BitrateLimit>"
            "</tt:RateControl>"
            "<tt:H264>"
            "<tt:GovLength>50</tt:GovLength>"
            "<tt:H264Profile>Main</tt:H264Profile>"
            "</tt:H264>"
            "<tt:Multicast>"
            "<tt:Address><tt:Type>IPv4</tt:Type><tt:IPv4Address>0.0.0.0</tt:IPv4Address></tt:Address>"
            "<tt:Port>0</tt:Port><tt:TTL>0</tt:TTL><tt:AutoStart>false</tt:AutoStart>"
            "</tt:Multicast>"
            "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
            "</tt:VideoEncoderConfiguration>"
        ).arg(tok, name);
    };

    const auto vscCfg = [](const QString &tok) {
        return QString(
            "<tt:VideoSourceConfiguration token=\"%1\">"
            "<tt:Name>VideoSourceConfig</tt:Name>"
            "<tt:UseCount>2</tt:UseCount>"
            "<tt:SourceToken>VideoSource_0</tt:SourceToken>"
            "<tt:Bounds x=\"0\" y=\"0\" width=\"1920\" height=\"1080\"/>"
            "</tt:VideoSourceConfiguration>"
        ).arg(tok);
    };

    return QString(
        "<trt:GetProfilesResponse>"
        // Main stream profile
        "<trt:Profiles fixed=\"true\" token=\"profile_main\">"
        "<tt:Name>MainStream</tt:Name>"
        "%1"  // VSC
        "%2"  // VEC
        "</trt:Profiles>"
        // Sub stream profile
        "<trt:Profiles fixed=\"true\" token=\"profile_sub\">"
        "<tt:Name>SubStream</tt:Name>"
        "%3"  // VSC
        "%4"  // VEC sub
        "</trt:Profiles>"
        "</trt:GetProfilesResponse>"
    ).arg(vscCfg("vsc_main"),
          videoEncCfg("vec_main", "VideoEncoderMain"),
          vscCfg("vsc_sub"),
          videoEncCfg("vec_sub", "VideoEncoderSub"));
}

QString OnvifServer::rspGetVideoSources() const
{
    return QString(
        "<trt:GetVideoSourcesResponse>"
        "<trt:VideoSources token=\"VideoSource_0\">"
        "<tt:Framerate>25</tt:Framerate>"
        "<tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>"
        "</trt:VideoSources>"
        "</trt:GetVideoSourcesResponse>"
    );
}

QString OnvifServer::rspGetStreamUri() const
{
    // Determine which profile was requested (main or sub)
    // For simplicity, always return main stream.  The full implementation
    // would parse the ProfileToken from the request body.
    const QString uri = mainStreamUrl();

    return QString(
        "<trt:GetStreamUriResponse>"
        "<trt:MediaUri>"
        "<tt:Uri>%1</tt:Uri>"
        "<tt:InvalidAfterConnect>false</tt:InvalidAfterConnect>"
        "<tt:InvalidAfterReboot>false</tt:InvalidAfterReboot>"
        "<tt:Timeout>PT0S</tt:Timeout>"
        "</trt:MediaUri>"
        "</trt:GetStreamUriResponse>"
    ).arg(uri);
}

QString OnvifServer::rspGetVideoEncoderConfigurations() const
{
    return QString(
        "<trt:GetVideoEncoderConfigurationsResponse>"
        "<trt:Configurations token=\"vec_main\">"
        "<tt:Name>VideoEncoderMain</tt:Name>"
        "<tt:UseCount>1</tt:UseCount>"
        "<tt:Encoding>H264</tt:Encoding>"
        "<tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>"
        "<tt:Quality>5</tt:Quality>"
        "<tt:RateControl>"
        "<tt:FrameRateLimit>25</tt:FrameRateLimit>"
        "<tt:EncodingInterval>1</tt:EncodingInterval>"
        "<tt:BitrateLimit>4096</tt:BitrateLimit>"
        "</tt:RateControl>"
        "</trt:Configurations>"
        "</trt:GetVideoEncoderConfigurationsResponse>"
    );
}
