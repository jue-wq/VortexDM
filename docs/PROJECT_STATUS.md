# Project status

- Project: VortexDM (C++23)
- Status: **IN_PROGRESS**
- Current phase: **Phase 2 — persistence and reliable HTTP engine**
- Supported targets: Windows 10/11 x64, Linux x64 and Android 9+ arm64-v8a; macOS excluded.
- Implemented: Phase 1 core baseline, crash-safe positional writer and 16-thread SHA-256 test; Phase 2 SQLite WAL v1 migration schema, download/stream/segment repositories and unit-test coverage added.
- CI verification for latest C++ commit, Windows and sanitizer runs are pending. No phase is marked DONE.
- Next action: verify migrations/segment test on CI, then implement Range probing and segmented scheduler.
