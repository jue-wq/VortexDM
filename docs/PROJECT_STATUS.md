# Project status

- Project: **VortexDM** (C++23)
- Status: **IN_PROGRESS**
- Current implementation phase: **Phase 6 — responsive Qt UI and release**
- Supported targets: Windows 10/11 x64, Linux x64 and Android 9+ arm64-v8a. macOS is explicitly excluded.
- Android ad blocking: use the original, unmodified Raymond Hill uBlock Origin distributed through Mozilla Firefox for Android. The VortexDM Android app/Bridge handles download handoff; it does not ship a replacement filter engine. Embedded GeckoView + upstream uBO remains an optional, unverified experiment.
- Completed implementation areas: core/platform scaffolding, SQLite WAL persistence, segmented HTTP transfers and recovery, yt-dlp extraction profiles, HLS/DASH manifest parsing, ffmpeg mux primitive, desktop Native Messaging, Android authenticated loopback IPC, foreground-service scaffold, Android 10+ MediaStore export, and responsive Qt/QML shell.
- Recent additions: Android 9 SAF document-picker/copy helpers (Qt activity-result wiring still pending); queue dispatch now ignores tasks assigned to inactive queues, with regression coverage added.
- Remaining implementation gaps: end-to-end HLS/DASH download-to-mux orchestration; full queue scheduling/category routing; Android 9 SAF Qt activity-result wiring; app-to-extension status push; desktop settings/dialog polish; Android device acceptance; installers and signed APK/AAB; SLO benchmarks.
- Recent maintenance: worker shutdown now uses the worker-owned atomic stop flag; the task monitor uses a portable std::thread plus an atomic stop flag (no std::jthread dependency).
- Verification policy: implementation-first. Consolidated tests/platform builds are deferred until implementation is complete. Do not mark any phase DONE without passing CI and acceptance evidence.
- Latest relevant CI run at this checkpoint: run 44993181657 was still executing when inspected; its jobs had not yet completed. A newer push triggered another CI run.
- Next action: finish the remaining implementation gaps, then execute and repair the complete Windows/Linux/Android and sanitizer validation matrix.
