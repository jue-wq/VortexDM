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
- [x] SQLite WAL bootstrap and schema version migrations v1-v2 (default queue seed)
- [x] Downloads, download_streams, segments, queues, categories, ytdlp_profiles and settings tables
- [x] Download/stream/segment upsert APIs and segment checkpoint persistence
- [x] Repository migration and checkpoint unit test baseline
- [x] HTTP HEAD + Range 0-0 fallback probe and parallel byte-range transfer engine\n- [x] Bounded segment scheduler and tail work-stealing primitive\n- [x] Bounded exponential retry/backoff integrated into probe and active transfer attempts
- [x] Segment journal recovery coordinator wired into the active ranged-download worker
- [x] Recovery primitives and active worker: reopen existing partial file without truncation, query durable checkpoints and fetch only missing byte ranges
- [x] File fsync-before-SQLite checkpoint ordering helper and recovery coordinator acceptance fixture

## Phase 3 — yt-dlp extraction and toolchain
- [x] Shell-free cross-platform process runner with bounded output, timeout and cancellation
- [x] Extraction-only yt-dlp JSON metadata/playlist parser and format model
- [x] Persisted yt-dlp source profiles, SHA-256 verification and rollback-safe binary activation
- [x] URL refresh hook for expired media URLs
- [x] yt-dlp extraction task execution from the active profile with single/playlist modes and process cancellation

## Phase 4 — HLS/DASH and muxing
- [x] HLS master/media playlist parser, relative URI resolution and AES-128 key metadata (ffmpeg performs supported decryption)
- [x] DASH MPD parser for SegmentTemplate/SegmentTimeline, SegmentList and SegmentBase
- [x] ffmpeg shell-free stream-copy muxing with optional subtitle and metadata/chapter mapping
- [ ] Composite task integration tests

## Phase 5 — App services, queues and cross-platform IPC
- [x] Android ad-blocking architecture decision: original uBlock Origin in Firefox for Android; Browser Bridge handles download handoff only
- [ ] Optional embedded GeckoView + unmodified upstream uBO compatibility proof-of-concept and GPL-3.0 review
- [x] Desktop Native Messaging frame reader baseline
- [ ] Local-user-only Windows/Linux host registration
- [x] Android opt-in loopback IPC server bound to 127.0.0.1 with 256-bit per-launch API key, CORS preflight, size caps and SQLite task persistence
- [x] Android foreground dataSync service scaffold, app-private storage and Android 10+ MediaStore export
- [ ] Android 9 SAF/legacy export and physical process-death recovery acceptance
- [ ] Queue schedule windows/concurrency, category auto-routing and typed application services
- [x] Native host persists acknowledged add_download/add_batch/add_batch_chunk tasks into SQLite with queue IDs
- [x] Android loopback handshake, add_download, add_batch and chunk assembly/persistence
- [x] Task-control dispatch (pause/resume/cancel) and foreground dataSync service scaffold
- [ ] App-to-extension status push and physical-device foreground verification

## Phase 6 — Responsive Qt UI and release
- [x] User-approved VortexDM icon asset added to Qt resource bundle and application window icon
- [x] Optional Qt 6/QML app shell baseline
- [x] Basic desktop download list, live byte progress, named queues and pause/resume/cancel controls
- [ ] Full ABDM-inspired dialogs, filters, categories and detailed task settings
- [x] Android portrait-friendly responsive layout and touch-sized controls
- [ ] Landscape/back navigation and Android device layout acceptance
- [ ] Full theme tokens, settings screens and export controls polish
- [ ] Windows installer, Linux packages and Android APK/AAB
- [ ] SLO benchmarks and final acceptance suite
