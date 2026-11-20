# VortexDM

Modern C++23 desktop download manager. The repository is being initialized from the
project's technical architecture and six-phase development plan.

## Current status

- [x] Phase 1 repository and build-system foundation
- [ ] Phase 1 storage, crash-safe writer, and CI
- [ ] Phase 2 segmented HTTP engine and CLI
- [ ] Phase 3 toolchain manager
- [ ] Phase 4 media extraction and streaming
- [ ] Phase 5 queues and IPC
- [ ] Phase 6 Qt 6 / QML desktop UI

## Build (CMake + vcpkg)

Requirements: CMake 3.28+, a C++23 compiler, Git, and vcpkg.

```sh
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug
```

On Windows, use `windows-msvc-debug`. On macOS, use `macos-clang-debug`.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for non-negotiable module
boundaries and engineering rules. Phase-specific acceptance criteria remain in
the supplied technical plan.
