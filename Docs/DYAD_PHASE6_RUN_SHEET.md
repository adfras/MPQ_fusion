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
4. The lobby is a TWO-STAGE flow (2026-07-17 redesign): first you face your mirror
   clone and pick YOUR avatar (menu shows only the "You" row; desk path
   `mp.DyadSelectAvatar self <Name>`), then Confirm avatar (`mp.DyadLockChoices`).
   The mirror goes away and the partner stage begins: pick the partner
   (`mp.DyadSelectAvatar partner <Name>`) and a recording-driven copy of them stands
   where the mirror was, moving on the pinned segment. Confirm partner
   (`mp.DyadLockChoices` again) locks; "Waiting for partner…" until the player's
   READY lands, then both apps travel together.
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
| Skip the lobby entirely | KNOWN BROKEN 2026-07-17: only the lobby stage loads `mp.DyadConditionFile`, so a direct interaction boot runs without a session (and the partner rig did not render in the direct-boot check). Use the normal lobby → travel flow; a fix chip is pending |
| Want the live-trial tracking panel back while in a dyad room | `mp.DyadKeepTrackingPanel 1` (the dyad stages suppress `mp.QuestVrTrackingPanel` every tick by default — the trial arm policy re-writes it 1 on every respawn, so the suppression has to keep winning) |
| Want the webcam preview screen back while in a dyad room | `mp.DyadKeepWebcamPreview 1` (same tick-suppression for `mp.AutoQuestWebcamPreview`; it otherwise parks as the person-sized dark screen camera-right and hides the menu column — the back-wall self-view surface still shows the camera feed) |
| Want the wire partner visible in the LOBBY (Phase 3-style debugging) | `mp.DyadLobbyWirePartner 1` (default 0: the lobby shows only your partner-choice preview; the recorded partner appears across the table after travel) |
| Kill all dyad behavior instantly | `mp.DyadRole` "" (wire dark), `mp.DyadGhostPartner 0`; every mp.Dyad* default is off |
| Second-guessing the drive path | `mp.TrackingFusionDatasetReplayStatus`, and the partner phase name in fetch traces reads `wire` / `wire_frozen` |

## Known references before judging

- Ghost/partner WRIST TWIST runs hot vs the replay-map baseline: the canonical dataset
  predates `camera_hands`, and row-stream rigs run the live wrist stack data-starved —
  the documented 2026-07-05 class, details + upgrade paths in
  `Docs/dyad_evidence/phase0/README.md`.
- MORE GENERALLY (Alan, 2026-07-17): the rows carry raw observations, NOT calibration
  state — every recorded-partner rig re-runs live calibration from scratch on the
  segment it is fed, so the preview/partner can look wrong (proportions, reach,
  wrists), worst in the first seconds after spawn. The pinned 2–26 s window IS the
  recording's calibration block (hand raises), which converges the rig but reads as
  calibration gestures, not natural behavior. Real fix = a purpose-made partner
  recording (capture-session decision); cheap mitigation available on request =
  pre-roll the partner stream hidden during stage 1 so it reveals already settled.
- Terry is not in the built cast (only `MHC_Terry.uasset` exists, unassembled): menus
  list the six built MetaHumans. Creator-built additions stay out of scope per the plan.
- Hudson's groom renders coarse at desk LOD; in-headset it is the real judgment call.
- 2026-07-17 desk-trial composition pass: dyad rooms are lit (skylight + fills in both
  maps), the live-trial tracking panel and the webcam preview card are auto-suppressed
  inside dyad rooms (mp.DyadKeep* CVars restore them), the menu sits camera-left at
  chest height with its dark rows below every face, and the lobby no longer spawns a
  wire-driven rig behind the self-view mirror copy. The center avatar IS the self-view
  mirror; the blue wall rect is wall art (renders pale blue until lit content settles).
- KNOWN COSMETIC ISSUE: a dark board on the camera-RIGHT in desk views — a mirrored
  ghost of the menu widget (reflects across the room's center plane, ignores depth,
  no owning actor; mechanism unidentified after an exhaustive hunt — see the pending
  task chip). It overlaps nothing interactive. If it is visible IN-HEADSET during the
  pilot, say so — that observation picks the next diagnostic branch.

## After the pilot

Phase 6's exit note (worn impressions + one dry-run artifact bundle) gates Phase 7,
which stays parked until ethics approval (`DYADIC_STUDY_PLAN.md` Phase 7 checklist).
