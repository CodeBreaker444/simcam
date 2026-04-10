<div align="center">

<pre>
███████╗██╗███╗   ███╗ ██████╗ █████╗ ███╗   ███╗
██╔════╝██║████╗ ████║██╔════╝██╔══██╗████╗ ████║
███████╗██║██╔████╔██║██║     ███████║██╔████╔██║
╚════██║██║██║╚██╔╝██║██║     ██╔══██║██║╚██╔╝██║
███████║██║██║ ╚═╝ ██║╚██████╗██║  ██║██║ ╚═╝ ██║
╚══════╝╚═╝╚═╝     ╚═╝ ╚═════╝╚═╝  ╚═╝╚═╝     ╚═╝
</pre>

<img src="images/appicon.svg" width="90" height="90" alt="SimCam Icon" />
<p style="text-align:center;font-weight :bold;">SimCam</p>

<p><strong>ONVIF-compliant IP Camera Simulator &nbsp;·&nbsp; Windows &nbsp;·&nbsp; Qt6 &nbsp;·&nbsp; C++</strong></p>

<p>
Simulates a real IP camera by looping a local video file and publishing it as an<br/>
authenticated RTSP stream. Exposes full ONVIF device discovery so any NVR, VMS,<br/>
or IP camera tool can find and connect to it automatically — no physical hardware required.
</p>

<p><a href="https://github.com/codebreaker444/simcam">github.com/codebreaker444/simcam</a></p>

</div>

---

## Screenshot

<p align="center">
  <img src="images/simcam-screenshot.jpeg" width="720" alt="SimCam Screenshot" />
</p>

---

## Features

| Feature | Detail |
|---|---|
| **Camera brands** | Hikvision, CPPlus, Dahua, Axis, Generic |
| **RTSP streaming** | Infinite video loop via FFmpeg → MediaMTX |
| **Authentication** | Username / Password enforced on RTSP and ONVIF |
| **ONVIF Device Service** | GetCapabilities, GetDeviceInfo, GetSystemDateAndTime, GetServices, GetScopes, GetNetworkInterfaces |
| **ONVIF Media Service** | GetProfiles, GetVideoSources, GetStreamUri, GetVideoEncoderConfigurations |
| **WS-Discovery** | Multicast UDP 239.255.255.250:3702 — Hello / Bye / ProbeMatch |
| **Auto-restart** | FFmpeg restarts automatically if it exits unexpectedly |

---

## Prerequisites

### 1. FFmpeg (required)
Download a pre-built Windows binary:

- **[BtbN FFmpeg-Builds (GitHub Releases)](https://github.com/BtbN/FFmpeg-Builds/releases)**
  — recommended, direct `ffmpeg.exe` download
- Or via the official site: https://ffmpeg.org/download.html → Windows → pick a build

Either drop `ffmpeg.exe` somewhere on your `PATH`, or use the Browse button inside the app.

### 2. MediaMTX (required)
Download the latest Windows release:

- **[bluenviron/mediamtx (GitHub Releases)](https://github.com/bluenviron/mediamtx/releases)**
  — grab `mediamtx_vX.Y.Z_windows_amd64.zip`, extract `mediamtx.exe`

Place `mediamtx.exe` next to `IPCameraSimulator.exe`, or browse to it inside the app.

### 3. Qt 6.x (for building from source)
Install Qt 6 via the [Qt Online Installer](https://www.qt.io/download) or:
```bat
winget install Qt.Qt6
```
Required modules: `Qt6Core`, `Qt6Widgets`, `Qt6Network`

### 4. CMake ≥ 3.20 + a C++17 compiler
- **MSVC** — install via [Visual Studio 2022](https://visualstudio.microsoft.com/) (Desktop C++ workload)
- **MinGW** — bundled with Qt installer or via `winget install GnuWin32.Make`

---

## Building from Source

```bat
git clone https://github.com/codebreaker444/simcam.git
cd simcam

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The compiled binary lands at `build\Release\IPCameraSimulator.exe`.

### Qt Creator (alternative)
1. Open Qt Creator → **Open Project** → select `CMakeLists.txt`
2. Configure with a Qt 6 kit
3. Build → Run

### Deploying (making it portable)
After building, run `windeployqt` to bundle Qt DLLs next to the exe:
```bat
windeployqt build\Release\IPCameraSimulator.exe
```
Then copy `ffmpeg.exe` and `mediamtx.exe` into the same folder.

---

## Running

1. Launch `IPCameraSimulator.exe`
2. **Tool Paths** — point the app at `ffmpeg.exe` and `mediamtx.exe` if they are not on your PATH (download links are shown in the app)
3. **Video Source** — browse to any `.mp4 / .mkv / .avi / .mov / .ts / .h264 / .h265` file
4. **Camera Configuration** — choose a camera brand, set credentials and ports
5. Click **▶ Start Simulator**
6. The active RTSP and ONVIF URLs appear in the status section; paste them into your NVR/VMS

### Default ports

| Service | Protocol | Default Port |
|---|---|---|
| RTSP stream | TCP | 8554 |
| ONVIF HTTP | TCP | 8080 |
| WS-Discovery | UDP multicast | 3702 |

---

## Architecture

```
┌──────────────────────────────────────────────────────┐
│                   MainWindow (Qt UI)                  │
└───┬──────────────┬───────────────┬───────────────────┘
    │              │               │               │
    ▼              ▼               ▼               ▼
MediaMtxManager  RtspStreamer  OnvifServer   WsDiscovery
(mediamtx.exe)  (ffmpeg.exe)  HTTP:8080     UDP:3702
     ▲               │         SOAP/XML     Multicast
     │               │
     └───────────────┘
       rtsp://publisher:***@127.0.0.1:8554/<path>
                    │
              [MediaMTX serves]
                    │
       rtsp://user:pass@<LOCAL_IP>:8554/<camera_path>
                    ▲
              NVR / VMS Client
```

### Camera-Specific RTSP Path Formats

| Brand | Main Stream URL |
|---|---|
| Hikvision | `rtsp://user:pass@ip:8554/Streaming/Channels/101` |
| CPPlus / Dahua | `rtsp://user:pass@ip:8554/cam/realmonitor?channel=1&subtype=0` |
| Axis | `rtsp://user:pass@ip:8554/axis-media/media.amp` |
| Generic | `rtsp://user:pass@ip:8554/live/main` |

### ONVIF WS-Security

The simulator accepts both authentication modes:
- **PasswordText** — plaintext password in the SOAP header
- **PasswordDigest** — `Base64(SHA1(Nonce + Created + Password))`

---

## How NVR Discovery Works

1. NVR sends a WS-Discovery **Probe** to `239.255.255.250:3702`
2. Simulator responds with **ProbeMatch** containing the ONVIF service URL
3. NVR calls **GetCapabilities** → receives device & media service endpoints
4. NVR calls **GetProfiles** + **GetStreamUri** → receives the RTSP URL
5. NVR opens the RTSP stream via MediaMTX using the configured credentials

---

## Firewall (Windows)

Open the following ports inbound:
```
UDP  3702   – WS-Discovery multicast
TCP  8554   – RTSP
TCP  8080   – ONVIF HTTP
```

Quick PowerShell (run as Administrator):
```powershell
New-NetFirewallRule -DisplayName "SimCam RTSP"  -Direction Inbound -Protocol TCP -LocalPort 8554 -Action Allow
New-NetFirewallRule -DisplayName "SimCam ONVIF" -Direction Inbound -Protocol TCP -LocalPort 8080 -Action Allow
New-NetFirewallRule -DisplayName "SimCam WSD"   -Direction Inbound -Protocol UDP -LocalPort 3702 -Action Allow
```

---

## License

MIT — see [LICENSE](LICENSE) for details.
