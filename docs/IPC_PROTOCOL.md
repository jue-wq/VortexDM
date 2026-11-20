# VortexDM Browser Bridge IPC Protocol v1 (proposed)

> Status: **partial Native Messaging host implementation**. A standalone Firefox host executable now validates the pinned Gecko extension ID, requires a v1 handshake, and handles durable `add_download` ACKs. Batch, media extraction, and task-control requests are explicitly rejected until their adapters exist.

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

The standalone host requires a v1 handshake as the first frame and closes the session on malformed/unsupported protocol messages. It currently does not publish the initial `status_update` or `queues_sync` messages. The handshake itself does not require an ACK.

## 3. Extension → desktop messages

### `add_download`

Required: `url` (absolute HTTP or HTTPS URL).

Optional: `request_id`, `filename`, `headers` (string map), `cookies` (transient cookie objects), `referrer`, `download_page`, `auto_start`, `queue_id`.

The current dispatcher validates a basic HTTP(S) URL, strips directory components from the requested filename, atomically persists the download and request ID in SQLite, and only then returns a positive ACK. This is durable inbox acceptance, not proof that the application's transfer queue has started the item. The browser extension may cancel its original browser download only after receiving this positive ACK; production queue integration is still required before that behavior is safe to ship. Headers, cookies, referrer, download-page, queue selection, and `auto_start` are currently not applied by this dispatcher; in particular, cookies are ignored and never persisted. The ACK therefore confirms only that the URL and safe filename were durably recorded.

### `extract_media`

Required: `webpage_url` (absolute HTTP or HTTPS URL).

Optional: `request_id`, `page_title`, `user_agent`, `cookies`, `playlist_mode`.

A positive ACK means the extraction job has been accepted for processing, not that media extraction has finished. Extractor output is data and must never be executed as code.

### `add_batch`

Required: non-empty `links[]`, each containing an absolute HTTP or HTTPS `url`.

Optional: `request_id`, `queue_id`, `auto_start`, `chunk_index`, `chunk_total`. Each link may include `filename`, `headers`, and `referrer`.

When chunk metadata is present, `chunk_index` is zero-based and `chunk_total` is the total number of chunks. The host must bound memory and assembly time, reject duplicate/conflicting chunks, and enqueue the batch only after a complete batch has been assembled. The extension schema permits up to 128 chunks per batch; the desktop assembler default must accept that same maximum (subject to its independent byte, item-count, and pending-batch limits). ACK each chunk only after it has been safely accepted into the bounded assembly state. A missing chunk must expire without partially enqueueing the batch.

### `task_control`

Required: `action` = `pause`, `resume`, or `focus_app`.

Optional: `request_id`, `task_id`.


The desktop batch assembler and current single-download dispatcher reject non-HTTP(S) URLs, empty authorities, URL user-info credentials, and ASCII whitespace/control characters. This is only defense in depth, not a complete standards-compliant URL parser or SSRF/network policy.

## 4. Desktop → extension messages

### `ack`

```json
{ "type": "ack", "request_id": "unique-request-id", "accepted": true }
```

A negative response may include `error`:

```json
{ "type": "ack", "request_id": "unique-request-id", "accepted": false, "error": "queue is full" }
```

Every request carrying `request_id` must receive exactly one ACK. The host must de-duplicate repeated request IDs within a bounded retention window so reconnect/retry does not enqueue duplicate work. ACKs confirm acceptance only, not completion. The desktop persistence layer currently accepts request IDs of 1–128 printable ASCII characters (no spaces or control bytes); the dispatcher must reject values outside this subset before enqueue.

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

The browser bridge implements the v1-shaped Zod schemas, Native Messaging framing via the browser API, reconnect logic, and a development mock host. VortexDM now contains a JSON dispatcher for protocol-v1 handshake and `add_download`, durable SQLite request-ID deduplication, and a bounded in-memory batch assembler. The dispatcher explicitly rejects `add_batch`, `extract_media`, and `task_control`; the batch assembler is not yet wired into dispatch. A standalone host executable and a Linux Firefox manifest template are now present. The host accepts only the pinned Firefox extension ID `browser-bridge@vortexdm.app`; Chrome is deliberately denied until its stable extension ID is pinned. The host writes to a per-user `native-inbox.sqlite` database (override with `VORTEXDM_DB_PATH`), not the app's transfer queue. Windows/macOS manifest registration, a real application queue adapter, status publisher, and process-level E2E suite are still missing. End-to-end desktop integration is therefore **not complete**.

Before declaring v1 stable, complete platform registration, wire the inbox to the application transfer queue, implement complete batch assembly, add full URL parsing/network policy, publish status/queue state, and run deterministic process-level tests for frames, ACK/reconnect, and browser-download cancellation ordering.
