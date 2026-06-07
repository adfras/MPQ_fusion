# TestingKit5 Documentation Index

Status: active index created 2026-06-07.

Start here for current Codex work in TestingKit5. Date-stamped files are historical unless this index or `MEDIAPIPE_VALIDATION_AND_OPERATIONS.md` marks them as current.

## Active Operational Docs

- `MEDIAPIPE_VALIDATION_AND_OPERATIONS.md`: current build, VR proof, Camo/iPhone setup, MPQ Stage 0/1/2A validation, and review-packaging rules.
- `../MPQ_Stage2A_Conflict_Stress_Test_Plan.md`: current Stage 2A shoulder/clavicle conflict stress-test script and pass criteria.
- `MEDIAPIPE_PIPELINE_WALKTHROUGH.md`: broader MediaPipe/Quest architecture and historical context.
- `METAHUMAN_PROFILE_DRIVEN_RETARGETING.md`: MetaHuman profile and retargeting context.
- `AVATAR_PROFILE_DRIVEN_EMBODIMENT.md`: profile-driven embodied avatar setup.

## Current MPQ Fusion State

- Primary MPQ test map: `/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01`.
- Active MediaPipe-only reference map: `/Game/MCPBench/Maps/L_MCP_MediaPipeMannyRoom`.
- Stage 0 shadow capture, Stage 1 vertical torso/pelvis hint, and Stage 2A shoulder/clavicle hint are the active fusion path.
- Quest/HMD remain authoritative for HMD camera, wrists, hands, fingers, and arm endpoints.
- MediaPipe arm fallback remains out of scope.

## Build Rule

Do not use Live Coding for Codex-driven builds. Close Unreal and `LiveCodingConsole.exe`, then run the normal editor build command from `AGENTS.md`.
