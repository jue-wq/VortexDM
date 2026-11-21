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
- [x] Phase 1 SQLite WAL persistence, download metadata, offset writer, guarded compare-and-swap state transitions, and unit tests
- [ ] Phase 1 OS-level durable flush, crash recovery, and fault-injection validation
- [x] Vortex Shield host-rule filter subset and EasyList/EasyPrivacy subscriptions
- [ ] Full ABP/uBlock-compatible parser, cosmetic filters, and per-site controls
- [ ] Official uBlock Origin extension compatibility validation
- [x] Phase 2 inclusive HTTP byte-range planner and unit tests
- [x] Phase 2 transactional FIFO claim primitive for queued downloads (multi-worker safe)
- [ ] Phase 2 HTTP transport, retries, segment scheduler, and CLI
- [x] Phase 2 strict HTTP response metadata parser and unit tests (Content-Length, Content-Range, Accept-Ranges, Retry-After, validators)
- [ ] Phase 3 toolchain manager
- [ ] Phase 4 media extraction and streaming
- [x] Phase 5 Native Messaging v1 frame codec and protocol draft
- [x] Phase 5 JSON dispatcher, Firefox Native Messaging host, durable request de-duplication, and bounded batch assembler foundation
- [x] Phase 5 crash-replay-safe native inbox importer and delivery ledger
- [ ] Phase 5 wire inbox importer into the running application queue; batch/media/task-control dispatch and status publishing
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
production download manager. The desktop Native Messaging host is partial: it
stages single-download requests in a separate SQLite inbox and safely returns
negative ACKs until an application queue worker consumes them. A reusable,
idempotent inbox importer is implemented but is not yet invoked by an app
startup/queue worker. Vortex Shield now fetches and caches EasyList and
EasyPrivacy, but only parses a conservative host-rule subset. It does not yet
provide full ABP/uBlock syntax or uBlock Origin feature parity.

See docs/ARCHITECTURE.md and docs/ANDROID_ARCHITECTURE.md for module
boundaries and Android-specific design decisions.
