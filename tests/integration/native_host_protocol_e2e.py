#!/usr/bin/env python3
"""Process-level Native Messaging protocol and persistence regression test."""
from __future__ import annotations

import json
import os
import sqlite3
import struct
import subprocess
import sys
from pathlib import Path


def frame(message: dict) -> bytes:
    payload = json.dumps(message, separators=(",", ":")).encode("utf-8")
    return struct.pack("<I", len(payload)) + payload


def read_frames(data: bytes) -> list[dict]:
    messages: list[dict] = []
    offset = 0
    while offset < len(data):
        if len(data) - offset < 4:
            raise AssertionError("host emitted a truncated frame header")
        length = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        if length == 0 or offset + length > len(data):
            raise AssertionError("host emitted an invalid frame length")
        messages.append(json.loads(data[offset : offset + length]))
        offset += length
    return messages


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: native_host_protocol_e2e.py HOST DB_PATH")
    host = Path(sys.argv[1]).resolve()
    db_path = Path(sys.argv[2]).resolve()
    db_path.parent.mkdir(parents=True, exist_ok=True)
    if db_path.exists():
        db_path.unlink()
    env = os.environ.copy()
    env["VORTEXDM_DB_PATH"] = str(db_path)

    handshake = {
        "type": "handshake",
        "protocol_version": 1,
        "extension_version": "0.1.0",
        "capabilities": ["add_download"],
    }
    request = {
        "type": "add_download",
        "request_id": "e2e-stable-request-001",
        "url": "https://example.test/file.bin",
        "filename": "file.bin",
    }
    completed = subprocess.run(
        [str(host), "browser-bridge@vortexdm.app"],
        input=frame(handshake) + frame(request) + frame(request),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
        timeout=15,
        check=False,
    )
    if completed.returncode != 0:
        raise AssertionError(
            f"trusted host exited {completed.returncode}: "
            f"{completed.stderr.decode(errors='replace')}"
        )
    replies = read_frames(completed.stdout)
    if len(replies) != 2:
        raise AssertionError(f"expected two ACKs, received {len(replies)}")
    for reply in replies:
        if reply.get("type") != "ack" or reply.get("request_id") != request["request_id"]:
            raise AssertionError(f"malformed ACK: {reply!r}")
        if reply.get("accepted") is not False:
            raise AssertionError("host must not claim app-queue acceptance before adapter integration")
    if not all("application queue integration is not enabled" in r.get("error", "") for r in replies):
        raise AssertionError("ACK must clearly report that the application queue is not integrated")

    with sqlite3.connect(db_path) as db:
        downloads = db.execute("SELECT url, destination FROM downloads").fetchall()
        bindings = db.execute("SELECT request_id, download_id FROM native_request_dedup").fetchall()
    if downloads != [("https://example.test/file.bin", "file.bin")]:
        raise AssertionError(f"request replay created unexpected download rows: {downloads!r}")
    if len(bindings) != 1 or bindings[0][0] != request["request_id"]:
        raise AssertionError(f"request ID was not durably deduplicated: {bindings!r}")

    # Regression: a request before handshake must not be dispatched or persisted.
    pre_handshake_db = db_path.with_name(db_path.stem + "-pre-handshake.sqlite")
    pre_env = env.copy()
    pre_env["VORTEXDM_DB_PATH"] = str(pre_handshake_db)
    rejected = subprocess.run(
        [str(host), "browser-bridge@vortexdm.app"],
        input=frame(request),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=pre_env,
        timeout=15,
        check=False,
    )
    if rejected.returncode != 8:
        raise AssertionError(f"pre-handshake request should exit 8, got {rejected.returncode}")
    with sqlite3.connect(pre_handshake_db) as db:
        count = db.execute("SELECT COUNT(*) FROM downloads").fetchone()[0]
    if count != 0:
        raise AssertionError("pre-handshake request mutated persistent downloads")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
