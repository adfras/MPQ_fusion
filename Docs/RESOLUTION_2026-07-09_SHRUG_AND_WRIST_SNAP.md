# RESOLUTION — arms-down shrug loss + right-wrist snap (2026-07-09)

Continues `HANDOFF_2026-07-09_SHRUG_AND_WRIST_SNAP.md`. Every claim below is
reproducible from the 2026-07-09 session log (worn window 07:27:34–07:31 UTC;
GNU grep or Python — ripgrep silently fails on these logs).

## 1. Arms-down shrug: the signal was eaten before the drive, not after it

What the log shows (and the code confirms):

- The fusion shrug drive (`DriveClavicleShrugCS`) is the ONLY clavicle writer on
  the live chain-direct mirror (DriveClavicles=0 by design; the BodyFusion
  clavicle write requires `bUseBodyFusionArm`, and body fusion ran trace-only —
  `bodyAuthority=NoMediaPipe reason="trace-only"` all session). The legacy
  weight-gated clavicle block (handoff lead #1) cannot run live at all, so
  `mp.ClavicleDebug` gating was NOT the mechanism.
- The apply path is sound. `FCSPose::SafeSetCSBoneTransforms` converts
  CS-flagged children back to local before a parent write (checked against the
  UE 5.8 source), so the clavicle rotation propagates to the arm even though the
  drive pre-reads the upper-arm transforms for RigScale. The arm-raise lifts the
  user SAW on the mirror came from this same drive (shoulder landmarks ride up
  during raises) — the render path was never broken.
- The loss was amplitude, upstream of the apply:
  - The 1.5cm deadband was a hard subtraction: every shrug lost 1.5cm.
  - The 90s up-adapting rest reference learned from the lifts themselves:
    across three minutes of shrug reps the right rest ref walked 45.9 → 47.2cm
    (`mp.ClavicleShrugFusion` rows), eating ~3cm of every late shrug.
  - Net: his proven ~7.7cm camera shrug applied only 3.5–5.5cm in bursts
    (bilateral rep pattern visible 07:28:29–43, 07:29:28–36) — reading as
    "nothing moves" against a 10cm+ standard.
  - The prior "live renders 1.4cm" figure was the OLD weights-of-evidence path
    (its 0.25 direction clamp caps at ~2cm); the geometric drive shipped the
    night of 07-06 replay-verified only. 2026-07-09 was its first live session.

Fixes (`MediaPipePoseDrivenAnimInstance_BodyPoseSolve.cpp`):

- **Quiet-gated rest reference**: baseline learns upward at 90s only within
  2.5cm of rest; active lifts adapt at 600s (posture still converges, shrugs no
  longer feed their own baseline). Down-adapt stays 2.5s.
- **Soft-knee deadband**: output still exactly 0 at/below 1.5cm (resting jitter
  suppressed), but the amplitude is restored on real lifts — 7.7cm over rest now
  passes 7.6cm (was 6.2 before rest-ref losses even started).
- **Per-instance log throttles** (`ShrugGate` + `ClavicleShrugFusion` moved off
  function-statics into BodyState): the acceptance actor can no longer be
  starved of evidence rows by Manny (10 vs 220 rows last session), and the
  fusion row now prints `appliedCm=` — the rig-side upper-arm rise actually
  produced that frame, the number the mirror verdict is judged on.

Expected live: sustained shrug ≈ `7.6 × rigScale(≈1.18) ≈ 9cm` on the mirror.

## 2. Right-wrist snap: two ownership churns, both fixed by holding the quest basis

Mechanism (a) — palm-roll basis flapping (the frequent snap):

- 38 sampled `wristPalmFallback=1` rows, all side=R, in bursts during side
  raises; `wristPalmHeld` never engaged. Extracting `wristSemanticOffsetDeg`
  across source switches shows the projected-basis roll and the swing-corrected
  fallback disagree by **20–130°** while the projection score oscillates around
  the 0.45 threshold — the old first-match-wins selection switched measurement
  bases per frame, snapping the wrist roll on every switch.
- Fix (`MediaPipePoseDrivenAnimInstance_QuestHandRotation.cpp`, PalmMode>=2
  path): measurement-source **hysteresis + continuity bias**, keyed state.
  Short projection dips (<0.30s) HOLD the last roll (`wristPalmHeld=1` now
  actually fires); a committed switch requires the dwell and is rebased through
  a continuity bias so the output roll never steps; the bias decays (0.6s
  half-life) only while the calibrated primary is measuring — the fallback
  contributes relative motion, never its untrusted absolute offset.

Mechanism (b) — camera takeover on tracked-flag flicker:

- The full timeline (Manny rows carry the same quest stream) shows the right
  hand untracked 07:30:31.6→36.1 and 36.9→41+, with a 0.7s re-track between.
  One dropped frame instantly latched camera hand ownership
  (`bArmRescueActive || !bQuestSideTrackedForArm` — no entry dwell), suppressing
  `DriveQuestHandCS` (its held-rotation path never got the chance to run), and
  the rescue's untracked clause seized the arm 0.15s into the drop
  (`armOwner=cameraRescue`, `handRotApplied=0`).
- Fix (`MediaPipePoseDrivenAnimInstance_QuestArmSolve.cpp`): keyed
  `LastQuestSideTrackedTimeSeconds` recency; the rescue's untracked clauses and
  the camera-hand latch entry now coast through flickers shorter than
  `mp.QuestWristLostTrackingGraceSeconds` (0.35s, the existing flicker CVar —
  no new tuning). During the grace `DriveQuestHandCS` keeps running and its own
  held-rotation path bridges the frames. The **divergence trigger bypasses the
  grace** — direct camera evidence the quest pose is WRONG still takes the arm
  immediately, and true multi-second dropouts still hand over per the
  2026-07-02 user rule (never hold an arm against the camera).
- `mp.MediaPipeCameraHandTrace 1` is now armed by the interactive boot rig
  (`init_unreal.py`) — it logs only ownership transitions and was off in the
  session that needed it.

## Verification

- Automation suite: 157/157 (raw defaults keep both changed paths dormant:
  `mp.MediaPipeClavicleShrugDirect=0`, `mp.QuestPalmMode=0` outside the
  candidate variant, so replay byte-stability is untouched).
- Acceptance: worn mirror test per the handoff's definition of done.
