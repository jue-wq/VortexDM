# VortexDM Architecture Guardrails

These rules are mandatory for all modules and contributors.

## 1. Module boundaries

- `vortex-core` is a UI-independent C++23 library. It must not include Qt headers
  or depend on QML, windowing, or platform UI APIs.
- UI, CLI, IPC, and native-host code are adapters around the core; business rules
  belong in the core.
- Network, persistence, process execution, and filesystem operations are exposed
  behind narrow interfaces so tests can replace them.
- Public headers must include only what they need and must not expose third-party
  implementation types unless unavoidable.

## 2. Error and memory policy

- Use `vortex::Result<T>` (`std::expected<T, VortexError>`) at core boundaries.
- Do not use raw owning `new` / `delete`; use RAII and standard smart pointers.
- Exceptions must not cross core/API boundaries. Catch exceptions at adapter
  boundaries and convert them to `VortexError`.
- Cancellation must be explicit and propagated with `std::stop_token`.

## 3. Concurrency and data integrity

- Every mutable task has one authoritative state owner.
- Segment ranges must be disjoint, validated, and persisted before workers write.
- A completed segment is not durable merely because bytes reached a userspace
  buffer. Define and test the file flush / metadata commit ordering.
- SQLite transactions protect database state; they do not make filesystem writes
  part of the same atomic transaction.
- Recovery must reconcile database checkpoints with the actual partial file.
  Never claim cross-resource, absolute crash safety without a defined recovery
  protocol and fault-injection tests.

## 4. Networking and security

- TLS certificate verification is enabled by default and cannot be silently
  disabled.
- Redirects, proxy settings, cookies, and extracted HTTP headers are treated as
  untrusted input and validated at boundaries.
- Secrets (cookies, proxy credentials, API tokens) must never appear in logs.
- Local IPC must authenticate the peer where the platform supports it and must
  validate message size, schema, and protocol version.
- Extractor output is data, not executable instructions. The extractor is never
  the download transport.

## 5. Testing and quality gates

- Unit tests cover parsers, state transitions, range allocation, serialization,
  and error mapping.
- Integration tests use deterministic local mock servers and temporary
  directories; they must not depend on public websites.
- Run ASan/UBSan in CI where supported. Run TSan in a separate concurrency job.
- A phase is complete only when its documented acceptance criteria are met by
  repeatable tests on the supported toolchains.

## 6. Implemented foundation (current)

- SHA-256 file hashing through OpenSSL EVP.
- SQLite database with WAL, foreign-key enforcement, versioned migration
  metadata, download task CRUD/progress persistence, and per-segment checkpoint
  metadata (byte range, SHA-256, and update time).
- Synchronized offset writer with pre-sized partial files, strict bounds checks,
  and an explicit `flush_durable()` using `FlushFileBuffers` on Windows and
  `fsync` on POSIX.
- Segment checkpoint manager that orders write -> OS-level durable file flush ->
  on-disk SHA-256 verification -> SQLite checkpoint insert. Its recovery pass
  re-hashes stored ranges and removes malformed or mismatched checkpoints while
  retaining metadata on file I/O errors. This is a recoverable two-resource
  protocol, not an atomic filesystem/database transaction. Directory-entry
  durability, crash fault-injection, and end-to-end restart orchestration remain
  pending. The manager requires the caller to supply the same path opened by
  its offset writer.
- Balanced inclusive HTTP byte-range planner with a configurable segment cap.
- Bounded exponential retry policy for transient HTTP failures, including capped Retry-After handling.
- Strict HTTP `Content-Range` parser and partial-response validator.
- libcurl-based streaming HTTP client with TLS peer/host verification, bounded connect timeout, cooperative cancellation, header capture, and redirects disabled for explicit policy validation.
- Bounded range coordinator that stages at most 4 MiB per request, validates status 206, exact requested byte bounds, resource size, `Content-Length`, and received body size before writing. Transient network failures and HTTP 408/429/5xx use capped exponential backoff and numeric `Retry-After` handling.
- Native Messaging v1 frame codec with little-endian length prefix, 1 MiB payload cap, exact-frame validation, and UTF-8 validation.
- Unit tests for hashing, database persistence, offset writes, range
  partitioning, retry decisions, and `Content-Range` validation.

The parallel segment scheduler, end-to-end restart orchestration around the checkpoint manager, Android JNI linkage to the shared storage/download modules, JSON schema validation, native host process, and end-to-end browser-to-desktop queue adapter are not implemented yet.

## 7. Phase 1 implementation order

1. Build configuration and dependency resolution.
2. Typed error/result primitives and logging.
3. SQLite connection, WAL configuration, migrations, and repositories.
4. Preallocated offset-based file writer with explicit durability semantics.
5. Fault-injection and parallel-write tests.
