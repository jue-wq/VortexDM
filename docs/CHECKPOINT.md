# Durable checkpoint

- Current phase: 2 of 6 (IN_PROGRESS)
- Platform scope: Windows + Linux + Android arm64; macOS excluded.
- Android uBlock decision: use original Raymond Hill uBlock Origin via official Mozilla Add-ons in Firefox for Android. VortexDM remains the downloader; Browser Bridge performs handoff, not a uBO clone. Embedded GeckoView + upstream uBO is experimental until API/device/GPL checks pass. See docs/ANDROID_UBO_INTEGRATION.md.
- Phase 1 baseline includes CrashSafeFileWriter (POSIX pwrite / Windows OVERLAPPED), preallocation, durable flush and 16-thread/100 MiB SHA-256 test. CI run 44954098299 passed Windows, Linux, Android arm64, ASan/UBSan and TSan.
- Phase 2 now includes SQLite WAL schema migration v1, downloads/streams/segments/queues/categories/toolchain/settings tables, typed record structs, upsert operations and a migration/segment persistence unit test.
- Phase 2 includes HTTP HEAD with Range 0-0 fallback, parallel byte-range downloads, bounded scheduler/tail stealing, capped exponential retry, non-truncating partial-file reopen, segment journal query/planning, a recovery coordinator, and an fsync-before-SQLite checkpoint helper. Recovery and ordering acceptance fixtures have been added; CI execution is intentionally deferred until the planned phases are implemented.
- Branding: the newly supplied VortexDM icon (32px PNG derived from the uploaded artwork) is bundled in the Qt resource system and set as the application window icon. The original ICO/PNG masters remain the design source.
- Icon CI for commit `fd96c2f` is running; no phase is marked DONE.
- Phase 3 implementation now includes shell-free process execution, yt-dlp metadata/playlist parsing, persisted source profiles, SHA-256-verified staged activation/rollback and expired-media URL refresh. Phase 4 has a shell-free ffmpeg stream-copy mux service. HLS/DASH manifest handling and complete composite orchestration remain outstanding. Defer full test pass until all planned phases are implemented as requested.
- Next task: confirm CI for migration tests, then implement Range probing and segment scheduling.
