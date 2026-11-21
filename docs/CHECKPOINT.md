# Durable checkpoint

- Current phase: 1 of 6 (IN_PROGRESS)
- Included: CMake/vcpkg baseline, shared IPC v1, URL validation, task state machine, libcurl transfer baseline, SQLite WAL bootstrap, CLI/Native Messaging shells, optional Qt QML shell, unit tests, progress validator and CI matrix.
- CI findings: checkpoint validation passed. All three build jobs stopped before configure because the vcpkg manifest did not pin a builtin baseline. Baseline has now been pinned to `93ad765c7b66ab9ce97469ec23a90b15c3a14ee5`; new matrix run required.
- No phase is marked DONE. The C++ compiler and tests have not yet completed in CI.
- Resume: read progress.json; run `python tools/check_progress.py`; configure/build/test; fix errors; append run URL and SHA evidence before changing checks.
- Next task: verify multi-OS build and host frame tests, then implement transactional segment persistence.
