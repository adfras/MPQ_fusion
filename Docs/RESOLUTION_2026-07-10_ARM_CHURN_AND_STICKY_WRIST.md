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
