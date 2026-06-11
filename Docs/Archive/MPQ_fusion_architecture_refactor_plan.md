# MPQ_fusion Unreal Engine C++ Architecture / Refactor Plan

**Repository:** `adfras/MPQ_fusion`  
**Review scope:** `Source/MediaPipeDriver`, `Source/MediaPipeDriverEditor`, `Config`, and plugins only where relevant to runtime/editor bridge behavior.  
**Generated:** 2026-05-27  
**Assumption:** Unreal Content assets, binaries, generated folders, `Saved`, `Intermediate`, `DerivedDataCache`, and local DLLs are intentionally excluded from the repository; this plan is based only on source/config files available in GitHub.

---

## Executive Summary

The avatar embodiment system has collapsed too many responsibilities into the pose-driven animation node and nearby runtime files. The most important problem is that **sensor ingestion, source authority, BodyFusion, profile calibration, MetaHuman behavior, generic humanoid behavior, skeleton-specific bone writing, Quest hand/wrist/arm logic, diagnostics, debug commands, and tests are all coupled through `MediaPipePoseDrivenAnimInstance.*` and `MediaPipeBodyFusion.*`.**

The refactor should move toward this architecture:

```text
Tracking source acquisition
    -> immutable source frames
    -> pure deterministic BodyFusion / embodiment pipeline
    -> semantic fused avatar pose
    -> per-skeleton adapter/writer
    -> anim graph bone writes

Diagnostics/debug consume trace output, but never change runtime behavior.
```

The first practical implementation step should be a **pelvis authority invariant test** plus an explicit pelvis-follow policy gate. This directly targets the current bug vector where chest/head follow can make the pelvis chase the HMD.

---

## Key Source References

Primary files reviewed:

- `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.h`
- `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp`
- `Source/MediaPipeDriver/MediaPipeBodyFusion.h`
- `Source/MediaPipeDriver/MediaPipeBodyFusion.cpp`
- `Source/MediaPipeDriver/MediaPipeAvatarEmbodimentProfile.h`
- `Source/MediaPipeDriver/MediaPipeAvatarEmbodimentProfile.cpp`
- `Source/MediaPipeDriver/MediaPipeMetaHumanProfile.h`
- `Source/MediaPipeDriver/MediaPipeMetaHumanProfile.cpp`
- `Source/MediaPipeDriver/MediaPipeBodySolverMath.h`
- `Source/MediaPipeDriver/MediaPipeBodySolverMath.cpp`
- Quest arm/wrist/finger solver files under `Source/MediaPipeDriver`
- runtime diagnostics/debug/test files under `Source/MediaPipeDriver`

External references checked for skeleton/retargeting implications:

- Unreal Engine Skeletons documentation: https://dev.epicgames.com/documentation/unreal-engine/skeletons-in-unreal-engine
- MetaHuman asset / RigLogic / corrective joint documentation: https://dev.epicgames.com/documentation/metahuman/metahuman-asset-changes

These references support a key design decision: **MetaHuman-specific behavior should be isolated to profile and skeleton adapter/writer layers, not embedded in BodyFusion or the generic animation node.** Unreal skeletons are bone hierarchies with retargeting settings, while MetaHumans add additional rig/corrective complexity. The code should therefore model semantic chains and adapters, not flat hardcoded bone names in solver code.

---

# 1. Diagnosis of Current Architecture Problems

## 1.1 `FAnimNode_MediaPipePoseDriven` is a god object

`MediaPipePoseDrivenAnimInstance.*` is doing too much.

Current responsibilities include:

- animation graph lifecycle;
- target/profile resolution;
- reference pose caching;
- HMD pose acquisition;
- Quest hand/wrist ingestion;
- BodyFusion source frame construction;
- BodyFusion calibration/reset state;
- authority selection plumbing;
- MetaHuman profile state;
- generic humanoid state;
- pelvis/root grounding;
- spine/chest/head solving;
- Quest wrist position solving;
- Quest hand rotation;
- Quest finger drive;
- arm twist logic;
- leg logic;
- debug command registration;
- diagnostics/logging;
- capture/replay support.

This violates the most important boundary for an Unreal animation node: the node should bridge animation lifecycle and bone writes, not own the entire embodiment domain.

### Specific symptoms

The header exposes a large `FAnimNode_MediaPipePoseDriven` with policy and state for:

- pelvis translation;
- FK root grounding;
- leg IK;
- spine/head smoothing;
- arm IK;
- Quest hand tracking;
- Quest wrist behavior;
- finger curls;
- twist/clamp policy;
- reference pose dimensions;
- BodyFusion state;
- MetaHuman state;
- cached HMD and Quest source state.

It also hardcodes semantic and concrete bone names such as:

- `root`;
- `pelvis`;
- `spine_01`, `spine_02`, `spine_03`, `spine_04`, `spine_05`;
- neck/head;
- clavicles;
- upper/lower arm;
- twist bones;
- legs;
- finger chains.

That hardcoding makes it too easy for MetaHuman, Manny, and generic humanoids to diverge in behavior accidentally.

---

## 1.2 `PreUpdate` mixes lifecycle, source capture, reset, profile resolution, and debug behavior

`PreUpdate` should mostly snapshot game-thread data for the animation thread. Instead, it currently participates in:

- target reset;
- Quest hand state reset;
- full-arm-chain state reset;
- BodyFusion source-frame reset;
- authority reset;
- MetaHuman profile reset;
- HMD cache reset;
- avatar rig / MetaHuman profile resolution;
- calibration reset handling;
- body/arm/leg/foot/yaw/wrist/smoothing/continuity state resets.

This makes lifecycle bugs likely. It also makes it hard to reason about whether a visual defect is caused by source ingestion, policy, calibration, solver math, adapter logic, or debug state.

---

## 1.3 Debug commands and runtime behavior are coupled

`MediaPipePoseDrivenAnimInstance.cpp` contains runtime console command behavior, including BodyFusion calibration reset and Quest wrist calibration/reset paths.

This is risky because debug commands and CVars can become hidden runtime policy. The correct model is:

```text
Console command / CVar
    -> diagnostic request or explicit reset request
    -> targeted component/avatar instance
    -> no hidden global behavior mutation
```

Debug controls may enable logs, overlays, trace capture, or capture/replay. They must not silently change source authority, pelvis behavior, calibration behavior, or solver policy.

---

## 1.4 Quest/OpenXR source ingestion is inside the animation implementation

The pose-driven animation implementation directly reaches into Quest/OpenXR hand tracking and converts keypoint states into runtime hand snapshots.

That code belongs in a tracking source bridge, not in an anim node or skeleton writer.

Desired boundary:

```text
Quest/OpenXR bridge
    -> FMediaPipeQuestHandTrackingFrame / FMediaPipeTrackingSourceFrame
    -> embodiment pipeline
    -> skeleton adapter/writer
```

The anim node should consume an immutable source frame. It should not query the XR runtime directly.

---

## 1.5 `MediaPipeBodyFusion.*` mixes semantic solving with avatar writing

`MediaPipeBodyFusion.h` currently contains several conceptual layers together:

- tracking source state/freshness;
- source frame types;
- calibration input/output;
- authority modes;
- fused pose data;
- debug-error structs;
- BodyFusion solver input;
- BodyFusion solver;
- avatar pose writer.

The presence of `FMediaPipeAvatarPoseWriter` in BodyFusion is the wrong dependency direction. BodyFusion should not write bones. It should produce a semantic pose such as:

```text
head/eye/chest/pelvis/limbs/fingers + authority + confidence + diagnostics
```

A skeleton adapter should decide how that semantic result maps to MetaHuman, Manny, or a generic humanoid skeleton.

---

## 1.6 Embodied hips-only pelvis behavior is a high-risk bug vector

The current BodyFusion code has an embodied hips-only path that resolves upper/lower body ownership, computes chest position from profile offsets and follow deltas, and then applies a `PelvisFollowDelta` from chest/head follow into `PelvisWorld`.

That is the exact bug vector for:

- pelvis drift;
- pelvis chasing the HMD;
- neck/chest compensation errors;
- local-view motion coupling;
- hard-to-debug head/chest/pelvis pivot bugs.

Pelvis following upper-body/HMD motion must be an explicit policy mode, not a side effect.

---

## 1.7 `MediaPipeAvatarEmbodimentProfile.*` is both data and solver

`FMediaPipeAvatarEmbodimentProfile` contains useful profile data:

- skeleton family;
- forward axis;
- yaw offset;
- eye/head/chest/neck/pelvis offsets;
- upper-body follow settings;
- expected limb lengths;
- bone map;
- local-view policy.

But the same area also defines solver input/output and static solve functions for avatar forward/up, camera-anchored solving, and HMD-relative Quest wrist mapping.

Profiles should be authorable data plus immutable runtime snapshots. Solver functions should live in pure deterministic solver files.

---

## 1.8 `MediaPipeMetaHumanProfile.*` is closer to the right model

The MetaHuman profile layer already has the right flavor:

- profile definitions;
- soft references;
- `UDataAsset` usage;
- config settings;
- validation structs;
- resolved target state;
- arm-source mode.

The problem is not that MetaHuman has a profile. The problem is that generic humanoids do not get the same clean treatment, so the anim node ends up branching across avatar families.

MetaHuman-specific helper/corrective bone names should live in a MetaHuman adapter/profile, not in BodyFusion or the generic anim node.

---

## 1.9 `MediaPipeBodySolverMath.*` is the best current example

`MediaPipeBodySolverMath.*` is close to the architecture the rest of the system needs. It is mostly pure structs/functions for semantic body basis, avatar arm basis, foot forward, leg/arm basis rotation, elbow-plane solving, and pelvis planar offset.

Use this as the extraction style:

- explicit input structs;
- explicit result structs;
- no `UObject` dependencies;
- no skeletal mesh component;
- no anim graph dependency;
- no debug CVar dependency;
- deterministic tests.

---

## 1.10 Quest solver files are partially good but still too coupled

`FMediaPipeQuestConstrainedArmSolver` and `FMediaPipeQuestWristApplyPolicy` already look like useful deterministic solver/policy types. They should be kept and further isolated.

`MediaPipeQuestFingerSolver` is weaker because it includes `MediaPipePoseDrivenAnimInstance.h`. That makes a supposedly pure finger solver depend on the giant anim node. This should be fixed early.

Desired change:

```cpp
// Bad
#include "MediaPipePoseDrivenAnimInstance.h"

// Good
#include "MediaPipeQuestHandTypes.h"
```

Move shared Quest hand snapshot types into a lightweight header such as:

```text
MediaPipeQuestHandTypes.h
```

---

# 2. Proposed Target Architecture

## 2.1 Runtime layer 1: tracking source acquisition

Create or refactor toward:

```cpp
UMediaPipeEmbodimentSourceComponent
FMediaPipeTrackingSourceFrame
FMediaPipeTrackingSourceStatus
FMediaPipeQuestHandTrackingFrame
FMediaPipeFullArmChainFrame
FMediaPipePoseFrameValidator
```

Responsibilities:

- read MediaPipe pose source;
- read HMD source;
- read Quest hand source;
- read Quest full-arm-chain source;
- run on the game thread;
- produce immutable timestamped source frames;
- never write bones;
- never know MetaHuman/Manny/generic skeleton details;
- never make final BodyFusion authority decisions.

`FMediaPipeTrackingSourceFrame` already exists conceptually, but it should move out of `MediaPipeBodyFusion.h` into:

```text
MediaPipeTrackingSourceTypes.h
```

---

## 2.2 Runtime layer 2: profile and adapter data

Create:

```cpp
UMediaPipeAvatarEmbodimentProfileAsset
FMediaPipeAvatarRuntimeProfile
UMediaPipeSkeletonAdapterDataAsset
FMediaPipeSkeletonBoneMap
FMediaPipeSemanticBoneChain
FMediaPipeLocalViewPolicy
```

Keep/adapt:

```cpp
UMediaPipeMetaHumanRetargetProfile
UMediaPipeMetaHumanProfileSettings
FMediaPipeMetaHumanProfileDefinition
```

Profile data should own:

- avatar forward axis/yaw offset;
- camera-to-eye/head/chest/neck/pelvis offsets;
- upper-body follow policy;
- pelvis authority policy;
- expected body proportions;
- default source authority preferences;
- local-view policy;
- skeleton adapter asset reference.

Adapter data should own:

- root and pelvis semantic bones;
- spine chain;
- semantic chest bone or chest chain region;
- neck chain;
- head bone;
- clavicle/upper-arm/lower-arm/hand chains;
- thigh/calf/foot/toe chains;
- twist bones;
- corrective/helper bones;
- finger chains;
- optional MetaHuman helper bones;
- missing-bone tolerance rules.

Important: the adapter should model **chains and semantic roles**, not just flat names like `spine_03`.

---

## 2.3 Runtime layer 3: pure deterministic solvers

Create or preserve:

```cpp
FMediaPipeBodyFusionAuthorityPolicy
FMediaPipeEmbodimentCalibrationSolver
FMediaPipeBodyFusionSolver
FMediaPipeTorsoFusionSolver
FMediaPipePelvisFusionSolver
FMediaPipeSpineSolver
FMediaPipeArmIkSolver
FMediaPipeLegSolver
FMediaPipeQuestArmSolver
FMediaPipeQuestWristSolver
FMediaPipeQuestFingerPoseSolver
FMediaPipeArmTwistSolver
MediaPipeBodySolverMath
```

Rules for this layer:

- input structs in;
- result structs out;
- no `UObject`;
- no `USkeletalMeshComponent`;
- no `FAnimInstanceProxy`;
- no `FCSPose`;
- no XR interface calls;
- no CVars;
- no debug drawing;
- no hidden global state;
- deterministic automation tests.

---

## 2.4 Runtime layer 4: BodyFusion / embodiment pipeline

Create:

```cpp
FMediaPipeEmbodimentPipeline
FMediaPipeEmbodimentPipelineInput
FMediaPipeEmbodimentPipelineOutput
FMediaPipeEmbodimentPipelineState
FMediaPipeFusedAvatarPose
FMediaPipeBodyFusionDiagnosticsFrame
```

Responsibilities:

- consume `FMediaPipeTrackingSourceFrame`;
- consume `FMediaPipeAvatarRuntimeProfile`;
- consume previous pipeline state;
- call calibration, authority, torso, pelvis, limb, Quest solvers;
- produce a fused semantic body pose;
- produce diagnostics trace;
- never write skeleton bones;
- never know MetaHuman helper bones;
- never query HMD/OpenXR directly.

---

## 2.5 Runtime layer 5: skeleton adapters/writers

Create:

```cpp
IMediaPipeSkeletonPoseAdapter
FMediaPipeSkeletonPoseAdapterBase
FMediaPipeGenericHumanoidPoseAdapter
FMediaPipeMannyPoseAdapter
FMediaPipeMetaHumanPoseAdapter
FMediaPipeReferencePoseCache
FMediaPipeBoneWriteContext
FMediaPipeComponentSpaceBoneWriter
```

Responsibilities:

- resolve `FBoneReference` / compact pose indices;
- cache reference-pose dimensions;
- translate semantic fused pose into component-space bone writes;
- apply skeleton-specific helper/twist/corrective/finger behavior;
- treat missing optional bones as no-op plus diagnostics;
- keep MetaHuman/Manny/generic differences out of BodyFusion.

The solver path should be shared. The adapter output may differ.

---

## 2.6 Runtime layer 6: animation bridge

Slim `FAnimNode_MediaPipePoseDriven` to something closer to:

```cpp
struct FAnimNode_MediaPipePoseDriven final : FAnimNode_SkeletalControlBase
{
    UPROPERTY(EditAnywhere)
    TObjectPtr<UMediaPipeEmbodimentSourceComponent> Source;

    UPROPERTY(EditAnywhere)
    TObjectPtr<UMediaPipeAvatarEmbodimentProfileAsset> AvatarProfile;

    FMediaPipeAvatarRuntimeProfile RuntimeProfile;
    FMediaPipeEmbodimentPipelineState PipelineState;
    TUniquePtr<IMediaPipeSkeletonPoseAdapter> SkeletonAdapter;

    // Evaluate:
    // 1. consume cached source frame;
    // 2. run embodiment pipeline;
    // 3. ask adapter to write bones.
};
```

`UMediaPipePoseDrivenAnimInstance` should become a thin Blueprint-facing wrapper with setters and reset requests.

It should not:

- register debug commands;
- query OpenXR;
- resolve MetaHuman targets directly;
- own Quest capture/replay;
- make BodyFusion policy decisions;
- contain skeleton-specific helper bone arrays.

---

## 2.7 Runtime layer 7: diagnostics/debug

Create:

```cpp
UMediaPipeEmbodimentDiagnosticsSubsystem
UMediaPipeAvatarDiagnosticsComponent
FMediaPipeDiagnosticsSink
FMediaPipeDebugCommandRouter
FMediaPipeQuestCaptureReplayService
```

Rules:

- CVars may enable logs, traces, overlays, and capture/replay;
- CVars must not change solver authority;
- console commands enqueue explicit reset requests;
- reset requests target a specific component/avatar instance;
- diagnostics consume pipeline traces;
- diagnostics do not mutate solver policy.

---

# 3. Text Dependency Diagram

```text
Game thread source acquisition
--------------------------------
UMediaPipePoseTrackerComponent / Quest-HMD bridge / FullArmChain provider
        |
        v
FMediaPipeTrackingSourceFrame
        |
        v

Animation bridge
--------------------------------
FAnimNode_MediaPipePoseDriven
        |
        | uses immutable frame + runtime profile
        v
FMediaPipeEmbodimentPipeline
        |
        +--> FMediaPipeBodyFusionAuthorityPolicy
        +--> FMediaPipeEmbodimentCalibrationSolver
        +--> FMediaPipeBodyFusionSolver
        +--> FMediaPipeTorsoFusionSolver
        +--> FMediaPipePelvisFusionSolver
        +--> FMediaPipeArmIkSolver / FMediaPipeLegSolver
        +--> FMediaPipeQuestArmSolver
        +--> FMediaPipeQuestWristSolver
        +--> FMediaPipeQuestFingerPoseSolver
        |
        v
FMediaPipeFusedAvatarPose
        |
        v

Skeleton-specific writing
--------------------------------
IMediaPipeSkeletonPoseAdapter
        |
        +--> FMediaPipeGenericHumanoidPoseAdapter
        +--> FMediaPipeMannyPoseAdapter
        +--> FMediaPipeMetaHumanPoseAdapter
        |
        v
FMediaPipeComponentSpaceBoneWriter -> FCSPose

Diagnostics side channel
--------------------------------
FMediaPipeBodyFusionDiagnosticsFrame
        |
        v
UMediaPipeEmbodimentDiagnosticsSubsystem / logs / overlays / capture-replay

Forbidden dependencies
--------------------------------
Pure solvers  -X-> UObject / AnimInstance / XR / CVars / FCSPose
BodyFusion    -X-> bone names / MetaHuman helper bones / Manny twist names
Debug CVars   -X-> authority decisions / runtime behavior changes
Adapters      -X-> sensor acquisition
```

---

# 4. What Should Move Where

## 4.1 Pure deterministic solver code

Move or keep here:

- source freshness classification;
- BodyFusion authority selection;
- embodied upper-body / hips-only policy;
- HMD eye/head/chest solve;
- calibration solve;
- pelvis solve;
- pelvis follow gating;
- torso basis;
- neck/chest constraints;
- leg basis / foot forward / IK;
- arm IK / elbow-plane solve;
- Quest wrist mapping;
- Quest hand basis;
- Quest finger curl/retargeting;
- arm twist math;
- smoothing/continuity with explicit state passed in/out.

This is the `MediaPipeBodySolverMath.*` style.

---

## 4.2 `UAnimInstance` / anim node

Keep only:

- animation graph lifecycle;
- game-thread snapshot copy;
- animation-thread evaluation call;
- adapter cache lifecycle;
- final component-space write call;
- Blueprint-facing setters;
- reset request forwarding.

Remove:

- OpenXR/Quest tracker reads;
- BodyFusion source-frame construction;
- MetaHuman profile resolution;
- hardcoded bone maps;
- hardcoded MetaHuman helper names;
- debug command registration;
- capture/replay;
- calibration global maps;
- debug logging decisions.

---

## 4.3 Profile data / data assets

Move here:

- upper-body follow alpha;
- chest/neck/head/pelvis offsets;
- camera anchoring offsets;
- expected body proportions;
- pelvis authority mode;
- source authority preferences;
- skeleton family;
- skeleton adapter asset;
- local-view policy;
- retarget tolerances.

MetaHuman already has profile/data-asset/config infrastructure. General humanoid profiles should mirror that pattern.

---

## 4.4 Per-skeleton adapters/writers

Move here:

- bone names;
- reference-pose cache;
- spine-chain interpretation;
- chest semantic bone selection;
- neck-chain distribution;
- twist bones;
- finger chain layout;
- MetaHuman helper/corrective bones;
- Manny-specific twist layout;
- missing-bone fallback behavior;
- local-view bone write/hide behavior.

BodyFusion should output a semantic body pose. The adapter decides whether that writes to `spine_03`, `spine_05`, helper bones, twist bones, or no bone at all.

---

## 4.5 Diagnostics/debug-only code

Move here:

- `mp.BodyFusion.ResetCalibration`;
- Quest wrist reset command;
- Quest hand capture/replay;
- debug reporters;
- comparison diagnostics;
- pose diagnostic formatting;
- visual overlays;
- log throttling.

The solver may emit a diagnostics frame. It must not read a CVar to change behavior.

---

# 5. Files / Classes to Split, Rename, or Delete

## 5.1 Split `MediaPipePoseDrivenAnimInstance.*`

Create:

```text
MediaPipePoseDrivenAnimInstance.h/.cpp
    Keep only UMediaPipePoseDrivenAnimInstance and Blueprint-facing wrapper behavior.

MediaPipePoseDrivenAnimNode.h/.cpp
    Move FAnimNode_MediaPipePoseDriven here, slimmed down.

MediaPipeEmbodimentAnimRuntimeState.h/.cpp
    Own per-instance runtime state copied between game/eval phases.

MediaPipeReferencePoseCache.h/.cpp
    Extract BuildReferencePoseCache and all ref-pose lengths/transforms.

MediaPipeBoneWriteContext.h/.cpp
    Common component-space write helper.

MediaPipeGenericHumanoidPoseAdapter.h/.cpp
MediaPipeMannyPoseAdapter.h/.cpp
MediaPipeMetaHumanPoseAdapter.h/.cpp
    Own bone references and skeleton-specific writes.

MediaPipeQuestHandTrackingSource.h/.cpp
    Move OpenXR hand read/snapshot code here.

MediaPipeEmbodimentDebugCommands.cpp
    Move reset/capture/replay console commands here.
```

Retire current `.inl` files gradually:

```text
MediaPipePoseDrivenAnimInstance_ReferenceCache.inl
    -> MediaPipeReferencePoseCache.cpp

MediaPipePoseDrivenAnimInstance_BodyPoseSolve.inl
MediaPipePoseDrivenAnimInstance_TorsoBasis.inl
MediaPipePoseDrivenAnimInstance_BodyState.inl
    -> MediaPipeEmbodimentPipeline / torso / pelvis solvers

MediaPipePoseDrivenAnimInstance_LegSolve.inl
    -> MediaPipeLegSolver.*

MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl
    -> Quest arm solver + adapter writer split

MediaPipePoseDrivenAnimInstance_QuestArmWristSolve.inl
MediaPipePoseDrivenAnimInstance_QuestSpaceMapping.inl
    -> MediaPipeQuestWristSolver.*

MediaPipePoseDrivenAnimInstance_QuestHandRotation.inl
MediaPipePoseDrivenAnimInstance_QuestFingerDrive.inl
    -> Quest hand/finger solvers + adapter writer

MediaPipePoseDrivenAnimInstance_ArmTwist.inl
    -> MediaPipeArmTwistSolver.*
```

---

## 5.2 Split `MediaPipeBodyFusion.*`

Create:

```text
MediaPipeTrackingSourceTypes.h
    FMediaPipeTrackingSourceFrame
    source freshness/status types

MediaPipeBodyFusionTypes.h
    FMediaPipeFusedAvatarPose
    body region / owner enums

MediaPipeBodyFusionAuthorityPolicy.h/.cpp
    DefaultHybrid
    DefaultEmbodiedUpperBody
    DefaultEmbodiedHipsOnly
    ResolveUpperLimbOwner
    ResolveLowerBodyOwner

MediaPipeEmbodimentCalibrationSolver.h/.cpp
    TryBuildNeutralCalibration
    calibration transforms

MediaPipeBodyFusionSolver.h/.cpp
    Pure semantic pose fusion only

MediaPipeFusedAvatarPose.h
    Final semantic output type

MediaPipeBodyFusionDiagnostics.h/.cpp
    Debug/error metrics frame
```

Move `FMediaPipeAvatarPoseWriter` out of BodyFusion.

Suggested destination:

```text
MediaPipeComponentSpaceBoneWriter.h/.cpp
```

or:

```text
MediaPipeSkeletonPoseAdapterBase.h/.cpp
```

---

## 5.3 Split `MediaPipeAvatarEmbodimentProfile.*`

Create:

```text
MediaPipeAvatarProfileTypes.h
    enums, semantic profile structs

MediaPipeAvatarEmbodimentProfileAsset.h/.cpp
    UDataAsset authoring surface

MediaPipeAvatarRuntimeProfile.h
    immutable runtime copy

MediaPipeSkeletonAdapterDataAsset.h/.cpp
    semantic bone chains and skeleton-specific metadata

MediaPipeAvatarProfileResolver.h/.cpp
    target/profile resolution outside anim node

MediaPipeCameraAnchoredAvatarSolver.h/.cpp
    Move current camera-anchored/profile solve functions here
```

The existing flat `FMediaPipeAvatarBoneMap` can survive as a compatibility layer, but new code should use semantic chain data.

---

## 5.4 Refactor `MediaPipeMetaHumanProfile.*`

Keep:

```text
FMediaPipeMetaHumanProfileDefinition
UMediaPipeMetaHumanRetargetProfile
UMediaPipeMetaHumanProfileSettings
FMediaPipeResolvedMetaHumanTarget
```

Move:

```text
MetaHuman target resolution
    -> MediaPipeAvatarProfileResolver

MetaHuman helper/corrective bone names
    -> MediaPipeMetaHumanPoseAdapter / MetaHuman skeleton adapter asset

MetaHuman-specific arm-source override
    -> profile policy consumed by generic pipeline
```

Rule: BodyFusion and the anim node must not branch on “MetaHuman” for solve behavior. The semantic solve should be shared.

---

## 5.5 Fix `MediaPipeQuestFingerSolver`

Remove dependency on the anim instance.

Create:

```text
MediaPipeQuestHandTypes.h
```

Move into it:

- `FQuestHandTrackingSnapshot`;
- finger constants;
- keypoint wrappers needed by the finger solver;
- lightweight hand-side enums if needed.

Then change the solver include from:

```cpp
#include "MediaPipePoseDrivenAnimInstance.h"
```

to:

```cpp
#include "MediaPipeQuestHandTypes.h"
```

This is a small, high-value dependency cleanup.

---

# 6. Phased Refactor Plan

## Phase 1: Safety and test harness

Add invariant tests before moving big code.

Create:

```text
Source/MediaPipeDriver/MediaPipeBodyFusionInvariantsTests.cpp
Source/MediaPipeDriver/MediaPipeEmbodimentPipelineTests.cpp
Source/MediaPipeDriver/MediaPipeSkeletonAdapterContractTests.cpp
```

Build fixtures for:

```text
HMD only
HMD + fresh MediaPipe hips
HMD + stale MediaPipe hips
HMD + Quest hands
HMD + Quest full-arm chain
Generic humanoid profile
Manny-like profile
MetaHuman profile
Embodied upper-body authority
Embodied hips-only authority
```

### First invariant tests

#### 1. HMD owns head/eye

```text
Given:
- fresh HMD pose
- fresh MediaPipe pose landmarks
- any avatar profile

Assert:
- eye owner == HMD
- head owner == HMD
- MediaPipe head landmarks do not change head transform
- HMD yaw/pitch/roll propagate to the adapter write
```

#### 2. Pelvis does not chase HMD

```text
Given:
- embodied hips-only authority
- reliable MediaPipe hips
- HMD A
- HMD B differs from A only by planar X/Y

Assert:
- pelvis planar X/Y is unchanged
- unless EPelvisAuthorityMode::FollowUpperBodyExplicit is enabled
```

#### 3. Chest follows calibrated profile behavior

```text
Given:
- fixed calibration
- fixed profile chest/neck/head offsets
- fixed HMD
- fixed pelvis

Assert:
- chest output is deterministic
- chest output is bounded by profile offsets and follow alpha
```

#### 4. Debug CVars do not affect solver output

```text
Given:
- same source frame
- same profile
- same previous state

When:
- debug/log/capture settings are toggled

Assert:
- semantic fused pose is identical
- authority result is identical
- calibration result is identical
```

#### 5. MetaHuman/generic share semantic solve

```text
Given:
- identical source frame
- equivalent runtime profile
- generic adapter
- MetaHuman adapter

Assert:
- FMediaPipeFusedAvatarPose is the same before skeleton writing
- differences appear only in adapter write plan / helper / corrective bone writes
```

#### 6. No stale Quest write

```text
Given:
- stale Quest wrist/finger samples
- previous reliable Quest samples

Assert:
- authority follows explicit stale/hold/fallback policy
- stale Quest data is not treated as fresh live input
```

### Add dependency guard tests/scripts

Add a simple script or automation test that fails if pure solver headers include forbidden runtime dependencies.

Forbidden includes for pure solvers:

```text
MediaPipePoseDrivenAnimInstance.h
AnimInstance.h
SkeletalMeshComponent.h
XRTrackingSystemBase.h
IHandTracker.h
ConsoleManager.h
```

---

## Phase 2: Extract pure solvers

Start with minimal behavior change, except where behavior is demonstrably unsafe.

Steps:

1. Move `FMediaPipeTrackingSourceFrame` and source freshness structs out of `MediaPipeBodyFusion.h`.

2. Extract:

```cpp
FMediaPipeBodyFusionAuthorityPolicy
FMediaPipeEmbodimentCalibrationSolver
FMediaPipePelvisFusionSolver
FMediaPipeTorsoFusionSolver
```

3. Replace implicit pelvis-follow with explicit policy:

```cpp
enum class EMediaPipePelvisAuthorityMode : uint8
{
    ProfileLocked,
    MediaPipeHipsVerticalOnly,
    MediaPipeHipsFull,
    FollowUpperBodyExplicit
};
```

4. Default embodied hips-only to:

```cpp
EMediaPipePelvisAuthorityMode::MediaPipeHipsVerticalOnly
```

5. Gate the current pelvis follow behavior:

```cpp
if (Input.Profile.PelvisAuthorityMode == EMediaPipePelvisAuthorityMode::FollowUpperBodyExplicit)
{
    PelvisWorld += PelvisFollowDelta;
}
```

6. Move hand/finger math out of anim-node dependencies.

7. Keep old call sites alive temporarily. The anim node can still call the new extracted solver functions until Phase 4.

---

## Phase 3: Profile/adapters cleanup

1. Introduce:

```cpp
UMediaPipeSkeletonAdapterDataAsset
```

2. Replace flat-only bone mapping with semantic chains:

```cpp
struct FMediaPipeSemanticSkeletonMap
{
    FName Root;
    FName Pelvis;
    TArray<FName> SpineChain;
    TArray<FName> NeckChain;
    FName Head;

    FMediaPipeLimbChain LeftArm;
    FMediaPipeLimbChain RightArm;
    FMediaPipeLimbChain LeftLeg;
    FMediaPipeLimbChain RightLeg;

    TArray<FName> LeftArmTwistBones;
    TArray<FName> RightArmTwistBones;
    TArray<FName> CorrectiveBones;
    TArray<FMediaPipeFingerChain> Fingers;
};
```

3. Add adapter assets:

```text
DA_MediaPipe_GenericHumanoidAdapter
DA_MediaPipe_MannyAdapter
DA_MediaPipe_MetaHumanAdapter
```

4. Move hardcoded MetaHuman helper names into:

```text
DA_MediaPipe_MetaHumanAdapter
```

or:

```text
FMediaPipeMetaHumanPoseAdapter
```

5. Convert `FMediaPipeAvatarEmbodimentProfile` into an immutable runtime snapshot.

6. Add an authoring asset:

```cpp
UMediaPipeAvatarEmbodimentProfileAsset
```

7. Validate adapter data against the runtime `USkeleton` / `USkeletalMesh` and report missing optional/required bones.

Because the GitHub repository intentionally excludes Content assets, the runtime must not assume a single skeleton asset is present. Validation needs to happen against whatever mesh/skeleton is actually assigned in the project.

---

## Phase 4: Slim down `MediaPipePoseDrivenAnimInstance`

Target final evaluation shape:

```cpp
void FAnimNode_MediaPipePoseDriven::EvaluateSkeletalControl_AnyThread(
    FComponentSpacePoseContext& Context,
    TArray<FBoneTransform>& OutBoneTransforms)
{
    FMediaPipeEmbodimentPipelineInput Input;
    Input.SourceFrame = CachedSourceFrame;
    Input.Profile = RuntimeProfile;
    Input.DeltaSeconds = Context.GetDeltaTime();
    Input.PendingReset = PendingReset;

    FMediaPipeEmbodimentPipelineOutput Output;
    Pipeline.Evaluate(Input, PipelineState, Output);

    FMediaPipeBoneWriteContext WriteContext(Context, OutBoneTransforms);
    SkeletonAdapter->WritePose(Output.FusedPose, Output.LimbPose, WriteContext);
}
```

Target `PreUpdate` shape:

```cpp
void FAnimNode_MediaPipePoseDriven::PreUpdate(const UAnimInstance* InAnimInstance)
{
    CachedSourceFrame = SourceComponent->GetLatestFrame_GameThread();
    RuntimeProfile = ProfileResolver->GetRuntimeProfile_GameThread();
    PendingReset = ResetRouter->ConsumeResetRequestsFor(TargetAvatarId);
}
```

Remove from the anim node:

- `BuildBodyFusionSourceFrame_GameThread`;
- direct HMD reads;
- direct Quest hand reads;
- MetaHuman target resolver;
- debug console command state;
- capture/replay;
- hardcoded helper bone arrays;
- most reference-pose fields;
- most solver state.

---

## Phase 5: Diagnostics/debug cleanup

Move console/debug code into:

```text
MediaPipeEmbodimentDebugCommands.cpp
MediaPipeQuestCaptureReplayService.cpp
MediaPipeEmbodimentDiagnosticsSubsystem.cpp
```

Replace global debug maps/counters with per-avatar diagnostic IDs.

Debug CVars become read-only presentation controls:

```text
show logs
show overlay
dump solve frame
capture/replay source frame
```

Any CVar that changes runtime solve behavior must become a real profile/policy field and get an invariant test.

Diagnostics should consume:

```cpp
struct FMediaPipeEmbodimentDiagnosticsFrame
{
    FMediaPipeTrackingSourceSummary Sources;
    FMediaPipeAuthoritySummary Authority;
    FMediaPipeCalibrationSummary Calibration;
    FMediaPipePoseErrorSummary Errors;
};
```

---

## Phase 6: Delete obsolete paths

After parity and invariant tests are green, delete or retire:

```text
MediaPipePoseDrivenAnimInstance_* .inl files
FMediaPipeAvatarPoseWriter from BodyFusion
hardcoded MetaHuman helper arrays in anim cpp
static/global reset/debug maps in anim cpp
direct OpenXR hand read in anim cpp
legacy MetaHuman arm path, or isolate and deprecate it in adapter code
```

Keep compatibility shims briefly, but mark them deprecated in comments and remove them once adapter parity is confirmed.

---

# 7. Required Invariants to Test

## 7.1 HMD owns head/eye

```text
Given:
- fresh HMD pose
- fresh MediaPipe pose landmarks
- any avatar profile

Assert:
- Eye owner == HMD
- Head owner == HMD
- MediaPipe head landmarks do not change head transform
- HMD yaw/pitch/roll propagate to head adapter write
```

Purpose:

- prevents MediaPipe head authority from causing neck stretching;
- prevents accidental chest/head pivoting;
- keeps VR embodiment anchored to the HMD.

---

## 7.2 Chest follows calibrated profile behavior

```text
Given:
- fixed calibration
- fixed profile chest/neck/head offsets
- fixed HMD
- fixed pelvis

Assert:
- chest position equals calibrated profile solve within tolerance
- UpperBodyFollowAlpha affects only the documented blend
- auto-calibration produces deterministic alpha from the same input
```

Purpose:

- prevents chest drift;
- keeps calibration behavior visible and testable;
- avoids profile-specific branches in generic solver code.

---

## 7.3 Pelvis does not chase HMD unless explicitly allowed

```text
Given:
- embodied hips-only authority
- reliable MediaPipe hip landmarks
- fixed MediaPipe pelvis/hips
- HMD A and HMD B differ only in planar X/Y

Assert:
- Pelvis planar X/Y is equal for A and B
- unless PelvisAuthorityMode == FollowUpperBodyExplicit
```

Purpose:

- prevents pelvis drift;
- prevents HMD translation side effects;
- protects against head/chest/pelvis coupling bugs.

---

## 7.4 Debug CVars never activate runtime behavior

```text
Given:
- same source frame
- same profile
- same previous state

When:
- debug logging, overlay, capture, or replay CVars are toggled

Assert:
- fused semantic pose is transform-equivalent
- authority result is unchanged
- calibration result is unchanged
```

Purpose:

- prevents debug side effects;
- makes diagnostics safe to enable in runtime builds;
- forces behavior changes into profile/policy data.

---

## 7.5 MetaHuman and generic humanoids share the same solver path

```text
Given:
- identical source frame
- equivalent semantic runtime profile
- generic adapter
- MetaHuman adapter

Assert:
- FMediaPipeFusedAvatarPose is the same before writing
- differences appear only in adapter write plan / helper / corrective bone writes
```

Purpose:

- stops BodyFusion from becoming MetaHuman-aware;
- makes Manny, MetaHuman, and generic humanoids use one semantic embodiment solve;
- keeps skeleton differences localized.

---

## 7.6 No hidden skeleton assumptions

```text
Given:
- adapter missing optional twist/helper/corrective bones

Assert:
- solver output is unchanged
- adapter emits diagnostic warning
- no invalid bone write occurs
```

Purpose:

- supports skeletons without full MetaHuman/Manny bone sets;
- prevents missing optional bones from crashing or corrupting pose output.

---

## 7.7 Neck stretch bound

```text
Given:
- HMD moves up/down/forward
- calibrated chest/head/neck lengths

Assert:
- head-to-neck and neck-to-chest distances stay within profile min/max
- pelvis does not translate as compensation unless explicit pelvis mode allows it
```

Purpose:

- catches neck stretching early;
- prevents hidden compensatory pelvis/chest translation.

---

## 7.8 Quest freshness authority

```text
Given:
- stale Quest hand
- fresh MediaPipe arm
- previous reliable Quest wrist

Assert:
- wrist/hand authority follows explicit policy
- no stale Quest transform is treated as live
- hold/fallback behavior is flagged in diagnostics
```

Purpose:

- prevents stale Quest samples from silently driving hands;
- makes dropout behavior testable.

---

# 8. Minimal First Implementation Step

The first high-value, low-risk step is:

## Add the pelvis authority invariant test and gate `PelvisFollowDelta`

Create:

```text
Source/MediaPipeDriver/MediaPipeBodyFusionInvariantsTests.cpp
```

Add:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMediaPipeBodyFusionPelvisDoesNotFollowHmdPlanarTest,
    "MediaPipe.BodyFusion.Invariants.PelvisDoesNotFollowHmdPlanar",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
```

Test shape:

```cpp
// Build profile with embodied hips-only authority.
// Build reliable MediaPipe hip/pelvis landmarks.
// Build fresh HMD pose A.
// Solve -> PoseA.
// Copy input; move only HMD planar X/Y.
// Solve -> PoseB.
// Assert PoseA.PelvisWorld.XY == PoseB.PelvisWorld.XY.
```

Then add:

```cpp
enum class EMediaPipePelvisAuthorityMode : uint8
{
    ProfileLocked,
    MediaPipeHipsVerticalOnly,
    MediaPipeHipsFull,
    FollowUpperBodyExplicit
};
```

Default embodied hips-only to:

```cpp
EMediaPipePelvisAuthorityMode::MediaPipeHipsVerticalOnly
```

Finally, gate the current chest-derived pelvis-follow block:

```cpp
if (Input.Profile.PelvisAuthorityMode == EMediaPipePelvisAuthorityMode::FollowUpperBodyExplicit)
{
    PelvisWorld += PelvisFollowDelta;
}
```

This creates immediate protection against pelvis drift and HMD side effects without requiring the full animation-node rewrite.

---

# 9. Practical Implementation Notes

## 9.1 Suggested commit sequence

1. Add invariant tests around current BodyFusion behavior.
2. Introduce `MediaPipeTrackingSourceTypes.h` and move source frame types.
3. Introduce `EMediaPipePelvisAuthorityMode` and gate pelvis follow.
4. Extract authority policy into `MediaPipeBodyFusionAuthorityPolicy.*`.
5. Extract calibration solver into `MediaPipeEmbodimentCalibrationSolver.*`.
6. Extract Quest hand source types from the anim instance.
7. Remove `MediaPipeQuestFingerSolver` dependency on `MediaPipePoseDrivenAnimInstance.h`.
8. Add skeleton adapter data asset types.
9. Add generic adapter writer while keeping current anim path alive.
10. Add MetaHuman adapter writer and move helper/corrective bone names.
11. Replace direct anim-node bone-writing paths with adapter calls.
12. Move debug commands into diagnostics/debug files.
13. Delete obsolete `.inl` and legacy paths.

---

## 9.2 Suggested folder organization inside `Source/MediaPipeDriver`

Initially, avoid Unreal module splitting. Use folders/includes first:

```text
Source/MediaPipeDriver/Public/
    Embodiment/
        MediaPipeTrackingSourceTypes.h
        MediaPipeEmbodimentPipeline.h
        MediaPipeFusedAvatarPose.h
        MediaPipeAvatarRuntimeProfile.h
    Profiles/
        MediaPipeAvatarEmbodimentProfileAsset.h
        MediaPipeSkeletonAdapterDataAsset.h
        MediaPipeMetaHumanProfile.h
    Solvers/
        MediaPipeBodySolverMath.h
        MediaPipeBodyFusionAuthorityPolicy.h
        MediaPipeEmbodimentCalibrationSolver.h
        MediaPipeTorsoFusionSolver.h
        MediaPipePelvisFusionSolver.h
        MediaPipeQuestWristSolver.h
        MediaPipeQuestFingerPoseSolver.h
    Adapters/
        MediaPipeSkeletonPoseAdapter.h
        MediaPipeGenericHumanoidPoseAdapter.h
        MediaPipeMannyPoseAdapter.h
        MediaPipeMetaHumanPoseAdapter.h
    Diagnostics/
        MediaPipeEmbodimentDiagnostics.h

Source/MediaPipeDriver/Private/
    Embodiment/
    Profiles/
    Solvers/
    Adapters/
    Diagnostics/
    Anim/
```

Only split Unreal modules later if include hygiene is clean.

---

## 9.3 Ownership boundary cheat sheet

### Use `UObject` / `UDataAsset` for

- authorable profile assets;
- skeleton adapter data assets;
- editor-visible validation settings;
- config/settings objects;
- subsystems/components that need reflection/lifetime management.

### Use `UActorComponent` for

- game-thread source acquisition;
- avatar instance diagnostics;
- component-level reset routing;
- runtime bridge between actor/avatar and anim instance.

### Use `UAnimInstance` / anim node for

- animation lifecycle;
- source/profile snapshot consumption;
- pipeline call;
- adapter write call;
- Blueprint integration.

### Use `USTRUCT` for

- source frames;
- runtime profiles;
- solver inputs/results;
- diagnostics frames;
- semantic pose types;
- adapter bone maps.

### Use pure C++ helper classes for

- BodyFusion authority;
- calibration;
- torso/pelvis/spine/limb solving;
- Quest wrist/hand/finger logic;
- transform math;
- deterministic policy evaluation.

---

# 10. Things Not To Do

Do not split into many Unreal modules first. Clean the dependencies inside `MediaPipeDriver` before introducing module boundaries.

Do not put solvers into `UObject` classes. Solver code should be plain structs/functions with explicit state.

Do not add new CVars to select runtime behavior. Runtime behavior belongs in profile/policy data and must be testable.

Do not special-case MetaHuman inside BodyFusion. MetaHuman belongs in profile data and skeleton adapter/writer code.

Do not hardcode `spine_03` or `spine_05` as “the chest” in solver code. The adapter should map semantic chest to the target skeleton’s actual hierarchy.

Do not use visual screenshots or PIE as the primary test strategy. Use deterministic solver tests first, then a smaller number of integration/visual tests.

Do not let local-view hiding alter the fused body pose. Local-view behavior is a write/render policy, not a solver policy.

Do not keep Quest finger solver dependent on the anim instance. Move the shared hand snapshot/types out of `MediaPipePoseDrivenAnimInstance.h`.

Do not delete old paths before invariant tests and adapter parity tests are green. Mark them deprecated, isolate them, and then remove them in Phase 6.

Do not make BodyFusion output bone names. BodyFusion should output semantic pose data only.

Do not allow debug/reset commands to operate through hidden globals when a specific avatar/component target can be used.

---

# 11. Definition of Done

The refactor is successful when all of the following are true:

1. `FAnimNode_MediaPipePoseDriven` is primarily an animation bridge, not the embodiment system.
2. BodyFusion produces a semantic fused avatar pose and does not write bones.
3. MetaHuman, Manny, and generic humanoids use the same semantic solver path.
4. Skeleton-specific differences live in adapter/profile data.
5. Quest source ingestion is outside the anim node.
6. Quest hand/finger/wrist solvers do not include the anim instance header.
7. Debug CVars cannot change runtime solve behavior.
8. Pelvis follow behavior is explicit profile policy.
9. HMD head/eye ownership is protected by tests.
10. Neck/chest/pelvis invariants are covered by deterministic tests.
11. Missing optional skeleton bones are warnings/no-ops, not crashes or silent corruption.
12. Most solver tests run without PIE, screenshots, or Content assets.

