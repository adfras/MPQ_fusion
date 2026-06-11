# TestingKit5 Documentation Index

Status: active index created 2026-06-07.

Start here for current Codex work in TestingKit5. Date-stamped files are historical unless this index or `MEDIAPIPE_VALIDATION_AND_OPERATIONS.md` marks them as current.

## Active Operational Docs

- `MEDIAPIPE_VALIDATION_AND_OPERATIONS.md`: current build, VR proof, Camo/iPhone setup, MPQ Stage 0/1/2A validation, and review-packaging rules.
- `../MPQ_Stage2A_Conflict_Stress_Test_Plan.md`: current clean-fusion MPQ shoulder/shrug evidence script and pass criteria.
- `MEDIAPIPE_PIPELINE_WALKTHROUGH.md`: broader MediaPipe/Quest architecture and historical context.
- `METAHUMAN_PROFILE_DRIVEN_RETARGETING.md`: MetaHuman profile and retargeting context.
- `AVATAR_PROFILE_DRIVEN_EMBODIMENT.md`: profile-driven embodied avatar setup.
- `AVATAR_LOCKED_SYNC_CALIBRATION_CAPTURE_PROTOCOL.md`: current one-run avatar-locked sync calibration capture protocol with seven green 30-second movement blocks and lower-body policy/source interpretation.
- `AVATAR_REPLAY_OUTPUT_FIX_CHECKLIST.md`: active checklist for deterministic replay-output avatar following fixes on `/Game/MetaHumanRooms/L_MetaHumanRecordedQuestMediaPipeReplay_01`.

## Current MPQ Fusion State

- Primary MPQ test map: `/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01`.
- Active MediaPipe-only reference map: `/Game/MCPBench/Maps/L_MCP_MediaPipeMannyRoom`.
- BodyFusion owns avatar authority and final pose output.
- Quest/HMD provide source observations for HMD/head plus wrist, hand, and finger endpoints.
- MediaPipe provides body-pose observations, including shoulders and shrugs that Quest cannot directly track.
- Stage 1/Stage 2 direct MetaHuman bone-offset layers and raw AutoQuest clavicle/spine/pelvis writes are not the active fusion path; shoulder output is routed through the fused pose writer and avatar profile.
- MetaHuman helper leaf coverage is part of the fused pose writer path: clavicle out/scap/pec, upper-arm twist/corrective/bicep/tricep, lower-arm corrective, and wrist inner/outer helper groups are accounted for from the current main-chain pose. Do not force-set the upper-arm shoulder socket to the MediaPipe shoulder point.
- MediaPipe arm fallback remains out of scope.

## Build Rule

Do not use Live Coding for Codex-driven builds. Close Unreal and `LiveCodingConsole.exe`, then run the normal editor build command from `AGENTS.md`.
