# VP2 Manny Tracking Fix Review

Status: compacted 2026-06-05 for `D:\Epic\Unreal_Projects\TestingKit5`.

## Scope

Review Manny shoulder/head tracking against `Saved/Videos/VP2.mp4`. This is an investigation checklist, not an accepted runtime fix until analyzer metrics and visual proof pass.

## Source Anchors

- `Source/MediaPipeDriver/PoseDriven/Inline/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl`
- `Source/MediaPipeDriver/PoseDriven/Inline/MediaPipePoseDrivenAnimInstance_BodyPoseSolve.inl`
- `Source/MediaPipeDriver/PoseDriven/MediaPipePoseDrivenSolverState.h`
- `Scripts/analyze_vp2_manny_shoulder_head.py`
- `Scripts/analyze_vp2_manny_motion_signals.py`
- `Scripts/create_manny_landmark_comparison.py`
- `Scripts/create_manny_bone_overlay_comparison.py`
- `Scripts/create_mediapipe_pose_overlay.py`

## Current Hypothesis

The old metrics showed VP2 has clear bilateral shoulder/head motion while Manny output was too flat and partly side-cancelling. The likely failure modes were:

- per-side shoulder/head clearance references drifting independently;
- source shoulder width used where rig shoulder width should scale output;
- left/right mirror conventions cancelling bilateral shrug;
- clavicle translation/rotation being overwritten later in the write chain;
- head pitch relying on weak face proxies instead of stable internal face ratios.

## Review Order

1. Run current unmodified TestingKit5 against VP2 and capture metrics.
2. Confirm whether source bilateral clearance, Manny clavicle output, head pitch, and final bone transforms are all logged.
3. If output is still flat, verify whether bilateral clearance is computed once per body frame and shared by both sides.
4. Scale visible shoulder lift from Manny rig width, not noisy MediaPipe source shoulder width.
5. Verify mirror/landmark side conventions before changing weights.
6. If a lift is computed but not visible, inspect final pose-write order and move only the final chain offset needed to survive overwrites.

## Commands

```text
mp.PlacedEmbodiedVideoFile D:/Epic/Unreal_Projects/TestingKit5/Saved/Videos/VP2.mp4
mp.StartPlacedEmbodiedTracking
```

Analyzer examples:

```powershell
python Scripts\analyze_vp2_manny_shoulder_head.py --video Saved\Videos\VP2.mp4 --model Content\MediaPipe\pose_landmarker.task
python Scripts\analyze_vp2_manny_motion_signals.py
```

## Acceptance

- Manny bilateral shrug range should be visibly non-flat and materially larger than the old near-zero range.
- VP2/Manny bilateral shrug correlation should improve before increasing global shrug weight.
- Head pitch must match visual VP2 direction and not rely on Euler wrapping as acceptance.
- Visual proof and analyzer output are both required.
