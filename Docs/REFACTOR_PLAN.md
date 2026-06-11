# TestingKit5 Structural Refactor & Cleanup Plan

Status: plan authored 2026-06-11 from a measured audit of the working tree. Not yet executed.
Execute one phase per session, in order. Do not combine phases. Stop on any gate failure and
apply the phase rollback note before diagnosing.

Goal: well structured, organised, as simple as possible, easy to understand — WITHOUT changing
runtime behavior of the working replay solve. This is a structure/clarity refactor, not a
solve-tuning task.

---

## 1. Hard invariants (every phase must protect all five)

1. **Replay keeps working end to end**: map
   `/Game/MetaHumanRooms/L_MetaHumanRecordedQuestMediaPipeReplay_01`, dataset
   `Saved/CodexAgent/Diagnostics/tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656_replay_source*`
   (16 files, ~670 MB, measured). The canonical dataset is NEVER deleted or moved without a
   hash-verified copy (made in Phase 0).
2. **Replay policy semantics** stay exactly as encoded in
   `Source/MediaPipeDriver/Diagnostics/MediaPipeTrackingFusionDatasetReplay.cpp:836-871`
   (`ApplyReplayPoseCVars_GameThread`): `mp.MediaPipeUseLegIK=0`,
   `mp.MediaPipeUseLegIKFootPlant=0`, `mp.MediaPipeUseFkRootGrounding=1`,
   `mp.MediaPipeLegKneeBackwardPoleSuppression=0.6`, `mp.MediaPipeFootGroundedWorldUp=1`,
   `r.HairStrands.Simulation=0`, shadow/screen-percentage restore
   (`sg.ShadowQuality 3`, `r.ShadowQuality 5`, `r.Shadow.MaxResolution 2048`,
   `r.ScreenPercentage 100`), and BodyFusion lower body diagnostics-only for ALL avatars
   (`ShouldUseAvatarLockedReplay`, `MediaPipePoseDrivenAnimInstance.cpp:1932-1939` +
   early return at `:2180-2186`). That function's CVar list is the policy contract; any phase
   touching policy code must reproduce it value-for-value.
3. **Avatar generality**: identical behavior for Manny and all six MetaHuman profiles
   (Kellan, Wallace, Emory, Hudson, Maria, Payton) through profile-driven code. No phase may
   introduce a per-avatar branch (review diff for avatar-name conditionals before commit).
4. **Verification gates** (section 2) pass after EVERY phase, at the level the phase class
   requires.
5. **No new VR/headset captures. Never move the player pawn during PIE measurements** — in
   the replay map the player pawn IS the embodied avatar
   (`MP_PlacedEmbodiedMetaHumanPawn`); use a summoned CameraActor for screenshots.

## 2. Verification gates

All build/automation steps require the editor closed (no `UnrealEditor.exe`,
no `LiveCodingConsole.exe`). No Live Coding, ever.

- **GATE-BUILD** — bounded editor build succeeds:
  `D:\Epic\UE_5.8\Engine\Build\BatchFiles\Build.bat TestingKit5Editor Win64 Development
  -Project="D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject" -WaitMutex -NoUBA
  -UBANoDetour -MaxParallelActions=4` (or `Tools\BuildTestingKit5EditorFast.ps1` once Phase 1
  fixes its false-stall kill).
- **GATE-AUTO** — headless automation, exit code 0, expected count met:
  `UnrealEditor-Cmd.exe <uproject> -nullrhi -unattended -nop4 -nosplash
  -ExecCmds="Automation RunTests TestingKit5.MediaPipe; Quit"
  -TestExit="Automation Test Queue Empty"` = **112/112** (since Phase 6; was
  `TestingKit5.MediaPipe+TestingKit3.MediaPipe.BodyFusion+TestingKit3.MediaPipe.BodySolverMath`
  = 54/54 before the prefix migration. Execution-time correction to section 3.6: the true
  pre-migration inventory was 94 `TestingKit3.*` names — 93 in Tests/ plus one registered
  inside `MediaPipePoseDrivenAnimInstance.cpp:387` — plus 18 `TestingKit5.*` = 112. A further
  ~32 tests use a bare `MediaPipe.*` prefix and are outside this gate's filter.)
- **GATE-PY** — `python Tools/TestAnalyzeTrackingFusionDataset.py` = 20/20 and
  `python Tools/PlotKellanReplayMeasurements.py --selftest` OK.
- **GATE-PIE** — live PIE replay measurement on the replay map with
  `Tools/kellan_replay_bone_sampler.py` (promoted from `Saved/CodexAgent/` in Phase 4; until
  then use the `Saved/CodexAgent/` copy): seek 147 s, 66 s capture, BOTH actors
  (`MP_LiveMetaHumanKellan`, hidden verification Manny). Pass criteria (2026-06-10 final
  baseline): knee L range ≈ 121–175°, knee R ≈ 113–175° on both avatars; lowest-ball median =
  grounded reference (Kellan 0.80 cm, Manny 0.75 cm); 0 penetration frames; segment-length
  drift 0.0000 cm. Do not move the player pawn during capture.

Phase classes:

- **MECHANICAL** (move/rename/delete, no logic change): GATE-BUILD + GATE-AUTO + GATE-PY.
  Doc-only/disk-only phases may skip GATE-BUILD where stated.
- **BEHAVIORAL** (touches runtime code paths): all four gates including GATE-PIE.

## 3. Measured baseline (evidence the phases are built on)

All numbers measured 2026-06-11 on the working tree.

### 3.1 Oversized files
- `Source/MediaPipeDriver/PoseDriven/MediaPipePoseDrivenSkeletalActor.cpp` — **5,476 lines**.
  Lines 41–4967 are one anonymous namespace of capture/diagnostic tooling (23 file-local
  CVars at :43-660; MPQ shadow auto-start :661-1251; dataset JSON helpers :1252-2117; capture
  CVar snapshot/restore :2118-2200; HUD drawing :2201-2293; tracking-fusion dataset recorder
  :2294-3450; Manny bone timeseries recorder + analyzer invocation :3451-3760; capture
  preparation commands :3761-4201; MPQ shadow fusion capture :4202-4740; console command
  registrations :4741-4767; placement/material helpers :4820-4967). The actual actor
  implementation is only :4968-5476 (~510 lines).
- `Source/MediaPipeDriver/Runtime/MediaPipeDriverRuntime.cpp` — **3,954 lines**, containing
  **482 `SetConsoleInt/Float` calls**. `ApplyAutoQuestProfile` spans :3142-3631 (~490 lines of
  raw CVar stomps); `ApplyStableMediaPipeRetargetProfile` :575-626; the leg-freeze regression
  guard `ReassertTrackingFusionReplayPoseCVarsIfActive` :562-573.
- `Source/MediaPipeDriver/PoseDriven/MediaPipePoseDrivenAnimInstance.cpp` — **3,435 lines**,
  plus `.inl` includes at :1818, :1877-1879, :2967-2973; `_QuestArmWristSolve.inl` chains four
  more. The single TU compiles ≈ **14,000 lines** (3,435 + QuestArmSolve 3,568 + QuestHandRotation
  1,711 + BodyPoseSolve 1,346 + QuestSpaceMapping 921 + QuestFingerDrive 884 + ReferenceCache
  777 + LegSolve 732 + ArmTwist 447 + TorsoBasis 186 + BodyState 6), takes >2 min silent, and
  is the direct cause of the build wrapper's false stall kills.

### 3.2 CVars
- **414 distinct `mp.*` `TAutoConsoleVariable` definitions** in `Source/MediaPipeDriver`.
  269 live in `Runtime/MediaPipeRuntimeCVars.cpp`; ~145 are file-local in 8 other files
  (`MediaPipeDriverRuntime.cpp` ~53, `MediaPipeSourceConditioner.cpp` 30,
  `MediaPipePoseDrivenSkeletalActor.cpp` 24, `MediaPipeQuestWebcamSourceActor.cpp` 12,
  `MediaPipeBodyFusionRegionQuality.cpp` 7, `MediaPipeTrackingFusionDatasetReplay.cpp` 5,
  `MediaPipeSolvedPose.cpp` 4, `MediaPipePoseTrackerComponent.cpp` 2).
- **539 `SetConsole*` writer calls** across 4 files: `MediaPipeDriverRuntime.cpp` 482,
  `MediaPipeTrackingFusionDatasetReplay.cpp` 51, `MediaPipeEmbodiedAvatarPawn.cpp` 3,
  `MediaPipeDriverRuntime.h` 3 — plus the `SetConsoleVariable*ForTrackingDataset` /
  `*ForShadowCapture` families in `MediaPipePoseDrivenSkeletalActor.cpp`, plus
  `Config/DefaultEngine.ini:266-288` setting 23 `mp.*` values at startup. Multiple
  uncoordinated writers are exactly how the 2026-06-10 leg-freeze regression happened.

### 3.3 Verified dead code
- `ShouldUseBodyFusionStage1TorsoPelvisHintForEvaluation` returns hard `false`
  (`MediaPipePoseDrivenAnimInstance.cpp:1941-1944`); `DriveBodyFusionStage1TorsoPelvisHintCS`
  is a `(void)` no-op (:1967-1972). Related: CVar defs `MediaPipeRuntimeCVars.cpp:95-115`,
  accessors `MediaPipeBodyFusionRuntime.cpp:130-160` + snapshot fields :36-46, MPQ capture
  plumbing in `MediaPipePoseDrivenSkeletalActor.cpp` (:91-95, :3489-3492, :3957, :4032-4062,
  :4129-4134, :4329 and the `bStage1TorsoPelvisHint` arguments threaded through).
- Unreachable block in `DriveBodyFusionPoseCS`: early `return false` when
  `bAvatarLockedReplay` (`MediaPipePoseDrivenAnimInstance.cpp:2180-2186`) makes the
  `bAvatarLockedReplay && BodyFusionFrame.Pose.Head.bValid` block at :2269-2287 unreachable.
- `MannyBodyRig` `UControlRigComponent`: `mp.UseMannyBodyRig` default **0**
  (`MediaPipePoseDrivenSkeletalActor.cpp:43-47`); no `Config/*.ini` reference; binary grep of
  every `Content/**/*.umap|*.uasset` found **zero** references. Code:
  `MediaPipePoseDrivenSkeletalActor.h:40,61,69` and `.cpp:4976-4982,5029-5031,5122-5163,5400-5406`.
- Leg target-IK + foot-plant path: `MediaPipePoseDrivenAnimInstance_LegSolve.inl:455-732`
  (`bDoLegIK` at :455, foot-plant lock :523-571). Replay policy forces both off; measured
  worse on Kellan replay (Docs/AVATAR_REPLAY_OUTPUT_FIX_CHECKLIST.md, target-IK vs final
  table). Binary grep of Content found zero `mp.MediaPipeUseLegIK` references. CAUTION:
  `ApplyStableMediaPipeRetargetProfile` sets `mp.MediaPipeUseLegIK=1` (with
  `mp.MediaPipeDriveLegs=0`) at `MediaPipeDriverRuntime.cpp:584` — decision phase (12) must
  confirm the path is inert when `DriveLegs=0` before deletion.

### 3.4 Root clutter (29 files at root; 21 tracked)
- Untracked disk litter (already gitignored): `Test.txt`–`Test4.txt` (Codex bridge trace
  summaries, "consolidated 2026-06-05"), `ce0a1c04-….png` (1.2 MB), `ump_shared.dll` (8.4 MB).
- `ump_shared.dll` at root is dead: the DLL resolver
  (`MediaPipePoseTrackerComponent.cpp:1155-1184`) only probes `Binaries/Win64[/mediapipe]` and
  `ThirdParty/mediapipe_wrapper/`, where an identical-size copy exists.
- `Start_Codex_Unreal_Agent.bat` hardcodes `D:\Epic\UE_5.7\...\UnrealEditor.exe`; project is
  UE 5.8 (falls back to shell association, so it silently works by accident).
- `Automation_TestingKit5.sln/.slnx`: regenerable, referenced by nothing (repo-wide grep: 0 hits).
- 11 tracked root `.md/.MD` files overlapping `Docs/` (classification in Phase 3).

### 3.5 Uncommitted working state (highest immediate risk)
`git status`: **19 modified + 9 untracked files** — the entire verified 2026-06-10 replay
quality state (anim instance, solver math, replay runtime, region quality, tests, docs) and,
critically, the replay map itself
(`Content/MetaHumanRooms/L_MetaHumanRecordedQuestMediaPipeReplay_01.umap`, untracked, 0.2 MB)
and `Tools/PlotKellanReplayMeasurements.py` (untracked). The verification sampler
`kellan_replay_bone_sampler.py` lives in gitignored `Saved/CodexAgent/`. The verified-working
state currently exists ONLY on this disk.

### 3.6 Tests
110 automation test names in `Source/MediaPipeDriver/Tests/*.cpp`: **92 `TestingKit3.MediaPipe.*`
+ 18 `TestingKit5.MediaPipe.*`**. The 54/54 gate is the union of three filters. Doc references
to `TestingKit3.*` names are almost all historical evidence logs (must NOT be rewritten); the
only active-doc caveat is `Docs/MEDIAPIPE_VALIDATION_AND_OPERATIONS.md:20`. No Tools/Scripts
file filters by prefix (grep verified).

### 3.7 Disk
`Saved/` ≈ **22.0 GB**: `CodexAgent/Diagnostics` 17.6 GB / 6,720 files (mpq_shadow_latency
5.5 GB / 57 files, all 06-07/06-08; tracking_fusion_dataset 7.2 GB / 172 files incl. one
666 MB `tracking_fusion_dataset_correlation_full_body.json`; vp2 3.4 GB / 589 files),
`QuestScreenshots` 1.79 GB, `Crashes` 1.17 GB, `Saved/Logs` 394 MB (9 editor logs >16 MB, all
06-10), `CodexAgent/Screenshots` 254 MB / 354 files, `Videos` 174 MB. Also `_MCPBench/Tools`
156 MB (gitignored), `ReviewDeliverables/` 39 MB (gitignored), `Tools/__pycache__` 0.6 MB,
`Tools/MetaXRCompare` **empty directory**.

---

## 4. Phases

Ordering principle: safety net first, then the tooling everyone depends on, then zero-code-risk
deletions/moves (high relief), then code restructuring from provably-dead outward to behavioral.

---

### Phase 0 — Safety net: commit the working state, back up the dataset, record the baseline
- **Class**: MECHANICAL (no code edits).
- **Scope**:
  1. `git add` the 9 untracked files (including the replay map umap) and commit all 28 dirty
     entries as the "verified 2026-06-10 replay quality state". Tag `replay-solve-baseline-20260610`.
  2. Copy the 16 canonical dataset files
     (`tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656*`, ~670 MB) to
     a backup location OUTSIDE the project tree (e.g. `D:\Backups\TestingKit5_CanonicalReplayDataset\`),
     write a SHA-256 manifest, verify hashes match. Do NOT commit 670 MB to git.
  3. Run all four gates once and record results in this file's execution log (build, 54/54,
     20/20 + selftest, PIE numbers) — this is the regression reference for every later phase.
- **Expected delta**: +1 commit, +1 tag, +670 MB backup. No source changes.
- **Risk**: minimal. Committing the umap is additive.
- **Invariant protection**: this phase IS the protection — invariant 1's "verified copy"
  and the rollback anchor for every later phase.
- **Gate**: all four (establishes baseline numbers).
- **Rollback**: none needed (additive only).

### Phase 1 — Fix the fast-build wrapper's false stall kill
- **Class**: MECHANICAL (tooling only; no runtime code).
- **Scope**: `Tools/BuildTestingKit5EditorFast.ps1` (stall loop :112-136), `AGENTS.md` build
  rules (:17-19).
- **Change**: the wrapper currently treats "no stdout/stderr/UBT-log growth for 120 s" as a
  stall and kills the build tree — but the ~14k-line anim-instance TU (section 3.1) compiles
  >2 min producing no output. Add a third progress signal: sample
  `cl.exe`/`link.exe` `TotalProcessorTime` each 5 s poll; growing CPU time = progress, reset
  the stall timer. Kill only when logs AND compiler CPU are both flat for `StallSeconds`.
  Include the CPU-probe evidence in the stall exception text. Update `AGENTS.md` to describe
  the new behavior and remove the "treat 120 s quiet as stall" manual advice.
- **Expected delta**: ~+25 lines in the wrapper; ~5 lines in AGENTS.md.
- **Risk**: low. Worst case the wrapper waits longer before killing a genuinely-hung build.
- **Invariant protection**: none of the runtime code is touched; gates verify the wrapper
  itself by exercising it on the worst-case TU.
- **Gate**: GATE-BUILD run TWICE via the wrapper — once incremental, once after touching
  `MediaPipePoseDrivenAnimInstance.cpp` (forces the >2 min TU) — must complete without a
  false kill. GATE-AUTO + GATE-PY.
- **Rollback**: `git checkout` the two files; the raw bounded command remains the fallback.

### Phase 2 — Root clutter removal
- **Class**: MECHANICAL.
- **Scope** (root directory only):
  - Delete untracked litter: `Test.txt`–`Test4.txt` (content = bridge-trace lessons already
    consolidated; if any lesson is still wanted, fold into `AGENTS.md` first — they are 4×<1 KB,
    review takes minutes), `ce0a1c04-ffbc-4977-acea-4c11f4340857.png`.
  - Delete root `ump_shared.dll` after hash-comparing with
    `ThirdParty/mediapipe_wrapper/ump_shared.dll` (evidence: resolver never probes root,
    section 3.4; if hashes differ, keep both and investigate before deleting).
  - Fix `Start_Codex_Unreal_Agent.bat`: `UE_5.7` → `UE_5.8`.
  - `git rm` `Automation_TestingKit5.sln` + `Automation_TestingKit5.slnx` (regenerable,
    zero references). Keep `TestingKit5.sln/.slnx` (actively used by VS).
- **Expected delta**: root 29 → 21 files; −10 MB disk; −2 tracked files.
- **Risk**: minimal; everything deleted is unreferenced and/or regenerable.
- **Invariant protection**: no Source/Content/Saved paths touched.
- **Gate**: GATE-BUILD + GATE-AUTO + GATE-PY (cheap insurance that nothing referenced the
  deleted files).
- **Rollback**: untracked deletions are unrecoverable — hence the hash check and lesson review
  BEFORE deleting; tracked removals revert via git.

### Phase 3 — Documentation consolidation into `Docs/` + single index
- **Class**: MECHANICAL (no code).
- **Scope**: `git mv` root markdown into `Docs/` or `Docs/Archive/`; update `Docs/README.md`.
  - → `Docs/` (active): `MPQ_Stage2A_Conflict_Stress_Test_Plan.md` (README already links it).
  - → `Docs/Archive/` (superseded/historical, tracked): `steps.MD`, `body_fusion_steps.MD`,
    `MPQ_fusion_architecture_refactor_plan.md`, `MPQ_fusion_do_now_checklist.md`,
    `MPQ_fusion_refactor_cutback_checklist.md`, `Codex_Agent_Efficiency_Audit.md`,
    `Codex_Unreal_Agent_Implementation_Audit.md`, `Codex_Unreal_Agent_Plan.md`,
    `MediaPipe_Shoulder_Baseline.md`, `VP2_Manny_Tracking_Fix_Solutions.md`.
  - `MPQ_shadow_fusion_review_deliverables_20260605.md` is untracked AND gitignore-matched —
    move to `Docs/Archive/` only if the gitignore pattern is narrowed; otherwise leave with
    `ReviewDeliverables/`.
  - Also archive clearly-dated `Docs/` files: `MEDIAPIPE_REFACTOR_STATE_2026-05-17.md`,
    `MEDIAPIPE_TRACKING_ISSUE_AND_NEXT_PLAN_2026-05-25.md`,
    `EMBODIMENT_ARCHITECTURE_COMPARISON_2026-05-24.md`,
    `EMORY_FORWARD_LEAN_NECK_FINDINGS_2026-05-26.md`, `QUEST_WRIST_SOLVE_FREEZE_2026-05-11.md`,
    `WALLACE_ARM_PIPELINE_AUDIT_2026-05-19.md`, `WALLACE_QUEST_VR_ARM_ROLLBACK_ANALYSIS_2026-05-17.md`,
    `AVATAR_LOCKED_SYNC_IMPLEMENTATION_CHECKLIST.md`, `AVATAR_LOCKED_SYNC_REMAINING_CHECKLIST.md`
    (both completed). Keep active: `README.md`, `MEDIAPIPE_VALIDATION_AND_OPERATIONS.md`,
    `MEDIAPIPE_PIPELINE_WALKTHROUGH.md`, `METAHUMAN_PROFILE_DRIVEN_RETARGETING.md`,
    `AVATAR_PROFILE_DRIVEN_EMBODIMENT.md`, `AVATAR_LOCKED_SYNC_CALIBRATION_CAPTURE_PROTOCOL.md`,
    `AVATAR_REPLAY_OUTPUT_FIX_CHECKLIST.md`, `MEDIAPIPE_VR_MIRROR_BASELINE.md`,
    `MEDIAPIPE_WEBCAM_INCLUSION.md`, `WALLACE_QUEST_VR_CURRENT_DEFAULTS_HANDOFF.md`,
    `WALLACE_QUEST_VR_EMBODIMENT_GUARDRAILS.md`, this plan.
  - Create `Docs/Archive/INDEX.md`: one line per archived doc (what it was, why archived,
    date). Rewrite `Docs/README.md` as the single authoritative index (active docs, archive
    pointer, evidence-data locations). Historical doc CONTENT is not edited.
  - AGENTS.md stays at root (agent entrypoint convention).
- **Expected delta**: root .md count 13 → 1 (AGENTS.md); `Docs/` becomes the single doc tree.
- **Risk**: low; moves only. Some docs cross-reference root paths (e.g. README links
  `../MPQ_Stage2A_...`) — fix links in the same commit (grep `\]\(\.\./` and bare root names).
- **Invariant protection**: no code/data touched.
- **Gate**: GATE-PY (scripts embed no doc paths — verify with grep before declaring) +
  link-check grep. GATE-BUILD/AUTO not required (no code), run GATE-AUTO anyway if cheap.
- **Rollback**: `git revert` (pure moves).

### Phase 4 — Saved/ retention policy, deletion manifest, cleanup script, .gitignore
- **Class**: MECHANICAL (disk + tooling; no runtime code).
- **Pre-condition**: Phase 0 dataset backup verified.
- **Scope**:
  1. Promote load-bearing scripts out of gitignored `Saved/CodexAgent/` into `Tools/` and
     commit: `kellan_replay_bone_sampler.py` (GATE-PIE depends on it). Audit the other
     `Saved/CodexAgent/*.py` one-offs; promote only what the gates or docs reference.
  2. Write `Tools/CleanCodexAgentOutputs.ps1` with `-DryRun` default and `-Apply` to execute.
     Inputs: a keep-manifest (explicit globs) + retention rule "keep newest 2 runs per capture
     family" (family = filename prefix before the timestamp). Hard-coded NEVER-delete list:
     `*20260609_170656_replay_source*` (canonical dataset + manifest).
  3. Keep-manifest (from section 3.7 audit): the canonical dataset (670 MB); 2026-06-10
     evidence — `kellan_live_pie_bone_measure_baseline_20260610_*.json`,
     `kellan_live_pie_bone_measure_after_grounding_20260610_173948.json`,
     `live_pie_bone_measure_*_final_all_avatars_20260610_*.json`,
     `final_quality_plots_20260610/`, `kellan_replay_quality_plots_20260610/`,
     `tracking_fusion_dataset_replay_avatar_output_tk3direct_final_20260609_210445*` (final
     replay-output evidence chain from the checklist doc); showcase + final screenshots
     (32 files matched `showcase|final_all_avatars|after_grounding|baseline_fix`).
  4. Deletion manifest (path, size, age, why-safe) generated by the script in dry-run; review,
     then apply. Expected major entries: `mpq_shadow_latency_*` 5.5 GB (06-07/06-08 Stage 2A
     tuning runs; conclusions captured in `MPQ_Stage2A_Conflict_Stress_Test_Plan.md` evidence
     sections — keep only the final run cited there), `vp2 family` 3.4 GB (superseded VP2
     investigation; doc archived in Phase 3), `tracking_fusion_dataset_correlation_full_body.json`
     666 MB (intermediate), non-final replay-output datasets + their `_signal_plots` (~1.5 GB),
     `Saved/Crashes/*` >14 days (1.17 GB), `Saved/Logs/TestingKit5-backup-*` keep newest 5
     (~330 MB freed), `Saved/CodexAgent/BuildLogs` keep newest 5. `QuestScreenshots/` (1.79 GB)
     and `Videos/` (174 MB) are VR-session evidence — list in the manifest but require explicit
     user approval. `_MCPBench/Tools` (156 MB) and `ReviewDeliverables/` (39 MB): user decision,
     listed with sizes.
  5. `.gitignore` corrections: add `Saved/CodexAgent/` is already covered by `Saved/`; narrow
     the global `*.png`/`*.dll` rules to root-anchored (`/*.png`, `/*.dll`) so future tracked
     evidence images/plugin DLLs aren't silently ignored; keep `Test*.txt` root-anchored
     (`/Test*.txt`).
- **Expected delta**: `Saved/` 22 GB → ≈1.5–2 GB; +1 reusable cleanup script with dry-run.
- **Risk**: medium (irreversible deletions) — mitigated by Phase 0 backup, dry-run review,
  hard-coded never-delete list, and user approval for the flagged categories.
- **Invariant protection**: canonical dataset protected three ways (backup + never-delete
  list + keep-manifest). GATE-PIE after cleanup proves the replay still loads from disk.
- **Gate**: GATE-PY + **GATE-PIE** (replay must still load the dataset end-to-end) +
  GATE-BUILD/AUTO unaffected but run GATE-AUTO once.
- **Rollback**: restore canonical files from the Phase 0 backup; other deletions are accepted
  as permanent (that is the point) — hence dry-run sign-off first.

### Phase 5 — Tools/ and Scripts/ consolidation
- **Class**: MECHANICAL.
- **Scope**:
  - Keep in `Tools/` (verified load-bearing): `AnalyzeTrackingFusionDataset.py`,
    `TestAnalyzeTrackingFusionDataset.py`, `PlotKellanReplayMeasurements.py`,
    `BuildTestingKit5EditorFast.ps1`, `BuildTrackingFusionReplayCache.py`,
    `SetupRecordedQuestMediaPipeReplayMap.py`, `VerifyRecordedQuestMediaPipeReplayMap.py`,
    `kellan_replay_bone_sampler.py` (Phase 4), and the three invoked from C++/ini —
    `analyze_manny_head_trace.py` (`MediaPipePoseDrivenSkeletalActor.cpp:70-77`,
    `DefaultEngine.ini:287`), `AnalyzeMPQShadowFusionCapture.py` (`...cpp:118,4250,4321`),
    `AnalyzeTrackingFusionDataset.py` (`...cpp:172-177`).
  - Move to `Tools/Archive/` with an INDEX.md line each: the May-era Wallace/Quest-wrist/VR
    one-offs (`AnalyzeMetaHumanArmTwitchLog.ps1` [67 bytes], `AnalyzeMetaHumanBodyReplayLog.ps1`,
    `AnalyzeQuestWristRollLog.ps1`, `AnalyzeWallaceArmTwitchLog.ps1`,
    `CaptureMetaHumanQuestVrEvidence.ps1`, `CaptureWallaceQuestVrEvidence.ps1`,
    `CheckMetaHumanGenericProfileGuards.ps1`, `CheckMetaHumanProfileVrPreviewLog.ps1`,
    `CheckQuestXrLiveReadiness.ps1`, `CheckWallaceArmRollDiagnosticLog.ps1`,
    `CheckWallaceArmSourceGuards.ps1`, `CheckWallaceQuestVrEmbodimentLog.ps1`,
    `PrepareMetaHumanVrPreviewProfile.ps1`, `RunQuestWristObjectiveGate.ps1`,
    `StartQuestMirrorEvidenceCapture.ps1`, `StopQuestMirrorEvidenceCapture.ps1`,
    `TestMetaHumanBodyReplayExpectations.ps1`), the one-off embodiment/`Inspect*` scripts
    (`ApplyEmbodiedPawnArchitecture.py`, `InspectEmbodimentArchitecture.py`,
    `InspectEmbodimentDetails.py`, `InspectMirrorAssets.py`, `InspectMirrorMapActors.py`,
    `InspectMovementAvatarMap.py`, `InspectWallacePostProcess.py`,
    `VerifyEmbodiedPawnArchitecture.py`), and the Manny/holistic analyzers IF the in-phase
    reference grep (`Source|Tools|Docs(active)|Config`) confirms nothing current calls them
    (`AnalyzeMannyMediaPipeTimeseries.py`, `CompareHolisticMannySignals.py`).
  - Merge `Scripts/` (5 vp2/manny one-off .py, all 06-07, superseded) into `Tools/Archive/`;
    delete the now-empty `Scripts/`.
  - Delete the empty `Tools/MetaXRCompare/` directory and `Tools/__pycache__/`.
- **Expected delta**: `Tools/` 37 entries → ~12 active + Archive; one obvious place for
  current tooling.
- **Risk**: low. Every move is preceded by a recorded reference-grep; C++-invoked scripts are
  pinned by path and stay put.
- **Invariant protection**: gate scripts stay at their exact paths; GATE-PY exercises them.
- **Gate**: GATE-PY + GATE-AUTO + GATE-BUILD (C++ references Tools paths as defaults — must
  still resolve).
- **Rollback**: `git revert` (pure moves of tracked files).

### Phase 6 — Test prefix migration `TestingKit3.MediaPipe.*` → `TestingKit5.MediaPipe.*`
- **Class**: MECHANICAL (string literals in test registrations only).
- **Scope**: 92 test-name literals across `Source/MediaPipeDriver/Tests/` (11 files; full
  per-file list in section 3.6 grep). Update active docs that state filters:
  `Docs/MEDIAPIPE_VALIDATION_AND_OPERATIONS.md:20` (remove the naming-debt caveat), the
  GATE-AUTO definition in THIS file, and any AGENTS.md mention. Historical/archived docs are
  NOT rewritten — add one line to `Docs/Archive/INDEX.md`: "pre-2026-06 evidence logs use the
  old TestingKit3.* test names". No Tools/Scripts file filters by prefix (verified).
- **Expected delta**: 92 renames; gate filter simplifies to one prefix; expected test count
  becomes 110/110.
- **Risk**: low; the only failure mode is a missed rename shrinking discovery — caught by the
  exact-count assertion (110).
- **Invariant protection**: pure rename; the count assertion proves no test was lost.
- **Gate**: GATE-BUILD + GATE-AUTO with filter `TestingKit5.MediaPipe` = **110/110** + GATE-PY.
- **Rollback**: `git revert` of the rename commit.

### Phase 7 — CVar inventory: generated reference table + ownership classification
- **Class**: MECHANICAL (adds a generator + doc; minimal code movement).
- **Scope**:
  1. New `Tools/GenerateCVarReference.py`: parse `TAutoConsoleVariable` definitions
     (name/default/help/file:line) across `Source/MediaPipeDriver`, find readers
     (`GetValueOn*` on the variable / `FindConsoleVariable` by name) and writers
     (`SetConsole*` / `->Set(` by name, incl. `DefaultEngine.ini`). Emit
     `Docs/CVAR_REFERENCE.md`: one row per CVar — default, definition site, readers, writers,
     classification (policy / diagnostic / capture-tooling / dead-candidate), and a
     **multiple-writers flag** (the leg-freeze conflict class).
  2. Add a self-test mode (`--selftest`) and run it in GATE-PY from now on.
  3. Mechanical relocation ONLY for CVars that are read or written across files but defined
     file-locally (the conflict-prone subset, e.g. the replay-policy CVars listed in
     invariant 2) → move those definitions into `MediaPipeRuntimeCVars.cpp/.h`. Single-file
     diagnostics stay local (documented in the table) to avoid pointless churn.
  4. The dead-candidate column (default-off + zero writers + zero readers outside snapshot
     plumbing) becomes the evidence feed for Phase 8 and the final cleanup backlog.
- **Expected delta**: +1 script, +1 generated doc (~414 rows), ~10–25 CVar definitions
  relocated, zero behavior change.
- **Risk**: low-medium (relocations change initialization file but `TAutoConsoleVariable`
  registration is order-independent for these); each relocation is a single-commit unit.
- **Invariant protection**: replay-policy CVar list (invariant 2) diffed against the generated
  table to prove every policy CVar still exists with an unchanged default.
- **Gate**: GATE-BUILD + GATE-AUTO + GATE-PY (now incl. generator selftest). Run GATE-PIE once
  if any replay-policy CVar definition was relocated.
- **Rollback**: revert relocation commits individually; the generator/doc are additive.

### Phase 8 — Provably-dead code removal
- **Class**: BEHAVIORAL (touches runtime files; logic is provably inert — full gates anyway).
- **Scope** (each item = own commit, evidence in commit message):
  - **8a Stage 1 torso/pelvis hint stack** (section 3.3): remove the hard-false gate + no-op
    driver (`MediaPipePoseDrivenAnimInstance.cpp:1941-1944,1967-1972` + header decls
    `:693,695` + call sites `:3012`), `FMediaPipeBodyFusionRuntimePolicy` Stage1 accessors and
    snapshot fields (`MediaPipeBodyFusionRuntime.cpp:36-46,130-160`), CVar definitions
    (`MediaPipeRuntimeCVars.cpp:95-115`, `.h:26-29`), and the MPQ capture plumbing that
    threads `bStage1TorsoPelvisHint` (`MediaPipePoseDrivenSkeletalActor.cpp`, lines in 3.3;
    `MediaPipeTrackingFusionDatasetReplay*.cpp:110,681` resets). If external capture scripts
    pass `Stage1TorsoPelvisHint=` args, keep arg PARSING as accepted-and-ignored with a log
    line for one release (grep `Saved/CodexAgent` + Tools for the arg name first).
  - **8b Unreachable replay block**: delete `MediaPipePoseDrivenAnimInstance.cpp:2269-2287`
    (early return at :2180-2186 proves unreachability), keeping the comment that replay never
    reaches BodyFusion pose writes.
  - **8c MannyBodyRig**: remove component, `EnsureMannyBodyRig`, `bMannyBodyRigMapped`,
    `mp.UseMannyBodyRig` (evidence: default 0, zero Config/Content references — section 3.3).
    Check `MediaPipeDriver.Build.cs` afterwards: if `ControlRig` was only needed for this,
    drop the module dependency (build-time win).
- **Expected delta**: ≈ −350 to −450 lines net; −5 dead CVars.
- **Risk**: medium-low. Everything removed is unreachable/no-op/never-enabled by measurement,
  not by assumption.
- **Invariant protection**: none of the removed paths participates in the replay solve
  (8a returns false before any pose write; 8b is unreachable; 8c never ticks with CVar=0) —
  and GATE-PIE re-proves the solve numerically.
- **Gate**: all four, GATE-PIE compared against Phase 0 baseline numbers.
- **Rollback**: revert the individual commit (one per sub-item).

### Phase 9 — Extract the capture/diagnostics tooling out of `MediaPipePoseDrivenSkeletalActor.cpp`
- **Class**: MECHANICAL (cut/paste moves, no logic edits) — but staged, one extraction per commit.
- **Scope** (boundaries measured in section 3.1; all new files under
  `Source/MediaPipeDriver/Diagnostics/`):
  - 9a `MediaPipeTrackingFusionDatasetRecorder.cpp/.h` ← dataset recorder + JSON/sidecar
    helpers (:1252-2117, :2294-3450) ≈ 2,000 lines.
  - 9b `MediaPipeMPQShadowCapture.cpp/.h` ← MPQ shadow auto-start/arming/capture
    (:661-1251, :3916-4740) ≈ 1,400 lines.
  - 9c `MediaPipeMannyBoneTimeseries.cpp/.h` ← Manny head/bone timeseries recorder + analyzer
    invocation (:3451-3915) ≈ 460 lines.
  - 9d `MediaPipeCaptureCVarScopes.cpp/.h` ← capture CVar snapshot/restore helpers
    (:2118-2200) ≈ 80 lines (interim home; Phase 11 turns these into policy layers).
  - 9e `MediaPipeTrackingFusionDatasetHud.cpp/.h` ← HUD drawing (:2201-2293) ≈ 90 lines.
  - 9f Console-command registrations (:4741-4767) move beside their implementations. The
    23 file-local capture CVars move with their owning section. The actor file keeps
    :4820-5476 (placement/material helpers + actor) ≈ 650 lines.
  - Rule: function bodies are MOVED VERBATIM (whitespace-identical); only includes,
    forward declarations, and linkage (anonymous-namespace → static or namespaced) change.
    `Tick`'s calls into the recorders become calls through the new headers.
- **Expected delta**: `MediaPipePoseDrivenSkeletalActor.cpp` 5,476 → ≈650 lines; +5 focused
  TUs; net LOC ≈ unchanged (moves). Faster incremental builds (actor edits no longer recompile
  4,300 lines of tooling).
- **Risk**: medium (large mechanical surface). Mitigation: one extraction per commit, verbatim
  moves, `git diff --color-moved` review to prove move-only.
- **Invariant protection**: recorder/capture behavior is gate-covered by the
  `TestingKit5.MediaPipe.Diagnostics.TrackingFusionDataset*` automation (13 tests) and
  GATE-PIE; the replay policy code itself is not in this file.
- **Gate**: GATE-BUILD + GATE-AUTO + GATE-PY per commit; **GATE-PIE once after 9f**.
- **Rollback**: revert the offending extraction commit (each compiles independently).

### Phase 10 — Split the anim-instance mega-TU
- **Class**: MECHANICAL (TU restructuring, zero logic edits).
- **Scope**: `PoseDriven/MediaPipePoseDrivenAnimInstance.cpp` + 11 `.inl` files in
  `PoseDriven/Inline/`.
  1. Move the shared file-local statics (`MediaPipePoseDrivenAnimInstance.cpp:555,1388-1725`:
     Quest basis/roll helpers, hemisphere lock, remaps, arm surface hint) into a private
     header `PoseDriven/MediaPipePoseDrivenAnimInstanceShared.h` (inline/static) — they are
     pure functions. `HalfLifeToAlpha` is already a member (:1746), callable from any TU.
  2. Convert each substantive `.inl` into its own `.cpp` TU compiling the same member-function
     definitions: `_QuestArmSolve` (3,568), `_QuestHandRotation` (1,711), `_BodyPoseSolve`
     (1,346), `_QuestSpaceMapping` (921), `_QuestFingerDrive` (884), `_ReferenceCache` (777),
     `_LegSolve` (732), `_ArmTwist` (447), `_TorsoBasis` (186). Fold `_BodyState.inl` (6 lines)
     and `_QuestArmWristSolve.inl` (4 include lines) away.
  3. Delete the `#include "*.inl"` lines (:1818,1877-1879,2967-2973).
- **Expected delta**: one ~14,000-line TU → 10 TUs, largest ≈3,600 lines; with
  `-MaxParallelActions=4` the worst single compile drops from >2 min to well under the
  wrapper's stall window. This permanently removes the false-stall trigger Phase 1 worked
  around.
- **Risk**: medium. Failure modes are compile-time only (missing include/static visibility),
  not behavioral — definitions are unchanged.
- **Invariant protection**: the leg/body solve code is moved verbatim; GATE-PIE numerically
  re-proves the solve (this file family IS the replay solve, so the PIE gate is mandatory
  despite the mechanical classification).
- **Gate**: all four. GATE-PIE compared to Phase 0 baseline.
- **Rollback**: revert the split commit(s); the `.inl` layout is restored exactly.

### Phase 11 — Layered CVar policy model (replaces profile stomps and snapshot/restore pairs)
- **Class**: BEHAVIORAL — the core architectural fix, sub-phased.
- **Design** (one new pair `Runtime/MediaPipeCVarPolicy.cpp/.h`):
  - A policy = named struct: `{ FName PolicyId; int32 Priority; TArray<FCVarSetting> }`.
    Layers and priority (low→high): `EngineConfig/Baseline` < `LiveProfile` (AutoQuest,
    StableMediaPipeRetarget, MediaPipeOnlyEmbodiedWebcam — data tables, not code walls) <
    `CaptureScope` (dataset capture, MPQ shadow capture, sync-calibration visible policy) <
    `ReplayEvaluation` (the invariant-2 list, verbatim).
  - One applier (`FMediaPipeCVarPolicyStack`): apply/remove a layer recomputes each affected
    CVar from the highest-priority active layer; every transition logs
    `mp.Policy: apply=<id> prio=<n> changed=<k> overridden-by-higher=<m>`.
  - A lower-priority apply can never overwrite a higher active layer — the leg-freeze class
    of bug becomes structurally impossible instead of guard-papered.
- **Sub-phases** (each: own commit, full gates):
  - 11a Introduce the stack; port `ApplyReplayPoseCVars_GameThread` to a `ReplayEvaluation`
    layer whose table is byte-for-byte the invariant-2 list. Keep
    `ReassertTrackingFusionReplayPoseCVarsIfActive` calling the stack (now idempotent).
  - 11b Port the three live profiles: `ApplyStableMediaPipeRetargetProfile`
    (`MediaPipeDriverRuntime.cpp:575-626`), `ApplyMediaPipeOnlyEmbodiedWebcamProfile` (:628+),
    `ApplyAutoQuestProfile` (:3142-3631) into `LiveProfile` data tables. The ~490-line wall
    becomes a table; unconditional `SetConsole*` calls in these paths are eliminated.
  - 11c Port the capture snapshot/restore pairs from Phase 9d
    (`SuppressTrackingFusionDatasetDiagnosticLogCVars`/`Restore…`,
    `ApplyAvatarLockedSyncCalibrationVisiblePolicy`/`Restore…`,
    `ApplyMPQShadowFusionCaptureCVars`) into scoped `CaptureScope` layers.
  - 11d With priority ownership proven over two full gate runs, demote the reassert guard to
    a logged assertion (keep the log row — it is the regression tripwire), then remove the
    redundant re-stomp.
- **Expected delta**: `MediaPipeDriverRuntime.cpp` ≈ −600 lines of imperative stomps (482
  `SetConsole*` calls → table entries + a handful of dynamic ones); +~450 lines policy
  machinery + tables; net ≈ −300 lines and one auditable choke point with logged transitions.
- **Risk**: HIGH — this is the one phase that can silently change effective CVar values.
  Mitigations: (1) before/after dump — run PIE replay and live-map startup, snapshot all 414
  `mp.*` effective values (`DumpConsoleCommands`-style script) and diff; must be empty per
  sub-phase; (2) sub-phase granularity; (3) the Phase 7 generated table flags any CVar whose
  writer set changed unexpectedly.
- **Invariant protection**: invariant 2's list is the `ReplayEvaluation` table itself, diffed
  in review; GATE-PIE after every sub-phase; the CVar-dump diff proves live-VR profiles still
  produce identical effective values (protects live behavior we cannot PIE-measure without a
  headset).
- **Gate**: all four per sub-phase + the effective-CVar dump diff (empty).
- **Rollback**: revert the sub-phase commit; layers are additive until 11d removes the guard,
  so the old path keeps working throughout.

### Phase 12 — Leg target-IK + foot-plant path: decide and record
- **Class**: BEHAVIORAL.
- **Scope**: `PoseDriven/Inline(.cpp after Phase 10)/…_LegSolve…:455-732` (`bDoLegIK`,
  foot-plant lock), CVars `mp.MediaPipeUseLegIK`, `mp.MediaPipeUseLegIKFootPlant`,
  `LegIK*`/`FootPlant*` tunables; `MediaPipeDriverRuntime.cpp:584` (`UseLegIK=1` in the stable
  retarget profile).
- **Decision procedure** (record outcome + evidence in this section when executed):
  1. Verify reachability in live paths: confirm whether `mp.MediaPipeDriveLegs=0` (set by both
     live profiles) prevents the leg solve from reaching `bDoLegIK` at all (read the call
     site/early-outs above :455). If yes, the IK path is exercised by NO active configuration
     (replay forces it off; live never enters the leg solve) — **recommendation: DELETE** the
     target-IK branch and foot-plant lock, remove the two policy CVars from the replay table
     only after updating invariant 2's contract note, and drop `UseLegIK=1` from the stable
     profile table. Reference implementations remain in TestingKit3 and in git history at tag
     `replay-solve-baseline-20260610`.
  2. If any live configuration can reach it, **keep behind CVar**, document it in
     `Docs/CVAR_REFERENCE.md` as live-VR-only/off-for-replay, and close the item.
  - Evidence already in hand: measured worse on replay (checklist residual table:
    target-IK p95 legs 5.610 cm vs direct 5.362 cm, feet 7.652 vs 7.403); replay policy
    forces it off; zero Content references to `mp.MediaPipeUseLegIK` (binary grep).
- **Expected delta**: if deleted, ≈ −280 lines + ~8 dead tunable CVars.
- **Risk**: medium; deletion touches the live leg-solve file (replay-verified by GATE-PIE;
  live legs are driven only during replay policy anyway, with `DriveLegs=0` in live VR).
- **Invariant protection**: replay never uses the path (policy forces 0 — invariant 2);
  GATE-PIE re-proves knee/foot numbers; avatar generality untouched (no per-avatar code here).
- **Gate**: all four.
- **Rollback**: revert commit; baseline tag preserves the implementation.

### Phase 13 — Documentation accuracy pass + final index
- **Class**: MECHANICAL (docs only).
- **Scope**: claim-by-claim re-verification of the ACTIVE doc set against code, with a
  recorded checklist in the commit message. Known stale claims to start from:
  - `AGENTS.md`: primary-map list omits the replay map
    `/Game/MetaHumanRooms/L_MetaHumanRecordedQuestMediaPipeReplay_01` (it is now a first-class
    workflow); wrapper stall guidance superseded by Phase 1; verify every quoted command still
    matches (UE_5.8 paths, bounded flags).
  - `Docs/MEDIAPIPE_VALIDATION_AND_OPERATIONS.md`: TestingKit3 naming caveat removed in
    Phase 6; verify build/automation command blocks; add the 110/110 expectation.
  - `Docs/MEDIAPIPE_PIPELINE_WALKTHROUGH.md` + `METAHUMAN_PROFILE_DRIVEN_RETARGETING.md`:
    check for TestingKit3/TestingKit4 path references and 2026-06-10-superseded behavior
    descriptions (replay policy guard, unified avatar-locked replay for all avatars,
    region-quality diagnostics, hair-sim replay policy, FK root grounding smoothing).
  - `Docs/README.md`: final authoritative index — active docs, `Docs/Archive/INDEX.md`,
    `Docs/CVAR_REFERENCE.md`, this plan, evidence-data keep-manifest location, gate
    definitions pointer.
  - Update the line that file/line references in THIS plan drifted during phases 8–12
    (re-grep anchors; the plan is itself a doc to keep accurate).
- **Expected delta**: docs only; no code.
- **Risk**: minimal.
- **Invariant protection**: n/a (no code/data).
- **Gate**: GATE-PY (cheap) + reviewer pass of the claims checklist.
- **Rollback**: `git revert`.

---

## 5. Target end-state directory tree (project-managed paths)

```
TestingKit5/
├── AGENTS.md                  # agent entrypoint (accurate build + replay workflow)
├── TestingKit5.uproject / .sln / .slnx / .gitignore / .gitattributes / .vsconfig
├── Start_Codex_Unreal_Agent.bat   # UE_5.8
├── Config/                    # unchanged (5 ini)
├── Content/                   # unchanged; replay map TRACKED
├── Docs/
│   ├── README.md              # single authoritative index
│   ├── REFACTOR_PLAN.md       # this plan + execution log
│   ├── CVAR_REFERENCE.md      # generated (Phase 7)
│   ├── <≈12 active docs>
│   └── Archive/               # dated/superseded docs + INDEX.md
├── Plugins/  ThirdParty/  AgentBridge/   # unchanged
├── Source/
│   ├── TestingKit5/           # template variants (unchanged)
│   └── MediaPipeDriver/
│       ├── Avatar/ BodyFusion/ Core/ Embodiment/ Quest/ Tracking/   # unchanged shape
│       ├── Diagnostics/       # + DatasetRecorder, MPQShadowCapture, MannyBoneTimeseries,
│       │                      #   DatasetHud (from Phase 9)
│       ├── PoseDriven/        # SkeletalActor ≈650 lines; AnimInstance split into ~10 TUs;
│       │                      #   Inline/ dissolved; Shared.h helpers
│       ├── Runtime/           # MediaPipeCVarPolicy.cpp/.h (layered policy stack);
│       │                      #   RuntimeCVars as the single multi-file CVar home;
│       │                      #   DriverRuntime.cpp ≈3,300 lines and shrinking
│       └── Tests/             # all names TestingKit5.MediaPipe.* (110)
├── Tools/
│   ├── <≈12 active scripts incl. kellan_replay_bone_sampler.py,
│   │    GenerateCVarReference.py, CleanCodexAgentOutputs.ps1>
│   └── Archive/               # one-offs + INDEX.md
└── Saved/                     # ≈1.5–2 GB: canonical dataset + keep-manifest evidence only
```

Removed entirely: root litter files, `Scripts/`, `Tools/MetaXRCompare/`, `Tools/__pycache__/`,
`Automation_TestingKit5.sln/.slnx`; `_MCPBench`/`ReviewDeliverables`/`QuestScreenshots`
pending user decision (sizes listed in Phase 4).

## 6. Non-goals (explicitly out of scope)

- No solver tuning: no changes to solve math, smoothing constants, thresholds, or policy
  VALUES — only to where they live and who is allowed to write them.
- No new features, no new diagnostics beyond the generated CVar reference.
- No capture-protocol changes; no new VR/headset captures; no re-recording of datasets.
- No Content/asset reorganisation (maps, MetaHumans, materials stay put).
- No plugin (`CodexAgent`, `McpAutomationBridge`, `UnrealMCP`) or AgentBridge refactoring.
- No engine/UE-version changes; no Build.cs dependency additions (removals only where proven
  dead, Phase 8c).

## 7. Estimated total impact

- **Code**: net ≈ **−1,200 to −1,900 lines** in `Source/MediaPipeDriver` (dead code 600–900;
  policy-model net ≈ −300; leg-IK deletion ≈ −280 if Phase 12 resolves to delete). Largest
  file 5,476 → ≈650 lines; worst-case TU ≈14,000 → ≈3,600 lines (and the build wrapper's
  false-kill class eliminated twice over, Phases 1 + 10).
- **CVar surface**: 414 definitions → ≈400 with every multi-file CVar centrally defined, every
  writer known, one logged policy choke point, and a generated always-current reference.
- **Root directory**: 29 files → 8.
- **Disk**: ≈ **−20 GB** (`Saved/` 22 GB → 1.5–2 GB) + 156 MB `_MCPBench/Tools` + 1.79 GB
  `QuestScreenshots` pending user approval; canonical dataset preserved twice (in place +
  hash-verified backup).
- **Tests**: one prefix, 110 discoverable under a single filter, exact-count gate.

## 8. Execution log

| Phase | Date | Commit/tag | Gates | Notes |
| ----- | ---- | ---------- | ----- | ----- |
| 7 | 2026-06-11 | (commit below) | BUILD ok; AUTO 112/112; PY 20/20 + plot selftest + generator selftest | `Tools/GenerateCVarReference.py` (+`--selftest`, now part of GATE-PY) emits `Docs/CVAR_REFERENCE.md`: **415** mp.* definitions (415 incl. editor module), **143** file-local, **175 multi-writer**. Relocation judgment: NO definitions moved — every invariant-2 replay-policy CVar is already centrally defined; the cross-file-written file-locals are the `Record*`/`RegionQuality*` capture-tooling families whose owning sections move wholesale in Phase 9, so relocating now would be double churn. The multi-writer table is the Phase 11 worklist. |
| 6 | 2026-06-11 | `424fbfc` | BUILD wrapper ok ×2; AUTO `TestingKit5.MediaPipe` **112/112**; PY 20/20 + selftest | 94 quoted names renamed (93 in Tests/ + 1 registered inside `MediaPipePoseDrivenAnimInstance.cpp:387` — section 3.6's count was 2 short). Runtime `TestingKit3_*` actor tags and `TestingKit3FullArmChainOpenXRBodyTracking` provider name intentionally untouched (baked into map/profile data; out of scope). First gate run failed 111/1: the migration EXPOSED a pre-existing leak — `Diagnostics.TrackingFusionDatasetCVars` executes the replay-output prepare command (applies the full replay CVar policy) but only snapshot/restored 27 of the touched CVars, leaking `mp.BodyFusion.Calibration*` into the newly-in-filter `Runtime.CVars` default assertions. Fixed by snapshotting all 20 policy-touched CVars in that test. Follow-up noted: ~32 tests use a bare `MediaPipe.*` prefix outside this gate's filter. |
| 5 | 2026-06-11 | `401faa7` | BUILD wrapper ok; AUTO 54/54; PY 20/20 + selftest | 25 scripts → `Tools/Archive/` + INDEX.md (20 from Tools, 5 from removed `Scripts/`); each verified unreferenced by active Docs/Source/Config/AGENTS.md. 7 scripts cited by `METAHUMAN_PROFILE_DRIVEN_RETARGETING.md`/`MEDIAPIPE_PIPELINE_WALKTHROUGH.md` stayed active. Deleted empty `Tools/MetaXRCompare/` and `Tools/__pycache__/`. Active `Tools/` = 19 scripts. |
| 4 | 2026-06-11 | `ded32a4` | AUTO 54/54; PY 20/20 + selftest; PIE PASS-with-note (see below) | Deleted 1,100 items / 11.6 GB via new `Tools/CleanCodexAgentOutputs.ps1` (dry-run reviewed; protected-item check clean; canonical dataset 16 items intact). `Saved/` 22 GB → ~10.3 GB (Diagnostics 17.6 → 6.9 GB; QuestScreenshots 1.79 GB + Videos 174 MB + `_MCPBench` 158 MB + ReviewDeliverables 39 MB + Crashes 1.17 GB report-only pending user decision). Promoted `kellan_replay_bone_sampler.py` + `compare_replay_measurements.py` to `Tools/`. `.gitignore`: root-anchored `/*.dll`, `/*.png`, `/Test*.txt`; explicit ThirdParty dll ignore; `*.tmp.*`. PIE note: editor ticked at exactly 8 Hz (528 samples/66 s; background-CPU-throttle signature), so the single-frame deep-squat knee_l minimum aliased (124.6/122.2 vs 121.1 across runs, same value both avatars); every non-extremum metric matched baseline to 3 d.p. (knee_r_min 112.925 exact, maxes exact, ball medians ±0.003, 0 penetration, drift 0.0) and no runtime file changed in Phases 1–4 — replay verified loading end-to-end post-cleanup. Future PIE gates: check sample_count ≈30 Hz; bring editor to foreground before sampling. |
| 3 | 2026-06-11 | `65ad9e2` | PY 20/20 + selftest; link-check grep clean (no stale `../` refs; no Source/Tools/Config refs to moved files) | Root .md count 13 → 1 (AGENTS.md). `MPQ_Stage2A_Conflict_Stress_Test_Plan.md` → `Docs/`; 10 root MDs + 9 dated Docs files → `Docs/Archive/` (now 20 files + `INDEX.md`). `MPQ_shadow_fusion_review_deliverables_20260605.md` (untracked, gitignored) → `ReviewDeliverables/`. `Docs/README.md` rewritten as single authoritative index. `MEDIAPIPE_PIPELINE_WALKTHROUGH.md` baseline refs repointed to `Archive/`. |
| 2 | 2026-06-11 | `e9c3a26` (+`ca5f2cd` tmp-file fixup) | BUILD wrapper ok; AUTO 54/54; PY 20/20 + selftest | Deleted untracked root litter: `Test.txt`–`Test4.txt` (lessons verified present in AGENTS.md bridge guidance), `ce0a1c04-*.png`, root `ump_shared.dll` (SHA-256 identical to `ThirdParty/mediapipe_wrapper/ump_shared.dll`: EBB1A650…45A3B). Fixed `Start_Codex_Unreal_Agent.bat` UE_5.7→UE_5.8. `git rm` `Automation_TestingKit5.sln/.slnx` (regenerable, zero references). Root: 29 → 21 files. |
| 1 | 2026-06-11 | `9ad2c36` | BUILD via wrapper: cold rebuild incl. 2.45 GB shared PCH, 90 actions, 150.7 s, survived silent stretches; AUTO 54/54; PY 20/20 + selftest | Old CPU-threshold probe falsely killed a live build ("cl/link CPU stuck at 12s" while cl.exe showed +42k page faults/6 s); replaced with activity signature = ANY change in cpu/io/page-fault/process-count of project build processes. Also fixed `Start-Process` ExitCode-null quirk (cache handle + WaitForExit + UBT Result-line fallback) which misreported a succeeded build as failed. AGENTS.md stall guidance updated. |
| 0 | 2026-06-11 | `0e233f0` + tag `replay-solve-baseline-20260610`; plan `2826092` | BUILD ok (43.6 s, link-only); AUTO 54/54 exit 0; PY 20/20 + selftest OK; PIE PASS both avatars | Dataset backed up to `D:\Backups\TestingKit5_CanonicalReplayDataset` (15 files SHA-256 verified + plots). PIE vs 06-10 final: Kellan knee L 121.09–174.96 / R 112.93–174.96 identical, ball median 0.81 vs 0.80, 0 penetration, drift 0.0; Manny identical knees, ball 0.76 vs 0.75. Comparator: `Tools/compare_replay_measurements.py`. Note: editor PIE driven via TestingKit5 AgentBridge on port 8766 (`CODEX_AGENT_PORT=8766`) because port 8765 hosts a TestingKit6-bound bridge. |
