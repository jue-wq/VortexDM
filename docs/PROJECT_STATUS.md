# Project status

- Project: VortexDM (C++23)
- Status: **IN_PROGRESS**
- Current phase: **Phase 1 — C++23 foundation and platform build matrix**
- Supported targets: Windows 10/11, Linux and Android 9+ arm64-v8a. **macOS removed.**
- Latest platform correction: Android arm64 preset and CI build added; desktop Native Messaging host excluded from Android.
- Not yet verified: Android cross-build and desktop CI must pass before Phase 1 completion. No phase is marked DONE.
- Next action: validate Windows/Linux/Android builds, then continue reliable persistence and crash recovery.

The machine-readable source of truth is `../progress.json`; run `python tools/check_progress.py` before updating phase status. See `PLATFORM_SUPPORT.md`.
