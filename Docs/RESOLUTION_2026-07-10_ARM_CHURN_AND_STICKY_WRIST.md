# RESOLUTION — arm ownership churn + sticky-wrong wrist (2026-07-10)

Continues `HANDOFF_2026-07-10_ARM_CHURN_AND_STICKY_WRIST.md`. Every claim below is
reproducible from the 2026-07-10 session log (worn window 00:25:02–00:32 UTC; GNU grep
or Python — ripgrep silently returns 0 matches on these logs).

## Log verification (full worn window — the handoff's sampled numbers confirmed)

- **Camera-hand latch churn on Kellan**: cameraLatched entries split exactly as the
  handoff said — L 11 / R 7 with `rescue=1 questTracked=1` (the rescue dragging the
  WRIST while Quest tracking was healthy), L 13 / R 5 with `questTracked=0` (real
  dropouts, legitimate). 24/12 latches and 23/11 handbacks (L/R) in ~7 minutes.
- **Divergence rescue seized tracked arms**: `mp.ArmOverheadRescue` active on 4.0% (L)
  / 2.4% (R) of Kellan frames; `conditions=1` while `questTracked=1 chainFresh=1` on
  L 82 / R 54 rows (full count; handoff's L 60 / R 41 was the sampled subset).
  Divergence while ACTIVE: median 14.5 (L) / 18.0 (R) cm against the single 30 cm
  threshold — most active frames sat far BELOW the entry threshold, i.e. the latch was
  flapping around it (enter and exit share one compare; the only hysteresis was the
  0.15 s / 0.3 s dwells).
- **Diagnostics starvation**: `mp.ArmOverheadRescue` emitted at frame rate (3,522
  rows/side — its "1 Hz" throttle was a node member wiped by CacheBones every frame);
  `mp.QuestWristSolve` for the acceptance mirror Kellan L emitted **4 rows all
  session** (Manny L got 998 — a function-static throttle pair rationed rows across
  ALL actors). Third session running that evidence starvation hurt.
- **Shrug is healthy and untouched**: Kellan `appliedCm` peak 7.2, 23 rows ≥ 5 cm
  (L 21 / R 2), rest reference stable 47.1–48.3 (quiet gate holding).

## Fixes (single build, each independently defensible from the log)

1. **Divergence rescue OFF in the candidate variant**
   (`Runtime/MediaPipeDriverRuntime.cpp`, candidate list): the
   `mp.MediaPipeArmRescueShoulderRelDivergence=1` entry is commented out with the worn
   verdict recorded. Candidate now runs the BASELINE rescue (overhead + fully-gone
   only) — the exact arm behavior the user's A/B preferred — while keeping heavy-model
   legs, shrug, pelvis anchor, camera yaw, and palm trims. The 2026-07-02 USER RULE is
   intact: quest-side FULLY gone still hands the arm to the camera (that clause never
   depended on the divergence trigger). Re-add divergence only with real enter/exit
   hysteresis plus a hold-tracked veto (`questTracked=1 && chainFresh=1` must never
   divergence-seize).
2. **Hand rotation decoupled from arm rescue**
   (`PoseDriven/MediaPipePoseDrivenAnimInstance_QuestArmSolve.cpp`, camera-hand
   ownership latch): entry clause was `bArmRescueActive || !recentlyTracked`; it is now
   `!recentlyTracked` only (both the keyed latch and the pre-PreUpdate fallback). The
   wrist follows the camera ONLY on actual Quest tracking loss past the 0.35 s grace; a
   camera-owned arm keeps its Quest hand rotation. Real dropouts still latch (a
   fully-gone arm is also `!recentlyTracked`), so the 2026-07-03 frozen-hand feedback
   case still gets the camera hand.
3. **Bounded continuity-bias lifetime**
   (`PoseDriven/MediaPipePoseDrivenAnimInstance_QuestHandRotation.cpp` + new keyed
   field `PalmRollBiasAgeSeconds`): the palm-roll bias re-anchored on every primary
   resume and decayed (0.6 s half-life) only during unbroken primary stretches — during
   raises the primary never ran long enough, so a wrong roll persisted indefinitely
   ("only occasionally corrects itself"). The bias now ages: it re-grounds (age 0)
   whenever |bias| ≤ 3°, and past a 0.75 s cap it rate-limited-converges to whichever
   source is measuring — primary frames decay at 0.2 s half-life with a 60°/s floor,
   fallback frames at a gentle 1.0 s half-life toward the swing-corrected absolute
   (the only measurement present). No branch steps the output per frame; the
   no-stepping property of 2026-07-09 is preserved, and a young (< 0.75 s) bias keeps
   yesterday's semantics exactly. Worst-case wrong roll now converges in ~1 s of
   measured frames instead of never.
4. **Diagnostic throttles moved to the keyed store**
   (`Quest/MediaPipeQuestWristCalibrationState.h` — `ArmRescueLastLogTimeSeconds`,
   `QuestWristSolveLastLogTimeSeconds`): both rows now throttle per actor-side and
   survive CacheBones; the wrist-solve row's cross-actor global static remains only for
   RuntimeStateKey==0 evaluations (keyed-store rule: never write the shared key-0
   bucket). Kellan can no longer be starved of acceptance evidence by Manny. The
   DiagnosticsState members remain (SolverState reset tests assert them) but are no
   longer consulted by these two rows.

## Deliberately NOT changed

- **Palm trims stay 36.8 / −11.1** (`mp.QuestWristPalmTrimLeft/RightDeg`): these are
  not blind numbers — they were mesh-gain-calibrated round-5 on 2026-07-06 (residual
  L −0.8 / R +0.4°). The sticky bias + rescue-dragged wrist explain the broken-looking
  left wrist at least as well, so the calibrated values get first shot under the fixed
  ownership. If the left wrist STILL angles on arms-forward after this build, the A/B
  is live-flippable without a rebuild: `mp.QuestWristPalmTrimLeftDeg 0` +
  `mp.QuestWristPalmTrimRightDeg 0` (baseline values). NOTE: switching
  `mp.MediaPipeSettingsVariant` to baseline live does NOT reset candidate-only CVars
  (the policy layer keeps current values on removal) — use the direct CVar writes.
- **Shrug drive and its CVars untouched** (`ShrugDirect/ShrugWeight/ShrugMinCm` still
  candidate=1); legs keep the heavy model.

## Verification

- Automation suite: 157/157 green (raw defaults keep every changed live path dormant:
  `mp.MediaPipeArmOverheadRescue=0`, `mp.QuestPalmMode=0`,
  `mp.MediaPipeHandRotationOnQuestLoss=0` outside the live trial; the variant lists are
  asserted by no test).
- Next worn session, expect in the log: **zero** `cameraLatched` rows with
  `rescue=1 questTracked=1`; rescue rows show `shoulderRel=0` and no
  `conditions=1` with `questTracked=1 chainFresh=1`; `mp.ArmOverheadRescue` at ~1
  row/s/side/actor (not 3,522); Kellan L `mp.QuestWristSolve` rows present at the same
  cadence as Manny's; any wristPalmHeld/fallback stretch ends with the roll converging
  within ~1 s of primary frames.
- Acceptance: worn mirror test per the handoff's definition of done (arms-forward
  holds + side raises, judged ONLY on MP_LiveMetaHumanKellan).

---

# ROUND 2 (same day): captures falsified the loss-takeover story; row-named mechanisms

The user's 01:50 UTC worn captures (vrpreview 095022 broken wrist, 095052 floppy hand)
showed the HUD GREEN — my "the floppy wrist is the webcam loss-takeover" explanation was
WRONG for those moments. Row extraction from the 01:49 boot (first session with un-starved
per-actor wrist diagnostics) named the real branches:

- **Both capture windows ran the APPLIED quest path**: `questTracked=1 handRotApplied=1
  wristPalmHeld=0 wristPalmFallback=0`, semantic score 0.83–0.98, `calibrationState=
  Tracking`, `armOwner=chainDirect`, zero CameraHandTrace transitions in-window. The
  wrongness was constant SHAPING of the applied rotation, not ownership churn and not the
  (now-bounded) bias — the sustained 64–72° offset in window 2 never decayed.
- **The calibration wipe-on-flicker is the deeper defect**: every `Tracking →
  WaitingForStablePose` transition in the boot coincided with `questTracked=0`. The
  per-frame solve source (`bUseSemanticRollSolve`) is data-dependent; a loss degrades the
  quest/forearm bases for a few frames, the source flips, and the source-mismatch guard
  WIPED the accepted calibration immediately. Consequences measured: (1) the held-rotation
  bridge requires a calibration, so the hand snapped limp with NO grace on every flicker;
  (2) R hand spent 11.5s dangling (`handRotApplied=0`) after the 01:51:02 loss, 33% of its
  session rows total; (3) every mid-motion re-accept baked a NEW arbitrary neutral basis —
  the shifting constant wrist offsets seen in both captures (L re-accepted at 01:50:48.193,
  4s before the broken-wrist capture, mid-movement).

## Round-2 fixes (single build with the round-1 staged changes)

1. **Palm trims OUT of candidate** (`mp.QuestWristPalmTrim*` → engine default 0): direct
   user instruction; the 2026-07-06 gain-calibration history is preserved in the comment
   for any future refit.
2. **Quest-only hands on dropout** (`mp.MediaPipeHandRotationOnQuestLoss=0`,
   `mp.MediaPipeFingersOnQuestLoss=0` in candidate): no webcam wrist takeover; dropouts
   bridge with the forearm-local held rotation (grace 0.45s + fade 0.75s to the upstream
   pose), fingers hold via the finger pose gate.
3. **Transient source-flip guard** (`RotationCalibrationSourceMismatchSeconds`, keyed): a
   solve-source mismatch must accumulate 1.0s of solve-reachable frames before it may wipe
   the calibration; during the dwell the held path bridges and the calibration survives.
   Restores instant resume on re-track and stops the re-anchor lottery. A real solve-mode
   change still recalibrates one second later.
4. **Quest-reach chain extension** (`mp.MediaPipeChainReachFromQuestHand`, candidate=1.0,
   default 0 byte-stable): the chain retargeter rebuilds arms from body-chain DIRECTIONS at
   fixed avatar segment lengths and its synthesized elbow never straightens (measured reach
   avg 41–46cm, max ~52 vs avatar ~52 straight). The REAL hand-tracking wrist's distance
   from the chain's own shoulder over the chain's own segment sum = true straightness
   fraction; the wrist target extends radially to that fraction of avatar full reach
   (stretch-only, 0.15s half-life smoothing, decays on hand loss), elbow re-solved with
   the two-bone cosine rule in the existing swivel plane. Successor to the old quest-wrist
   reach-scale calibration (`ComputeReachScaleCalibration`), which is gated off while the
   chain owns arms. Evidence row: `mp.ChainReachExtend` (keyed throttle).

Expected rows next session: `calibrationState=Tracking` surviving hand flickers (no
`WaitingForStablePose` stretches while worn), `handRotApplied=1` on tracked frames
throughout, `mp.ChainReachExtend` showing `frac→1.0` and `appliedExtCm>0` on full
extensions, and no `wristRotCalibHad=0` rows outside genuine multi-second losses.

## Round 3 (same day): reach extension chased transients — arms pumped a few cm

Worn verdict after round 2: arms much better but "jump a few centimetres during
movements, frequently when I closed my fists". The `mp.ChainReachExtend` rows named it:
applied extension pumped 0.1→6.2→0.6cm (L) / 0.1→5.2→0.2cm (R) across three 1Hz rows at
02:47:12–14 with `desired` spiking 0→18.8→1.4cm, plus sub-second 0→3.5→0 oscillations
aliased by the 1Hz throttle — all with `tracked=1` (zero of ten >1cm dips coincided with
a tracked-flag flip; the flag-flicker hypothesis was checked and is FALSE). Two transient
sources: the low-latency hand-tracking wrist LEADS the laggy body chain during fast moves
(the fraction reads ~0.95 while the chain still reads bent → huge momentary "deficit"),
and a fist-close makes the hand tracker re-fit its model, stepping the wrist estimate a
few cm at constant pose. The 0.15s smoothing rendered every such round trip.

Fix (same CVar, no new levers): the extension now requires the straightness fraction
SUSTAINED ≥0.85 for 0.35s, ramps in across 0.85→0.97, caps at 8cm (the plausible chain
deficit — lag spikes cannot pass), and eases asymmetrically (0.45s half-life in, 0.8s
out). Steady-state full extensions keep their full assist; transients render as nothing.
Evidence row gained `highS=` (the sustained-fraction dwell).

## Round 4 (same day): residual jumps + drift = the camera direction correction

Post-round-3 worn verdict: better, but arms still jump occasionally and drift. Rows:
reach extension now peaks 1.3cm (exonerated); the one full-scale event captured was
`mp.MediaPipeArmDirectionFromCamera`'s blend going 0.00→1.00 on BOTH arms at 03:22:21
(rel 0.00→0.97) — the binary rel-0.5 vote with a 0.15s ease toggled the entire learned
direction correction. Sub-second reliability dips (fist closes move the wrist landmark's
confidence) alias invisibly at 1Hz, and the correction's magnitude was logged nowhere —
drift was unmeasurable.

Fixes/instruments (same build):
1. **Hysteresis on the camera direction vote** (keyed latch): engage rel ≥0.6 sustained
   0.3s, release rel <0.4 (or no camera arm) sustained 0.3s; blend eases 0.4s in / 1.2s
   out. Application now runs from keyed state through camera dropouts (the old outer
   gate required a measured camera arm — one unmeasured frame un-applied the whole
   correction instantly). Learning now requires reliable measured frames (the old code
   kept learning from garbage while the blend decayed).
2. **`mp.ArmDirCorrection` row** (1Hz, keyed): correction angles (elbowCorrDeg/
   wristCorrDeg), engaged/rel/dirAlpha — drift is now a measurable curve; wandering
   angles at quiet standing are the signature, and a bound is the fix if proven.
3. **`mp.ArmJumpTrace` row** (event-driven, gated on mp.MediaPipeCameraHandTrace):
   per-frame residual of the final wrist target's motion vs the RAW chain source's
   motion (real movement cancels; residual >1.2cm in one frame = solver-injected jump);
   on trigger, names each stage's contribution and its change (dirOffCm/dDirCm,
   extOffCm/dExtCm, guardOffCm/dGuardCm) — every future jump names its culprit.
