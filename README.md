# VortexDM

C++23 download manager with a UI-independent core, libcurl transfer engine, SQLite WAL persistence, CLI and optional Qt 6/QML desktop/mobile shell.

## Supported targets
- Windows 10/11 (x64)
- Linux (x64; X11/Wayland)
- Android 9+ (arm64-v8a; Qt 6 Android package for the APK UI)

**macOS is not a target.** Android is the replacement. The Android app uses the same C++ core and a responsive Qt Quick UI; browser integration uses an explicitly enabled loopback HTTP bridge because Android Firefox does not expose Native Messaging.

## Build
Desktop: `cmake --preset default && cmake --build --preset default && ctest --preset default`

Android core: `cmake --preset android-arm64 && cmake --build --preset android-arm64`

For an Android APK, install a matching Qt 6 Android kit and configure with `-DVORTEX_BUILD_UI=ON`; see `docs/PLATFORM_SUPPORT.md`.

## Durable checkpoints
Read `docs/PROJECT_STATUS.md`, `docs/PHASE_CHECKLIST.md`, `docs/CHECKPOINT.md` and `progress.json`. Run `python tools/check_progress.py` before closing a phase. A phase may be marked DONE only with passing checks and CI evidence.

The matching extension is https://github.com/jue-wq/vortex-browser-bridg. The shared contract is `docs/IPC_PROTOCOL.md`.
