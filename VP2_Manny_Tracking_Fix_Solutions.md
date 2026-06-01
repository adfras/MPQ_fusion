# VP2 Manny Tracking Fix Solutions

## Scope

This markdown is for the Unreal project:

```text
D:\Epic\Unreal_Projects\TestingKit5
```

Primary files:

```text
Source/MediaPipeDriver/PoseDriven/Inline/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl
Source/MediaPipeDriver/PoseDriven/Inline/MediaPipePoseDrivenAnimInstance_BodyPoseSolve.inl
Source/MediaPipeDriver/PoseDriven/MediaPipePoseDrivenSolverState.h
Scripts/analyze_vp2_manny_motion_signals.py
```

Target video:

```text
Saved/Videos/VP2.mp4
```

The goal is to make Manny visibly and metrically follow VP2 for:

1. Bilateral shoulder shrugs.
2. Left/right shoulder asymmetry without destroying bilateral shrug.
3. Head/chin pitch.
4. Head lateral motion.

---

## Current Evidence From The Handoff

The most important latest metrics are from:

```text
data/vp2_manny_motion_signals_clearance_fix_live.json
```

The completed trial shows:

| Signal | VP2 Range | Manny Range | Correlation | Meaning |
|---|---:|---:|---:|---|
| Bilateral shoulder-to-head shrug clearance | ~0.161 | ~0.015 | ~0.193 | VP2 has clear shrug peaks; Manny barely moves. |
| Left shoulder clearance | present | present | ~0.381 | Left side partly follows. |
| Right shoulder clearance | present | present | ~-0.187 | Right side is inverted or overwritten. |
| `head_y_norm_delta -> head_local_pitch_delta` | n/a | n/a | ~0.174 | Weak head pitch driver. |
| `mouth_y_norm_delta -> head_local_pitch_delta` | n/a | n/a | ~0.190 | Still weak if used alone. |
| `mouth_eye_y -> head_local_pitch_delta` | n/a | n/a | ~0.411 | Best head pitch candidate in feature probe. |
| `nose_eye_y -> head_local_pitch_delta` | n/a | n/a | ~0.295 | Secondary head pitch candidate. |

The key problem is not simply “the shrug weight is too low.” The solver is still allowing per-side/noisy signals to cancel each other, while the visible VP2 motion is primarily **bilateral shoulder-to-head clearance shrinkage**.

---

# Executive Fix

Use this as the main design rule:

> A shrug is both shoulders moving closer to the head. Compute that once as a shared bilateral clearance signal, then apply it to both clavicles. Side-specific shoulder clearance should only be a small asymmetry correction.

The implementation should do four things:

1. Replace independent left/right clearance references with one shared bilateral reference.
2. Use symmetric paired head landmarks: ears pair, else eyes pair, else nose for both sides.
3. Apply the same main shrug lift to both clavicles and upperarm chains.
4. Use face-internal mouth/eye and nose/eye ratios for head pitch instead of mostly shoulder/nose vertical movement.

---

# Solution 1: Make Bilateral Clearance The Primary Shrug Driver

## Why

The previous hip-to-shoulder and left-vs-right metrics miss a real bilateral shrug. A bilateral shrug is not primarily left shoulder higher than right shoulder. It is:

```text
left shoulder closer to left-side head landmark
right shoulder closer to right-side head landmark
```

So the source signal should be:

```text
left_clearance  = left_shoulder_y  - left_head_side_y
right_clearance = right_shoulder_y - right_head_side_y
bilateral_clearance = average(left_clearance, right_clearance)

shrug = reference_bilateral_clearance - current_bilateral_clearance
```

Image-space `Y` increases downward, so when the shoulder moves upward toward the head, clearance gets smaller and `reference - current` becomes positive.

## Current Mismatch

`FMediaPipeBodySolverState` already contains:

```cpp
bool bHasBilateralShoulderHeadClearanceReference = false;
float BilateralShoulderHeadClearanceReferenceCm = 0.0f;
double LastBilateralShoulderHeadClearanceReferencePoseTimeSeconds = -1.0;
```

But the active clavicle logic is still computing clearance inside each side branch and using `FMediaPipeArmSolverState::ShoulderHeadClearanceReferenceCm`. That allows the left and right sides to drift independently, which is exactly what can flatten the bilateral Manny curve and invert the right side.

## Replacement Rule

Inside the shoulder/clavicle solve, compute the bilateral clearance once per frame using both shoulders and paired head landmarks, then feed that same value into both left and right clavicle branches.

A clean implementation is:

```cpp
struct FShoulderClearance2DResult
{
    bool bValid = false;
    float LeftClearanceCm = 0.0f;
    float RightClearanceCm = 0.0f;
    float BilateralClearanceCm = 0.0f;
    float BilateralShrugCm = 0.0f;
    float LeftAsymCm = 0.0f;
    float RightAsymCm = 0.0f;
};
```

Then compute it before solving each side, or cache it at the beginning of the clavicle block.

---

# Solution 2: Use Symmetric Head Landmark Fallback

## Why

The current 2D clearance fallback is side-local:

```cpp
left side:  left ear -> left eye -> nose
right side: right ear -> right eye -> nose
```

That is dangerous because one side can use an ear while the other side uses an eye or the nose. Then the two sides are no longer measuring the same anatomical height. This can produce false right-side inversion.

## Correct Fallback

Use paired landmarks only:

```text
1. If both ears are valid, use left ear and right ear.
2. Else if both eyes are valid, use left eye and right eye.
3. Else if the nose is valid, use the nose for both sides.
4. Else skip the clearance driver for this frame.
```

## Code Pattern

Use this helper near the existing `TryGetNormalizedXY` block in `MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl`:

```cpp
auto TryGetPairedHeadSidePoints2D = [&]() -> TOptional<TPair<FVector2D, FVector2D>>
{
    FVector2D LEar2D, REar2D;
    FVector2D LEye2D, REye2D;
    FVector2D Nose2D;

    const bool bHasEarPair =
        TryGetNormalizedXY((int32)EMediaPipePoseLandmark::LeftEar, LEar2D) &&
        TryGetNormalizedXY((int32)EMediaPipePoseLandmark::RightEar, REar2D);

    if (bHasEarPair)
    {
        return TPair<FVector2D, FVector2D>(LEar2D, REar2D);
    }

    const bool bHasEyePair =
        TryGetNormalizedXY((int32)EMediaPipePoseLandmark::LeftEye, LEye2D) &&
        TryGetNormalizedXY((int32)EMediaPipePoseLandmark::RightEye, REye2D);

    if (bHasEyePair)
    {
        return TPair<FVector2D, FVector2D>(LEye2D, REye2D);
    }

    const bool bHasNose =
        TryGetNormalizedXY((int32)EMediaPipePoseLandmark::Nose, Nose2D);

    if (bHasNose)
    {
        return TPair<FVector2D, FVector2D>(Nose2D, Nose2D);
    }

    return TOptional<TPair<FVector2D, FVector2D>>();
};
```

If your module does not currently include the header that defines `TOptional`, either include the relevant Unreal header or use a simple output-parameter helper:

```cpp
auto TryGetPairedHeadSidePoints2D = [&](FVector2D& OutLeft, FVector2D& OutRight) -> bool
{
    FVector2D LEar2D, REar2D;
    FVector2D LEye2D, REye2D;
    FVector2D Nose2D;

    if (TryGetNormalizedXY((int32)EMediaPipePoseLandmark::LeftEar, LEar2D) &&
        TryGetNormalizedXY((int32)EMediaPipePoseLandmark::RightEar, REar2D))
    {
        OutLeft = LEar2D;
        OutRight = REar2D;
        return true;
    }

    if (TryGetNormalizedXY((int32)EMediaPipePoseLandmark::LeftEye, LEye2D) &&
        TryGetNormalizedXY((int32)EMediaPipePoseLandmark::RightEye, REye2D))
    {
        OutLeft = LEye2D;
        OutRight = REye2D;
        return true;
    }

    if (TryGetNormalizedXY((int32)EMediaPipePoseLandmark::Nose, Nose2D))
    {
        OutLeft = Nose2D;
        OutRight = Nose2D;
        return true;
    }

    return false;
};
```

---

# Solution 3: Compute One Shared Bilateral Shrug Value

## Code

Use this after you have both shoulder points and both head-side points:

```cpp
FShoulderClearance2DResult Clearance2D;

FVector2D LShoulder2D = FVector2D::ZeroVector;
FVector2D RShoulder2D = FVector2D::ZeroVector;
FVector2D LHeadSide2D = FVector2D::ZeroVector;
FVector2D RHeadSide2D = FVector2D::ZeroVector;

const bool bHasShoulders2D =
    TryGetNormalizedXY((int32)EMediaPipePoseLandmark::LeftShoulder, LShoulder2D) &&
    TryGetNormalizedXY((int32)EMediaPipePoseLandmark::RightShoulder, RShoulder2D);

const bool bHasHeadSide2D =
    TryGetPairedHeadSidePoints2D(LHeadSide2D, RHeadSide2D);

if (bHasShoulders2D && bHasHeadSide2D)
{
    const float ShoulderSpan2D = FMath::Max(
        FVector2D::Distance(LShoulder2D, RShoulder2D),
        0.05f);

    const float RigShoulderWidthCm = ResolveRigShoulderWidthCm();

    const float LeftClearanceCm =
        ((LShoulder2D.Y - LHeadSide2D.Y) / ShoulderSpan2D) * RigShoulderWidthCm;

    const float RightClearanceCm =
        ((RShoulder2D.Y - RHeadSide2D.Y) / ShoulderSpan2D) * RigShoulderWidthCm;

    const float BilateralClearanceCm =
        0.5f * (LeftClearanceCm + RightClearanceCm);

    if (BilateralClearanceCm > KINDA_SMALL_NUMBER)
    {
        if (!BodyState.bHasBilateralShoulderHeadClearanceReference ||
            BodyState.BilateralShoulderHeadClearanceReferenceCm <= KINDA_SMALL_NUMBER)
        {
            BodyState.bHasBilateralShoulderHeadClearanceReference = true;
            BodyState.BilateralShoulderHeadClearanceReferenceCm = BilateralClearanceCm;
        }

        // Learn larger unshrugged clearance quickly. Let smaller clearance persist long
        // enough to become a visible shrug instead of being absorbed immediately.
        const float ClearanceReferenceHalfLifeSeconds =
            BilateralClearanceCm > BodyState.BilateralShoulderHeadClearanceReferenceCm
                ? 0.20f
                : 8.00f;

        const float ClearanceReferenceAlpha =
            HalfLifeToAlpha(ClearanceReferenceHalfLifeSeconds, DeltaSeconds);

        BodyState.BilateralShoulderHeadClearanceReferenceCm = FMath::Lerp(
            BodyState.BilateralShoulderHeadClearanceReferenceCm,
            BilateralClearanceCm,
            ClearanceReferenceAlpha);

        Clearance2D.bValid = true;
        Clearance2D.LeftClearanceCm = LeftClearanceCm;
        Clearance2D.RightClearanceCm = RightClearanceCm;
        Clearance2D.BilateralClearanceCm = BilateralClearanceCm;
        Clearance2D.BilateralShrugCm = FMath::Max(
            0.0f,
            BodyState.BilateralShoulderHeadClearanceReferenceCm - BilateralClearanceCm);

        // Positive value means this side is closer to the head than the opposite side.
        Clearance2D.LeftAsymCm = RightClearanceCm - LeftClearanceCm;
        Clearance2D.RightAsymCm = LeftClearanceCm - RightClearanceCm;
    }
}
```

## Important

Do not use a different `ShoulderHeadClearanceReferenceCm` for each arm as the main shrug source. Per-arm clearance references can remain only as a fallback diagnostic, not as the primary shoulder shrug driver.

---

# Solution 4: Resolve Rig Shoulder Width Instead Of Source Shoulder Width

## Why

The source 2D clearance is normalized by VP2 shoulder width. The output movement should be scaled by Manny’s rig width, not by noisy world-space source shoulder width.

If the VP2 clearance change is `0.16 shoulder widths`, Manny should move by roughly:

```text
0.16 * Manny shoulder width
```

That gives visible, rig-proportional motion.

## Code

Use component-space upperarm roots to estimate Manny’s effective shoulder width:

```cpp
auto ResolveRigShoulderWidthCm = [&]() -> float
{
    float RigShoulderWidthCm = ShoulderWidthCm; // fallback from source/world logic

    if (UpperArmL.IsValidToEvaluate() && UpperArmR.IsValidToEvaluate())
    {
        const FVector LeftUpperComp =
            CSPose.GetComponentSpaceTransform(UpperArmL.CachedCompactPoseIndex).GetTranslation();

        const FVector RightUpperComp =
            CSPose.GetComponentSpaceTransform(UpperArmR.CachedCompactPoseIndex).GetTranslation();

        const FVector AcrossComp = RightUpperComp - LeftUpperComp;
        const FVector ShoulderRightSafe = ShoulderRightComp.GetSafeNormal();

        if (!ShoulderRightSafe.IsNearlyZero())
        {
            const float AcrossShoulderCm = FMath::Abs(
                FVector::DotProduct(AcrossComp, ShoulderRightSafe));

            if (AcrossShoulderCm > 5.0f)
            {
                RigShoulderWidthCm = AcrossShoulderCm;
            }
        }
    }

    return RigShoulderWidthCm;
};
```

If this lambda cannot see `UpperArmL`/`UpperArmR` in the current scope, compute it once in the surrounding function and pass the value into the clavicle section.

---

# Solution 5: Drive Both Clavicles From The Same Bilateral Shrug

## Why

The chart shows Manny side signals partially cancel. That means the solver is producing shoulder asymmetry/roll more than a true bilateral shrug.

The main signal must be the same on both sides:

```text
left main lift  = bilateral shrug
right main lift = bilateral shrug
```

Then add a small side correction only after the main bilateral signal is present.

## Code Inside Each Side Branch

Replace the side-local clearance contribution with this:

```cpp
if (Clearance2D.bValid)
{
    const float BilateralShrugCm = Clearance2D.BilateralShrugCm;

    const float SideAsymCm = bIsLeft
        ? Clearance2D.LeftAsymCm
        : Clearance2D.RightAsymCm;

    // Keep asymmetry small. It should add shoulder character, not cancel the shrug.
    const float SideAsymLiftCm = FMath::Clamp(SideAsymCm * 0.35f, -1.5f, 2.5f);

    const float FinalSideShrugCm = FMath::Max(0.0f, BilateralShrugCm + SideAsymLiftCm);

    ShoulderHeadClearanceCm = bIsLeft
        ? Clearance2D.LeftClearanceCm
        : Clearance2D.RightClearanceCm;

    ShoulderHeadClearanceShrugCm = FinalSideShrugCm;
    ShoulderShrugCm = FMath::Max(ShoulderShrugCm, FinalSideShrugCm);

    ShoulderShrugW +=
        RemapPositiveUnbounded(FinalSideShrugCm, ShrugStartCm, ShrugFullCm) *
        ClavicleShrugWeight *
        0.70f;

    ShoulderLiftTranslationCm += FinalSideShrugCm * 0.85f;

    bAppliedHeadClearanceShrug = true;
}
```

## Temporarily Reduce Competing Drivers

While validating the fix, reduce the older shoulder height and shoulder asymmetry contributions so they cannot hide the new bilateral signal:

```cpp
// Validation mode: keep these small until bilateral shrug is proven.
ShoulderLiftTranslationCm += ShoulderSignedLiftCm * 0.00f;
ShoulderLiftTranslationCm += ShoulderRelativeLiftCm * 0.00f;
ShoulderLiftTranslationCm += AbsoluteScreenLiftCm * 0.03f;
ShoulderLiftTranslationCm += RelativeScreenLiftCm * 0.00f;
```

After the bilateral curve is correct, restore small values if they improve secondary shoulder detail.

Recommended final approximate values after validation:

```cpp
ShoulderLiftTranslationCm += ShoulderSignedLiftCm * 0.01f;
ShoulderLiftTranslationCm += ShoulderRelativeLiftCm * 0.01f;
ShoulderLiftTranslationCm += AbsoluteScreenLiftCm * 0.03f;
ShoulderLiftTranslationCm += RelativeScreenLiftCm * 0.01f;
```

---

# Solution 6: Verify That Translation Actually Sticks

## Why

The current code calls:

```cpp
ApplyTranslationDeltaCS(CSPose, ClavBone, ClavicleLiftDeltaComp);
ApplyTranslationDeltaCS(CSPose, UpperBone, ClavicleLiftDeltaComp);
ApplyTranslationDeltaCS(CSPose, LowerBone, ClavicleLiftDeltaComp);
ApplyTranslationDeltaCS(CSPose, HandBone, ClavicleLiftDeltaComp);
```

That is the right idea, but the visible Manny result is almost flat. So one of these may be true:

1. `clearanceShrugCm` is still zero.
2. `liftTranslateCm` is too small.
3. The translation is being written but later overwritten.
4. The translation is moving upperarm roots but not the visually important clavicle endpoint/deformation chain.
5. The analyzer is reading a bone that does not reflect the visible deformation.

## Diagnostic Patch

Add a direct post-write measurement:

```cpp
float AppliedClavicleLiftCm = 0.0f;
float AppliedUpperLiftCm = 0.0f;

FVector ClavicleBefore = FVector::ZeroVector;
FVector UpperBefore = FVector::ZeroVector;

if (ClavBone.IsValidToEvaluate())
{
    ClavicleBefore = CSPose.GetComponentSpaceTransform(
        ClavBone.CachedCompactPoseIndex).GetTranslation();
}

if (UpperBone.IsValidToEvaluate())
{
    UpperBefore = CSPose.GetComponentSpaceTransform(
        UpperBone.CachedCompactPoseIndex).GetTranslation();
}

ApplyTranslationDeltaCS(CSPose, ClavBone, ClavicleLiftDeltaComp);
ApplyTranslationDeltaCS(CSPose, UpperBone, ClavicleLiftDeltaComp);
ApplyTranslationDeltaCS(CSPose, LowerBone, ClavicleLiftDeltaComp);
ApplyTranslationDeltaCS(CSPose, HandBone, ClavicleLiftDeltaComp);

const FVector LiftDirComp = UpComp.GetSafeNormal();

if (!LiftDirComp.IsNearlyZero())
{
    if (ClavBone.IsValidToEvaluate())
    {
        const FVector ClavicleAfter = CSPose.GetComponentSpaceTransform(
            ClavBone.CachedCompactPoseIndex).GetTranslation();

        AppliedClavicleLiftCm = FVector::DotProduct(
            ClavicleAfter - ClavicleBefore,
            LiftDirComp);
    }

    if (UpperBone.IsValidToEvaluate())
    {
        const FVector UpperAfter = CSPose.GetComponentSpaceTransform(
            UpperBone.CachedCompactPoseIndex).GetTranslation();

        AppliedUpperLiftCm = FVector::DotProduct(
            UpperAfter - UpperBefore,
            LiftDirComp);
    }
}
```

Extend `mp.ClavicleDebug`:

```cpp
TEXT("... liftTranslateCm=%.1f appliedClavLiftCm=%.1f appliedUpperLiftCm=%.1f headClearanceCm=%.1f clearanceShrugCm=%.1f ...")
```

Expected values during a strong VP2 shrug peak:

```text
clearanceShrugCm       3-8 cm
liftTranslateCm        2-6 cm
appliedClavLiftCm      close to liftTranslateCm
appliedUpperLiftCm     close to liftTranslateCm
```

If `clearanceShrugCm` is positive but `appliedUpperLiftCm` is near zero, the translation write path is failing or overwritten.

---

# Solution 7: Move The Lift To A Final Chain Offset If It Is Overwritten

## Why

If later upperarm/lowerarm solve code rewrites component-space transforms from reference data, an early lift can disappear. The safest pattern is:

1. Compute the desired clavicle/chain lift early.
2. Apply all rotations normally.
3. Apply the lift as a final chain translation after the arm solve has finished.

## Pattern

Create pending values:

```cpp
FVector PendingClavicleLiftDeltaComp = FVector::ZeroVector;
bool bHasPendingClavicleLift = false;
```

Inside the clavicle block:

```cpp
const FVector LiftDirComp = UpComp.GetSafeNormal();
if (!LiftDirComp.IsNearlyZero())
{
    PendingClavicleLiftDeltaComp =
        LiftDirComp * ClavicleArmState.SmoothedClavicleLiftTranslationCm;

    bHasPendingClavicleLift =
        !PendingClavicleLiftDeltaComp.IsNearlyZero();
}
```

Then after upperarm/lowerarm rotations and before final hand/finger-only operations:

```cpp
if (bHasPendingClavicleLift)
{
    ApplyTranslationDeltaCS(CSPose, ClavBone, PendingClavicleLiftDeltaComp);
    ApplyTranslationDeltaCS(CSPose, UpperBone, PendingClavicleLiftDeltaComp);
    ApplyTranslationDeltaCS(CSPose, LowerBone, PendingClavicleLiftDeltaComp);
    ApplyTranslationDeltaCS(CSPose, HandBone, PendingClavicleLiftDeltaComp);
}
```

This makes shrug a final positional chain offset instead of a value that can be silently erased by downstream arm solve code.

---

# Solution 8: Audit Mirroring And Landmark Side Conventions

## Why

The new 2D clearance path reads directly from:

```cpp
PoseFrame.Normalized.Points[LmIdx]
```

The world-space path may already be using a helper that handles mirroring, actor facing, or left/right swaps. If 2D uses raw normalized landmarks while world-space uses mirrored/sanitized landmarks, the right side can invert.

## Required Rule

`TryGetNormalizedXY` must use the same side convention as `TryGetLmWorld`.

## Minimal Patch

If the project uses horizontal mirroring only:

```cpp
auto TryGetNormalizedXY = [&](const int32 LmIdx, FVector2D& Out) -> bool
{
    if (LmIdx < 0 || !PoseFrame.Normalized.IsValidIndex(LmIdx))
    {
        return false;
    }

    const FMediaPipePoseLandmark& Lm = PoseFrame.Normalized.Points[LmIdx];

    if (Lm.Presence <= KINDA_SMALL_NUMBER &&
        Lm.Visibility <= KINDA_SMALL_NUMBER &&
        Lm.Reliability <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    Out = FVector2D(Lm.X, Lm.Y);

    if (bMirrorLandmarksLR)
    {
        Out.X = 1.0f - Out.X;
    }

    return true;
};
```

If the project swaps landmark indices under mirroring, add a mapping helper:

```cpp
int32 ResolveMirroredPoseLandmarkIndex(const int32 LmIdx)
{
    if (!bMirrorLandmarksLR)
    {
        return LmIdx;
    }

    switch ((EMediaPipePoseLandmark)LmIdx)
    {
    case EMediaPipePoseLandmark::LeftShoulder: return (int32)EMediaPipePoseLandmark::RightShoulder;
    case EMediaPipePoseLandmark::RightShoulder: return (int32)EMediaPipePoseLandmark::LeftShoulder;
    case EMediaPipePoseLandmark::LeftEar: return (int32)EMediaPipePoseLandmark::RightEar;
    case EMediaPipePoseLandmark::RightEar: return (int32)EMediaPipePoseLandmark::LeftEar;
    case EMediaPipePoseLandmark::LeftEye: return (int32)EMediaPipePoseLandmark::RightEye;
    case EMediaPipePoseLandmark::RightEye: return (int32)EMediaPipePoseLandmark::LeftEye;
    default: return LmIdx;
    }
}
```

Then call `ResolveMirroredPoseLandmarkIndex` inside `TryGetNormalizedXY`.

---

# Solution 9: Fix Head/Chin Pitch Using Mouth/Eye And Nose/Eye Ratios

## Why

The current head pitch calculation mostly uses:

```cpp
ShoulderNosePitchInput
NoseDelta2D.Y
CenterDelta2D.Y
```

Those are weak because they include body bob, shoulder motion, camera framing, and whole-head vertical movement.

The feature probe showed stronger candidates:

```text
mouth_eye_y correlation to Manny current head pitch: ~0.411
nose_eye_y  correlation to Manny current head pitch: ~0.295
```

So the pitch source should be a face-internal ratio.

## Add Mouth Landmarks

In `MediaPipePoseDrivenAnimInstance_BodyPoseSolve.inl`, near the existing eye/ear/nose 2D reads, add:

```cpp
FVector2D LMouth2D = FVector2D::ZeroVector;
FVector2D RMouth2D = FVector2D::ZeroVector;

const bool bHasMouth2D =
    TryGetNormalizedXY((int32)EMediaPipePoseLandmark::MouthLeft, LMouth2D) &&
    TryGetNormalizedXY((int32)EMediaPipePoseLandmark::MouthRight, RMouth2D);
```

If the enum names are not defined, MediaPipe pose landmark indices are commonly:

```text
9  = mouth left
10 = mouth right
```

So the fallback is:

```cpp
const bool bHasMouth2D =
    TryGetNormalizedXY(9, LMouth2D) &&
    TryGetNormalizedXY(10, RMouth2D);
```

## Compute Face-Internal Pitch Proxies

After `HeadCenter2D` / `FaceSpan2D` setup:

```cpp
float NoseEyePitchProxy = 0.0f;
float MouthEyePitchProxy = 0.0f;
float MouthEarPitchProxy = 0.0f;

bool bHasNoseEyePitchProxy = false;
bool bHasMouthEyePitchProxy = false;
bool bHasMouthEarPitchProxy = false;

if (bHasEyes2D)
{
    const FVector2D EyeMid2D = (LEye2D + REye2D) * 0.5f;
    const float EyeSpan2D = FMath::Max(FVector2D::Distance(LEye2D, REye2D), 0.02f);

    if (bHasNose2D)
    {
        NoseEyePitchProxy = (Nose2D.Y - EyeMid2D.Y) / EyeSpan2D;
        bHasNoseEyePitchProxy = true;
    }

    if (bHasMouth2D)
    {
        const FVector2D MouthMid2D = (LMouth2D + RMouth2D) * 0.5f;
        MouthEyePitchProxy = (MouthMid2D.Y - EyeMid2D.Y) / EyeSpan2D;
        bHasMouthEyePitchProxy = true;
    }
}

if (bHasEars2D && bHasMouth2D)
{
    const FVector2D EarMid2D = (LEar2D + REar2D) * 0.5f;
    const FVector2D MouthMid2D = (LMouth2D + RMouth2D) * 0.5f;
    const float EarSpan2D = FMath::Max(FVector2D::Distance(LEar2D, REar2D), 0.02f);

    MouthEarPitchProxy = (MouthMid2D.Y - EarMid2D.Y) / EarSpan2D;
    bHasMouthEarPitchProxy = true;
}
```

## Initialize References

`FMediaPipeBodySolverState` already has:

```cpp
float HeadScreenNoseEyeReference = 0.0f;
float HeadScreenMouthEyeReference = 0.0f;
float HeadScreenMouthEarReference = 0.0f;
```

Use them when `bHasHeadScreenReference` is initialized:

```cpp
if (!BodyState.bHasHeadScreenReference)
{
    BodyState.bHasHeadScreenReference = true;
    BodyState.HeadScreenCenterReference = HeadCenterOffset2D;
    BodyState.HeadScreenNoseReference = NoseOffset2D;
    BodyState.HeadScreenShoulderNoseReference = NoseShoulderOffset2D;
    BodyState.HeadScreenLateralAngleReferenceDeg = HeadLateralAngleDeg;
    BodyState.HeadScreenRollReferenceDeg = EyeRollDeg;

    BodyState.HeadScreenNoseEyeReference = bHasNoseEyePitchProxy ? NoseEyePitchProxy : 0.0f;
    BodyState.HeadScreenMouthEyeReference = bHasMouthEyePitchProxy ? MouthEyePitchProxy : 0.0f;
    BodyState.HeadScreenMouthEarReference = bHasMouthEarPitchProxy ? MouthEarPitchProxy : 0.0f;
}
```

## Replace Pitch Formula

Before the current `ScreenHeadPitchDeg` assignment, compute:

```cpp
float FacePitchInput = 0.0f;
bool bHasFacePitchInput = false;

if (bHasMouthEyePitchProxy)
{
    FacePitchInput = MouthEyePitchProxy - BodyState.HeadScreenMouthEyeReference;
    bHasFacePitchInput = true;
}
else if (bHasNoseEyePitchProxy)
{
    FacePitchInput = NoseEyePitchProxy - BodyState.HeadScreenNoseEyeReference;
    bHasFacePitchInput = true;
}
else if (bHasMouthEarPitchProxy)
{
    FacePitchInput = MouthEarPitchProxy - BodyState.HeadScreenMouthEarReference;
    bHasFacePitchInput = true;
}
```

Then replace:

```cpp
ScreenHeadPitchDeg = FMath::Clamp(
    (ShoulderNosePitchInput * 22.0f + NoseDelta2D.Y * 45.0f + CenterDelta2D.Y * 30.0f) * ScreenWeight,
    -45.0f,
    45.0f);
```

with:

```cpp
const float PitchFromFace = bHasFacePitchInput
    ? FacePitchInput
    : ShoulderNosePitchInput;

ScreenHeadPitchDeg = FMath::Clamp(
    (PitchFromFace * 65.0f +
     ShoulderNosePitchInput * 12.0f +
     NoseDelta2D.Y * 16.0f +
     CenterDelta2D.Y * 8.0f) * ScreenWeight,
    -45.0f,
    45.0f);
```

If the first validation chart shows pitch inversion, flip only the face term:

```cpp
PitchFromFace * -65.0f
```

Do not flip yaw/roll while testing pitch.

## Update Face References Slowly

Near the existing reference update block:

```cpp
const float ReferenceAlpha = HalfLifeToAlpha(12.0f, DeltaSeconds);
```

add:

```cpp
if (bHasNoseEyePitchProxy)
{
    BodyState.HeadScreenNoseEyeReference = FMath::Lerp(
        BodyState.HeadScreenNoseEyeReference,
        NoseEyePitchProxy,
        ReferenceAlpha);
}

if (bHasMouthEyePitchProxy)
{
    BodyState.HeadScreenMouthEyeReference = FMath::Lerp(
        BodyState.HeadScreenMouthEyeReference,
        MouthEyePitchProxy,
        ReferenceAlpha);
}

if (bHasMouthEarPitchProxy)
{
    BodyState.HeadScreenMouthEarReference = FMath::Lerp(
        BodyState.HeadScreenMouthEarReference,
        MouthEarPitchProxy,
        ReferenceAlpha);
}
```

## Add To Head Debug

Extend `mp.HeadDebug` with:

```text
facePitchInput
noseEyePitch
mouthEyePitch
mouthEarPitch
```

This makes it clear whether the head pitch signal is present and whether it is being clamped, inverted, or absorbed.

---

# Solution 10: Update Analyzer To Measure The Fixed Signals

## Why

The analyzer must validate the exact signal you are now driving. Otherwise you can fix the runtime and still fail the chart.

## Add Or Keep These VP2 Signals

```text
left_shrug_clearance_norm_delta
right_shrug_clearance_norm_delta
bilateral_shrug_clearance_norm_delta
mouth_eye_y_norm_delta
nose_eye_y_norm_delta
mouth_ear_y_norm_delta
```

The VP2 shrug formula should use paired head landmarks, matching runtime:

```python
left_clearance = (left_shoulder_y - left_head_side_y) / shoulder_width
right_clearance = (right_shoulder_y - right_head_side_y) / shoulder_width
bilateral = 0.5 * (left_clearance + right_clearance)
```

Then convert to a positive shrug delta by subtracting from the baseline/reference if needed:

```python
bilateral_shrug_delta = reference_bilateral_clearance - bilateral_clearance
```

## Add Manny Runtime/Bone Signals

```text
left_upperarm_root_lift_cm
right_upperarm_root_lift_cm
bilateral_upperarm_root_lift_cm
left_clavicle_root_lift_cm
right_clavicle_root_lift_cm
bilateral_clavicle_root_lift_cm
left_clavicle_endpoint_lift_cm
right_clavicle_endpoint_lift_cm
bilateral_clavicle_endpoint_lift_cm
head_local_parent_angle_deg_delta
neck02_local_parent_angle_deg_delta
head_local_pitch_delta
```

## Do Not Use Raw Euler Wrapping As Acceptance

Avoid accepting/rejecting head pitch using raw `head_pitch_delta` if it has rotator wrapping artifacts. Prefer:

```text
head_local_parent_angle_deg_delta
neck02_local_parent_angle_deg_delta
parent-relative quaternion deltas
```

---

# Validation Protocol

## Step 1: Run The Current Untrialed Build Once

The handoff says the latest C++ build succeeded after the final 2D clearance patch, but no VP2 PIE trial was run after that final patch.

First run VP2 using the current code and inspect `mp.ClavicleDebug`.

Command:

```text
mp.StartPlacedEmbodiedTracking video=D:/Epic/Unreal_Projects/TestingKit5/Saved/Videos/VP2.mp4
```

Expected debug values during shrug peaks:

```text
headClearanceCm      changes over time
clearanceShrugCm     non-zero, ideally 3-8 cm
liftTranslateCm      non-zero, ideally 2-6 cm
```

If `clearanceShrugCm` is still `0.0`, do not tune weights. Fix source signal first using Solutions 1-3.

## Step 2: Verify Translation Write

If `clearanceShrugCm` and `liftTranslateCm` are positive, check:

```text
appliedClavLiftCm
appliedUpperLiftCm
```

Expected:

```text
appliedClavLiftCm  close to liftTranslateCm
appliedUpperLiftCm close to liftTranslateCm
```

If applied values are near zero, fix the transform write path using Solutions 6-7.

## Step 3: Re-run Analyzer

Use the same analysis window as the handoff:

```powershell
python Scripts/analyze_vp2_manny_motion_signals.py `
  --vp2 Saved/Videos/VP2.mp4 `
  --model Content/MediaPipe/pose_landmarker.task `
  --manny-jsonl Saved/CodexAgent/Diagnostics/vp2_manny_visible_timeseries_clearance_fix_ticker.json `
  --manny-component live `
  --out-prefix Saved/CodexAgent/Diagnostics/vp2_manny_motion_signals_next_live `
  --auto-shift `
  --start 2 `
  --end 18.5
```

## Minimum Acceptance Targets

```text
Manny bilateral shrug range:        > 0.06
VP2/Manny bilateral shrug corr:     > 0.45
Right-side shrug corr:              positive, not negative
Visible Manny shoulder shrugs:      yes
Head/chin pitch visible:            yes
Head pitch corr initial target:     > 0.35
No severe rotator wrapping spikes:  yes
```

## Strong Acceptance Targets

```text
Manny bilateral shrug range:        0.08-0.14
VP2/Manny bilateral shrug corr:     > 0.55
Left/right shrug corr:              both positive
Head/chin pitch corr:               > 0.45 if mouth-eye proxy is stable
```

---

# Recommended Implementation Order

1. Run the latest untrialed 2D clearance build once and record `mp.ClavicleDebug`.
2. Replace side-local head fallback with paired ears/eyes/nose fallback.
3. Compute one shared bilateral shoulder-to-head clearance value.
4. Use `BodyState.BilateralShoulderHeadClearanceReferenceCm` as the main reference.
5. Feed the same `BilateralShrugCm` into both clavicle branches.
6. Add only a small left/right asymmetry correction.
7. Temporarily reduce older shoulder-height and shoulder-asymmetry lift drivers.
8. Add `appliedClavLiftCm` and `appliedUpperLiftCm` diagnostics.
9. If the applied lift is lost, move lift application to the final arm-chain transform step.
10. Add mouth/eye and nose/eye head pitch proxies.
11. Extend the analyzer with the exact runtime signals.
12. Validate by range and correlation, not viewport impression alone.

---

# Common Failure Cases And Fixes

## Failure: `clearanceShrugCm` Is Always Zero

Likely causes:

```text
- Bad landmark fallback.
- Per-side reference immediately absorbs the shrug.
- Y sign is wrong.
- 2D landmarks are not using the same mirrored side convention as world landmarks.
```

Fixes:

```text
- Use paired fallback.
- Use shared bilateral reference.
- Confirm image-space formula is shoulder_y - head_y.
- Audit mirroring.
```

## Failure: `clearanceShrugCm` Is Positive But Manny Still Does Not Shrug

Likely causes:

```text
- Translation not applied.
- Translation applied before later solve overwrite.
- Analyzer reads upperarm root, but visual deformation is driven elsewhere.
```

Fixes:

```text
- Add appliedClavLiftCm and appliedUpperLiftCm.
- Move lift write to final chain offset.
- Also sample clavicle endpoint/deformation bones in analyzer.
```

## Failure: Left Side Works, Right Side Is Negative

Likely causes:

```text
- Landmark mirroring mismatch.
- Right side fallback uses a different head landmark than left side.
- Right side asymmetry driver overpowers bilateral shrug.
```

Fixes:

```text
- Paired head fallback only.
- Same bilateral value applied to both sides.
- Clamp asymmetry to a small correction.
```

## Failure: Head Pitch Still Looks Flat

Likely causes:

```text
- Pitch is driven by body/head vertical bob instead of face-internal motion.
- Mouth landmarks are not being read.
- Pitch sign is inverted.
- Head/neck max-step smoothing is too restrictive.
```

Fixes:

```text
- Use mouth_eye_y first, nose_eye_y second.
- Log facePitchInput.
- Flip only the face pitch term if needed.
- Check max step and smoothing after the signal is proven present.
```

---

# Final Recommendation

The highest-impact fix is:

```text
one shared bilateral shoulder-to-head clearance driver
+ paired head landmark fallback
+ same bilateral lift applied to both clavicles
+ small capped side asymmetry correction
```

Do not try to solve this by only increasing `CVarMediaPipeClavicleShrugWeight`. The latest metrics show the source/output relationship is structurally wrong: Manny’s bilateral signal is too flat and the right side is inverted. More weight would amplify the wrong or cancelling signal.

For head/chin movement, the best next driver is:

```text
mouth_eye_y_norm_delta
```

with `nose_eye_y_norm_delta` as fallback, because those were stronger in the VP2 feature probe than raw head vertical motion.
