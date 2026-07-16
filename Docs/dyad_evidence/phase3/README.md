# Dyad Phase 3 evidence — the dyad wire + recorded seat B (DYADIC_STUDY_PLAN)

2026-07-16. New module `DyadLink` (FSocket TCP, newline-delimited JSON: HELLO/CHOICES/
READY/GO+GO_ACK/HEARTBEAT/BYE + ROW stream of schema-v2 source rows), `UDyadLinkSubsystem`
(GameInstance-owned so the wire survives travel; everything behind `mp.DyadRole`, default
"" = dark), `FDyadWireObservationSource` (the wire as a dyad observation source: restamp
on arrival, hold-last through gaps, freeze on timeout), and `Tools/dyad_partner_player.py`
— seat B as a recording, full protocol, seamless segment looping.

Inbound rows parse through the replay cache's OWN parser (exposed as
`ParseSourceRowObject`), and outbound rows serialize with the parser's own landmark-name
table — a wire-driven partner and a file-driven ghost see identical observations for
identical rows (round-trip pinned by unit test).

## Gate → evidence (loopback on one machine)

`host_wire_extract.log` (host = lobby `-game`, `mp.DyadRole host`, auto-journey armed;
seat B = the partner player on localhost streaming the canonical cache segment 2s+26s):

- **Handshake round-trips:** `wire_hello peerSeat=B ... rttMs=30.4` (offsetMs is the
  cross-process monotonic-epoch difference — logged, not chased, per plan);
  `wire_peer_choices` (log-only by design); auto-`wire_peer_ready`.
- **Partner pawn animates from the wire:** `MP_DyadWirePartner` rig spawned and driven —
  8,934 finger-solve rows, 4,467 arm-chain rows, 1,140 wrist-solve rows under its actor
  name; and it re-skinned Kellan→Maria→Payton as the local journey changed the partner
  choice (per-viewer appearance dressing wire data locally).
- **Loop seams survived:** player run 1 streamed 1,628 rows across **2 seams**
  (`player1_extract.log`); the host consumed continuously (no pose resets logged — the
  seam is invisible to freshness checks because payload `t` is rebased per pass, pinned
  monotonic by the player self-test).
- **Drop → freeze → reconnect → resume:** peer BYE → partner frozen in place + socket
  dropped + host immediately re-listening → player run 2 accepted → HELLO again →
  `stream resumed after freeze (rows=1629)`. A dropped partner never ends the session.
- **Tests 219 → 224:** framing (partial/interleaved reads, CRLF, oversized-line poison),
  clock-offset math, ROW serialize→parse round trip through the replay parser, restamp
  contract, wire-source hold-last/freeze/resume semantics.
- **Player self-test** (`player_selftest.log`): fixture host asserts HELLO-first with
  seat B + protocol version, CHOICES, delayed READY, GO→GO_ACK, heartbeats, gapless seq,
  payload `t` strictly monotonic across ≥2 seams, BYE last.

## The chosen loop segment (pinned for Phase 5 condition files)

`--start 2 --duration 26` — the head block of the canonical recording. Rationale:
hands hang at the sides for the entire block (least exposure of the known
camera-hand-starved wrist path, see phase0/README), the motion is gaze-like head
movement — plausible for a partner across a table — and both segment ends are
countdown/settle windows, so the loop seam is a small calm-pose step the drive path's
gap tolerance absorbs.

## Found-and-fixed during the gate

- `FTickableGameObject` CDO also ticked: the class-default subsystem object opened its
  OWN listener (SO_REUSEADDR double-bind) with a null session and swallowed the peer.
  Fixed with a `GetTickableTickType` CDO guard.
- A gracefully-closed peer never trips a UE socket error: BYE and heartbeat-timeout now
  explicitly `DropPeer` so the host returns to listening immediately.
- The player's `sendall` on its non-blocking socket raised WinError 10035 under real
  host traffic — replaced with a buffered drain-on-loop send.

## Repro

```
# host (lobby, wire dark until mp.DyadRole set):
UnrealEditor-Cmd.exe <uproject> /Game/MetaHumanRooms/L_DyadLobby_01 -game -windowed \
  -ExecCmds="mp.DyadRole host, mp.DyadLobbyAutoJourneySeconds 8" ...
# seat B:
python Tools/dyad_partner_player.py --start 2 --duration 26 --ready-delay 3
# protocol self-test (no UE needed):
python Tools/test_dyad_partner_player.py
```
