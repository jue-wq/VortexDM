# Durable checkpoint

- Current phase: 1 of 6 (IN_PROGRESS)
- Platform scope: Windows + Linux + Android arm64; macOS is explicitly excluded.
- Implemented: C++23 core baseline, libcurl single stream, SQLite WAL bootstrap, CLI, desktop Native Messaging frame reader, optional Qt UI, shared IPC v1 and Android arm64 build preset.
- CI evidence: run 44953047590 — checkpoint passed, Linux desktop configure/build/test passed, Android SDK/NDK + vcpkg setup passed, Android CMake configure and core build passed. Windows desktop configure/build is still in progress.
- Remaining Phase 1 DoD: Windows build, sanitizer coverage and 16-thread crash-safe writer test.
- No phase is marked DONE.
- Resume: read progress.json; run python tools/check_progress.py; wait for Windows build result; fix any build errors; implement crash-safe file writer/sanitizer gate; attach test run URL and commit SHA before phase transition.
- Next task: close Windows matrix, then finish crash-safe file writer and transactional SQLite migrations.
