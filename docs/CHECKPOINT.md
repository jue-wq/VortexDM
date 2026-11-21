# Durable checkpoint

- Current phase: 1 of 6 (IN_PROGRESS)
- Platform scope: Windows + Linux + Android arm64; macOS is excluded.
- New phase-1 code: CrashSafeFileWriter uses positional pwrite on POSIX/Android and Windows OVERLAPPED writes, preallocates file size, and exposes explicit durable flush.
- New acceptance test: 16 threads write 100 distinct 1 MiB segments (100 MiB total) in shuffled order; final file is verified with SHA-256.
- CI before this change: run 44953047590 passed checkpoint, Linux desktop and Android arm64 builds; Windows desktop is pending.
- Sanitizer CI matrix now runs ASan/UBSan and TSan separately; results pending.
- No phase is marked DONE.
- Resume: run python tools/check_progress.py; run Windows/Linux/Android matrix; inspect 100 MiB SHA-256 test; resolve sanitizer failures before transition.
- Next task: complete matrix and sanitizer runs, then implement transactional SQLite migrations and segment journal.
