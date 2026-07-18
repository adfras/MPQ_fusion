# HANDOFF — Fix Maria/Hudson/Payton groom hair (2026-07-18)

Read `AGENTS.md` and the memory file
`~/.claude/projects/C--Users-Alan/memory/alan-working-preferences.md` first.
Work in `D:\Epic\Unreal_Projects\TestingKit5` DIRECTLY — **never a git worktree**:
`Content/*` is gitignored, so a worktree has no maps and no MetaHumans.

## The task, in one line

When Maria, Hudson, or Payton is spawned as a DRIVEN avatar (lobby menu selection,
mirror self-view, dyad preview rigs), their strand hair renders as an opaque orange
speckled blob; a VANILLA `BP_Maria` spawned in the same world renders perfect hair.
Find the step in our spawn/driving path that breaks it, fix it, and prove it.

## Acceptance (Alan's bar)

1. Desk PIE `L_DyadLobby_01`: select each of Maria/Hudson/Payton via
   `mp.DyadSelectAvatar self <Name>` — mirror clone shows REAL hair. Screenshot each.
2. Re-run the portrait soak and regenerate portraits (steps below); all six PNGs
   visually correct.
3. One short worn check by Alan (his eyes are the only judge). Never claim "fixed"
   without image evidence from the participant's viewpoint.
4. Suite still green: expect **232** successes (`Automation RunTests MediaPipe`).

## Established facts — do not re-litigate (evidence in Saved/Screenshots/WindowsEditor)

- **Content is INNOCENT**: `Verify_VanillaMaria3.png` — vanilla `BP_Maria` spawned at
  (-70,-30) renders flawless long dark hair IN THE SAME FRAME as the blob-headed
  driven copy. This is the pivotal fact; everything else hangs off it.
- Material parents WERE genuinely missing (`/Game/MetaHumans/Common/Materials/MI_Hair*`
  → `M_hair_v4`) and were copied in from TestingKit3 on 2026-07-18. That fix is real
  and must STAY, but it did not clear the blob. Parent chain verified loading
  end-to-end; registry needed `scan_paths_synchronous` after the copy.
- Eliminated with evidence: shader compilation (blob survives full drain + settle),
  renderer config (`r.HairStrands.* = 1`, SkinCache on, deferred shading both
  projects), LODSync forcing (`mp.AutoQuestVrMetaHumanForcedLod` -1 / 0 / 1 all blob:
  `Verify_MariaLodAuto.png`, `Verify_MariaLod0.png`), scene captures (none exist —
  the self-view clone is a REAL actor standing AT the mirror (0,115,0), viewed
  directly), per-mesh skin cache (our code never touches it), global scalability
  (side-by-side rules out anything global).
- Groom components on driven copies are fully wired: assets + bindings set, visible,
  attached to the Face component, same `Maria_FaceMesh` as vanilla.
- Emory/Wallace/Kellan render fine driven — their coarse hair representations are
  good; the three broken ones are the long/heavy grooms.
- TestingKit5 now has full TK3 content parity for Common + these three characters
  (`cp -rn` added zero files beyond the first Materials pass).

## The decisive next experiment — a 3-way bisect of OUR pipeline

The only remaining difference between perfect-vanilla and blob-driven is what our
code does at/after spawn. Bisect it: reproduce the driven spawn but skip ONE step at
a time; whichever skipped step makes the hair render is the bug.

Code pointers:
- `Source/DyadStudy/DyadAvatarRigFactory.cpp:48-90` — dyad rig spawn:
  `SpawnActorDeferred` → tag → `FinishSpawningActor` →
  `MediaPipeDriverRuntime::ApplyLiveMetaHumanQualityProfile(MetaHumanActor)`.
- `Source/MediaPipeDriver/Runtime/MediaPipeDriverRuntime.cpp:1507`
  `ApplyAutoQuestMetaHumanQualityProfile` (LODSync force — already exonerated alone,
  but test it inside the full path), spawn sites at lines ~1885 and ~1935 (live +
  self-view MetaHuman spawns — read what happens AFTER each FinishSpawningActor:
  anim-instance replacement, active-profile set, tags, tick settings).
- The respawn path: `UDyadAvatarSwapLibrary::RespawnPawn` (Source/DyadStudy).
- Suspects in priority order: (a) the pose-driven anim-instance replacement on the
  body/FACE meshes (grooms deform from the face mesh's skinned data — check what the
  driver changes on those mesh components: tick options, update-rate optimizations,
  `VisibilityBasedAnimTickOption`, master-pose/leader-pose setup); (b) the per-mesh
  active-profile resolver (P0 machinery); (c) anything that swaps or re-registers
  the Face mesh after the groom binding initialized.
- Quick variant of the bisect: spawn a vanilla BP_Maria, then apply our steps to IT
  one at a time (quality profile → anim instance → profile/tags) until it blobs.

## Working the editor (the in-editor MCP)

`http://localhost:3000/mcp` (Streamable HTTP, McpAutomationBridge). Pattern:
POST `initialize` (protocolVersion 2025-03-26) → `notifications/initialized` →
`tools/call`. Everything useful goes through
`system_control {action: "execute_python", code: "..."}` — full editor Python.
Known quirks: first calls after editor boot fail silently (wait ~20 s after MCP
answers); `load_level` can outlive the HTTP timeout but still succeed (verify via
readback, not the call result); `TestingKitPlayerController` has no `console_command`
python attr — use `unreal.SystemLibrary.execute_console_command(world, ...)`;
`get_game_world()` on `UnrealEditorSubsystem` reaches the PIE world; HighResShot
renders on the NEXT presented frame. `manage_widget_authoring` tree verbs and
`map_input_action` are broken (drop names / no-op) — don't use them.

## Build / test gotchas (they cost this machine a full morning)

- Editor + LiveCodingConsole must be CLOSED before any build.
- Use direct UBT, not the wrapper (the wrapper's UBA cache restores corrupt objs):
  `/d/Epic/UE_5.8/Engine/Build/BatchFiles/Build.bat TestingKit5Editor Win64
  Development -Project=D:/Epic/Unreal_Projects/TestingKit5/TestingKit5.uproject
  -WaitMutex -NoUBA -UBANoDetour -MaxParallelActions=2`
- Something on this machine (likely Malwarebytes) randomly kills `cl.exe`/`bash`
  with NO diagnostic — a silent exit-1 on a file you didn't touch is the flake:
  just retry; finished objs persist. Ask Alan about excluding `D:\Epic` from AV.
- Suite: `UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests MediaPipe;
  Quit" -TestExit="Automation Test Queue Empty" -unattended -nop4 -nosplash
  -nullrhi`; judge by `grep -c "Test Completed. Result={Success}"` (expect 232) in
  `Saved/Logs/TestingKit5.log` — GNU grep, never ripgrep on Saved/Logs; grep -c
  returning 0 matches exits 1 (not a failure).
- The editor RELAUNCH rotates `TestingKit5.log` — count from the right file.

## After the fix

1. Regenerate portraits: PIE the lobby → console `mp.DyadRespawnSoakSeconds 8` →
   wait a full cast cycle (~8 shots, ~2 min) → `mp.DyadRespawnSoakSeconds 0` → stop
   PIE → `python Tools/make_dyad_portraits.py` → READ all six PNGs yourself
   (Content/DyadStudy/Portraits/). Identity anchors: Emory = red-sweater short
   adult, Wallace = grey-shirt long hair, Kellan = athletic dark-haired adult.
2. Suite (expect 232+; count only grows), commit with an honest message stating
   what is worn-verified vs desk-only, push to main.
3. Stage Alan's worn check: editor open on `L_DyadLobby_01`,
   `mp.DyadRole host`, `mp.DyadSeat A`,
   `mp.DyadConditionFile Config/DyadStudy/condition_free_pilot.json`; watch
   `Saved/Logs/TestingKit5.log` for `host listening on port 8123` then run
   `python Tools/dyad_partner_player.py --start 2 --duration 26 --ready-delay 15`.
   Read `Saved/QuestScreenshots/<session>/` frames unprompted afterwards.

## Alan's working practice (violations broke trust before — non-negotiable)

Fixes, not diagnostics — instrument silently, present results. Own mistakes in the
first sentence. One worn test, under ~30 seconds, only when everything is staged.
The mirror avatar as HE sees it is the only acceptance judge. Do everything asked
or state explicitly what is not done. Never claim "fixed" without his-viewpoint
image evidence.

## Still pending elsewhere (do not absorb into this task)

Alan's worn verdicts on: Wallace arms after calibration persistence
(mp.QuestArmCalibrationPersist), the resized upper-left menu + fat Confirm button,
shrug damping, exposure word (PP volume pinned Manual EV -3.55). Terry has no
profile and no portrait (registry has 6, plan says 7). Interaction-room chips per
DYAD_PHASE6_RUN_SHEET.md stay parked.
