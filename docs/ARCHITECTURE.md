# VortexDM architecture

## Dependency direction
`vortex_core` is independent of Qt, QML and browser APIs. UI, CLI, desktop Native Messaging host and Android app are adapters over the core. Network code owns transfer handles; repository writes are serialized.

## Supported platforms
Windows 10/11, Linux (X11/Wayland), and Android 9+ arm64-v8a. macOS is explicitly out of scope. Android builds the same C++ core using the Android NDK and vcpkg `arm64-android` triplet; Qt Quick supplies the mobile UI when the Qt Android kit is installed.

## Current implementation boundary
The initial baseline provides HTTP(S) URL validation, a libcurl single-stream transfer, a task state machine, SQLite WAL initialization, CLI entry point and desktop 4-byte little-endian Native Messaging frame handling. Parallel range scheduling, crash-safe segment journals, HLS/DASH, signed extractor URL refresh, queue scheduler, Android foreground-service lifecycle, Android loopback API and production installers remain incomplete; see `PHASE_CHECKLIST.md`.

## Android-specific invariants
- Download state and temporary files live in app-private storage.
- Long-running downloads require a foreground-service implementation and process-death recovery before production.
- Do not build the desktop Native Messaging executable for Android.
- An Android local bridge, when implemented, must bind exclusively to 127.0.0.1, use a random API key and be opt-in. Never expose a public LAN listener.
- Keep QML responsive for portrait/landscape and touch controls.

## Security
- Never invoke a shell to start a process.
- Never persist browser cookie values or log request secrets.
- Allow only HTTP and HTTPS download URLs.
- Sanitize output file names and keep writes under the selected directory.
- Keep local IPC scoped to the current OS user; do not expose an unauthenticated public listener.
- yt-dlp is metadata-extractor-only (`--dump-single-json --no-download`).

## IPC
`IPC_PROTOCOL.md` is the source of truth. Desktop frames are 4-byte little-endian length-prefixed UTF-8 JSON, maximum 1 MiB. Android loopback bridge must preserve the same envelope and validation.
