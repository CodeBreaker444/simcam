#pragma once
#include <QString>
#include <QMap>

// ─────────────────────────────────────────────────────────────────────────────
// Camera brand profile – drives the RTSP URL path format that gets exposed.
// MediaMTX internally publishes on rtspInternalPath; external clients and
// ONVIF consumers see rtspExternalPath (auth-prefixed by the caller).
// ─────────────────────────────────────────────────────────────────────────────
struct CameraProfile {
    QString displayName;           // shown in UI combo
    QString vendor;                // reported in ONVIF GetDeviceInformation
    QString model;                 // reported in ONVIF GetDeviceInformation
    QString rtspInternalPath;      // MediaMTX publish/read path (no query str)
    QString rtspExternalPathFmt;   // printf-style with %1=user %2=pass %3=ip %4=port
    QString subStreamPathFmt;      // sub-stream variant (reported as second profile)
    QString hardware;              // ONVIF hardware string
};

// Returns the full map of supported camera profiles, keyed by display name.
inline QMap<QString, CameraProfile> cameraProfiles()
{
    QMap<QString, CameraProfile> m;

    m["Hikvision"] = {
        "Hikvision",
        "Hikvision",
        "DS-2CD2143G2-I",
        "Streaming/Channels/101",
        "rtsp://%1:%2@%3:%4/Streaming/Channels/101",
        "rtsp://%1:%2@%3:%4/Streaming/Channels/102",
        "DS-2CD2143G2-I"
    };

    m["CPPlus"] = {
        "CPPlus",
        "CP Plus",
        "CP-UNC-TA21L3C",
        "cam/realmonitor",
        "rtsp://%1:%2@%3:%4/cam/realmonitor?channel=1&subtype=0",
        "rtsp://%1:%2@%3:%4/cam/realmonitor?channel=1&subtype=1",
        "CP-UNC-TA21L3C"
    };

    m["Dahua"] = {
        "Dahua",
        "Dahua",
        "IPC-HDW2831T-AS",
        "cam/realmonitor",
        "rtsp://%1:%2@%3:%4/cam/realmonitor?channel=1&subtype=0",
        "rtsp://%1:%2@%3:%4/cam/realmonitor?channel=1&subtype=1",
        "IPC-HDW2831T-AS"
    };

    m["Axis"] = {
        "Axis",
        "Axis Communications",
        "P3245-V",
        "axis-media/media",
        "rtsp://%1:%2@%3:%4/axis-media/media.amp",
        "rtsp://%1:%2@%3:%4/axis-media/media.amp?camera=2",
        "P3245-V"
    };

    m["Generic"] = {
        "Generic",
        "Generic",
        "IP-Camera-1080P",
        "live/main",
        "rtsp://%1:%2@%3:%4/live/main",
        "rtsp://%1:%2@%3:%4/live/sub",
        "IP-Camera-1080P"
    };

    return m;
}
