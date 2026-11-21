# VortexDM

Modern C++23 download manager for **Windows, Linux, and Android**. Android is
a first-class mobile product with a GeckoView browser and Vortex Shield content
blocking. macOS is not a supported target.

## Current status

The repository is an actively developed foundation, not a feature-complete download manager. The checked boxes below describe implemented components; unchecked items are not shipped capabilities.

- [x] Phase 1: C++23 build foundation, SQLite WAL metadata store, synchronized offset writer, and unit tests
- [x] Phase 1: explicit OS-level file flush primitive (Windows `FlushFileBuffers`, POSIX `fsync`)
- [ ] Phase 1: directory-entry durability, ordered checkpoint commits, crash recovery, and fault-injection validation
- [x] Android: Gradle / GeckoView browser bootstrap, navigation controls, and Android CI
- [x] Android: Vortex Shield host-rule subset with EasyList/EasyPrivacy subscription caching
- [x] Android: user-initiated official uBlock Origin add-on installation path (compatibility still requires validation)
- [ ] Android: full ABP/uBlock filter compatibility, cosmetic filters, site controls, tabs, downloads, and background transfer lifecycle
- [x] Phase 2: inclusive HTTP byte-range planner, streaming TLS transport, strict partial-response validation, bounded range staging, and retry orchestration
- [ ] Phase 2: parallel segment scheduler, durable resume/checkpoint protocol, CLI, and end-to-end transfer tests
- [ ] Phase 3: toolchain manager
- [ ] Phase 4: media extraction and streaming integration
- [x] Phase 5: Native Messaging v1 frame codec and protocol draft
- [ ] Phase 5: JSON schema validation, production native-host process, ACK de-duplication, batch assembler, and queue adapter
- [ ] Phase 6: Qt 6 / QML desktop UI and production Android download UI

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
production download manager. Vortex Shield now fetches and caches EasyList and
EasyPrivacy, but only parses a conservative host-rule subset. It does not yet
provide full ABP/uBlock syntax or uBlock Origin feature parity.

See docs/ARCHITECTURE.md and docs/ANDROID_ARCHITECTURE.md for module
boundaries and Android-specific design decisions.
