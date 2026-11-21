# Durable checkpoint

- Current phase: 1 of 6 (IN_PROGRESS)
- Platform scope: Windows + Linux + Android arm64. macOS is excluded.
- Implemented: CMake/vcpkg baseline, shared IPC v1 contract, URL validation, task state machine, libcurl transfer baseline, SQLite WAL bootstrap, CLI, desktop-only Native Messaging host, optional Qt QML shell, tests and progress validator.
- Platform correction: CMake preset `android-arm64`, Android NDK/vcpkg triplet and Android CI cross-build have been added. Android core build does not package the Qt UI yet; APK requires a configured Qt Android kit.
- Not verified: all new target builds are pending CI. No phase is marked DONE.
- Resume sequence:
  1. Read `progress.json` and this file.
  2. Run `python tools/check_progress.py`.
  3. Run desktop matrix builds and `cmake --preset android-arm64 && cmake --build --preset android-arm64`.
  4. Fix compiler/link/test failures before checking any acceptance item.
  5. Append CI run URL + commit SHA to `progress.json`; update this checkpoint.
- Next task: verify the platform matrix, then implement transactional segments and crash-safe writes. Android foreground service and loopback IPC are Phase 5 requirements.
