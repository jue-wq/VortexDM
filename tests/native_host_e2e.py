import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def frame(message: dict) -> bytes:
    body = json.dumps(message, separators=(",", ":")).encode("utf-8")
    return struct.pack("<I", len(body)) + body


def read_frames(data: bytes) -> list[dict]:
    messages = []
    offset = 0
    while offset < len(data):
        if len(data) - offset < 4:
            raise AssertionError("truncated native messaging length prefix")
        size = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        if size <= 0 or size > 1024 * 1024 or len(data) - offset < size:
            raise AssertionError("invalid native messaging frame size")
        messages.append(json.loads(data[offset:offset + size]))
        offset += size
    return messages


def main() -> int:
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="vortex-native-host-e2e-") as temp:
        env = os.environ.copy()
        if os.name == "nt":
            env["LOCALAPPDATA"] = temp
        else:
            env["XDG_DATA_HOME"] = temp
        requests = [
            {"protocol_version": 1, "request_id": "hello", "timestamp": 1, "type": "handshake", "payload": {}},
            {"protocol_version": 1, "request_id": "add", "timestamp": 2, "type": "add_download",
             "payload": {"url": "https://example.com/file.bin", "filename": "file.bin", "queue_id": "default"}},
        ]
        requests_bytes = b"".join(frame(item) for item in requests)
        process = subprocess.run([str(binary)], input=requests_bytes, capture_output=True, env=env, timeout=20)
        if process.returncode != 0:
            raise AssertionError(f"native host exited {process.returncode}: {process.stderr.decode(errors='replace')}")
        responses = read_frames(process.stdout)
        if len(responses) != 2 or any(item.get("type") != "ack" for item in responses):
            raise AssertionError(f"unexpected initial responses: {responses!r}")
        if responses[0]["payload"].get("accepted") is not True:
            raise AssertionError("handshake was not accepted")
        task_id = responses[1]["payload"].get("task_id")
        if not isinstance(task_id, str) or not task_id:
            raise AssertionError("add_download did not return a task id")

        controls = []
        for index, action in enumerate(("pause", "resume", "cancel"), start=3):
            controls.append({"protocol_version": 1, "request_id": action, "timestamp": index,
                             "type": "task_control", "payload": {"task_id": task_id, "action": action}})
        control_run = subprocess.run([str(binary)], input=b"".join(frame(item) for item in controls),
                                     capture_output=True, env=env, timeout=20)
        if control_run.returncode != 0:
            raise AssertionError(f"native host control run exited {control_run.returncode}")
        control_responses = read_frames(control_run.stdout)
        if len(control_responses) != 3:
            raise AssertionError(f"expected 3 task-control responses, got {len(control_responses)}")
        for response, expected in zip(control_responses, ("Paused", "Queued", "Cancelled")):
            if response.get("type") != "ack" or response.get("payload", {}).get("status") != expected:
                raise AssertionError(f"unexpected task-control response: {response!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
