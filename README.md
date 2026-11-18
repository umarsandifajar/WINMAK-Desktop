# WINMAK Desktop

**A Native glass desktop for Windows 11: a floating taskbar and desktop widgets with real-time refraction.**

WINMAK Desktop is a customized build and widget expansion based on the original [Atrium Desktop](https://github.com/wynter-yaxley/atrium-desktop) project, featuring native C++/WinRT performance, Direct2D glass rendering, and a WinUI 3 settings interface.

---

## ✨ What's New in v2.8.0 (Modification-widget)

- **🕌 Prayer Times Widget (Jadwal Sholat):** Real-time daily prayer schedule (Subuh, Dzuhur, Ashar, Maghrib, Isya) with live countdown and progress indicator.
- **🎵 Native Audio Player Widget:** Real-time media session integration (Windows SMTC) with album artwork, track metadata, interactive seekbar, playback controls, and animated white bar visualizer.
- **🎤 Live Lyrics Widget:** Automatic timestamped lyrics synchronization powered by [LRCLIB](https://lrclib.net) displaying previous, active, and upcoming lyric lines in real time.
- **📦 Native Windows Installer (`.exe`):** Full Windows installation package with Start Menu shortcuts, optional auto-start on login, and clean uninstaller.

---

## 📥 Installation

1. Go to the [**Releases**](https://github.com/umarsandifajar/WINMAK-Desktop/releases/latest) page.
2. Download **`WINMAK-Setup-2.8.0.exe`**.
3. Run the installer and follow the on-screen setup wizard.

### Requirements
- Windows 11 (x64)
- Windows App SDK Runtime (handled automatically by the setup)

---

🙏 Credits & Attribution
Original Project & Core Engine: [wynter-yaxley/atrium-desktop]([wynter-yaxley/atrium-desktop](https://github.com/wynter-yaxley/atrium-desktop)) — Huge thanks to the original creator for the real-time refraction glass engine and architecture.

Widget Modifications & Windows Packaging: [umarsandifajar/WINMAK-Desktop](https://github.com/umarsandifajar/WINMAK-Desktop)

☕ Support & Donate
If you enjoy using WINMAK Desktop and want to support further widget development:

👉 Donate via [**sandiana.my.id**](http://sandiana.my.id)

----

## 🛠️ Building from Source

```powershell
git clone [https://github.com/umarsandifajar/WINMAK-Desktop.git](https://github.com/umarsandifajar/WINMAK-Desktop.git)
cd WINMAK-Desktop
./tools/build.ps1

