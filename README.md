# VortexDM

C++23 download manager with a UI-independent core, libcurl transfer engine, SQLite WAL persistence, Native Messaging host, CLI and optional Qt 6/QML shell.

## Build
```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```
Dependencies are declared in `vcpkg.json`. Qt UI is optional and disabled by default so the core and tests can build headlessly.

## Checkpoints
See `docs/PROJECT_STATUS.md`, `docs/PHASE_CHECKLIST.md` and `docs/CHECKPOINT.md`. Before changing phases, update `progress.json`, record test evidence and run `python tools/check_progress.py`.

The matching extension is https://github.com/jue-wq/vortex-browser-bridg. The shared contract is `docs/IPC_PROTOCOL.md`.
