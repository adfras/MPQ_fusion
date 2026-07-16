# Dyad Phase 1 evidence — respawn-based live re-skin (DYADIC_STUDY_PLAN)

2026-07-16. `UDyadAvatarSwapLibrary::RespawnPawn` + `mp.DyadRespawnAvatar <live|ghost> <name>`.
Respawn-never-mutate: the pawn AND its avatar-state satellites (driver actor, live
MetaHuman, self-view MetaHuman) are destroyed and re-assembled fresh, so every skeletal
mesh gets a new component id and the keyed solver stores start empty by construction.
The webcam source actor survives (sensor, not avatar state). Wrist calibration
re-latching from neutral (~1–2 s) after a swap is expected and accepted.

## Gate → evidence

- **Automation, respawn across the full cast:** 216/216 (was 214).
  `Dyad.Respawn.ContractAcrossCast` runs a created Game world through all six cast
  members: fresh pawn object each time, profile set before BeginPlay, config flags
  copied, stale tagged satellites destroyed, every mesh key fresh (not inherited),
  unknown profile refuses without destroying anything, Manny selects the internal
  replica. `Dyad.Respawn.CommandRegisteredAndArgSafety` covers the console surface.
- **Desk soak, live room, real assembly path:** `mp.DyadRespawnSoakSeconds 15` in a
  `-game` boot of the preview room (webcam live, AutoTrial stack armed): 8 swaps in
  ~200 s walking the whole cast (Wallace→Emory→Hudson→Kellan→Maria→Payton→wrap), zero
  crashes, and after EVERY swap the fresh presentation resolves
  `mp.MetaHumanProfile: resolved profile=<new> actor=MP_LiveMetaHuman<new> active=1
  valid=1` (`soak_8swaps_extract.log`) — the retargeter binds the new avatar cleanly,
  which is exactly what in-place mutation broke (2026-07-08).
- **Visual:** `DyadSoak_01_Wallace.png` — the outgoing avatar after a full settle
  interval: standing, intact, symmetric resting arms, no mangling (nobody in the webcam
  frame, so rest pose is the expected pose; the black quad is the desk-mode-disabled
  scene-capture mirror).

The soak subsystem (`mp.DyadRespawnSoakSeconds`, default 0) stays in the build as the
regression stress tool and Phase 6 rehearsal aid.

## Repro

```
UnrealEditor-Cmd.exe <uproject> /Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01 \
  -game -windowed -ResX=1280 -ResY=720 -nosplash -nopause \
  -ExecCmds="mp.DyadRespawnSoakSeconds 15" -abslog=<log>
# screenshots land in Saved/Screenshots/WindowsEditor/DyadSoak_<n>_<Profile>.png
```
