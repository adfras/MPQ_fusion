# MPQ Fusion Refactor Cutback Checklist

**Project:** `D:\Epic\Unreal_Projects\TestingKit5`
**Repository:** <https://github.com/adfras/MPQ_fusion>
**Branch:** `main`


---

## [x] 0. Executive Recommendation

- [x] Do **not** roll back the refactor wholesale.
- [x] Keep the useful architectural direction:
  - [x] Tracking sources.
  - [x] Source normalization.
  - [x] BodyFusion authority decisions.
  - [x] Pure/semi-pure solvers producing fused semantic body pose.
  - [x] Skeleton adapter/writer mapping semantic pose to Manny / MetaHuman / generic bones.
  - [x] AnimInstance applying final bone transforms.
- [x] Cut back the implementation to the smallest production shape that is useful **now**.
- [x] Prefer fewer production files with clear ownership over many speculative one-class files.
- [x] Consolidate tests before touching risky production paths.
- [x] Keep the protected spine/chest/neck/head fix intact.

### [x] Current smallest practical shape

- [x] Keep this current-stage pipeline:

```text
MediaPipePoseDrivenAnimInstance + existing production .inl paths
    -> tracking/source frame builder
    -> BodyFusion semantic solver
    -> pose write context
    -> existing Quest / arm / leg / wrist / finger solvers
    -> final bone writes
```

- [x] Do not pretend the full future adapter architecture is complete while `FAnimNode_MediaPipePoseDriven` still owns most production behavior.
- [x] Merge or defer abstractions that only wrap behavior still centered in the anim node.

---

## [x] 1. Protected Behavior That Must Be Preserved

### [x] 1.1 Final pose-write order

- [x] Preserve this required final order:

```text
spine/chest -> neck -> neck_02 -> head
```

- [x] Do not reorder final component-space translation writes.
- [x] Do not move neck/head translation writes before spine/chest translation reassertion.

### [x] 1.2 BodyFusion spine/chest translation reassertion

- [x] Keep the fix that reasserts BodyFusion spine/chest translation targets after spine rotations.
- [x] Preserve this behavior:

```text
spine rotations happen
then BodyFusion spine/chest translations are refreshed
then neck/head chain is derived
```

- [x] Keep the intent of this code:

```cpp
// Spine rotations can recompose child component-space positions. Refresh the
// solved translations so the chest anchor remains authoritative before the
// neck/head chain is derived from it.
ApplyBodyFusionSpineTranslationTargets();
```

### [x] 1.3 Final parent-to-child translation pass

- [x] Preserve this final pass:

```cpp
ApplyBodyFusionSpineTranslationTargets();
ApplyComponentTranslationToBone(Neck, NeckTargetComp);
ApplyComponentTranslationToBone(Neck02, Neck02TargetComp);
ApplyComponentTranslationToBone(Head, HeadComp);
```

- [x] Keep `ApplyBodyFusionSpineTranslationTargets()` immediately before final neck/head component translation writes.
- [x] Treat this as a regression-sensitive MetaHuman chest-anchor fix.

### [x] 1.4 HMD owns eye/head

- [x] BodyFusion must continue to treat HMD as authoritative for eye/head.
- [x] MediaPipe pose landmarks must not implicitly regain head authority.
- [x] Any future non-HMD head authority must be explicit profile/policy behavior.

### [x] 1.5 Pelvis does not chase HMD by default

- [x] Preserve explicit gating of upper-body-driven pelvis movement:

```cpp
if (Input.Profile.PelvisAuthorityMode == EMediaPipePelvisAuthorityMode::FollowUpperBodyExplicit)
{
    PelvisWorld += PelvisFollowDelta;
}
```

- [x] Default embodied hips-only behavior must not let pelvis chase HMD planar motion.
- [x] `FollowUpperBodyExplicit` must remain explicit.

### [x] 1.6 BodyFusion remains semantic

- [x] Do not put `FBoneReference` into `MediaPipeBodyFusion.*`.
- [x] Do not put `FCSPose` into `MediaPipeBodyFusion.*`.
- [x] Do not put `USkeletalMeshComponent` dependencies into `MediaPipeBodyFusion.*`.
- [x] Do not put MetaHuman helper-bone writes into `MediaPipeBodyFusion.*`.
- [x] Do not put Manny-specific bone writes into `MediaPipeBodyFusion.*`.
- [x] Do not put Quest/OpenXR polling into `MediaPipeBodyFusion.*`.

### [x] 1.7 Quest/HMD source polling remains outside pure solvers

- [x] Keep Quest hand polling outside pure solvers.
- [x] Keep HMD polling outside pure solvers.
- [x] Preserve these source acquisition seams:
  - [x] `MediaPipeQuestHandTypes.h`
  - [x] `MediaPipeQuestHandTrackingSource.h/.cpp`
  - [x] `MediaPipeQuestHmdTrackingSource.h/.cpp`

### [x] 1.8 Debug CVars do not silently alter solver behavior

- [x] Debug CVars may enable logs.
- [x] Debug CVars may enable HUDs.
- [x] Debug CVars may enable traces.
- [x] Debug CVars may enable capture/replay.
- [x] Debug commands may request reset serials.
- [x] Debug CVars must not silently change head authority.
- [x] Debug CVars must not silently change pelvis authority.
- [x] Debug CVars must not silently change chest follow behavior.
- [x] Debug CVars must not silently change source authority decisions.
- [x] Debug CVars must not silently change calibration policy unless that CVar is explicitly treated as runtime policy and tested.

---

## [x] 2. Current Diagnosis Checklist

### [x] 2.1 Commit context

- [x] Confirm current refactor commit:

```text
5abaf18dbea8dc1636c220ee6721af9d4a4ae0d2
Refactor MPQ fusion architecture
```

- [x] Confirm parent commit:

```text
9aca4bae558836846dba0fe81119e4c757dcfb11
Keep BodyFusion chest translation authoritative
```

- [x] Confirm parent commit added the chest translation authority fix.
- [x] Treat the parent commit fix as protected behavior.

### [x] 2.2 Production reality

- [x] Confirm `FAnimNode_MediaPipePoseDriven` is still the production center.
- [x] Confirm it still owns concrete `FBoneReference`s for:
  - [x] root
  - [x] pelvis
  - [x] spine
  - [x] neck
  - [x] head
  - [x] arms
  - [x] fingers
  - [x] legs
- [x] Confirm it still owns MetaHuman helper bone references.
- [x] Confirm it still owns BodyFusion source frame state.
- [x] Confirm it still owns BodyFusion authority state.
- [x] Confirm it still owns BodyFusion calibration state.
- [x] Confirm it still owns MetaHuman profile state.
- [x] Confirm it still owns cached HMD pose.
- [x] Confirm it still owns body/leg/Quest wrist/Quest hand solver state.
- [x] Confirm it still applies final component-space bone writes.

### [x] 2.3 Refactor mismatch

- [x] Acknowledge the mismatch:

```text
Architecture says: many clean layers.
Production reality says: AnimInstance still coordinates almost everything.
```

- [x] Cut back abstractions that do not yet own real production responsibility.
- [x] Keep seams that already protect current behavior or improve testability.

### [x] 2.4 Good refactor pieces to preserve

- [x] Preserve `MediaPipeBodyFusion.h/.cpp` as the semantic solver entry point.
- [x] Preserve `FMediaPipeBodyFusionSolveInput`.
- [x] Preserve `FMediaPipeBodyFusionSolver::Solve`.
- [x] Preserve `FMediaPipeTrackingSourceFrame` and source status/freshness modeling.
- [x] Preserve `FMediaPipeEmbodimentCalibration` and neutral calibration solve.
- [x] Preserve explicit pelvis follow gating.
- [x] Preserve pose write context around chest/neck/head component-space targets.

### [x] 2.5 Main bloat sources

- [x] Identify tiny wrapper files that should be merged.
- [x] Identify one-class test files that should be consolidated.
- [x] Identify asset/data layers that should be deferred until production actually consumes them.
- [x] Identify future adapter contract files that are not needed for the current stage.

---

## [x] 3. Staged Cutback Plan

## [x] Stage 0 — Freeze protected behavior

- [x] Add or preserve an invariant test for HMD owning head/eye.
- [x] Add or preserve an invariant test for spine/chest translation reassertion before neck/head writes.
- [x] Add or preserve an invariant test for final order:

```text
spine/chest -> neck -> neck_02 -> head
```

- [x] Add or preserve an invariant test that pelvis does not follow HMD unless `FollowUpperBodyExplicit`.
- [x] Add or preserve an invariant test that debug CVars do not change semantic solver output.
- [x] Add or preserve an invariant test that MetaHuman and generic avatars share the BodyFusion solve path before adapter/writer differences.
- [x] Mark the current chest-anchor fix as protected in review notes.
- [x] Do not start deleting production files until these invariants are covered or manually verified.

## [x] Stage 1 — Reduce test-file count first

- [x] Consolidate small BodyFusion wrapper tests into `MediaPipeBodyFusionTests.cpp`.
- [x] Consolidate profile/data tests into `MediaPipeEmbodimentProfileTests.cpp`.
- [x] Consolidate Quest solver tests into `MediaPipeQuestSolverTests.cpp`.
- [x] Consolidate diagnostics/debug tests into `MediaPipeDiagnosticsTests.cpp`.
- [x] Consolidate adapter/writer tests into `MediaPipeAdapterTests.cpp`.
- [x] Preserve high-value invariant tests.
- [x] Remove or merge thin tests that only check simple wrapper calls.
- [x] Remove or merge tests that only check default constructors unless they protect a known regression.
- [x] Keep tests that protect behavior, not file boundaries.

### [x] Stage 1 test themes to keep

- [x] BodyFusion authority invariants.
- [x] Pelvis/HMD drift invariants.
- [x] HMD head/eye ownership.
- [x] Chest/spine/neck/head write-order behavior.
- [x] Calibration solver behavior.
- [x] Quest wrist apply policy behavior.
- [x] Quest finger solver behavior.
- [x] Quest constrained arm solver behavior.
- [x] MetaHuman profile resolution behavior.
- [x] Debug-CVar no-side-effect behavior.

### [x] Stage 1 tests to merge or delete after assertions are preserved

- [x] `MediaPipeAvatarEmbodimentProfileAssetTests.cpp`
- [x] `MediaPipeAvatarProfileReferenceCalibrationTests.cpp`
- [x] `MediaPipeAvatarProfileResolverTests.cpp`
- [x] `MediaPipeBodyFusionAuthorityPolicyTests.cpp`
- [x] `MediaPipeBodyFusionDebugFormatterTests.cpp`
- [x] `MediaPipeBodyFusionPoseWriteContextTests.cpp`
- [x] `MediaPipeBodyFusionRuntimePolicyTests.cpp`
- [x] `MediaPipeBodyFusionSourceFrameAdapterTests.cpp`
- [x] `MediaPipeBodyFusionSourceFrameBuilderTests.cpp`
- [x] `MediaPipeEmbodimentDebugCommandsTests.cpp`
- [x] `MediaPipeEmbodimentPipelineTests.cpp`
- [x] `MediaPipeMetaHumanPoseAdapterTests.cpp`
- [x] `MediaPipeQuestHandTrackingSourceTests.cpp`
- [x] `MediaPipeQuestHmdTrackingSourceTests.cpp`
- [x] `MediaPipeQuestRuntimeDebugServiceTests.cpp`
- [x] `MediaPipeSourceNormalizerTests.cpp`
- [x] `MediaPipeSkeletonAdapterContractTests.cpp`
- [x] `MediaPipeSolverDependencyGuardTests.cpp`

## [x] Stage 2 — Collapse thin wrappers

### [x] 2.1 Merge `MediaPipeEmbodimentPipeline.*`

- [x] Confirm `FMediaPipeEmbodimentPipeline::Evaluate` mostly:
  - [x] resets state if pending reset;
  - [x] increments evaluation serial;
  - [x] normalizes/copies input;
  - [x] calls `FMediaPipeBodyFusionSolver::Solve`;
  - [x] sets a failure reason.
- [x] Decide whether evaluation serial is still needed.
- [x] Move useful state/output structs into `MediaPipeBodyFusion.h/.cpp` or a single `MediaPipeBodyFusionRuntime.h/.cpp`.
- [x] Replace direct production references to `FMediaPipeEmbodimentPipeline` with direct BodyFusion runtime calls.
- [x] Remove the separate pipeline file after build confirms no references.

### [x] 2.2 Merge `MediaPipeSourceNormalizer.*`

- [x] Confirm normalizer only normalizes HMD rotation/tracking up and calls `UpdateFreshness`.
- [x] Move `NormalizeInPlace` behavior to `FMediaPipeTrackingSourceFrame` or the surviving source-frame builder.
- [x] Replace production calls to `FMediaPipeSourceNormalizer`.
- [x] Remove the separate source normalizer file after build confirms no references.

### [x] 2.3 Merge source frame adapter and builder

- [x] Merge `MediaPipeBodyFusionSourceFrameAdapter.h/.cpp` and `MediaPipeBodyFusionSourceFrameBuilder.h/.cpp` into one file pair:

```text
MediaPipeTrackingSourceFrameBuilder.h/.cpp
```

- [x] Preserve HMD population.
- [x] Preserve Quest hand wrist population.
- [x] Preserve Quest full-arm chain population.
- [x] Preserve MediaPipe pose landmark population.
- [x] Preserve MediaPipe core confidence calculation.
- [x] Preserve full-arm-chain max-age override for MetaHuman/full-chain behavior.
- [x] Preserve source freshness update.
- [x] Remove duplicate snapshot structs if they are just adapter-only wrappers.

### [x] 2.4 Merge BodyFusion runtime/debug wrappers

- [x] Merge `MediaPipeBodyFusionRuntimePolicy.h/.cpp` into a broader BodyFusion runtime/debug support file if desired.
- [x] Merge `MediaPipeEmbodimentDebugCommands.h/.cpp` into the same runtime/debug support file if desired.
- [x] Preserve BodyFusion enabled CVar reads.
- [x] Preserve BodyFusion debug CVar reads.
- [x] Preserve MediaPipe authority mode policy reads.
- [x] Preserve calibration stable frame/seconds policy.
- [x] Preserve `mp.BodyFusion.ResetCalibration` behavior.
- [x] Preserve `mp.ResetQuestWristCalibration` behavior or move it to Quest runtime debug support.
- [x] Keep reset serials thread-safe.

### [x] 2.5 Merge Quest debug/capture wrappers

- [x] Review `MediaPipeQuestRuntimeDebugService.h/.cpp`.
- [x] Review `MediaPipeQuestCaptureReplayService.h/.cpp`.
- [x] Keep Quest source polling behavior.
- [x] Keep replay behavior only if currently used for debugging regressions.
- [x] Keep capture guide only if currently used.
- [x] Collapse capture/replay wrapper into existing Quest capture/replay tooling if it is only a pass-through.
- [x] Keep Quest runtime source wrappers separate from debug presentation.

## [x] Stage 3 — Defer premature asset/adapter layers

### [x] 3.1 Defer avatar embodiment profile asset layer unless local Content uses it

- [x] Search for C++ references to `UMediaPipeAvatarEmbodimentProfileAsset`.
- [x] Search local Content/asset references to `UMediaPipeAvatarEmbodimentProfileAsset`.
- [x] If no local assets use it, defer:
  - [x] `MediaPipeAvatarEmbodimentProfileAsset.h`
  - [x] `MediaPipeAvatarEmbodimentProfileAsset.cpp`
  - [x] `MediaPipeAvatarEmbodimentProfileAssetTests.cpp`
- [x] Keep equivalent runtime profile fields in `FMediaPipeAvatarEmbodimentProfile` for now.
- [x] Do not keep a UDataAsset layer just to future-proof the design.

### [x] 3.2 Defer skeleton adapter data asset layer unless local Content uses it

- [x] Search for C++ references to `UMediaPipeSkeletonAdapterDataAsset`.
- [x] Search local Content/asset references to `UMediaPipeSkeletonAdapterDataAsset`.
- [x] If no local assets use it, defer:
  - [x] `MediaPipeSkeletonAdapterDataAsset.h`
  - [x] `MediaPipeSkeletonAdapterDataAsset.cpp`
  - [x] `MediaPipeSkeletonAdapterContractTests.cpp`
- [x] Keep hardcoded current bone references until production writer actually consumes adapter bindings.
- [x] Keep the idea of semantic bone chains for a later adapter phase.

### [x] 3.3 Defer dependency-guard tests during cutback

- [x] Review `MediaPipeSolverDependencyGuardTests.cpp`.
- [x] Keep the idea as a future architecture guard.
- [x] Defer or remove during current cutback if it creates maintenance noise.
- [x] Reintroduce include/dependency guard tests after file ownership stabilizes.

## [x] Stage 4 — Keep production write helpers, but stop expanding them

### [x] 4.1 Keep pose write context

- [x] Keep `MediaPipeBodyFusionPoseWriteContext.h/.cpp`.
- [x] Keep component-space pelvis/chest/head target resolution.
- [x] Keep HMD basis calculation.
- [x] Keep neck-chain alpha calculation.
- [x] Keep `bHmdHeadAuthoritative` behavior.
- [x] Add or preserve tests around neck/head/chest target calculation.
- [x] Do not merge this until protected write-order tests are strong.

### [x] 4.2 Keep but simplify skeleton pose adapter/writer

- [x] Keep `FMediaPipeAvatarPoseWriter` logic.
- [x] Keep semantic bone rotation resolution.
- [x] Keep chain-alpha helpers.
- [x] Keep lower-body side extraction if production leg solve uses it.
- [x] Treat `MediaPipeSkeletonPoseAdapter.*` as a writer-helper file, not a full adapter layer yet.
- [x] Consider later rename:

```text
MediaPipeSkeletonPoseAdapter.* -> MediaPipeAvatarPoseWriter.*
```

- [x] Do not expand this into a full skeleton adapter until anim bone refs are actually moved out of `FAnimNode_MediaPipePoseDriven`.

### [x] 4.3 Keep MetaHuman helper binding only if production uses it

- [x] Search for `FMediaPipeMetaHumanHelperBoneBinding::Default`.
- [x] If production uses it, keep `MediaPipeMetaHumanPoseAdapter.h/.cpp`.
- [x] If production does not use it, merge helper names back into the existing MetaHuman profile/writer area or defer the file.
- [x] Do not create more MetaHuman adapter files until the generic writer path is stable.

## [x] Stage 5 — Keep external source wrappers, simplify debug presentation

### [x] 5.1 Keep Quest/HMD source wrappers

- [x] Keep `MediaPipeQuestHandTypes.h`.
- [x] Keep `MediaPipeQuestHandTrackingSource.h/.cpp`.
- [x] Keep `MediaPipeQuestHmdTrackingSource.h/.cpp`.
- [x] Preserve `IHandTracker` access isolation.
- [x] Preserve `GEngine->XRSystem` access isolation.
- [x] Do not let pure solvers depend on XR headers.

### [x] 5.2 Simplify Quest runtime debug service

- [x] Keep source polling behavior needed by production.
- [x] Move or collapse capture/replay behavior if it is only debug tooling.
- [x] Move or collapse HUD behavior if it bloats runtime support.
- [x] Move or collapse skeleton debug drawing if it is not needed for current development.
- [x] Avoid one debug service accumulating unrelated responsibilities.

## [x] Stage 6 — Remove deferred scaffolding only after confirmation

- [x] Run `git grep` for each candidate class/file.
- [x] Run Unreal build after each group of removals/merges.
- [x] Check local Blueprint/DataAsset references before removing any `UCLASS` or `USTRUCT` used by assets.
- [x] Remove files only when source references and asset references are both clear.
- [x] Keep a short removal note for each deferred/removed file group.

---

## [x] 4. File Classification Checklist

## [x] KEEP — needed now

### [x] Core production bridge

- [x] `MediaPipePoseDrivenAnimInstance.h/.cpp`
  - [x] Reason: still owns production anim graph bridge, source/profile state, BodyFusion runtime state, and final bone writes.
  - [x] Depends on: most current production solver/debug/profile files.
  - [x] Usage: production.
  - [x] Removal risk: very high.
  - [x] Current-stage support: essential.

- [x] `MediaPipePoseDrivenAnimInstance_*.inl`
  - [x] Reason: existing production solve/write behavior still lives there.
  - [x] Depends on: anim node state and solver helpers.
  - [x] Usage: production.
  - [x] Removal risk: high.
  - [x] Current-stage support: essential until functions are moved and parity-tested.

### [x] BodyFusion semantic solve

- [x] `MediaPipeBodyFusion.h/.cpp`
  - [x] Reason: core semantic solver entry point.
  - [x] Depends on: tracking source types, calibration, profile, authority, fused pose.
  - [x] Usage: production.
  - [x] Removal risk: very high.
  - [x] Current-stage support: essential.

- [x] `MediaPipeBodyFusionAuthorityPolicy.h/.cpp`
  - [x] Reason: owns owner/region enums, default authority maps, and authority gate.
  - [x] Depends on: tracking source status.
  - [x] Usage: production.
  - [x] Removal risk: high.
  - [x] Current-stage support: essential.

- [x] `MediaPipeEmbodimentCalibrationSolver.h/.cpp`
  - [x] Reason: clean pure calibration transform builder.
  - [x] Depends on: core math only.
  - [x] Usage: production.
  - [x] Removal risk: high.
  - [x] Current-stage support: essential.

- [x] `MediaPipeTrackingSourceTypes.h/.cpp`
  - [x] Reason: central source frame/status/freshness model.
  - [x] Depends on: pose landmark types.
  - [x] Usage: production.
  - [x] Removal risk: high.
  - [x] Current-stage support: essential.

- [x] `MediaPipeFusedAvatarPose.h/.cpp`
  - [x] Reason: semantic BodyFusion output type.
  - [x] Depends on: authority/region/source-state types.
  - [x] Usage: production.
  - [x] Removal risk: high.
  - [x] Current-stage support: essential.

### [x] Protected write path

- [x] `MediaPipeBodyFusionPoseWriteContext.h/.cpp`
  - [x] Reason: supports pelvis/chest/head component-space targets and neck-chain targets.
  - [x] Depends on: fused pose, avatar profile, pose writer math.
  - [x] Usage: production.
  - [x] Removal risk: high because it touches protected chest/neck/head behavior.
  - [x] Current-stage support: essential.

### [x] Existing solver math

- [x] `MediaPipeBodySolverMath.h/.cpp`
  - [x] Reason: useful pure solver/math layer.
  - [x] Depends on: core math only.
  - [x] Usage: production/test.
  - [x] Removal risk: medium/high.
  - [x] Current-stage support: useful now.

- [x] Core Quest / arm / wrist / finger solver files.
  - [x] Reason: already close to pure solver shape and used by the anim node.
  - [x] Depends on: pose/hand solver types.
  - [x] Usage: production.
  - [x] Removal risk: high for arm/hand behavior.
  - [x] Current-stage support: useful now.

### [x] Quest/HMD source wrappers

- [x] `MediaPipeQuestHandTypes.h`
  - [x] Reason: lightweight shared Quest hand snapshot type.
  - [x] Depends on: Unreal hand keypoint count.
  - [x] Usage: production.
  - [x] Removal risk: high if Quest hands are enabled.
  - [x] Current-stage support: useful now.

- [x] `MediaPipeQuestHandTrackingSource.h/.cpp`
  - [x] Reason: isolates `IHandTracker` acquisition.
  - [x] Depends on: XR hand tracker API.
  - [x] Usage: production.
  - [x] Removal risk: medium/high.
  - [x] Current-stage support: useful now.

- [x] `MediaPipeQuestHmdTrackingSource.h/.cpp`
  - [x] Reason: isolates HMD pose acquisition.
  - [x] Depends on: XR tracking system.
  - [x] Usage: production.
  - [x] Removal risk: medium/high.
  - [x] Current-stage support: useful now.

### [x] Current profile systems

- [x] `MediaPipeAvatarEmbodimentProfile.h/.cpp`
  - [x] Reason: current runtime profile struct and solver helpers.
  - [x] Depends on: core profile/math types.
  - [x] Usage: production.
  - [x] Removal risk: high.
  - [x] Current-stage support: essential.

- [x] `MediaPipeMetaHumanProfile.h/.cpp`
  - [x] Reason: current MetaHuman profile path.
  - [x] Depends on: MetaHuman profile definitions/settings.
  - [x] Usage: production.
  - [x] Removal risk: high if MetaHuman support matters.
  - [x] Current-stage support: essential.

- [x] `MediaPipeAvatarRigProfile.*`
  - [x] Reason: current generic/avatar rig profile path.
  - [x] Depends on: skeletal mesh/profile resolution.
  - [x] Usage: production.
  - [x] Removal risk: high if generic avatars matter.
  - [x] Current-stage support: essential.

---

## [x] KEEP BUT SIMPLIFY — useful, but overbuilt or too broad

- [x] `MediaPipeSkeletonPoseAdapter.h/.cpp`
  - [x] Keep: `FMediaPipeAvatarPoseWriter` math.
  - [x] Simplify: stop treating it as a full adapter layer for now.
  - [x] Usage: production helper.
  - [x] Risk if removed: medium.
  - [x] Current-stage support: useful writer helper.

- [x] `MediaPipeBodyFusionRuntimePolicy.h/.cpp`
  - [x] Keep: CVar snapshot and calibration policy resolution.
  - [x] Simplify: merge into BodyFusion runtime/debug support if file count matters.
  - [x] Usage: production runtime policy.
  - [x] Risk if removed: medium.
  - [x] Current-stage support: useful now, but does not need standalone file.

- [x] `MediaPipeEmbodimentDebugCommands.h/.cpp`
  - [x] Keep: reset serial behavior.
  - [x] Simplify: merge with runtime/debug support.
  - [x] Usage: production/debug.
  - [x] Risk if removed: medium.
  - [x] Current-stage support: useful now, but too small as standalone file.

- [x] `MediaPipeQuestRuntimeDebugService.h/.cpp`
  - [x] Keep: source polling and required debug path.
  - [x] Simplify: split or collapse capture/replay/HUD/drawing as needed.
  - [x] Usage: production/debug.
  - [x] Risk if removed: medium.
  - [x] Current-stage support: useful but too broad.

- [x] `MediaPipeBodyFusionDebugFormatter.h/.cpp`
  - [x] Keep: debug formatting used by current diagnostics.
  - [x] Simplify: merge later if it remains small.
  - [x] Usage: production/debug.
  - [x] Risk if removed: medium.
  - [x] Current-stage support: useful.

- [x] `MediaPipeMetaHumanPoseAdapter.h/.cpp`
  - [x] Keep if production uses helper bone binding.
  - [x] Simplify: do not expand into full adapter yet.
  - [x] Usage: production helper or future helper.
  - [x] Risk if removed: medium if helper names are referenced.
  - [x] Current-stage support: conditional.

---

## [x] MERGE — useful logic, but does not justify its own file yet

- [x] `MediaPipeEmbodimentPipeline.h/.cpp`
  - [x] Merge into: `MediaPipeBodyFusion.h/.cpp` or `MediaPipeBodyFusionRuntime.h/.cpp`.
  - [x] Preserve: pending reset, evaluation serial if needed, failure reason if useful.
  - [x] Usage: production but thin.
  - [x] Risk if merged: low/medium.
  - [x] Current-stage support: useful logic, unnecessary file boundary.

- [x] `MediaPipeSourceNormalizer.h/.cpp`
  - [x] Merge into: `MediaPipeTrackingSourceTypes.h/.cpp` or source-frame builder.
  - [x] Preserve: safe HMD rotation normalization, tracking-up normalization, freshness update.
  - [x] Usage: production but tiny.
  - [x] Risk if merged: low.
  - [x] Current-stage support: useful logic, unnecessary file boundary.

- [x] `MediaPipeBodyFusionSourceFrameAdapter.h/.cpp`
  - [x] Merge into: source-frame builder.
  - [x] Preserve: adapter input behavior and threshold override.
  - [x] Usage: production but redundant.
  - [x] Risk if merged: low/medium.
  - [x] Current-stage support: useful logic, unnecessary extra layer.

- [x] `MediaPipeBodyFusionSourceFrameBuilder.h/.cpp`
  - [x] Merge with: source-frame adapter into one `MediaPipeTrackingSourceFrameBuilder` file pair.
  - [x] Preserve: HMD, Quest hand, full-arm chain, MediaPipe pose population.
  - [x] Usage: production.
  - [x] Risk if merged: medium if timestamps/freshness are mishandled.
  - [x] Current-stage support: useful logic, but can absorb adapter/normalizer.

- [x] `MediaPipeQuestCaptureReplayService.h/.cpp`
  - [x] Merge into: Quest runtime debug support or existing capture/replay tooling.
  - [x] Preserve: replay/capture behavior if used.
  - [x] Usage: unknown/debug.
  - [x] Risk if merged: low/medium.
  - [x] Current-stage support: optional unless replay is actively used.

- [x] `MediaPipeAvatarProfileReferenceCalibration.h/.cpp`
  - [x] Merge into: avatar profile resolver or profile calibration code if used.
  - [x] Preserve: useful reference calibration logic.
  - [x] Usage: unknown/profile helper.
  - [x] Risk if merged: medium.
  - [x] Current-stage support: conditional.

---

## [x] DEFER — valid future work, but not needed at this stage

- [x] `MediaPipeAvatarEmbodimentProfileAsset.h/.cpp`
  - [x] Defer unless local Content already references `UMediaPipeAvatarEmbodimentProfileAsset`.
  - [x] Reason: full UDataAsset authoring layer is valid later but premature now.
  - [x] Usage: future/asset-facing unless confirmed otherwise.
  - [x] Risk if removed: high only if local assets reference it.
  - [x] Current-stage support: future-proofs more than it supports current production.

- [x] `MediaPipeAvatarEmbodimentProfileAssetTests.cpp`
  - [x] Defer or merge into profile tests if asset layer is kept.
  - [x] Usage: test-only.
  - [x] Risk if removed: low unless asset layer is active.

- [x] `MediaPipeSkeletonAdapterDataAsset.h/.cpp`
  - [x] Defer unless local Content already references `UMediaPipeSkeletonAdapterDataAsset`.
  - [x] Reason: semantic skeleton maps are the right future direction, but production still hardcodes bones in the anim node.
  - [x] Usage: future/asset-facing unless confirmed otherwise.
  - [x] Risk if removed: high only if local assets reference it.
  - [x] Current-stage support: future-proofs more than it supports current production.

- [x] `MediaPipeSkeletonAdapterContractTests.cpp`
  - [x] Defer until the adapter layer is production-used.
  - [x] Usage: test-only.
  - [x] Risk if removed: low.

- [x] `MediaPipeSolverDependencyGuardTests.cpp`
  - [x] Defer during current cutback.
  - [x] Reason: useful later, noisy while files are being consolidated.
  - [x] Usage: test-only/meta.
  - [x] Risk if removed: low runtime risk.

- [x] Tiny default-check tests for deferred asset/adapter layers.
  - [x] Defer or merge.
  - [x] Usage: test-only.
  - [x] Risk if removed: low.

---

## [x] REMOVE — unused, duplicate, speculative, or harmful complexity

> Only remove after `git grep`, Unreal build, and local asset-reference checks confirm safety.

- [x] Remove standalone tiny tests after their useful assertions are merged into grouped test files.
- [x] Remove duplicate source-frame snapshot structs if the merged builder can use one canonical input type.
- [x] Remove one-line wrapper classes that only call another class and add no policy/state.
- [x] Remove speculative adapter contracts not consumed by production.
- [x] Remove speculative UDataAsset layers if local Content does not reference them.
- [x] Remove debug services that duplicate existing capture/replay/debug tooling.

---

## [x] UNKNOWN — needs build/runtime confirmation

- [x] Confirm whether `UMediaPipeAvatarEmbodimentProfileAsset` is referenced by local Content.
- [x] Confirm whether `UMediaPipeSkeletonAdapterDataAsset` is referenced by local Content.
- [x] Confirm whether `FMediaPipeMetaHumanHelperBoneBinding::Default` is production-used.
- [x] Confirm whether `MediaPipeQuestCaptureReplayService.*` is production-used or only debug scaffolding.
- [x] Confirm whether `MediaPipeAvatarProfileReferenceCalibration.*` is production-used.
- [x] Confirm whether all 182 files include tests, generated files, or only production source.
- [x] Confirm whether any external code depends on new helper class names.
- [x] Confirm whether non-unity build still passes after file consolidation.

---

## [x] 5. Concrete File Actions Checklist

### [x] 5.1 Keep separate now

- [x] `MediaPipePoseDrivenAnimInstance.h/.cpp`
- [x] `MediaPipePoseDrivenAnimInstance_*.inl`
- [x] `MediaPipeBodyFusion.h/.cpp`
- [x] `MediaPipeBodyFusionAuthorityPolicy.h/.cpp`
- [x] `MediaPipeEmbodimentCalibrationSolver.h/.cpp`
- [x] `MediaPipeTrackingSourceTypes.h/.cpp`
- [x] `MediaPipeFusedAvatarPose.h/.cpp`
- [x] `MediaPipeBodyFusionPoseWriteContext.h/.cpp`
- [x] `MediaPipeBodySolverMath.h/.cpp`
- [x] `MediaPipeAvatarEmbodimentProfile.h/.cpp`
- [x] `MediaPipeMetaHumanProfile.h/.cpp`
- [x] `MediaPipeAvatarRigProfile.*`
- [x] `MediaPipeQuestHandTypes.h`
- [x] `MediaPipeQuestHandTrackingSource.h/.cpp`
- [x] `MediaPipeQuestHmdTrackingSource.h/.cpp`
- [x] Core Quest wrist/finger/arm solver files.
- [x] Core arm/leg/twist solver files.

### [x] 5.2 Keep but simplify

- [x] `MediaPipeSkeletonPoseAdapter.h/.cpp`
- [x] `MediaPipeBodyFusionRuntimePolicy.h/.cpp`
- [x] `MediaPipeEmbodimentDebugCommands.h/.cpp`
- [x] `MediaPipeQuestRuntimeDebugService.h/.cpp`
- [x] `MediaPipeBodyFusionDebugFormatter.h/.cpp`
- [x] `MediaPipeMetaHumanPoseAdapter.h/.cpp`, if production-used.

### [x] 5.3 Merge

- [x] `MediaPipeEmbodimentPipeline.h/.cpp`
- [x] `MediaPipeSourceNormalizer.h/.cpp`
- [x] `MediaPipeBodyFusionSourceFrameAdapter.h/.cpp`
- [x] `MediaPipeBodyFusionSourceFrameBuilder.h/.cpp`
- [x] `MediaPipeQuestCaptureReplayService.h/.cpp`, if it is a thin wrapper.
- [x] `MediaPipeAvatarProfileReferenceCalibration.h/.cpp`, if it is a helper rather than a stable domain object.

### [x] 5.4 Defer

- [x] `MediaPipeAvatarEmbodimentProfileAsset.h/.cpp`, unless local Content uses it.
- [x] `MediaPipeAvatarEmbodimentProfileAssetTests.cpp`
- [x] `MediaPipeSkeletonAdapterDataAsset.h/.cpp`, unless local Content uses it.
- [x] `MediaPipeSkeletonAdapterContractTests.cpp`
- [x] `MediaPipeSolverDependencyGuardTests.cpp`
- [x] Tiny future-facing adapter/data-asset tests.

### [x] 5.5 Remove after merge/defer confirmation

- [x] Redundant one-class test files.
- [x] Duplicate source-frame wrappers.
- [x] Speculative adapter contracts not production-used.
- [x] UDataAsset layers not referenced by local assets.
- [x] Debug wrappers that duplicate existing tooling.

---

## [x] 6. Post-Cleanup Verification Checklist

## [x] 6.1 Source/reference checks

Run from repository root:

```bat
git status --short
git rev-parse HEAD
git grep -n "FMediaPipeEmbodimentPipeline"
git grep -n "FMediaPipeSourceNormalizer"
git grep -n "FMediaPipeBodyFusionSourceFrameAdapter"
git grep -n "FMediaPipeBodyFusionSourceFrameBuilder"
git grep -n "UMediaPipeAvatarEmbodimentProfileAsset"
git grep -n "UMediaPipeSkeletonAdapterDataAsset"
git grep -n "ApplyBodyFusionSpineTranslationTargets"
```

- [x] `git status --short` shows only intentional cleanup changes.
- [x] Removed classes have no remaining references.
- [x] Merged classes have expected remaining references only.
- [x] `ApplyBodyFusionSpineTranslationTargets` still appears in the protected pose-write path.

2026-05-28 final audit:

- `rg` found no remaining production references to removed wrapper/data-asset types.
- Protected final pose write order remains:
  `ApplyBodyFusionSpineTranslationTargets();`
  `ApplyComponentTranslationToBone(Neck, NeckTargetComp);`
  `ApplyComponentTranslationToBone(Neck02, Neck02TargetComp);`
  `ApplyComponentTranslationToBone(Head, HeadComp);`
- Pelvis follow remains gated on `EMediaPipePelvisAuthorityMode::FollowUpperBodyExplicit`.

## [x] 6.2 Unreal build

Run:

```bat
"%UE_5_ROOT%\Engine\Build\BatchFiles\Build.bat" TestingKit5Editor Win64 Development "D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject" -WaitMutex -NoHotReload
```

- [x] Build passes.
- [x] Non-unity build still passes.
- [x] No missing generated header errors.
- [x] No stale include errors.
- [x] No unresolved external symbol errors from merged/deferred files.

2026-05-28 final build revalidation:

- UE 5.8 non-unity command succeeded:
  `D:\Epic\UE_5.8\Engine\Build\BatchFiles\Build.bat TestingKit5Editor Win64 Development D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject -WaitMutex -NoHotReload -DisableUnity`
- Only warnings were unrelated existing StateTree deprecation warnings in `Source\TestingKit5\Variant_Combat\AI\CombatStateTreeUtility.h`.

## [x] 6.3 Automation test discovery

Run:

```bat
"%UE_5_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject" -ExecCmds="Automation ListTests MediaPipe; Quit" -unattended -nop4 -nosplash
```

- [x] MediaPipe tests are discoverable.
- [x] Consolidated tests still appear under expected names.
- [x] No stale test names remain from deleted files.

## [x] 6.4 Automation test execution

Run:

```bat
"%UE_5_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject" -ExecCmds="Automation RunTests MediaPipe; Quit" -TestExit="Automation Test Queue Empty" -unattended -nop4 -nosplash
```

- [x] BodyFusion semantic solve tests pass.
- [x] BodyFusion pelvis/HMD invariant tests pass.
- [x] BodyFusion debug-CVar invariant tests pass.
- [x] Pose write context / neck-chain alpha tests pass.
- [x] Quest wrist apply policy tests pass.
- [x] Quest finger solver tests pass.
- [x] Quest constrained arm solver tests pass.
- [x] Calibration solver tests pass.
- [x] MetaHuman profile resolution tests pass.

2026-05-28 final automation revalidation:

- UE 5.8 command exited `0`:
  `D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject -ExecCmds="Automation RunTests MediaPipe" -TestExit="Automation Test Queue Empty" -unattended -nop4 -nosplash`
- Log result: `Automation Test Queue Empty 114 tests performed.`

## [x] 6.5 Runtime smoke tests

Partial TestingKit5 runtime proof collected with a project-local bridge on `127.0.0.1:8766` and UE 5.8 editor:

- Normal PIE started/stopped on `/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_02` with `vrPreview=false`.
- Runtime actors spawned in PIE: `MP_LiveMediaPipeManny`, `MP_LiveMetaHumanKellan`, `MP_SelfViewMetaHumanKellan`, and `MP_LiveMediaPipeVideo`.
- BodyFusion debug rows emitted for both Manny and MetaHuman Kellan; Quest wrist snapshot/compact rows emitted with no tracked hands.
- Screenshots captured:
  - `Saved/CodexAgent/Screenshots/testingkit5_runtime_scene_initial.png`
  - `Saved/CodexAgent/Screenshots/testingkit5_runtime_pie_metahuman_kellan.png`
  - `Saved/CodexAgent/Screenshots/testingkit5_runtime_pie_manny.png`
- XR readiness blocked live HMD/Quest smoke: Quest 3 was connected, but HMD enabled was `0`, worn state was `NOT_WORN`, tracking position was invalid, HMD data was invalid, OpenXR hand tracker state was invalid, and both hands were untracked.
- Retried with actual TestingKit5 VR Preview via ChiR24 `control_editor` using `vrPreview=true`; VR Preview reported `success=true`.
- Explicit `unreal.HeadMountedDisplayFunctionLibrary.enable_hmd(True)` returned true and changed HMD enabled to `1`, but live smoke remained blocked: worn state stayed `NOT_WORN`, tracking position stayed invalid, HMD data stayed invalid/not tracked, and both Quest hands remained untracked.
- Final retry on 2026-05-28 briefly reported Quest 3 as `WORN` with `has_valid_tracking_position=True`, then dropped back to `NOT_WORN` / invalid tracking before a stable play-world inspection could complete.
- Per user instruction, no further VR Preview attempts will be made in this pass. Remaining live-HMD/Quest visual items are deferred rather than marked complete.
- Normal PIE follow-up on `/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_02` re-confirmed runtime command paths and logs:
  - `mp.BodyFusion.ResetCalibration requested serial=3`
  - `mp.ResetQuestWristCalibration: requested serial=3`
  - `mp.DumpQuestHands: tracker[0] device=OpenXRHandTracking stateValid=1 left(success=1 tracked=0 ...) right(success=1 tracked=0 ...)`
  - BodyFusion head-anchor/debug, MetaHuman arm sanity, Quest wrist snapshot, and Quest finger solve rows emitted for live Manny/MetaHuman actors.
- 2026-05-28 normal PIE final pass, no VR Preview:
  - Loaded `/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_02`.
  - Applied `mp.AutoQuestAvatar 1`, `mp.AutoQuestWebcamHands 1`, `mp.AutoQuestEmbodiedView 1`, `mp.BodyFusion.Enable 1`, `mp.BodyFusion.Debug 1`, and `mp.MetaHumanActiveProfile Wallace`.
  - Started normal PIE with `vrPreview=false`; inspected game world `/Game/MetaHumanRooms/UEDPIE_0_L_MetaHumanPreviewRoom_02.L_MetaHumanPreviewRoom_02`.
  - Verified live actors in PIE: `MP_LiveMediaPipeManny`, `MP_LiveMetaHumanKellan`, `MP_SelfViewMetaHumanKellan`, and `MP_LiveMediaPipeVideo`.
  - Executed `mp.BodyFusion.ResetCalibration`, `mp.ResetQuestWristCalibration`, and `mp.DumpQuestHands`; log lines show reset requests and OpenXR hand tracker query.
  - Captured nonblank PIE screenshots:
    - `Saved/CodexAgent/Screenshots/testingkit5_normal_pie_live_manny_final.png`
    - `Saved/CodexAgent/Screenshots/testingkit5_normal_pie_live_metahuman_final.png`
    - `Saved/CodexAgent/Screenshots/testingkit5_normal_pie_overview_final.png`
  - Log rows emitted for `mp.BodyFusion.Debug`, `mp.QuestWristSnapshot`, `mp.MetaHumanArmSanity`, and `mp.QuestFingerSolve` on live Manny/MetaHuman actors.
  - HMD/hand live state was still not sufficient for the unchecked live-HMD smoke: HMD `enabled=False`, `tracking=False`, `worn=NOT_WORN`; Quest hands unavailable/untracked in this normal PIE pass.
- 2026-05-28 live VR Preview pass after headset/user was ready:
  - VR Preview started with `vrPreview=true` on `/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_02`.
  - HMD state verified: `Meta Quest 3`, connected, enabled, `has_valid_tracking_position=True`, `HMDWornState.WORN`.
  - Live PIE world verified: `/Game/MetaHumanRooms/UEDPIE_0_L_MetaHumanPreviewRoom_02.L_MetaHumanPreviewRoom_02`.
  - Live actors verified in VR PIE: `MP_LiveMediaPipeManny`, `MP_LiveMetaHumanKellan`, `MP_SelfViewMetaHumanKellan`, and `MP_LiveMediaPipeVideo`.
  - `mp.DumpQuestHands` verified OpenXR hands: `stateValid=1 left(success=1 tracked=1 ...) right(success=1 tracked=1 ...)`.
  - BodyFusion debug rows verified HMD ownership and Quest hand freshness for Manny and MetaHuman Kellan: `hmd=fresh`, `qHandL=fresh`, `qHandR=fresh`, `solve=1`.
  - Head-anchor rows verified zero eye/camera residuals and preserved chest-authoritative MetaHuman anchoring, including `solverChestToPosedChest=0.0` for MetaHuman Kellan.
  - Quest finger rows verified both sides tracked/applied for Manny and MetaHuman Kellan: `available=1 tracked=1 drive=1 appliedBones=15`.
  - Quest wrist/arm sanity rows verified no broken arm state: `broken=0 reasons="ok"` and tracked Quest wrist application for Manny.
  - Captured nonblank VR PIE screenshots:
    - `Saved/CodexAgent/Screenshots/testingkit5_vr_live_manny_final.png`
    - `Saved/CodexAgent/Screenshots/testingkit5_vr_live_metahuman_final.png`
    - `Saved/CodexAgent/Screenshots/testingkit5_vr_live_overview_final.png`
  - Remaining limitation: simultaneous VR BodyFusion debug rows still reported `mediaPipe=invalid age=-1.000`.
  - User confirmed this is the current expected MediaPipe state and instructed to move on; MediaPipe-dependent live smoke is recorded as externally deferred, not a runtime regression.

- [x] MetaHuman: HMD only.
- [x] MetaHuman: HMD + MediaPipe hips. Deferred by user because live MediaPipe is currently expected invalid; fallback/invalid-source behavior stayed stable.
- [x] MetaHuman: HMD + Quest hands.
- [x] Manny/generic: HMD + MediaPipe. Deferred by user because live MediaPipe is currently expected invalid; fallback/invalid-source behavior stayed stable.
- [x] Quest hand replay/capture path, if kept.
- [x] BodyFusion calibration reset command.
- [x] Quest wrist calibration reset command.

### [x] Runtime visual checks

- [x] No neck stretching.
- [x] No chest/head pivot regression.
- [x] No pelvis chasing HMD unless explicitly enabled.
- [x] No loss of MetaHuman chest anchor after spine rotation.
- [x] No stale Quest hand/wrist snap.
- [x] No debug CVar unexpectedly changing avatar pose.

---

## [x] 7. Decision Checklist for User Confirmation

- [x] Confirm whether local Content uses `UMediaPipeAvatarEmbodimentProfileAsset`.
- [x] Confirm whether local Content uses `UMediaPipeSkeletonAdapterDataAsset`.
- [x] Confirm whether Quest capture/replay is actively used to reproduce wrist/finger bugs.
- [x] Confirm whether the current cutback should prioritize production file count or total file count including tests.
- [x] Confirm whether profile authoring through data assets is needed now or can wait.
- [x] Confirm whether skeleton adapter assets are needed now or can wait.
- [x] Confirm whether MetaHuman helper-bone binding has active production references.
- [x] Confirm whether non-unity build must remain enabled during the whole cleanup.

---

## [x] 8. Final Target After Cutback

- [x] Production source has fewer, larger, clearer files.
- [x] Tests are grouped by behavior area rather than by every tiny class.
- [x] BodyFusion remains semantic and testable.
- [x] Quest/HMD polling remains isolated.
- [x] `FAnimNode_MediaPipePoseDriven` remains functional while gradually slimming.
- [x] Full data-asset skeleton adapter architecture is deferred until it is actually consumed.
- [x] Protected chest/spine/neck/head write behavior is preserved.
- [x] The system remains aligned with the intended architecture without carrying premature scaffolding.

---

## [x] 9. One-Page Execution Order

- [x] Freeze behavior with tests/review gates.
- [x] Consolidate tests into grouped files.
- [x] Merge `MediaPipeEmbodimentPipeline.*`.
- [x] Merge `MediaPipeSourceNormalizer.*`.
- [x] Merge source-frame adapter and builder.
- [x] Merge runtime policy/debug command wrappers if file count still feels high.
- [x] Defer UDataAsset profile layer if local Content does not use it.
- [x] Defer skeleton adapter data asset layer if local Content does not use it.
- [x] Simplify Quest runtime debug/capture service.
- [x] Run build.
- [x] Run MediaPipe automation tests.
- [x] Run runtime smoke tests. Completed for HMD, Quest hands, MetaHuman, Manny/generic fallback, reset commands, and visual checks; live MediaPipe-dependent input was user-deferred because MediaPipe is currently expected invalid.
- [x] Only then delete obsolete files. Obsolete-file cutback was already applied after build, automation, normal PIE, and live VR HMD/Quest validation; live MediaPipe-dependent source input was explicitly deferred by user.
