#!/usr/bin/env python3
"""Seat B of the dyad, played by a recording (DYADIC_STUDY_PLAN Phase 3).

Connects to a TestingKit5 host exactly as a human seat would: speaks the complete
control protocol (HELLO with clock exchange, CHOICES, auto-READY after a configurable
delay, GO acknowledgment, HEARTBEATs, clean BYE) and streams a schema-v2 replay cache's
rows at original timing. The host cannot tell — and must never need to know — whether
seat B is a person or this script. Until ethics approval unlocks the live two-person
pilot, this IS the partner; afterwards it stays the development loopback and the
protocol's reference client.

Looping is seamless: `--start/--duration` pin a segment whose seam lands on a calm
pose, and every pass rebases timestamps so the stream never stops or rewinds.

Canonical loop segment (chosen 2026-07-16, see DYADIC_STUDY_PLAN Phase 5): the head
block of the canonical recording, `--start 2 --duration 26`. Rationale: hands hang at
sides for the whole block (the known camera-hand-starved wrist path is least exposed),
motion is gaze-like head movement — plausible for a partner across a table — and both
segment ends are calm settle windows, so the loop seam is a small head-pose step the
drive path's gap tolerance absorbs.

Usage (loopback):
  python Tools/dyad_partner_player.py --cache Saved/CodexAgent/Diagnostics/\
tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656_replay_source_v2_manifest.json \
      --start 2 --duration 26 --ready-delay 3
"""
from __future__ import annotations

import argparse
import json
import socket
import sys
import time
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
PROTOCOL_VERSION = 1
DEFAULT_CACHE = (
    "Saved/CodexAgent/Diagnostics/"
    "tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656_replay_source_v2_manifest.json"
)


def mono_ms() -> float:
    return time.monotonic() * 1000.0


def load_rows(cache_path: Path) -> list[dict]:
    """Loads a replay cache (.jsonl or a *_manifest.json listing sample files)."""
    if not cache_path.is_absolute():
        cache_path = PROJECT_ROOT / cache_path
    if not cache_path.exists():
        raise FileNotFoundError(cache_path)

    sample_files: list[Path]
    if cache_path.suffix == ".jsonl":
        sample_files = [cache_path]
    else:
        manifest = json.loads(cache_path.read_text(encoding="utf-8"))
        sample_files = [
            (cache_path.parent / entry["relative_path"]).resolve()
            for entry in manifest.get("sample_files", [])
        ]
        if not sample_files:
            raise ValueError(f"manifest lists no sample_files: {cache_path}")

    rows: list[dict] = []
    for sample_file in sample_files:
        with sample_file.open(encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if not line:
                    continue
                try:
                    row = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if "t" in row and "fusion" in row:
                    rows.append(row)
    rows.sort(key=lambda row: row["t"])
    return rows


def slice_segment(rows: list[dict], start: float, duration: float) -> list[dict]:
    if duration <= 0:
        segment = [row for row in rows if row["t"] >= start]
    else:
        segment = [row for row in rows if start <= row["t"] < start + duration]
    if not segment:
        raise ValueError(f"segment start={start} duration={duration} selects no rows")
    return segment


class PartnerPlayer:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.sock: socket.socket | None = None
        self.recv_buffer = b""
        self.send_buffer = b""
        self.session_id = f"dyad_{time.strftime('%Y%m%d_%H%M%S')}_seatB"
        self.session_dir = Path(args.session_dir) if args.session_dir else (
            PROJECT_ROOT / "Saved" / "DyadStudy" / self.session_id)
        self.session_dir.mkdir(parents=True, exist_ok=True)
        (self.session_dir / "session.json").write_text(json.dumps({
            "sessionId": self.session_id,
            "seat": "B",
            "conditionTag": args.condition_tag,
            "wallClockIso": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "monotonicSeconds": time.monotonic(),
        }) + "\n", encoding="utf-8")
        self.events_file = (self.session_dir / "events.jsonl").open("a", encoding="utf-8")
        self.control_file = (self.session_dir / "control.jsonl").open("a", encoding="utf-8")
        self.rows_out_file = (self.session_dir / "rows_outbound.jsonl").open("a", encoding="utf-8")
        self.rows_in_file = (self.session_dir / "rows_inbound.jsonl").open("a", encoding="utf-8")
        self.hello_send_mono = 0.0
        self.clock_offset_ms: float | None = None
        self.go_received = False
        self.rows_sent = 0
        self.loops_completed = 0

    def record_event(self, kind: str, detail: str) -> None:
        self.events_file.write(json.dumps({
            "tMonoS": time.monotonic(), "kind": kind, "detail": detail,
        }) + "\n")
        self.events_file.flush()

    def record_control(self, outbound: bool, message: dict) -> None:
        self.control_file.write(json.dumps({
            "dir": "out" if outbound else "in",
            "tMonoS": time.monotonic(),
            "line": message,
        }) + "\n")
        self.control_file.flush()

    # --- wire helpers ---

    def send(self, message: dict) -> None:
        # Buffered send: the socket is non-blocking (recv is polled on the same socket),
        # so a momentarily full OS buffer must queue rather than raise (WinError 10035).
        line = json.dumps(message, separators=(",", ":"))
        if message.get("type") == "ROW":
            self.rows_out_file.write(line + "\n")
        else:
            self.record_control(True, message)
        self.send_buffer += (line + "\n").encode("utf-8")
        self.flush_send()

    def flush_send(self) -> None:
        assert self.sock is not None
        while self.send_buffer:
            try:
                sent = self.sock.send(self.send_buffer)
            except (BlockingIOError, InterruptedError):
                return
            if sent <= 0:
                return
            self.send_buffer = self.send_buffer[sent:]

    def poll_messages(self) -> list[dict]:
        assert self.sock is not None
        messages: list[dict] = []
        try:
            while True:
                chunk = self.sock.recv(65536)
                if not chunk:
                    raise ConnectionError("host closed the connection")
                self.recv_buffer += chunk
        except (BlockingIOError, TimeoutError):
            pass
        while b"\n" in self.recv_buffer:
            line, self.recv_buffer = self.recv_buffer.split(b"\n", 1)
            line = line.strip()
            if not line:
                continue
            try:
                messages.append(json.loads(line.decode("utf-8")))
            except json.JSONDecodeError:
                print(f"[player] ignoring malformed line ({len(line)} bytes)", flush=True)
        return messages

    def handle_message(self, message: dict) -> None:
        kind = message.get("type", "")
        if kind == "ROW":
            self.rows_in_file.write(json.dumps(message, separators=(",", ":")) + "\n")
            return
        self.record_control(False, message)
        if kind == "HELLO":
            recv_mono = mono_ms()
            peer_mono = float(message.get("monotonicMs", 0.0))
            rtt = max(0.0, recv_mono - self.hello_send_mono)
            self.clock_offset_ms = peer_mono - (self.hello_send_mono + rtt / 2.0)
            self.record_event("wire_hello", f"peerSeat={message.get('seat')} offsetMs={self.clock_offset_ms:.1f} rttMs={rtt:.1f}")
            print(
                f"[player] HELLO from seat {message.get('seat')} session={message.get('sessionId')} "
                f"offsetMs={self.clock_offset_ms:.1f} rttMs={rtt:.1f}",
                flush=True,
            )
        elif kind == "GO":
            level = message.get("level", "")
            print(f"[player] GO level={level} -> acking", flush=True)
            self.send({"type": "GO_ACK", "level": level})
            self.go_received = True
            self.record_event("wire_go_received", f"level={level}")
        elif kind == "CHOICES":
            print(f"[player] host CHOICES logged: {message}", flush=True)
        elif kind == "BYE":
            raise ConnectionError(f"host BYE: {message.get('reason', '')}")
        # HEARTBEAT / READY / GO_ACK / ROW from the host need no reply. Host rows are
        # ignored by design: the recording doesn't watch its partner.

    # --- protocol phases ---

    def connect_and_handshake(self) -> None:
        print(f"[player] connecting to {self.args.host}:{self.args.port}", flush=True)
        deadline = time.monotonic() + self.args.connect_timeout
        last_error: Exception | None = None
        while time.monotonic() < deadline:
            try:
                self.sock = socket.create_connection((self.args.host, self.args.port), timeout=2.0)
                break
            except OSError as error:
                last_error = error
                time.sleep(0.5)
        if self.sock is None:
            raise ConnectionError(f"could not connect: {last_error}")
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.sock.setblocking(False)

        self.hello_send_mono = mono_ms()
        self.send({
            "type": "HELLO",
            "seat": "B",
            "protocolVersion": PROTOCOL_VERSION,
            "sessionId": self.session_id,
            "wallClockMs": time.time() * 1000.0,
            "monotonicMs": self.hello_send_mono,
        })
        self.send({
            "type": "CHOICES",
            "selfAvatar": self.args.avatar,
            "partnerAvatar": self.args.partner_avatar,
            "choiceMode": "recorded",
        })
        print(f"[player] HELLO + CHOICES sent (avatar={self.args.avatar})", flush=True)

    def run(self) -> int:
        rows = load_rows(Path(self.args.cache))
        segment = slice_segment(rows, self.args.start, self.args.duration)
        segment_t0 = segment[0]["t"]
        segment_span = segment[-1]["t"] - segment_t0
        # The loop period includes one inter-row gap after the last row, so the first
        # row of the next pass lands strictly AFTER the previous pass's last row.
        mean_dt = segment_span / (len(segment) - 1) if len(segment) > 1 else 1.0 / 30.0
        segment_period = segment_span + mean_dt
        print(
            f"[player] cache rows={len(rows)} segment rows={len(segment)} "
            f"[{segment_t0:.2f}s..{segment[-1]['t']:.2f}s] span={segment_span:.2f}s "
            f"period={segment_period:.2f}s",
            flush=True,
        )

        self.connect_and_handshake()

        # Auto-READY after the configured delay (a human reading the lobby menu).
        ready_at = time.monotonic() + self.args.ready_delay
        ready_sent = False
        next_heartbeat = 0.0

        stream_started = time.monotonic()
        stream_deadline = (
            stream_started + self.args.play_seconds if self.args.play_seconds > 0 else None
        )
        row_index = 0
        pass_started = time.monotonic()
        assert self.sock is not None

        try:
            while True:
                now = time.monotonic()
                if stream_deadline and now >= stream_deadline:
                    print("[player] play window over", flush=True)
                    break
                self.flush_send()
                for message in self.poll_messages():
                    self.handle_message(message)
                if not ready_sent and now >= ready_at:
                    self.send({"type": "READY"})
                    ready_sent = True
                    self.record_event("wire_ready_sent", "")
                    print("[player] READY sent", flush=True)
                if now >= next_heartbeat:
                    self.send({"type": "HEARTBEAT", "monotonicMs": mono_ms()})
                    next_heartbeat = now + self.args.heartbeat_interval

                # Original-timing playback with seamless per-pass timestamp rebasing.
                elapsed_in_pass = now - pass_started
                if row_index >= len(segment):
                    if not self.args.loop:
                        print("[player] segment complete (loop off)", flush=True)
                        break
                    if elapsed_in_pass < segment_period / self.args.rate:
                        time.sleep(0.002)  # hold the seam for one inter-row gap
                        continue
                    self.loops_completed += 1
                    row_index = 0
                    pass_started = now
                    print(f"[player] loop seam crossed (pass {self.loops_completed})", flush=True)
                    continue
                row = segment[row_index]
                row_due = (row["t"] - segment_t0) / self.args.rate
                if elapsed_in_pass < row_due:
                    time.sleep(min(0.005, row_due - elapsed_in_pass))
                    continue
                stream_t = self.loops_completed * segment_period + (row["t"] - segment_t0)
                payload = dict(row)
                payload["t"] = stream_t  # rebased: monotonic across passes, never rewinds
                self.send({
                    "type": "ROW",
                    "seq": self.rows_sent,
                    "tMonoMs": mono_ms(),
                    "payload": payload,
                })
                self.rows_sent += 1
                row_index += 1
        except (ConnectionError, KeyboardInterrupt) as stop:
            print(f"[player] stopping: {stop}", flush=True)
        finally:
            try:
                if self.sock:
                    self.send({"type": "BYE", "reason": "player done"})
                    self.sock.close()
            except OSError:
                pass
        self.record_event("session_end", f"rows={self.rows_sent} loops={self.loops_completed}")
        for handle in (self.events_file, self.control_file, self.rows_out_file, self.rows_in_file):
            handle.close()
        print(
            f"[player] done: rows={self.rows_sent} loops={self.loops_completed} "
            f"ready={ready_sent} go={self.go_received} sessionDir={self.session_dir}",
            flush=True,
        )
        return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8123)
    parser.add_argument("--cache", default=DEFAULT_CACHE,
                        help="schema-v2 replay cache manifest or .jsonl (project-relative ok)")
    parser.add_argument("--start", type=float, default=2.0, help="segment start seconds")
    parser.add_argument("--duration", type=float, default=26.0,
                        help="segment duration seconds (<=0 = to end of recording)")
    parser.add_argument("--loop", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--rate", type=float, default=1.0, help="playback speed multiplier")
    parser.add_argument("--ready-delay", type=float, default=3.0,
                        help="seconds before auto-READY (a human reading the menu)")
    parser.add_argument("--heartbeat-interval", type=float, default=0.5)
    parser.add_argument("--connect-timeout", type=float, default=30.0)
    parser.add_argument("--play-seconds", type=float, default=0.0,
                        help="stop streaming after N seconds (0 = until interrupted)")
    parser.add_argument("--avatar", default="Kellan",
                        help="avatar id reported in CHOICES (log-only; appearance never crosses the wire)")
    parser.add_argument("--partner-avatar", default="Kellan",
                        help="partner-slot choice reported in CHOICES (log-only)")
    parser.add_argument("--session-dir", default="",
                        help="seat-B session folder (default Saved/DyadStudy/<sessionId>)")
    parser.add_argument("--condition-tag", default="recorded_partner",
                        help="condition tag stamped in this seat's session.json")
    args = parser.parse_args()
    return PartnerPlayer(args).run()


if __name__ == "__main__":
    sys.exit(main())
