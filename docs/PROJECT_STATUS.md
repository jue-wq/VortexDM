# Project status

- Project: VortexDM (C++23)
- Status: **IN_PROGRESS**
- Current phase: **Phase 1 — C++23 foundation and platform build matrix**
- Supported targets: Windows 10/11 x64, Linux x64 and Android 9+ arm64-v8a; macOS excluded.
- Implemented: portable crash-safe positional file writer (POSIX pwrite / Windows OVERLAPPED), preallocation, flush-to-disk and a 16-thread 100 MiB SHA-256 acceptance test.
- Previously verified: Linux desktop and Android arm64 CMake builds passed. Windows build and new sanitizer CI runs are pending.
- No phase is marked DONE until all checks and evidence are recorded.
