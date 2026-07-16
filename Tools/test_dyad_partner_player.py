#!/usr/bin/env python3
"""Self-test for Tools/dyad_partner_player.py against a fixture host (Phase 3 gate).

Stands up a minimal seat-A fixture server (accept one client, HELLO back, send GO,
collect everything), runs the player against a synthetic 5-row cache for a few loop
passes, and asserts the protocol contract:

  1. HELLO arrives first, with seat B and a protocol version.
  2. CHOICES arrives (log-only payload).
  3. READY arrives after the configured delay.
  4. GO is acknowledged with GO_ACK.
  5. ROW payload timestamps are strictly monotonic ACROSS loop seams (rebasing works),
     rows parse as schema-v2 (t + fusion.source), and sequence numbers are gapless.
  6. HEARTBEATs flow.
  7. BYE closes the session cleanly.

Run: python Tools/test_dyad_partner_player.py   (exit 0 = pass)
"""
from __future__ import annotations

import json
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
PLAYER = PROJECT_ROOT / "Tools" / "dyad_partner_player.py"
FIXTURE_PORT = 8127


def make_fixture_cache(path: Path) -> None:
    rows = []
    for index in range(5):
        rows.append({
            "t": index * 0.2,
            "phase": {"phase_name": f"fixture_{index}"},
            "fusion": {"source": {"hmd": {
                "has_pose": True,
                "loc": [float(index), 0.0, 150.0],
                "quat": [0.0, 0.0, 0.0, 1.0],
                "tracking_up": [0.0, 0.0, 1.0],
            }}},
        })
    path.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")


class FixtureHost:
    def __init__(self) -> None:
        self.messages: list[dict] = []
        self.error: str | None = None
        self.thread = threading.Thread(target=self.run, daemon=True)

    def run(self) -> None:
        try:
            with socket.create_server(("127.0.0.1", FIXTURE_PORT)) as server:
                server.settimeout(15.0)
                connection, _ = server.accept()
                with connection:
                    connection.settimeout(15.0)
                    buffer = b""
                    hello_sent = False
                    go_sent = False
                    deadline = time.monotonic() + 12.0
                    while time.monotonic() < deadline:
                        try:
                            chunk = connection.recv(65536)
                        except TimeoutError:
                            break
                        if not chunk:
                            break
                        buffer += chunk
                        while b"\n" in buffer:
                            line, buffer = buffer.split(b"\n", 1)
                            if not line.strip():
                                continue
                            message = json.loads(line.decode("utf-8"))
                            self.messages.append(message)
                            if message.get("type") == "HELLO" and not hello_sent:
                                hello_sent = True
                                connection.sendall((json.dumps({
                                    "type": "HELLO", "seat": "A", "protocolVersion": 1,
                                    "sessionId": "fixture", "wallClockMs": time.time() * 1000.0,
                                    "monotonicMs": time.monotonic() * 1000.0,
                                }) + "\n").encode())
                            if message.get("type") == "READY" and not go_sent:
                                go_sent = True
                                connection.sendall((json.dumps({
                                    "type": "GO", "level": "L_DyadInteraction_01",
                                }) + "\n").encode())
                            if message.get("type") == "BYE":
                                return
        except Exception as error:  # noqa: BLE001
            self.error = f"{type(error).__name__}: {error}"


def main() -> int:
    failures: list[str] = []
    with tempfile.TemporaryDirectory() as temp_dir:
        cache = Path(temp_dir) / "fixture_cache.jsonl"
        make_fixture_cache(cache)

        host = FixtureHost()
        host.thread.start()
        time.sleep(0.3)

        player = subprocess.run(
            [sys.executable, str(PLAYER),
             "--port", str(FIXTURE_PORT), "--cache", str(cache),
             "--start", "0", "--duration", "0",
             "--ready-delay", "1", "--play-seconds", "4",
             "--heartbeat-interval", "0.3"],
            capture_output=True, text=True, timeout=60,
        )
        host.thread.join(timeout=15)

    if player.returncode != 0:
        failures.append(f"player exit {player.returncode}: {player.stderr[-500:]}")
    if host.error:
        failures.append(f"fixture host error: {host.error}")

    kinds = [message.get("type") for message in host.messages]

    def check(condition: bool, label: str) -> None:
        if not condition:
            failures.append(label)

    check(bool(kinds) and kinds[0] == "HELLO", f"HELLO first (got {kinds[:3]})")
    hello = next((message for message in host.messages if message.get("type") == "HELLO"), {})
    check(hello.get("seat") == "B", "HELLO carries seat B")
    check(hello.get("protocolVersion") == 1, "HELLO carries protocol version")
    check("CHOICES" in kinds, "CHOICES sent")
    check("READY" in kinds, "READY sent")
    check("GO_ACK" in kinds, "GO acknowledged")
    check("HEARTBEAT" in kinds, "heartbeats flow")
    check(kinds[-1] == "BYE", f"BYE last (got {kinds[-1] if kinds else 'nothing'})")

    rows = [message for message in host.messages if message.get("type") == "ROW"]
    check(len(rows) >= 12, f"several loop passes streamed (rows={len(rows)}, need >=12)")
    sequences = [row.get("seq") for row in rows]
    check(sequences == list(range(len(rows))), "row sequence numbers gapless")
    payload_times = [row["payload"]["t"] for row in rows]
    check(all(b > a for a, b in zip(payload_times, payload_times[1:])),
          "payload t strictly monotonic across loop seams")
    check(all("source" in row["payload"].get("fusion", {}) for row in rows),
          "payloads carry fusion.source")
    # 5 rows per pass: seams sit at every multiple of 5; monotonicity across them was
    # asserted above, this confirms we actually crossed seams.
    check(len(rows) // 5 >= 2, "at least two seams crossed")

    if failures:
        print("FAIL:")
        for failure in failures:
            print(f"  - {failure}")
        print(f"player stdout tail:\n{player.stdout[-800:]}")
        return 1
    print(f"PASS: {len(host.messages)} messages, {len(rows)} rows, "
          f"{len(rows) // 5} passes, protocol contract intact")
    return 0


if __name__ == "__main__":
    sys.exit(main())
