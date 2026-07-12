# Avatar metric lock — child/short avatars at native size, user pose in control

**Status: PLANNED 2026-07-12. Phases 0–4 are agent-executable; Phase 5 is the user's
worn verdict. Base branch: `feature/tracking-quality` (tip 3b87de4).**

## The problem (measured, 2026-07-12)

Emory's body mesh is a genuine child: bind-pose height ~97.3 cm vs Kellan's ~136.6
(71%). Live-driven in the preview room he renders near-adult — PIE probe with zero
scaling anywhere (actor and component scale both 1.0):

| bone Z (cm)  | Manny (adult) | Emory driven | Emory native (approx) |
| ------------ | ------------- | ------------ | --------------------- |
| head         | 162.6         | **147.2**    | ~90                   |
| pelvis       | 95.9          | **88.6**     | ~63                   |
| foot_l       | 8.2           | 7.9          | ~8                    |

Driven Emory is ~130% of his native size (~91% of adult). Mechanism: several write
sites consume the USER'S absolute metrics — the pelvis rides the HMD-derived hip
height, the legs span from that pelvis to the real floor, the arm chain reaches to
the user's real wrist positions. Kellan always looked correct only because he is
approximately the user's size, so writing user-absolute positions onto him is
invisible. The codebase already names the intended philosophy — the calibration
profile mode is literally `avatar_locked_proteus` and REJECTS avatar-scale fields —
the live absolute-metric writers just predate any second body size.

Known-clean parts (verified in source 2026-07-12): the body-pose solve's only
translation write is `RefPelvisTranslationComp + SmoothedPelvisOffsetComp` (avatar
reference + offset); the full arm-chain retargeter carries per-profile
`ReferenceArmLengths`; `mp.MediaPipeChainReachFromQuestHand` re-extends by a FRACTION
of the chain's own segment sums; `FindOrSpawnMetaHumanActor` spawns unscaled. The
poison is confined to where OFFSETS/TARGETS are computed from user-metric absolutes.

## The design

One session-level embodiment scale per driven actor:

    S = avatar reference height / user standing reference height

(avatar side from the target skeleton's reference pose / MetaHuman profile; user side
from the HMD metric scaffold's standing reference — both already exist). Every
user-metric ABSOLUTE target is mapped into avatar space before the write:

- Heights (pelvis anchor, head/HMD-follow targets): scaled about the FLOOR by S.
- Arm endpoints (Quest wrist endpoint authority, camera-path wrist targets): scaled
  about the SHOULDER by the per-arm ratio (profile `ReferenceArmLengths` / the
  quest arm-length calibration's user arm length).
- The user's POSE (joint angles, direction vectors, timing, weight shifts) is never
  touched — proportions map, motion does not.

Kellan is the built-in regression gate: S ≈ 1 for him, so the armed feature must be a
near-no-op on the accepted stack (numeric gate below), while Emory (S ≈ 0.7) is the
visible test.

Explicit decision (NOT a phase): the embodied first-person camera and self-view
placement stay in REAL user metrics — VR comfort rules; only the rendered avatar
body is scale-mapped. If seeing "through a child's eyes" is ever wanted, that is a
separate opt-in arc.

## Iron rules (identical to TRACKING_QUALITY_PLAN)

1. Tracer rows land BEFORE behavior changes; every verdict argued from rows.
2. Continuous influences get the bias-eraser recipe (magnitude bound + quiet-gated
   learning + motion-faded application + slow tau); discrete switches get hysteresis
   plus entry/exit dwells. S itself is measured once-per-session-latch, not a live
   learner: latch it when the scaffold's standing reference is trusted, then hold.
3. All cross-frame state in the keyed store (`GetQuestWristRuntimeState(key)`
   pattern) or field-proven body-solver state. Never anim-node members, never key 0.
4. Baseline variant untouched; no existing accepted value tuned. Every new CVar
   default = byte-identical legacy behavior, registered with comments; candidate
   variant arms with dated AWAITING WORN VERDICT comments;
   `python Tools/GenerateCVarReference.py` after CVar changes.
5. Disarmed = byte-identical, proven by the existing refactor/tracking goldens plus
   new golden coverage where a touched writer lacks it.
6. Test count only grows (196 at plan start). Suite: headless UnrealEditor-Cmd,
   count `Test Completed. Result={Success}`; the known queued-quit race cuts the
   LAST test occasionally — rerun solo, don't debug.
7. Builds with the editor CLOSED via `Tools\BuildTestingKit5EditorFast.ps1`.
8. One phase = one commit + push on the feature branch; never uncommitted state at a
   stop point; per-phase report with commit hash, test count, numeric gate evidence,
   sample rows, state-compliance statement, surprises.
9. Log mining with Python/GNU grep only (ripgrep silently fails on Saved/Logs).
   Multi-line `python -c` via the Bash tool, never the PowerShell tool.
10. Headset-free verification via the AgentBridge (port 8765): `start_pie` /
    `stop_pie` / `run_unreal_python` bone-Z probes exactly like the 2026-07-12
    measurements. `mp.MirrorAvatar Emory` / `mp.MirrorAvatar Kellan` switches the
    probe avatar while the editor is idle. At most ONE human-at-webcam capture
    request in the whole plan, only if a phase gate truly cannot be judged from
    PIE probes + existing replay data.

## Writer census (Phase 0 deliverable, 2026-07-12)

Every site that converts user-metric absolutes into pose-space targets, from a
full source read; "rows" column filled from the Phase 0 PIE evidence. LIVE means
the site can fire in the gold-standard live stack; FUSED means it needs
`mp.BodyFusion.Enable` + `mp.BodyFusion.WritePose` + a usable fused pose
(WritePose defaults 0 and is armed by replay-evaluation/dataset-capture flows,
not the live trial lists).

### Height writers (Phase 1 targets)

| # | Site | User-metric absolute consumed | Writes | Verdict |
| - | ---- | ----------------------------- | ------ | ------- |
| H1 | `DriveBodyFusionPoseCS` pelvis, MediaPipePoseDrivenAnimInstance.cpp:1758+1784 | fused `Pose.Pelvis.LocationWorld` (world cm) | pelvis CS translation | FUSED; user-metric — map about floor by S |
| H2 | `ApplyBodyFusionSpineTranslationTargets`, same file :1919-1975 (re-applied :1995, :2092) | fused pelvis→chest world span | spine01..05 CS translations (stretches torso) | FUSED; user-metric — follows H1/H3 once endpoints map |
| H3 | fused neck/head translations + eye anchor, same file :2039-2097 | fused `Pose.Eye.LocationWorld` = HMD world; head placed so avatar EYE lands on it | neck/neck02/head CS translations | FUSED; user-metric — map about floor by S |
| H4 | raw-path pelvis compression, _BodyPoseSolve.cpp:264-336 | user hip/floor heights as a RATIO only | pelvis offset = avatar's own `ReferenceRigHipHeightCm` × alpha (down-only) | LIVE; CLEAN by construction (comment :301) |
| H5 | HMD height scaffold, _BodyPoseSolve.cpp:63-114 + MediaPipeBodySolverMath.cpp:355-428 | HMD Z normalized to `CompressionAlpha01` | no pose write; supplies S's USER side (`BaselineHeadZ`) | LIVE; CLEAN; runs only on the raw path (skipped when fused writes) |
| H6 | `DriveHmdHeadCS`, _BodyPoseSolve.cpp:1086 | HMD rotation only | head CS rotation | LIVE; CLEAN (no height) |
| H7 | FK root grounding, _BodyPoseSolve.cpp:533-605 | none (avatar's own ball-vs-ref-floor delta, capped) | root translation | LIVE; bounded corrector; `rootOffZ` row field is the evidence |
| H8 | embodiment calibration scale, MediaPipeEmbodimentCalibrationSolver.cpp:105 + EmbodiedFusionComponent.cpp:1147 | `Scale = clamp(max(DefaultEyeLocalOffset.Z, Observed)/Observed, 0.5, 1.8)` | scales fused-candidate MediaPipe landmarks into world | FUSED input path; avatar side is the SHARED 161.94 eye constant (`MakeBuiltInProfile` gives the whole cast the same eye offset; MediaPipeAvatarProfileResolver.cpp:26 just copies it), so Scale≈1 for any user at least that tall — the "avatar height" never comes from the actual mesh |
| H9 | best-available head fallback, EmbodiedFusionComponent.cpp:920 | raw HMD world location | `BestAvailablePose.HeadLocationWorld` (movement replica fallback) | Replica-only; not a skeletal pose write |

### Arm endpoint writers (Phase 2 targets)

| # | Site | User-metric absolute consumed | Writes | Verdict |
| - | ---- | ----------------------------- | ------ | ------- |
| A1 | Quest wrist endpoint, _QuestSpaceMapping.cpp:556 → :502/:538, applied _QuestArmSolve.cpp:1028-1030 | raw Quest wrist world; mapped about the avatar eye anchor with USER-metric HMD-relative offsets (MediaPipeAvatarEmbodimentProfile.cpp:663-679) | arm chain endpoint target | LIVE; user-metric — scale about shoulder by per-arm ratio |
| A2 | calibrated user reach, _QuestArmSolve.cpp:1221-1230 (forward reach), :1480-1516 (reach-scale), measured :1534-1735 | live shoulder→wrist distances (user cm) | reach clamp/normalization | LIVE; already clamped into the avatar envelope (`MaxReachCm` from Ref arm lens) — verify from rows, then per-arm ratio mapping |
| A3 | camera-path wrist, _QuestArmSolve.cpp:318-325; overhead rescue :341-378 | MediaPipe wrist world | arm endpoint when camera owns the arm | LIVE; user-metric — Phase 2 target |
| A4 | full-arm-chain retarget, MediaPipeMetaHumanArmRetargeter.cpp:49-72 | source directions only | avatar-native-length chain (ReferenceArmLengths from the TARGET skeleton ref pose, MediaPipeMetaHumanProfile.cpp:229-277) | CLEAN |
| A5 | chain-reach extension, MediaPipeReachExtender.cpp:25-35 + :67-84 | source-native FRACTION only | extension of avatar-native reach, capped 8cm | CLEAN (prove from rows, plan requirement) |

### Leg writers (Phase 3 targets)

| # | Site | User-metric absolute consumed | Writes | Verdict |
| - | ---- | ----------------------------- | ------ | ------- |
| L1 | `RefThighLen`/`RefCalfLen` sourcing, _ReferenceCache.cpp:812-813 | none (TARGET skeleton ref pose) | leg reference lengths | CLEAN — plan's verification requirement met in source; rows re-confirm |
| L2 | leg IK ankle target, _LegSolve.cpp:1136-1144 | `HipPosComp + (AnkleWorld - HipWorld)` — user-metric leg vector | ankle IK target (planted snaps to avatar rig floor) | user-metric — Phase 3 |
| L3 | direct-segment solve, _LegSolve.cpp:1363-1366 + foot-lock pin :1373-1444 | source directions only | ankle from avatar-native `RefThighLen`/`RefCalfLen` | CLEAN |

### W0 — VERDICT: the plan's premise was a measurement artifact (Phase 0, 2026-07-12)

The tracer rows + asset probes overturned the "problem (measured)" section.
Chain of evidence, every step reproducible from the committed baseline files:

1. **The drive is not stretching anyone.** Canonical-replay rows: driven spans
   vs the target skeleton's reference pose are 0.993–1.007 on Kellan, Manny AND
   EMORY; leg/arm segment sums exactly 1.000 (no translation-stretch anywhere).
2. **"Driven Emory 88.6/147.2" is his reference pose.** A bare spawned
   `m_srt_unw_body` and the full idle `BP_Emory` (whole MetaHuman stack, zero
   MediaPipe) hold pelvis 88.6 / head 147.2. The 2026-07-12 "live-driven" PIE
   probe measured the same numbers because bone-Z probes return the reference
   pose whether or not anything drives.
3. **The asset is internally CONSISTENT at short-adult scale.** The runtime
   inverse-bind matrices equal the reference skeleton (bindK = 1.000 in the
   `mp.EmbodimentScaleTrace.Bind` rows — on every cast member including Emory),
   and idle Emory RENDERS at skeleton height (side-by-side scene captures:
   `Docs/avatar_metric_lock_baseline/phase0_idle_emory.png`) — a short teen
   figure, not a 97cm child.
4. **The "bind-pose height ~97.3 cm" was imported-bounds packaging, not
   stature.** Cast survey of `SkeletalMesh.get_bounds()` top vs the neck_01
   bone: Wallace 0.955, Kellan 0.937 — but Emory 0.705, **Hudson (the TALL
   body!) 0.706**, Maria 0.716, Payton 0.726. Four avatars share Emory's "71%"
   ratio; it is how srt/tal/female body assets are packaged. Comparing Emory's
   bounds metadata (97.3) with Kellan's (136.6) and reading it as body height
   was a category error; head-bone stature says Emory = 94.5% of Kellan — a
   short adult by authoring (m_srt_unw), exactly what he renders at.

So: **there is no ~130% stretch, no child body in the project, and no defect in
the height path** — "driven at native size, user pose in control" is already
true of the accepted stack (rows). The census tables above stand as the map of
where USER-metric absolutes genuinely enter (they matter live: the Quest wrist
endpoint offsets are real user cm on an avatar whose arms are ~5-7% shorter),
which is what `mp.AvatarMetricLock` maps in Phases 1-2. The plan's approx
column ("native ~90/63") and the "~1.3 leg/torso stretch" Phase 0 gate clause
are voided by this evidence per iron rule 1 (every verdict argued from rows);
the corrected gate values are the measured native reference spans in
`Docs/avatar_metric_lock_baseline/`.

Phase 0 row evidence: `Docs/avatar_metric_lock_baseline/phase0_*`.

## Phases

### Phase 0 — Embodiment scale tracer + writer census (dark)

`mp.EmbodimentScaleTrace` (default 0, NOT in variant lists): per-actor ~1Hz rows with
(a) native reference spans from the target skeleton (pelvis height, spine+neck+head
chain length, leg length, per-arm length), (b) driven spans measured from the posed
component-space transforms, (c) the stretch ratio per region, (d) the latched S and
its inputs (avatar ref height, user standing ref, latch state), (e) per-writer
contribution fields filled in as writers are identified. Deliverable beyond the
tracer: a WRITER CENSUS in this doc — every site that converts user-metric absolutes
into pose-space targets (grep + row-confirmed: suspected = pelvis HMD anchor,
QuestSpaceMapping head/avatar translation, quest wrist endpoint authority, any leg
reference-length sourced from user space). No behavior change anywhere.
GATE: rows live on Kellan AND Emory in PIE (bridge-driven, no headset); Kellan
stretch ratios ≈ 1.0 (0.95–1.05), Emory shows the measured ~1.3 leg/torso stretch;
disarmed byte-identical (no writers touched); tests grow (ratio math unit tests).

### Phase 1 — Height writers behind `mp.AvatarMetricLock` (default 0, candidate 1)

Pelvis-anchor and head-follow height targets scaled about the floor by the latched S.
S latch: scaffold standing reference trusted + quiet, one latch per session, re-latch
only on explicit recalibration (rule 2).
GATE (all from PIE probes + rows, no headset): Emory driven pelvis within ±10% of
native reference pelvis height with feet on the floor plane; Kellan driven pelvis
delta vs flag-off < 2 cm (near-no-op proof); disarmed byte-identical via goldens;
candidate-armed dark with AWAITING WORN VERDICT comment.

### Phase 2 — Arm endpoint mapping

Quest wrist endpoints and camera-path wrist targets scaled about the per-side
shoulder by (profile reference arm length / calibrated user arm length). Verify from
rows that the chain-reach fraction path stays avatar-native (believed already true —
prove it, don't assume it).
GATE: Emory wrist targets stay within his native arm-span envelope (no chain
overstretch; WristLimit/ChainReach rows clean); Kellan WebcamAge/WristLimit row
statistics unchanged vs flag-off on the canonical replay (the tracking-quality
baseline tooling already scores this); disarmed byte-identical.

### Phase 3 — Leg consistency at native pelvis height

With the pelvis at native height, prove the direct-segment leg solve + P4 foot
contact/lock keep feet planted WITHOUT stretch. Verify `RefThighLen`/`RefCalfLen`
source is the TARGET skeleton's reference pose; if any leg length is user/source
sourced, swap it under the flag.
GATE: Emory PIE probe leg span ≈ native (±5%); FootSkateTrace planted rendered-foot
speed p90 on the canonical replay within the tracking-quality baseline envelope on
Kellan (no regression); disarmed byte-identical.

### Phase 4 — Full-cast PIE audit + docs

Bridge-driven PIE probe over the whole cast (Wallace, Emory, Hudson, Kellan, Maria,
Payton, Manny): bone-Z table per avatar flag-on vs flag-off, committed to
`Docs/avatar_metric_lock_baseline/`. Update SETUP_NEW_MACHINE.md (avatar sizes are
native under the flag), CVAR_REFERENCE, and the execution log below. Editor left on
the bare-boot gold standard with Emory selected.
GATE: every cast member's driven spans within ±10% of native in ref-pose PIE; all
tests green; pushed; tree clean.

### Phase 5 — WORN VERDICT (user only — the agent STOPS before this)

Protocol: (1) Kellan first, full 60-second script — must feel IDENTICAL to the
accepted stack (S≈1 regression gate); (2) `mp.MirrorAvatar Emory`, same script —
child-sized in the mirror, feet planted, arms reaching naturally within HIS
proportions, user's motion clearly his own. Bisect live with `mp.AvatarMetricLock 0`
↔ `1` (no restart). The mirror avatar is the only judge that counts.

## Out of scope (parked with reasons)

- First-person child-eye camera height — comfort/safety decision, separate opt-in.
- MetaHuman body DEFORMATION to match the user (explicitly forbidden by the proteus
  calibration contract — the avatar's body is locked, that is the point).
- Runtime S re-learning during a session — latch-once by design; a drifting S is a
  new bias-eraser problem nobody asked for.
- Manny-baseline scaling — Manny is adult-sized reference tooling; untouched.

## Execution log (fill per phase)

| Phase | Commit | Tests | What landed |
| ----- | ------ | ----- | ----------- |
| 0 | a08dd3b | 203/203 | mp.EmbodimentScaleTrace (boot-armed interactive, never in variant lists): per-actor ~1Hz native-vs-driven span rows + per-region ratios + dark once-per-session S latch (keyed store, HMD/camera pair selection) + per-writer evidence fields; .Bind row family (inverse-bind spans, bindK). WRITER CENSUS in this doc. VERDICT W0: premise refuted — replay rows show driven == reference pose within 0.7% on Kellan/Manny/EMORY (legs/arms exactly 1.000); idle BP_Emory holds the "driven" 88.6/147.2; bindK=1.000 everywhere; "97.3 child bind height" = imported-bounds packaging shared by Hudson/Maria/Payton. Emory is an authored short adult (94.5% of Kellan) rendering at native size. Evidence: phase0_* (rows, cast probe, idle captures). Race-cut HeadingAlignedGolden rerun solo green. |
| 1 | 26f04f7 | 205/205 | mp.AvatarMetricLock (default 0 = byte-identical; candidate 1, AWAITING WORN VERDICT, live-bisectable): with the session S latched, every fused-pose world target HEIGHT maps about the stage floor by S before DriveBodyFusionPoseCS consumes it (census H1-H3 — the only absolute height writers); planar motion untouched; MapHeightAboutFloor/MapFusedAvatarPoseHeightsAboutFloor pure + unit-tested. S latch marches from UpdateEmbodimentScaleLatchState (tracer OR lock armed), latch-once proven live (userRef drifted 168.8->170.3, latched S held). HMD pair fixed to eye-to-eye semantics (avatar resolved eye height vs scaffold baseline above the WORLD floor — the hip-centered source floor mixes frames by ~90cm). GATES (replay PIE, live bisect 1->0->1, Manny as motion control): Emory pelvis 84.98 = 95.9% of native ref 88.6 (±10% ✓) feet planted 7.92 vs native 7.9; Kellan flag-attributable pelvis delta ~0.02cm (< 2cm ✓; raw 0.36cm = replay-motion drift, matched by unmapped Manny's 0.38cm); latched S: Kellan 0.965 / Emory 0.911 / Manny 0.963 (src=hmd). Inert in the accepted live stack by construction (mp.BodyFusion.WritePose=0). Evidence: phase1_ab_*. |
| 2 | (Phase 2 commit) | 205/205 | VERIFICATION, no behavior change (iron rule 1 — no row convicts a defect). GATES from 55s+55s live-bisect replay windows per avatar: Emory wrist targets inside HIS native envelope (chainReach max 46.4-46.6 < fullReach 49.7 — note 49.7 vs Kellan's 52.0 = per-avatar native lengths; armR=1.000; zero overstretch); chain-reach fraction PROVEN avatar-native from rows (frac_med 0.87-0.89 identical across avatars while fullReachCm differs per avatar — fraction source-native, application avatar-native, exactly the plan's prove-don't-assume item); Kellan WristLimit out-rate flat across the bisect (6.3% vs 6.5%), effAgeMs 1.1 both states. The plan's shoulder-anchored ratio map is NOT landed: the accepted stack deliberately runs the equivalent machinery disabled (ApplyAutoQuestProfile sets mp.QuestConstrainedArmReachScaleCalibration=0 — the chain-path extension superseded it), re-adding it would double-correct, and pre-scaling the endpoint would poison the arm-length calibration's own reach measurements (corrected frames never teach — the WristAnatomicalClamp precedent). Census A1-A5 verdicts row-confirmed. Evidence: phase23_ab_*. |
| 3 | (Phase 3 commit) | 205/205 | VERIFICATION, no behavior change. RefThighLen/RefCalfLen confirmed sourced from the TARGET skeleton's reference pose (census L1, _ReferenceCache.cpp:812-813 via ref-pose component transforms) — nothing user/source-sourced, so the plan's conditional swap has nothing to swap. GATES: Emory leg span exactly native (legRL/legRR = 1.000 min=max, both lock states; ±5% trivially); FootSkate planted planarSpd p90 tracks the replay window content, not the avatar or the flag (window A: Kellan 23.2 / Emory 21.7; window B: 33.9 / 33.3; tracking-quality baseline full-run envelope ~29-33) — no regression on Kellan; disarmed byte-identical (defaults untouched, goldens green). Evidence: phase23_ab_*. |
