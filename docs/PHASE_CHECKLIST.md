# Phase checklist

A checked item describes an implemented/reviewed artifact; a phase is complete only when its Definition of Done and CI evidence exist.

## Phase 1 — C++23 foundation and platform build matrix
- [x] Repository layout, CMake presets and vcpkg manifest
- [x] Shared IPC v1 contract
- [x] URL validation and task state machine
- [x] Positional crash-safe file writer (pwrite / Windows OVERLAPPED) with preallocation and durable flush
- [x] 16-thread, 100 MiB, SHA-256 concurrent-write acceptance test
- [x] Clean Windows and Linux builds with warnings enabled (CI run 44954098299)
- [x] Android arm64 NDK cross-build green
- [x] ASan/UBSan and TSan CI runs green (CI run 44954098299)

## Phase 2 — Persistence and reliable HTTP engine
- [x] SQLite WAL bootstrap and schema version migration v1
- [x] Downloads, download_streams, segments, queues, categories, ytdlp_profiles and settings tables
- [x] Download/stream/segment upsert APIs and segment checkpoint persistence
- [x] Repository migration and checkpoint unit test baseline
- [x] HTTP HEAD + Range 0-0 fallback probe and parallel byte-range transfer engine\n- [x] Bounded segment scheduler and tail work-stealing primitive\n- [ ] Adaptive retry/backoff policy integrated into active transfers
- [ ] Segment journal recovery after forced process termination
- [ ] Crash-safe writer + SQLite commit ordering end-to-end test

## Phase 3 — yt-dlp extraction and toolchain
- [ ] Shell-free process runner with timeout/cancellation
- [ ] Extraction-only JSON metadata parser and format model
- [ ] yt-dlp source profiles, SHA-256 verification and rollback
- [ ] URL refresh hook for expired media URLs

## Phase 4 — HLS/DASH and muxing
- [ ] HLS master/media playlists and AES-128 support
- [ ] DASH SegmentTemplate/SegmentList/SegmentBase
- [ ] ffmpeg stream-copy muxing and subtitle/chapter handling
- [ ] Composite task integration tests

## Phase 5 — App services, queues and cross-platform IPC
- [x] Desktop Native Messaging frame reader baseline
- [ ] Local-user-only Windows/Linux host registration
- [ ] Android opt-in loopback IPC server (127.0.0.1 + random API key)
- [ ] Android foreground service, scoped storage and process-death recovery
- [ ] Queue/category scheduler and typed application services
- [ ] Full bidirectional IPC v1 and batch chunk assembler

## Phase 6 — Responsive Qt UI and release
- [x] User-approved VortexDM icon asset added to Qt resource bundle and application window icon
- [x] Optional Qt 6/QML app shell baseline
- [ ] Desktop ABDM-inspired download list, queues and dialogs
- [ ] Android portrait/landscape layouts, touch controls and back navigation
- [ ] Theme tokens and settings screens
- [ ] Windows installer, Linux packages and Android APK/AAB
- [ ] SLO benchmarks and final acceptance suite
