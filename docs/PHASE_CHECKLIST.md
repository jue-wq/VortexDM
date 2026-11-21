# Phase checklist

A checked item means code exists and has been reviewed; a phase is not complete until its Definition of Done has CI evidence in `progress.json`.

## Phase 1 — CMake, C++23 foundation and protocol
- [x] Repository layout, CMake presets and vcpkg manifest
- [x] Shared IPC v1 contract checked in
- [x] Initial URL validation, task state machine and local tests
- [ ] Windows, Linux and macOS clean builds verified
- [ ] Native host handshake/chunk integration tests pass

## Phase 2 — Persistence and reliable HTTP engine
- [x] SQLite WAL repository bootstrap
- [x] HTTP(S) libcurl single-stream download baseline
- [ ] Transactional task/segment journal and migrations
- [ ] Range probing, adaptive segmented scheduler and retries
- [ ] Crash-safe file writer and restart recovery tests

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

## Phase 5 — Application services and browser host
- [x] Native Messaging frame reader/writer baseline
- [ ] Local-user-only host registration/installers
- [ ] Queue/category scheduler and typed application services
- [ ] Full bidirectional IPC v1 and batch chunk assembler
- [ ] CLI parity for add/pause/resume/cancel/status

## Phase 6 — Desktop UI, packaging and release
- [x] Optional Qt 6/QML app shell baseline
- [ ] Main download list, queue/category models and dialogs
- [ ] Theme tokens and settings screens
- [ ] Signed/reproducible installers and multi-OS release artifacts
- [ ] SLO benchmarks and final acceptance suite
