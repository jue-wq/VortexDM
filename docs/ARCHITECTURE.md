# VortexDM architecture

## Dependency direction
`vortex_core` is independent of Qt, QML and browser APIs. UI, CLI and Native Messaging host are adapters over the core. Network code owns transfer handles; repository writes are serialized.

## Current implementation boundary
The initial baseline provides HTTP(S) URL validation, a libcurl single-stream transfer, a task state machine, SQLite WAL initialization, CLI entry point, and 4-byte little-endian Native Messaging frame handling. Parallel range scheduling, crash-safe segment journals, HLS/DASH, signed extractor URL refresh, queue scheduler, and production installers are not complete; see `PHASE_CHECKLIST.md`.

## Security
- Never invoke a shell to start a process.
- Never persist cookie values or log request secrets.
- Allow only HTTP and HTTPS download URLs.
- Sanitize output file names and keep writes under the selected directory.
- Keep local IPC scoped to the current OS user; do not expose an unauthenticated public listener.
- yt-dlp is metadata-extractor-only (`--dump-single-json --no-download`).

## IPC
`IPC_PROTOCOL.md` is the source of truth. Frames are 4-byte little-endian length-prefixed UTF-8 JSON, maximum 1 MiB.
