# Durable checkpoint

- Current phase: 2 of 6 (IN_PROGRESS)
- Platform scope: Windows + Linux + Android arm64; macOS excluded.
- Phase 1 baseline includes CrashSafeFileWriter (POSIX pwrite / Windows OVERLAPPED), preallocation, durable flush and 16-thread/100 MiB SHA-256 test. Linux and Android builds passed in a previous CI run; Windows and sanitizer results for the latest writer commit are pending.
- Phase 2 now includes SQLite WAL schema migration v1, downloads/streams/segments/queues/categories/toolchain/settings tables, typed record structs, upsert operations and a migration/segment persistence unit test.
- Not yet implemented: HTTP Range probing, segmented scheduler, work stealing, adaptive retry and process-kill recovery.
- No phase is marked DONE.
- Resume: run python tools/check_progress.py; run CMake build + ctest; fix Windows and sanitizer failures; add Range probe/scheduler only after this baseline test is green.
- Next task: confirm CI for migration tests, then implement Range probing and segment scheduling.
