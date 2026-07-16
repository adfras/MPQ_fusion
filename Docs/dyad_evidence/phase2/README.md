# Dyad Phase 2 evidence — lobby, selection menu, session subsystem (DYADIC_STUDY_PLAN)

2026-07-16. `UDyadSessionSubsystem` (GameInstance subsystem: choices, Free/Assigned/Yoked
gating, lock, session identity, timestamped event log), `L_DyadLobby_01` (preview-room
copy + one placed `ADyadLobbyStageActor`), the 100 %-C++ avatar menu
(`UDyadAvatarMenuWidget` on a world-space widget component — no Blueprint assets), the
live-pose tee (`FMediaPipeDyadLiveObservationTee`: the partner-preview rig is puppeted
from the SAME observations the live pawn's fusion just polled), and
`FDyadAvatarRigFactory` (Phase 0's ghost assembly extracted; ghost + preview share it).

## Gate → evidence

- **Unit tests (state machine):** 219/219 (was 216). Free-choice flow with revisions and
  lock; post-lock selection/configure refusals; Assigned/Yoked slots reject SelectXxx and
  hold their presets; yoked source id recorded; change/lock delegates; event log counts.
- **Full menu journey (deterministic, `mp.DyadLobbyAutoJourneySeconds`):**
  `journey_extract.log` — select self Kellan → live pawn respawns (Emory→Kellan);
  select partner Maria → preview rig spawns as Maria; change self Hudson → respawn;
  change partner Payton → preview respawns with fresh keys; lock; post-lock partner
  select → `select_rejected_locked` event, state unchanged; "auto journey complete
  (self=Hudson partner=Payton locked=1)".
- **Visual:** `DyadJourney_05_locked.png` — menu rendered world-space with both rows of
  portrait buttons, green selection frames exactly on Hudson (You) and Payton (Your
  partner), disabled Confirm bar, "Locked: you = Hudson, partner = Payton" status; the
  partner preview stands beside the self-view avatar, facing the participant.
  `DyadJourney_02_partner_Maria.png` — the earlier journey step with Maria as preview.
- **Portraits:** `Content/DyadStudy/Portraits/<Id>.png` for all six cast members,
  cropped from respawn-soak screenshots by `Tools/make_dyad_portraits.py`;
  runtime-loaded by the menu (no texture assets).

## Notes for Phase 6 (in-headset judgment)

- Quest pinch: WidgetInteractionComponents are wired onto the live pawn's motion
  controllers by the stage actor; the pinch→click key mapping is exercised in-headset
  (desk path: `mp.DyadSelectAvatar <self|partner> <name>` + `mp.DyadLockChoices` call
  the same C++ functions the buttons call).
- Menu placement/size are editable on the placed `MP_DyadLobbyStage` actor
  (`Tools/SetupDyadLobbyMap.py` re-asserts the canonical placement).
- Hudson's groom renders as a coarse blob at desk LOD (the pawn's own self-view shows
  the same rendering) — pre-existing desk-mode cosmetics, not a dyad regression.

## Repro

```
UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=".../Tools/SetupDyadLobbyMap.py" -unattended -nullrhi
UnrealEditor-Cmd.exe <uproject> /Game/MetaHumanRooms/L_DyadLobby_01 -game -windowed \
  -ResX=1280 -ResY=720 -nosplash -nopause -ExecCmds="mp.DyadLobbyAutoJourneySeconds 10" -abslog=<log>
# screenshots: Saved/Screenshots/WindowsEditor/DyadJourney_*.png
```
