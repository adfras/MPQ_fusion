# Dyad Phase 6 — solo pilot run sheet (Alan in the headset, seat B recorded)

Status: ready to run as of 2026-07-17 (Phases 0–5 landed, 229/229; see
`Docs/dyad_evidence/phase*/`). This is the human gate: menu usability by pinch, partner
plausibility/comfort at 2.5 m, task-phase feel, and the artifact review afterwards.

## Boot steps (seat A, this machine)

1. Phone on the tripod, Camo running (the usual mirror-demo webcam), headset on standby.
2. Close any editor. Launch the LOBBY in game mode with the wire armed:

   ```
   D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe ^
     "D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject" ^
     /Game/MetaHumanRooms/L_DyadLobby_01 -game ^
     -ExecCmds="mp.DyadRole host, mp.DyadSeat A, mp.DyadConditionFile Config/DyadStudy/condition_free_pilot.json"
   ```

   (Or run PIE from the editor with the same three CVars set in the console before play.
   The AutoTrial boot arms the accepted live stack + tracers as always; nothing else to
   remember. VR Preview for the worn passes.)
3. Start seat B (the recorded partner) in a second terminal:

   ```
   python Tools\dyad_partner_player.py --start 2 --duration 26 --ready-delay 15
   ```

   `--ready-delay 15` gives you time in the menu before the partner "confirms". The
   segment 2 s + 26 s is the pinned head-block loop (hands at sides, gaze-like motion,
   calm seam) — the same one the condition files pin.
4. In the lobby: pick your avatar and the partner's on the panel (pinch; if pinch is
   uncooperative, the keyboard path is `mp.DyadSelectAvatar self <Name>` /
   `mp.DyadSelectAvatar partner <Name>`), then Confirm choices
   (`mp.DyadLockChoices`). The status line shows "Waiting for partner…" until the
   player's READY lands, then both apps travel to the interaction room together.
5. In the interaction room: the partner stands across the table wearing YOUR
   partner-choice, driven by the recording. ~45 s in, the questionnaire panel appears
   between you and the table (Likert 1–7 per item; desk path
   `mp.DyadAnswerQuestionnaire <item> <score>`).
6. End: Escape/close the game; Ctrl-C the player if still running (it BYEs cleanly).

## The two condition files

- `Config/DyadStudy/condition_free_pilot.json` — both slots Free: you choose both
  avatars in the menu.
- `Config/DyadStudy/condition_assigned_pilot.json` — seat A assigned Emory (self) +
  Maria (partner): the menu renders locked, selections reject (events still log them as
  `select_rejected_mode` — that's data, not a bug), Confirm proceeds with the presets.

Both pin the partner segment (2 s + 26 s) and the 3 pilot questionnaire items. Copy one
to make a new condition; `conditionTag` names the session in every log row.

## What lands on disk

`Saved/DyadStudy/<sessionId>/` per seat (yours from the app, the player's from the
script): session.json, events.jsonl (selections with timestamps, lock, travel,
questionnaire answers), control.jsonl, rows_outbound/rows_inbound.jsonl. Afterwards:

```
python Tools\mine_dyad_session.py <your seatA folder> <player seatB folder>
```

prints and writes the scoreboard (distance, orientation, gaze, energy, synchrony).

## CVar escape hatches (live, no restart)

| Situation | Hatch |
| --- | --- |
| Wrong avatar / mangled-looking swap | `mp.DyadRespawnAvatar live <Name>` (respawn-not-mutate; wrist calibration re-latches ~1–2 s) |
| Partner rig wrong/frozen and rows ARE flowing | `mp.DyadRespawnAvatar ghost <Name>` for the ghost slot; for the wire partner just re-pick partner pre-lock (respawns the rig) |
| Partner frozen + "HEARTBEAT TIMEOUT" warning | expected freeze semantics — restart the player script; the host is already re-listening and resumes automatically |
| Menu unusable by pinch | `mp.DyadSelectAvatar …` + `mp.DyadLockChoices` from the console |
| Skip the lobby entirely | boot straight into `/Game/MetaHumanRooms/L_DyadInteraction_01` with the condition file set — the stage assembles from the session presets (Free condition needs choices, so use assigned) |
| Kill all dyad behavior instantly | `mp.DyadRole` "" (wire dark), `mp.DyadGhostPartner 0`; every mp.Dyad* default is off |
| Second-guessing the drive path | `mp.TrackingFusionDatasetReplayStatus`, and the partner phase name in fetch traces reads `wire` / `wire_frozen` |

## Known references before judging

- Ghost/partner WRIST TWIST runs hot vs the replay-map baseline: the canonical dataset
  predates `camera_hands`, and row-stream rigs run the live wrist stack data-starved —
  the documented 2026-07-05 class, details + upgrade paths in
  `Docs/dyad_evidence/phase0/README.md`. Judge partner plausibility with that in mind
  (the pinned head-block segment keeps hands down precisely to minimize it).
- Terry is not in the built cast (only `MHC_Terry.uasset` exists, unassembled): menus
  list the six built MetaHumans. Creator-built additions stay out of scope per the plan.
- Hudson's groom renders coarse at desk LOD; in-headset it is the real judgment call.

## After the pilot

Phase 6's exit note (worn impressions + one dry-run artifact bundle) gates Phase 7,
which stays parked until ethics approval (`DYADIC_STUDY_PLAN.md` Phase 7 checklist).
