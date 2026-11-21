# Durable checkpoint

- Current phase: 1 of 6 (IN_PROGRESS)
- Included: repo layout, CMake/vcpkg setup, shared IPC v1 contract, URL validation, task state machine, libcurl transfer baseline, SQLite WAL bootstrap, CLI/Native Messaging executable shells, optional Qt QML shell, unit tests and CI.
- Not yet verified: this commit has not yet produced observed compiler/platform CI results; no phase is marked DONE.
- Resume sequence:
  1. Read `progress.json` and this file.
  2. Run `python tools/check_progress.py`.
  3. Configure/build/test with documented CMake presets.
  4. Fix failures before marking checks complete.
  5. Append test run URL and commit SHA to `progress.json`, then update this checkpoint.
- Next engineering task: run matrix builds, then finish Phase 1 acceptance and implement transactional segment persistence.
