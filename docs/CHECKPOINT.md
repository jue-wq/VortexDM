# Durable checkpoint

- Current phase: 1 of 6 (IN_PROGRESS)
- Platform scope: Windows + Linux + Android arm64; macOS is explicitly excluded.
- Implemented: C++23 core baseline, libcurl single stream, SQLite WAL bootstrap, CLI, desktop Native Messaging frame reader, optional Qt UI, shared IPC v1, Android arm64 preset and CI.
- CI evidence: checkpoint job passed. Linux desktop build completed successfully in run 44952459886. Android SDK/NDK and vcpkg setup passed, but CMake configure failed while CMake FindThreads probed Android pthread support. The Android preset now sets CMAKE_HAVE_LIBC_PTHREAD=ON because Bionic provides pthread through libc; rerun required. Windows desktop job remains under verification.
- No phase is marked DONE.
- Resume: read progress.json; run python tools/check_progress.py; run desktop matrix and Android arm64 build; address compiler/link errors; attach CI run URL and commit SHA before marking checks.
- Next task: verify Android configure/build with the Bionic Threads hint, then implement durable segment persistence/crash recovery.
