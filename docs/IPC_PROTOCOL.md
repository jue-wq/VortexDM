# VortexDM Browser Bridge IPC Protocol v1 (proposed)

> Status: **shared implementation proposal; desktop host not implemented**. This document is the proposed contract for the Vortex Browser Bridge and VortexDM. It is not evidence that the desktop application currently accepts these messages. Update both repositories together when the native-host adapter is implemented.

## 1. Transport

The primary transport is a browser Native Messaging host named `com.vortexdm.native_host`.

Each frame is:
1. A 4-byte unsigned little-endian payload length.
2. Exactly that many UTF-8 bytes containing one JSON object.

The host must reject frames larger than 1 MiB, incomplete frames, invalid UTF-8, invalid JSON, and messages that fail schema validation. The extension targets a 900 KiB serialized JSON budget for batches to leave framing headroom.

The browser extension must not start a TCP listener. A localhost HTTP fallback is a separate opt-in transport and is not part of this Native Messaging contract.

## 2. Version negotiation

The extension sends once after each connection:

```json
{
  "type": "handshake",
  "protocol_version": 1,
  "extension_version": "0.1.0",
  "capabilities": ["native_messaging", "batch_chunking", "status_push", "reconnect"]
}
```

The host must reject unsupported protocol versions by closing the port or returning a documented incompatibility response. For v1, after accepting the handshake the host sends an initial `status_update` and `queues_sync`. The handshake itself does not require an ACK.

## 3. Extension → desktop messages

### `add_download`

Required: `url` (absolute HTTP or HTTPS URL).

Optional: `request_id`, `filename`, `headers` (string map), `cookies` (transient cookie objects), `referrer`, `download_page`, `auto_start`, `queue_id`.

A positive ACK means the download request has been validated and accepted into the desktop queue. It does not mean that the transfer has completed. The browser extension may cancel its original browser download only after receiving this positive ACK.

### `extract_media`

Required: `webpage_url` (absolute HTTP or HTTPS URL).

Optional: `request_id`, `page_title`, `user_agent`, `cookies`, `playlist_mode`.

A positive ACK means the extraction job has been accepted for processing, not that media extraction has finished. Extractor output is data and must never be executed as code.

### `add_batch`

Required: non-empty `links[]`, each containing an absolute HTTP or HTTPS `url`.

Optional: `request_id`, `queue_id`, `auto_start`, `chunk_index`, `chunk_total`. Each link may include `filename`, `headers`, and `referrer`.

When chunk metadata is present, `chunk_index` is zero-based and `chunk_total` is the total number of chunks. The host must bound memory and assembly time, reject duplicate/conflicting chunks, and enqueue the batch only after a complete batch has been assembled. ACK each chunk only after it has been safely accepted into the bounded assembly state. A missing chunk must expire without partially enqueueing the batch.

### `task_control`

Required: `action` = `pause`, `resume`, or `focus_app`.

Optional: `request_id`, `task_id`.

## 4. Desktop → extension messages

### `ack`

```json
{ "type": "ack", "request_id": "unique-request-id", "accepted": true }
```

A negative response may include `error`:

```json
{ "type": "ack", "request_id": "unique-request-id", "accepted": false, "error": "queue is full" }
```

Every request carrying `request_id` must receive exactly one ACK. The host must de-duplicate repeated request IDs within a bounded retention window so reconnect/retry does not enqueue duplicate work. ACKs confirm acceptance only, not completion.

### `status_update`

Required: `active_downloads[]`. Each item has `id`, `filename`, and `status`; optional `progress` (0–100) and `speed` (bytes/second). Optional top-level `total_speed` and `theme` (`modern-dark`, `pure-oled`, `modern-light`).

### `queues_sync`

Required: `queues[]`, each with `id` and `name`; optional `category`.

## 5. Cookie and secret handling

Cookies are optional transient fields only. The extension must read them only after an explicit user action and only when the user has granted the relevant browser permission. Neither side may persist cookies in extension settings, download metadata, logs, telemetry, or crash reports. Redact authorization headers and credentials from all diagnostics.

## 6. Native host security and registration

- The host must validate the connecting browser origin against the exact extension ID(s) shipped by the project.
- The host must use an absolute executable path and a narrowly scoped manifest.
- The host must not trust URLs, filenames, headers, cookie values, queue IDs, or task IDs received from the browser.
- Apply message-size, rate, queue-capacity, and batch-assembly limits.
- Never execute a URL, filename, header, or extractor result as a shell command.
- Register platform-specific host manifests as part of the VortexDM installer. Do not ask users to hand-edit manifests for production releases.

## 7. Implementation status

The browser bridge currently implements the v1-shaped Zod schemas, Native Messaging framing via the browser API, reconnect logic, and a development mock host. The VortexDM repository currently has no production Native Messaging host, request de-duplication store, batch assembler, or queue adapter. End-to-end desktop integration is therefore **not complete**.

Before declaring v1 stable, add host-side schema/serialization tests and deterministic integration tests that launch the real host process, exchange frames, test ACK timeouts/reconnects, and verify that a download is never cancelled before queue acceptance.
