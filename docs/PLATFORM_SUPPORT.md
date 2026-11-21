# Platform support and Android port

## Supported operating systems
| Target | Status | Build |
|---|---|---|
| Windows 10/11 x64 | Primary desktop target | CMake + vcpkg + MSVC |
| Linux x64 (X11/Wayland) | Primary desktop target | CMake + vcpkg + GCC/Clang |
| Android 9+ arm64-v8a | Mobile target | CMake + Android NDK + vcpkg `arm64-android` |
| macOS | **Not supported / removed** | No build or release job |

The Android application reuses the C++23 core and a responsive Qt Quick UI. The first Android CI gate cross-compiles the core and CLI; APK packaging is a separate gate requiring a Qt 6.8+ Android kit, Android SDK/NDK, JDK and Gradle.

## Android architecture differences
- Use `arm64-v8a` as the first supported ABI. Additional ABIs are not promised until tested.
- UI layouts must adapt to portrait/landscape, touch input, display cutouts and Android back/lifecycle events.
- Use app-private storage for partial downloads and database files; never assume desktop paths or unrestricted filesystem access.
- Downloads must cooperate with Android process suspension, battery restrictions and scoped storage. Long-running transfers need an Android foreground-service integration before release.
- Do not build `vortex-native-host` on Android. Firefox for Android does not expose `runtime.connectNative()`; use the opt-in loopback bridge below.
- Browser-to-app handoff on one device uses a random API key and an HTTP endpoint bound only to `127.0.0.1`. The endpoint is disabled by default, requires explicit enablement, validates protocol v1, and must never bind to `0.0.0.0`.
- Android ad/tracker filtering is provided by the original uBlock Origin extension in Firefox for Android, not by the VortexDM download manager or a VortexDM-written filter clone. See docs/ANDROID_UBO_INTEGRATION.md.

## Build prerequisites
Install Android SDK platform 35, Android NDK 27.2.12479018, CMake/Ninja and a vcpkg checkout matching `builtin-baseline` in `vcpkg.json`.

Core cross-compile:
```bash
export ANDROID_NDK_HOME="$ANDROID_SDK_ROOT/ndk/27.2.12479018"
cmake --preset android-arm64
cmake --build --preset android-arm64
```

APK packaging requires a Qt 6.8+ Android kit using the same NDK revision:
```bash
cmake --preset android-arm64 -DVORTEX_BUILD_UI=ON
cmake --build --preset android-arm64
```
The Android QML experience is not considered complete until APK install/launch, background download lifecycle and storage-permission tests pass on a real device and emulator.
