# Android build and runtime notes

## Supported target
- Android 9 (API 28) and newer
- Primary ABI: arm64-v8a
- Qt 6.8+ Quick and Network modules
- Android SDK, NDK and JDK versions supported by the selected Qt 6.8 kit
- macOS is not a supported target

## Build configuration
Use a Qt for Android kit and an Android arm64 vcpkg triplet. The project requires libcurl, SQLite, nlohmann-json, OpenSSL and tinyxml2 for the core. Set:
- `VORTEX_BUILD_UI=ON`
- `VORTEX_BUILD_TESTS=OFF` for the Android application build
- `ANDROID_ABI=arm64-v8a`
- `ANDROID_PLATFORM=android-28`
- `QT_ANDROID_PACKAGE_SOURCE_DIR=<repo>/android`

The Android package declares INTERNET and dataSync foreground-service permissions. The Java foreground service keeps the application process alive while the in-process download worker is active. If Android terminates the process, persisted queued tasks remain in SQLite and are picked up when VortexDM is launched again. Active task states are reset to Queued on worker startup; partial byte-range resume is not yet wired into the main queue worker.

## Browser Bridge connection
1. Launch VortexDM on the Android device.
2. In the app, copy the loopback port and one-time API key shown in the Android Browser Bridge panel.
3. In Firefox for Android, open Vortex Browser Bridge settings and enter the port/key.
4. Use the Bridge popup's queue selector to choose a VortexDM queue.
5. Confirm the app accepts a test link and shows it in the queue.

The server binds only to 127.0.0.1, chooses an ephemeral port, requires a per-launch 256-bit API key, caps request/header sizes, and exposes a health endpoint plus IPC v1. The key changes each app launch; re-enter it in Bridge settings after restarting VortexDM.

## Android ad blocking
Use the original Raymond Hill uBlock Origin extension distributed by Mozilla for Firefox for Android:
https://addons.mozilla.org/android/addon/ublock-origin/

Vortex Browser Bridge disables its own network and cosmetic filtering on Firefox for Android to avoid a duplicate blocker. uBO filters browser web content only; it is not a device-wide blocker.

## Release gates still required
- Build and install the arm64 APK on API 28, API 34 and current Android API devices
- Verify foreground-service notification and process survival with screen off
- Verify loopback CORS preflight, API-key rejection, large-batch chunks and app restart behavior on a physical device
- Verify Android download permission/scoped-storage behavior and destination access
- Run full core tests on desktop CI and Android instrumentation/device acceptance
