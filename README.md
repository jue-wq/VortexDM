# VortexDM

Modern C++23 download manager for **Windows, Linux, and Android**. Android is
a first-class mobile product with a GeckoView browser and Vortex Shield content
blocking. macOS is not a supported target.

## Current status

- [x] Phase 1 repository and desktop build-system foundation
- [x] Android Gradle / GeckoView application bootstrap
- [x] Android browser address bar and navigation controls
- [x] Bundled Vortex Shield WebExtension bootstrap
- [x] Android CI job replacing macOS CI
- [x] Phase 1 SQLite WAL persistence, download metadata, offset writer, and unit tests\n- [ ] Phase 1 OS-level durable flush, crash recovery, and fault-injection validation
- [ ] ABP/uBlock-compatible filter parser, subscriptions, and cosmetic filters
- [ ] Official uBlock Origin extension compatibility validation
- [x] Phase 2 inclusive HTTP byte-range planner and unit tests\n- [ ] Phase 2 HTTP transport, retries, segment scheduler, and CLI
- [ ] Phase 3 toolchain manager
- [ ] Phase 4 media extraction and streaming
- [x] Phase 5 Native Messaging v1 frame codec and protocol draft\n- [ ] Phase 5 JSON validation, host process, ACK de-duplication, batch assembler, and queue adapter
- [ ] Phase 6 Qt 6 / QML desktop UI and Android mobile UI

## Desktop build (CMake + vcpkg)

Requirements: CMake 3.28+, a C++23 compiler, Git, and vcpkg.

    cmake --preset linux-clang-debug
    cmake --build --preset linux-clang-debug
    ctest --preset linux-clang-debug

On Windows, use windows-msvc-debug.

## Android build

Requirements: JDK 17, Gradle 8.13, Android SDK 36, Android NDK 28.2,
and CMake 3.31.6.

    gradle -p android assembleDebug

The current Android build is a browser and blocker bootstrap, not yet a
production download manager. Vortex Shield currently blocks a small set of
domains; it does not yet parse EasyList/ABP syntax or provide uBlock Origin
feature parity.

See docs/ARCHITECTURE.md and docs/ANDROID_ARCHITECTURE.md for module
boundaries and Android-specific design decisions.
