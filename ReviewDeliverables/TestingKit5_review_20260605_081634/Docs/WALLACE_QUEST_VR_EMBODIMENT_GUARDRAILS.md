# Wallace Quest VR Embodiment Guardrails

Status: consolidated 2026-06-05.

## Non-Negotiable Rules

- Do not claim a VR embodiment fix without headset-visible proof.
- Do not use desktop PIE, build success, automation success, or logs alone as proof of headset behavior.
- Do not retune Wallace arm/wrist/finger defaults unless explicitly asked.
- Do not hide the full avatar globally to fix first-person view.
- Do not replace profile-derived body placement with ad hoc camera offsets.
- Do not make BodyFusion write skeleton-specific bones.

## Required Proof For VR Claims

- Quest connected, HMD enabled, worn state `WORN`, valid HMD tracking.
- Tracked Quest hands when hand/wrist/finger behavior is in scope.
- Relevant diagnostic rows, such as `mp.BodyFusion.Debug`, `mp.DumpQuestHands`, `mp.QuestWristSnapshot`, `mp.QuestWristRollCompact`, or `mp.MetaHumanArmSanity`.
- Focused screenshot or user headset confirmation showing the claimed result.

Current operational details: `Docs/MEDIAPIPE_VALIDATION_AND_OPERATIONS.md`.
