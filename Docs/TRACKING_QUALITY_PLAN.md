# Tracking Quality Plan — stability & realism upgrades (2026-07)

Source: the 2026-07-11 deep-research report (methods from journals + shipped systems,
scored against this system's actual weak spots). This plan turns its "do this next"
shortlist into executable phases in the house style: **one phase = one commit + push**,
desk-verifiable gates per phase, tracers before fixes, and **one consolidated worn A/B
session at the end** — no per-phase headset checks.

Unlike the corrector refactor (behavior-preserving), every phase here is a **behavior
change**. So the safety model is different: every feature ships **off by default**,
armed only in the **candidate** variant overlay via its own CVar, individually
toggleable live so the final worn session can bisect any regression by flipping CVars
instead of rebuilding. The baseline variant stays the pre-plan system, untouched.

## Iron rules (all phases)

- Tracer first: no fix lands before the row that would convict or acquit it exists.
  Mechanisms get named from rows, not from theory (house discipline).
- Every new continuous influence follows the bias-eraser recipe in full: magnitude
  bound + quiet-gated learning + motion-faded application + slow tau, and every
  discrete switch gets hysteresis + entry/exit dwell. No exceptions — each of the six
  July regressions came from skipping one of these.
- All cross-frame state lives in the keyed store (`GetQuestWristRuntimeState(key)` or
  the body-solver state where field-proven); never node members; never key 0.
- Editor closed for builds; use `Tools\BuildTestingKit5EditorFast.ps1` or the raw
  bounded command in AGENTS.md. Never Live Coding.
- Automation test count may only grow (164 at plan start). Replay/golden outputs must
  be byte-identical with all new CVars at their off defaults.
- Saved/Logs mining: GNU grep or Python, never ripgrep.
- The mirror avatar is the only judge; worn checks are batched into the final session.
- Prerequisite on a new machine: project operational per `SETUP_NEW_MACHINE.md`
  (build green, 164/164, gold-standard boot verified).

## Phase ordering rationale

The research report ranked foot-lock as the biggest visible win, but the build order
below puts the **signal-quality fixes first** (timestamp alignment, Z-distrust) because
the foot-contact detector consumes exactly the signals those phases clean up — building
it on today's transient-prone, depth-ambiguous stream would mean tuning it twice.

---

## Phase 0 — Metrics + baseline fingerprint (desk only)

Goal: know the before-numbers for every artifact this plan attacks.

- Add three tracer families (all throttled, keyed per actor+side, off outside
  CameraHandTrace-style arming):
  - `mp.FootSkateTrace`: per-foot rows — contact state (once it exists; for now
    height+velocity provisional label), horizontal speed of the ankle while
    provisionally planted, penetration depth. This is the foot-skate scoreboard.
  - `mp.WristLimitTrace`: wrist twist/swing angles vs anatomical range (report-only in
    this phase — measures how often and how far today's output leaves the range).
  - `mp.WebcamAgeTrace`: per-measurement age (capture timestamp vs solve time) and the
    residual magnitude the correctors currently compute against the *current* pose —
    the number Phase 1 should shrink.
- Mine the existing 2026-07-10 acceptance-session logs plus one fresh casual desk
  capture (webcam only, no headset needed) for baseline rows into
  `Docs/tracking_quality_baseline/`.
- Gate: build + 164/164 + rows appear and are sane. Commit.

## Phase 1 — Timestamped residuals (root-cause the transient class)

Research basis: out-of-sequence-measurement literature (Bar-Shalom; Stone Soup
reference impl; verified claims) — fusing a ~80 ms-stale measurement against the
current pose measurably degrades the estimate; the fix is to compute the residual
against the pose *at the measurement's own timestamp*.

- Add a short ring buffer of recent solved poses (relevant joints only: shoulders,
  elbows, wrists, pelvis, chest heading — whatever the five correctors reference) in
  the keyed store; depth ~250 ms.
- Plumb the webcam frame capture timestamp through to the correctors (MediaPipe
  already carries it; account for its ~80 ms forward prediction — the effective age is
  capture-to-now minus predicted-ahead; measure, don't assume, using WebcamAgeTrace).
- `mp.MediaPipeTimestampAlignedResiduals` (default 0, candidate 1): when on, every
  corrector's Learn() compares the measurement against the buffered pose nearest its
  timestamp. Apply() is unchanged — corrections still apply to the current pose.
- Gates: with flag off, corrector goldens byte-identical (this phase must be a no-op
  when disarmed). With flag on, desk capture shows learn-residual magnitude during
  motion drops vs Phase 0 baseline while quiet-pose residuals stay unchanged. 164+
  tests; new unit test for the ring-buffer lookup (interpolation + wraparound + missing
  history fallback = use current pose).

## Phase 2 — Swing-twist anatomical wrist clamp (guardrail, not corrector)

Research basis: Kenwright (twist-and-swing joint limits: "fast, robust, simple");
ECCV 2020 biomechanical hand constraints (limit values from biomechanics literature).

- Decompose the final wrist rotation (post everything: palm-roll machinery, holds,
  overrides) into twist about the forearm axis + swing; clamp each to a configurable
  anatomical range (`mp.WristAnatomicalClamp` default 0, candidate 1;
  `mp.WristTwistRangeDeg` / `mp.WristSwingRangeDeg` defaults from biomech literature,
  generous — this is a guardrail against impossible frames, not a stylistic limit).
- Clamp events emit on WristLimitTrace with the pre-clamp excess — after a week of
  sessions this row answers "was anything actually being caught?"
- This is the LAST op before the bone write on the wrist path; it must not feed back
  into any learner (a clamped frame must not teach the palm-roll machinery anything).
- Gates: flag off = byte-identical. Unit tests for the decomposition (round-trip,
  axis-flip parity for L/R — mind the thumb-parity lesson). 164+ tests. Desk replay of
  the 2026-07-09 wrist-snap session logs: the historical 20–130° flap frames must land
  inside the clamp.

## Phase 3 — Foreshortening → Z-distrust (kill the confident depth lies)

Research basis: ManiPose (NeurIPS 2024) — 2D→3D lifting is ill-posed exactly when a
limb foreshortens; the pragmatic version needs no new model.

- Per limb segment, compute the ratio of projected 2D length to expected 2D length at
  the current pose depth; when it foreshortens toward the camera past a threshold,
  scale down the *reliability* fed to every consumer of that limb's MediaPipe Z (leg
  solve, correctors' quiet/reliability gates). Smooth with dwell — reliability must
  not flap frame to frame.
- `mp.MediaPipeForeshortenZDistrust` (default 0, candidate 1) + a
  `mp.ForeshortenTrace` row (segment, ratio, applied reliability scale).
- Gates: flag off byte-identical; unit tests on synthetic geometry; desk check with
  the webcam: the documented right-knee-toward-camera raise produces distrust rows and
  visibly steadier Manny legs on the panel (Manny = raw webcam, judgeable at desk
  without the headset).

## Phase 4 — Foot contact + foot lock (the missing subsystem)

Research basis: Kovar et al. SCA 2002 (classical IK foot-plant fixer, "black box");
UnderPressure CGF 2022 (learned contact detection beats height/velocity thresholds on
noisy input — the upgrade path if heuristics disappoint); Move.ai/Rokoko shipped-filter
precedent; perception literature (hip flexion is the watched cue — spend error budget
on hip/knee angles, not exact foot position).

4a. **Contact detector**: per foot, height-above-ground + vertical & horizontal ankle
    velocity thresholds with hysteresis and entry/exit dwells (the bias-eraser switch
    recipe), consuming the Phase-3-cleaned reliability. State in keyed store.
    `mp.FootContactDetect` (default 0, candidate 1). FootSkateTrace gains the real
    contact state. Desk-tunable entirely from the panel + Manny.
4b. **Foot lock**: while a foot is in contact, pin its world target at the
    contact-entry position (with a small re-anchor budget, cm-bounded, for slow drift);
    solve the leg to the pinned foot through the existing leg chain (two-bone with the
    scaffold's knee conventions — do NOT bypass the scaffold; it owns flexion
    redistribution); blend out over an easing window on release. Cap the pin
    correction magnitude (~10 cm) so a bad contact label cannot drag a leg.
    `mp.FootLock` (default 0, candidate 1).
- Gates: flags off byte-identical; 164+ tests incl. detector unit tests (synthetic
  trajectories: walk, weight shift, deliberate flutter around thresholds must NOT
  chatter). Desk scoreboard vs Phase 0: planted-foot horizontal speed reduced ≥80%
  during a scripted weight-shift capture; penetration metric (existing replay tooling)
  not regressed. Judge on Kellan in the preview panel at desk first; the worn mirror
  confirms at the end.

## Phase 5 — Learned-prior bake-off (desk only, evaluation not integration)

Research basis: HMD-Poser (CVPR 2024, 205 Hz desktop), EgoPoser (ECCV 2024, 600+ fps),
AGRoL (CVPR 2023); adoption pattern from EgoPoseVR — learned pose projected back onto
headset constraints, prior proposes / quest spine disposes.

- OFFLINE ONLY this plan: run one model (start with HMD-Poser; fall back to EgoPoser)
  via ONNX against the canonical replay dataset's sparse inputs; score its legs and
  arm directions against the recorded solve with the existing scoreboard tools
  (`take_score.py` / `compare_replay_measurements.py` conventions).
- ⚠ GPU discipline: the 5070 Ti has a documented DirectML device-hang class on long
  solves — run inference in the chunked pattern, desk-only, editor logs mtime-watched.
  On a different work GPU this may be moot, but prove it.
- Deliverable: a numbers-in-hand go/no-go memo appended to this doc. Integration (as a
  sixth corrector wearing the standard contract: bounded, faded, quiet-gated) is a
  FUTURE plan gated on this memo — no live wiring in this plan.

## Phase 6 — Consolidated worn acceptance (the only headset session)

One session, candidate vs baseline A/B, all five feature CVars armed. Movement script
(~4 min): stand and inspect in mirror (highest-sensitivity condition per the perception
literature) → weight shifts + walk in place (foot lock) → squat (scaffold unchanged) →
knee raise toward camera (Z-distrust) → arm raises + extend + fists + wrist rolls +
hands behind back + overhead (regression sweep of the July arcs). If anything is worse:
bisect live by flipping the feature CVars one at a time — that is why they are
independent. Verdict + tracer comparison vs `Docs/tracking_quality_baseline/` recorded
here; features that pass get promoted into the candidate trial defaults, features that
fail stay dark with their rows archived.

---

## Success criteria (plan-level)

- Foot-skate scoreboard: planted-foot horizontal speed ↓ ≥80% vs Phase 0, zero new
  penetration, no knee chatter rows.
- WebcamAgeTrace: motion-window learn residuals materially below Phase 0 with no new
  ArmJumpTrace event classes.
- WristLimitTrace: zero anatomically-impossible rendered frames; clamp engages only on
  frames that were previously popping.
- 164 → higher test count, all green at every phase; all features byte-identical when
  disarmed; baseline variant meaning unchanged.
- Worn verdict on the mirror: "better", specifically during the standing-mirror
  inspection leg.

## Execution log (branch feature/tracking-quality, base = a4fb16e)

Gates per phase: build clean (editor closed, fast wrapper), automation count only grows
(164 at plan start), goldens byte-identical disarmed, phase scoreboard evidence. Desk
captures requiring a person at the webcam are batched (one request per phase) and land
as baseline-dir addenda when taken; the capture-independent gate evidence is listed.

| Phase | Commit | Tests | What landed |
| ----- | ------ | ----- | ----------- |
| 0 | 89cf5b4 | 169/169 | mp.FootSkateTrace / mp.WristLimitTrace / mp.WebcamAgeTrace (report-only, keyed throttles, default 0, not in variant lists); Diagnostics/MediaPipeTrackingQualityMetrics + 5 unit tests (incl. L/R parity); Tools/mine_tracking_quality_baseline.py; Docs/tracking_quality_baseline/ with the mined 2026-07-10 acceptance fingerprint (cross-checks REFACTOR_PLAN 9.2 exactly) + a 150s replay -game smoke run: 1049 FootSkate rows (planted planar speed p50 4.0-4.6 / p90 18-20 / max 33 cm/s), 312 WristLimit rows (12-13 out-of-envelope events/side, twist excess to 46 deg). WebcamAgeTrace has no replay rows by construction (live-only call site); fresh desk capture pending. |
| 1 | (this commit) | 176/176 | mp.MediaPipeTimestampAlignedResiduals (default 0, candidate 1): keyed ~250ms history rings (Correctors/MediaPipePoseHistoryRing.h; arm chain ring in FQuestWristSideRuntimeState, applied-yaw ring in FMediaPipeBodySolverState); arm-direction + heading LEARNERS compare measurements against the ring pose at capture-ts + conditioner-prediction (per-frame measured, no assumed constant); Apply() untouched; WebcamAgeTrace row gains aligned=/alElbowResidDeg=/alWristResidDeg=/histN=. Proofs: 6 refactor goldens byte-identical disarmed; corrector-level bit-equivalence asserts (aligned==current == unaligned); synthetic latency-ghost batteries pinned in tracking_quality_baseline/goldens/ (unaligned integrates the ghost, aligned stays <0.1 deg / <0.05 deg); 5 ring unit tests (interpolation, wraparound eviction, fallback contract, non-monotonic refusal, effective-time). Live A/B awaits the flag-on desk capture. Shrug/pelvis/reach correctors exempt with reasons (same-frame camera-only evidence; no webcam in loop; quest-vs-chain already dwell-protected). |

## Explicitly out of scope (parked with reasons)

- Live integration of a learned prior — gated on the Phase 5 memo.
- Physics-based solve (QuestSim class) — revisit only if Phase 4 leaves leg realism
  short; it is a project, not a feature.
- Meta IOBT/Generative Legs via Movement SDK — synthesis, not observation: a mirror
  that invents leg motion breaks the mirror contract. Reconsider only as a
  no-webcam fallback story.
- Webcam model upgrade (RTMW3D) — the July arc proved the bottleneck is fusion
  stability, not landmark accuracy; competes with the renderer for GPU. Revisit after
  this plan's scoreboard exists so the comparison is measurable.
