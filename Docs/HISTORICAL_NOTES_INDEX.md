# Historical Notes Index

Status: consolidated 2026-06-05. This file explains what the old long-form docs used to contain without reintroducing their repeated troubleshooting chronology.

## MediaPipe architecture and refactor

| File | Retained fact |
| --- | --- |
| `MPQ_fusion_architecture_refactor_plan.md` | Original target architecture: sources -> fusion -> fused semantic pose -> skeleton writer. Current state is partial and documented in `MEDIAPIPE_EMBODIMENT_CURRENT_STATE.md`. |
| `MPQ_fusion_refactor_cutback_checklist.md` | Cutback removed/merged speculative layers, preserved BodyFusion invariants, and left data-asset skeleton adapter expansion deferred. |
| `MPQ_fusion_do_now_checklist.md` | Current actions are now the short checklist in that file plus the debt list in the current-state doc. |
| `steps.MD` | Historical avatar embodiment refactor tracker; now compacted to retained decisions and current pointers. |
| `body_fusion_steps.MD` | Historical BodyFusion implementation tracker; now compacted to retained decisions and current pointers. |
| `Docs/MEDIAPIPE_REFACTOR_STATE_2026-05-17.md` | Earlier split of solver state and inline responsibilities; superseded by current source layout. |
| `Docs/MEDIAPIPE_PIPELINE_WALKTHROUGH.md` | Old full pipeline walkthrough; now compacted to current source map and operations docs. |

## Quest, Wallace, arm, and VR history

| File | Retained fact |
| --- | --- |
| `Docs/WALLACE_QUEST_VR_CURRENT_DEFAULTS_HANDOFF.md` | Historical chronology of Wallace/Quest defaults, performance, arm/finger experiments, and headset checks. Current facts are in `MEDIAPIPE_QUEST_WALLACE_CURRENT_STATE.md`. |
| `Docs/WALLACE_QUEST_VR_EMBODIMENT_GUARDRAILS.md` | Guardrail still valid: do not claim VR behavior without headset-visible proof. |
| `Docs/WALLACE_ARM_PIPELINE_AUDIT_2026-05-19.md` | Historical audit of arm source ownership, twist helpers, constrained arm solve, and diagnostics. |
| `Docs/WALLACE_QUEST_VR_ARM_ROLLBACK_ANALYSIS_2026-05-17.md` | Historical rollback context; not current tuning guidance. |
| `Docs/QUEST_WRIST_SOLVE_FREEZE_2026-05-11.md` | Historical wrist solve freeze baseline; current Quest rules are source-owned. |
| `Docs/MEDIAPIPE_VR_MIRROR_BASELINE.md` | Historical mirror/setup baseline; current mirror behavior is an operations/proof topic. |
| `Docs/Archive/WALLACE_LEGACY_ARM_SOURCE_2026-05-22.md` | Archive pointer for removed or superseded Wallace arm-source controls. |

## Profiles, tracking, and video notes

| File | Retained fact |
| --- | --- |
| `Docs/METAHUMAN_PROFILE_DRIVEN_RETARGETING.md` | Built-in and configured MetaHuman profile system remains active. |
| `Docs/AVATAR_PROFILE_DRIVEN_EMBODIMENT.md` | Avatar profile offsets remain the body/camera placement contract. |
| `Docs/MEDIAPIPE_WEBCAM_INCLUSION.md` | Webcam/video source path remains available through MediaPipe live video commands and placed embodied tracking. |
| `Docs/MEDIAPIPE_TRACKING_ISSUE_AND_NEXT_PLAN_2026-05-25.md` | Old neck/torso issue trail; current accepted behavior and debt are in the current-state doc. |
| `Docs/EMORY_FORWARD_LEAN_NECK_FINDINGS_2026-05-26.md` | Accepted Emory forward-lean neck fix; keep regression coverage but do not duplicate the full issue trail. |
| `MediaPipe_Shoulder_Baseline.md` | Historical Manny shoulder baseline; VP2-specific work now lives in the compact VP2 file. |
| `VP2_Manny_Tracking_Fix_Solutions.md` | Current VP2 Manny shoulder/head tracking review checklist and analyzer commands. |

## Codex bridge and MCP traces

| File | Retained fact |
| --- | --- |
| `Codex_Unreal_Agent_Plan.md` | Original bridge design; superseded by actual `AgentBridge` routes. |
| `Codex_Unreal_Agent_Implementation_Audit.md` | Historical implementation proof from TestingKit3; no longer current project evidence. |
| `Codex_Agent_Efficiency_Audit.md` | Lessons from inefficient MCP traces are now in `CODEX_BRIDGE_CURRENT_STATE.md`. |
| `Test.txt`, `Test2.txt`, `Test3.txt`, `Test4.txt` | Raw trace sessions for Blueprint/MCP bridge hardening; useful lessons were distilled into bridge guidance. |

## Stale naming warning

Old docs and some live source strings still say `TestingKit3`. In docs, use TestingKit5/UE 5.8 for project paths and commands. In code/tests, treat the old strings as naming debt until a dedicated rename pass updates automation filters, tags, and log parsers together.
