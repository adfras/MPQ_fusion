# Dyad Phase 4 evidence — ready handshake + travel (DYADIC_STUDY_PLAN)

2026-07-16. `FDyadTravelStateMachine` (the ready/go ordering rules as a pure state
machine), travel wiring in `UDyadLinkSubsystem` (lock → READY; both-ready → host GO
{level} → both OpenLevel; join honors GO even when it arrived before local lock; only
the host may GO; drops during the wait hold the wait, drops after travel change
nothing), `L_DyadInteraction_01` (preview-room copy: placed pawn = self spot, placed
`ADyadInteractionStageActor` = partner spot ~2.5 m face-to-face, neutral task table
between, mirror wall carries self-visibility — the pawn's self-view copy is disabled on
arrival since the partner stands where it would), and the lobby menu's
"Waiting for partner…" / "Starting…" status states.

The wire belongs to the GameInstance subsystem, so it survives `OpenLevel` by
construction; arrival re-binds the stream to the fresh partner rig's new mesh keys.

## Gate → evidence (loopback full journey, one player process end to end)

`journey_extract.log` + `DyadInteraction_Payton.png`:

- Player auto-READY arrived BEFORE the host locked (`wire_peer_ready`, then `kind=lock`)
  — the ordering case the recorded partner always produces — then lock → READY sent →
  both-ready → `wire_go_sent level=/Game/MetaHumanRooms/L_DyadInteraction_01` →
  `TRAVEL ->` → `travel_open_level` → `travel_arrived`.
- Arrival assembly: `self pawn wears Hudson (selfView off)`;
  `DyadRig(MP_DyadPartner): spawned avatar=Payton ... (fresh keys 81830/81836)` — the
  DyadLink stream re-bound to the new pawn in the new level, wearing MY partner choice.
- **Continuous streaming across the travel boundary:** ONE player process spanned the
  whole journey — 4,083 rows across **5 loop seams**, `go=True` (GO acknowledged), zero
  drops/timeouts/freezes until its final clean BYE after the play window (the only
  `dropping peer` line, post-BYE, returning the host to listening).
- Screenshot: the wire-driven Payton stands across the table spot facing the
  participant in the interaction room (the task table sits just below this camera's
  frustum; visible when looking down in-headset).
- **Tests 224 → 227:** ready/go ordering both ways round, duplicate-input idempotence,
  join honors early GO at lock and ignores late/duplicate GO, join never sends GO, host
  ignores a peer GO, disconnect-during-wait holds the wait and re-ready completes it,
  post-travel drop cancels nothing.

## Repro

```
UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=".../Tools/SetupDyadInteractionMap.py" -unattended -nullrhi
UnrealEditor-Cmd.exe <uproject> /Game/MetaHumanRooms/L_DyadLobby_01 -game -windowed \
  -ExecCmds="mp.DyadRole host, mp.DyadLobbyAutoJourneySeconds 8, mp.DyadInteractionArrivalShot 1" ...
python Tools/dyad_partner_player.py --start 2 --duration 26 --ready-delay 3 --play-seconds 150
```
