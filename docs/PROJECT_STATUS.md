# Project status

- Project: VortexDM (C++23)
- Status: **IN_PROGRESS**
- Current phase: **Phase 1 — C++23 foundation and platform build matrix**
- Supported targets: Windows 10/11 x64, Linux x64 and Android 9+ arm64-v8a. **macOS is not supported.**
- CI: Linux desktop configure/build/test passed; Android arm64 core configure/build passed; checkpoint validator passed. Windows desktop job is still running.
- Phase 1 remaining: Windows CI result, 16-thread crash-safe writer test and sanitizer coverage. No phase is marked DONE.
- Next action: close the platform matrix, then implement the crash-safe persistence acceptance gate.
