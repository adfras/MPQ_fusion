# TestingKit5 Documentation Index

Status: single authoritative index. Created 2026-06-07, restructured 2026-06-11 (all project
documentation now lives under `Docs/`; root keeps `AGENTS.md` plus `README.md`, the GitHub
landing page with the demo quickstart, added 2026-07-12). Date-stamped files are
historical unless this index marks them active.

**Running the demo:** see the root `README.md` — open the editor, everything self-arms,
`mp.MirrorAvatar <name>` between sessions. The long-form version with verification steps
and gotchas is `SETUP_NEW_MACHINE.md` §7-7d.

## Active Operational Docs

- `PROJECT_SHAPE.html`: the interactive architecture flow chart (open in any browser) —
  corrector rack, quest spine, final assembly, plus the 2026-07 quality-arc band with the
  change list and pending worn verdicts. A static Mermaid version renders in the root
  `README.md` on GitHub.
- `SETUP_NEW_MACHINE.md`: how to move this project to a new computer and get it
  operational — what git carries vs the ~9 GB carry-by-hand payload (MetaHumans,
  mediapipe_wrapper DLLs, canonical dataset), install list, first-build verification,
  live-mirror bring-up, and the field-notes gotcha list for a fresh agent.
- `MEDIAPIPE_VALIDATION_AND_OPERATIONS.md`: current build, VR proof, Camo/iPhone setup,
  MPQ Stage 0/1/2A validation, and review-packaging rules.
- `MPQ_Stage2A_Conflict_Stress_Test_Plan.md`: current clean-fusion MPQ shoulder/shrug evidence
  script and pass criteria.
- `MEDIAPIPE_PIPELINE_WALKTHROUGH.md`: broader MediaPipe/Quest architecture and historical context.
- `METAHUMAN_PROFILE_DRIVEN_RETARGETING.md`: MetaHuman profile and retargeting context.
- `METAHUMAN_GROOM_RULES.md`: ACTIVE rules for hair grooms on driven avatars — read
  BEFORE touching avatar visibility/spawn/LOD/tick code. Never sweep a groom-bearing
  component tree per frame; the hair-blob arc that produced these rules is closed in
  `RESOLUTION_2026-07-18_HAIR_GROOM_BLOB.md`.
- `AVATAR_PROFILE_DRIVEN_EMBODIMENT.md`: profile-driven embodied avatar setup.
- `AVATAR_LOCKED_SYNC_CALIBRATION_CAPTURE_PROTOCOL.md`: current one-run avatar-locked sync
  calibration capture protocol with seven green 30-second movement blocks and lower-body
  policy/source interpretation.
- `AVATAR_REPLAY_OUTPUT_FIX_CHECKLIST.md`: replay-output avatar following fixes on
  `/Game/MetaHumanRooms/L_MetaHumanRecordedQuestMediaPipeReplay_01`, including the 2026-06-10
  replay quality pass (policy guard, FK root grounding, region-quality diagnostics, hair fix)
  and the 2026-06-12 lower-body scaffold passes (HMD metric squat depth, grounded flexion
  correction, bend redistribution, flat feet, wrist/finger replay from the schema-v2 cache).
- `LOWER_BODY_SCAFFOLD_EXECUTION_REPORT_2026-06-12.md`: session report for the 2026-06-12
  lower-body scaffold + wrist/finger replay work (root causes, changes, verification).
- `LIVE_VR_TRIAL_EXECUTION_REPORT_2026-06-12.md`: session report for the 2026-06-12 live
  worn-headset trial (donning gate, body-tracking hip yaw/sway, in-VR tracking panel,
  finger-overlap investigation and verdict, operational gotchas).
- `MEDIAPIPE_VR_MIRROR_BASELINE.md`: VR mirror baseline setup.
- `MEDIAPIPE_WEBCAM_INCLUSION.md`: webcam source inclusion notes.
- `WALLACE_QUEST_VR_CURRENT_DEFAULTS_HANDOFF.md` / `WALLACE_QUEST_VR_EMBODIMENT_GUARDRAILS.md`:
  Wallace VR defaults and guardrails reference.

## Structure / Cleanup Work

- `DYADIC_STUDY_PLAN.md`: the ACTIVE 2026-07 plan for the two-participant research
  platform — asymmetric avatar choice (each participant picks their own AND their
  partner's appearance, per-viewer), lobby/menu flow, source-row streaming through
  the replay drive path, experiment harness with yoked-control conditions. Until
  ethics approval, seat B is a recording played through the real wire
  (`Tools/dyad_partner_player.py`); the human gate is a solo pilot, and the live
  dyad is a parked checklist phase that activates on approval.
- `TRACKING_QUALITY_PLAN.md`: the ACTIVE 2026-07 plan for stability/realism upgrades
  (timestamped residuals, wrist anatomical clamp, foreshortening Z-distrust, foot
  contact + lock, learned-prior bake-off) — research-derived, phased, desk-gated,
  one worn acceptance at the end.
- `tracking_quality_baseline/`: the plan's Phase-0 before-numbers (2026-07-11, branch
  `feature/tracking-quality`): the mined 2026-07-10 acceptance-session fingerprint plus
  captures taken with the mp.FootSkateTrace / mp.WristLimitTrace / mp.WebcamAgeTrace
  tracers armed. Regenerate summaries with `Tools/mine_tracking_quality_baseline.py`.
- `REFACTOR_PLAN.md`: the phased structural refactor and cleanup plan (gates, evidence,
  execution log). Read this before moving, deleting, or restructuring anything.
- `refactor_baseline/`: the corrector-refactor reference (2026-07-10, branch
  `refactor/correctors`): worn-session tracer fingerprint, the Phase-0 157-test log, and
  the `goldens/` characterization dumps that byte-lock every extracted corrector. See
  `REFACTOR_PLAN.md` section 9 for the execution log and the Phase-7 deferral record.
- `RESOLUTION_2026-07-10_ARM_CHURN_AND_STICKY_WRIST.md` (with
  `RESOLUTION_2026-07-09_SHRUG_AND_WRIST_SNAP.md`): the six-round arm-quality arc the
  correctors implement - read these to understand WHY each corrector has its bound, quiet
  gate, motion fade, and tracer.
- `CVAR_REFERENCE.md`: GENERATED inventory of every `mp.*` console variable (default,
  readers, writers, multi-writer conflict flags). Regenerate with
  `python Tools/GenerateCVarReference.py` after adding/removing CVars or writers.

## Current MPQ Fusion State

- Primary MPQ test map: `/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01`.
- Active MediaPipe-only reference map: `/Game/MCPBench/Maps/L_MCP_MediaPipeMannyRoom`.
- Recorded Quest+MediaPipe replay map: `/Game/MetaHumanRooms/L_MetaHumanRecordedQuestMediaPipeReplay_01`
  (canonical dataset: `Saved/CodexAgent/Diagnostics/tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656_replay_source*`;
  hash-verified backup at `D:\Backups\TestingKit5_CanonicalReplayDataset`).
- BodyFusion owns avatar authority and final pose output; during dataset replay the lower body
  is diagnostics-only for ALL avatars and the recorded landmarks drive the direct segment solve,
  corrected by the Quest/HMD lower-body scaffold (metric squat depth, grounded flexion,
  femur/shin redistribution, flat-foot pitch; `mp.MediaPipeLegScaffold*` CVars, replay-enabled).
- Dataset replay also drives wrist rotation and fingers from the recorded Quest hand skeletons
  (schema-v2 replay cache `..._replay_source_v2.jsonl`; the v1 wrist-only cache stays beside it).
- Quest/HMD provide source observations for HMD/head plus wrist, hand, and finger endpoints.
- MediaPipe provides body-pose observations, including shoulders and shrugs that Quest cannot
  directly track.
- Stage 1/Stage 2 direct MetaHuman bone-offset layers and raw AutoQuest clavicle/spine/pelvis
  writes are not the active fusion path; shoulder output is routed through the fused pose
  writer and avatar profile.
- MetaHuman helper leaf coverage is part of the fused pose writer path. Do not force-set the
  upper-arm shoulder socket to the MediaPipe shoulder point.
- MediaPipe arm fallback remains out of scope.

## Archive

- `Archive/INDEX.md`: one-line index of all historical/superseded docs (TestingKit3-era
  snapshots, completed checklists, dated investigations).

## Build Rule

Do not use Live Coding for Codex-driven builds. Close Unreal and `LiveCodingConsole.exe`, then
build with `Tools\BuildTestingKit5EditorFast.ps1` (or the raw bounded command in `AGENTS.md`).
