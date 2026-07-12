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
