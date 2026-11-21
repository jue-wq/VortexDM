# Durable checkpoint

- Current phase: 2 of 6 (IN_PROGRESS)
- Platform scope: Windows + Linux + Android arm64; macOS excluded.
- Phase 1 baseline includes CrashSafeFileWriter (POSIX pwrite / Windows OVERLAPPED), preallocation, durable flush and 16-thread/100 MiB SHA-256 test. CI run 44954098299 passed Windows, Linux, Android arm64, ASan/UBSan and TSan.
- Phase 2 now includes SQLite WAL schema migration v1, downloads/streams/segments/queues/categories/toolchain/settings tables, typed record structs, upsert operations and a migration/segment persistence unit test.
- Phase 2 also includes HTTP HEAD with Range 0-0 fallback, parallel byte-range downloads and a bounded segment scheduler with tail work stealing. Remaining: adaptive retry wired into transfers, segment journal recovery after forced termination, and writer/SQLite commit ordering E2E.
- Branding: the newly supplied VortexDM icon (32px PNG derived from the uploaded artwork) is bundled in the Qt resource system and set as the application window icon. The original ICO/PNG masters remain the design source.
- Icon CI for commit `fd96c2f` is running; no phase is marked DONE.
- Resume: run python tools/check_progress.py; run CMake build + ctest; finish Phase 2 recovery/retry integration; continue Android service/UI and release acceptance.
- Next task: confirm CI for migration tests, then implement Range probing and segment scheduling.
