# Lower-Body Scaffold + Wrist/Finger Replay - Execution Report 2026-06-12

Status: session report, written 2026-06-12. Detailed per-pass evidence lives in
`AVATAR_REPLAY_OUTPUT_FIX_CHECKLIST.md` (sections "2026-06-12 Lower-Body Scaffold Pass",
"2026-06-12 Follow-up", "2026-06-12 Wrist/Finger Replay"). This report is the session-level
summary: what was asked, what was found, what was changed, and how it was proven.

## Task

Improve the lower-body solve so monocular MediaPipe leg motion drives movement intent while
Quest/inferred body tracking corrects 3D distortion and avatar skeletons keep their own
proportions. Follow-ups raised during review: make the Quest headset the squat-depth
authority, fix the knee-too-low squat look, fix non-flat grounded feet, and enable
wrist/finger motion in the replay.

## Root causes found

1. The recorded MediaPipe landmark frame is HIP-CENTERED (hip Z exactly 0 across the whole
   canonical recording), so squat depth could only be inferred from the noisy monocular
   ankle-to-hip distance - the source of the leg squish/distortion.
2. The same recording carries a fresh metric Quest HMD pose on ~100% of samples (167 cm
   standing, 137-150 cm in squat blocks) that the lower body never consumed.
3. With FK legs + root grounding, pelvis-above-floor is EMERGENT from knee flexion, so a
   metric correction must adjust flexion, not just the pelvis offset.
4. Monocular front-facing capture cannot observe the femur's forward (depth) rotation; the
   recorded bend lands mostly in the shin, sinking the knee (femur ~28 deg / shin ~44 deg at
   the squat bottom where a natural squat is roughly the reverse).
5. Every driven grounded foot was pitched ~26 deg toe-up (sloped reference foot basis mapped
   onto a planarized horizontal forward), sinking the ankle to ball height and silently faking
   ~7 cm of squat depth. Pre-existing in the 06-10 baseline.
6. The v1 replay cache reduced each hand to one wrist point and the replay branch never fed
   the hand solvers - but the ORIGINAL dataset recorded full 26-keypoint Quest hand skeletons
   (positions + rotations) on 93-100% of all 6162 samples.

## Changes (all shared solve paths; no per-avatar branches; no bone scaling)

New pure math in `MediaPipeBodySolverMath` (each with automation tests):

- `UpdateHmdHeightScaffold`: rolling-window standing HMD baseline (per-slot maxima), torso
  lean compensation, dimensionless compression alpha + ramp-in confidence.
- `ComputeFusedPelvisCompression`: monocular timing intent fused with HMD metric depth.
  Replay policy sets HmdWeight 1.0 - the headset DETERMINES squat depth; MediaPipe covers
  dropouts/ramp-in and keeps owning timing, phase, momentum, lateral swing, bend plane, and
  lifted-foot motion.
- `AdjustGroundedLegFlexion`: law-of-cosines on the avatar's own thigh/calf lengths, clamped
  to its reference reach; rotates the measured segment directions in their own bend plane by
  a bounded delta (replay: weight 0.8, max 40 deg); straightening damped so knees never lock;
  lifted feet untouched.
- `RedistributeGroundedLegBend`: rigid in-bend-plane rotation so the shin keeps at most
  `ShinTiltShare` (0.35) of total flexion and the femur takes the rest; one-sided; bounded.
- `SolveGroundedFootPitch`: grounded feet keep solved heading/roll but pitch = reference
  flat-contact slope + heel-lift-driven plantar flexion (heel lift measured against the heel
  landmark's own observed floor); soles sit flat, heel raises/toe stands still work.

Wiring: `DrivePelvisTranslationCS` (scaffold update + fusion), `DriveLegCS` (flexion
correction, bend redistribution, foot pitch), scaffold state on `FMediaPipeBodySolverState`
(reset with timestamp rewinds/seeks). 13 new `mp.*` CVars (live defaults off; the replay
policy layer enables them) - see `CVAR_REFERENCE.md` (427 CVars).

Wrist/finger replay: `BuildTrackingFusionReplayCache.py` schema v2 carries the recorded hand
joints into the cache (`..._replay_source_v2.jsonl` built beside the untouched v1 canonical
files; regenerate with the tool from the full dataset manifest); the loader parses them
(`bLeft/RightHasFullKeypoints` flags on `FMediaPipeTrackingHandSourceSnapshot`); the anim node
replay branch populates `QuestHands` so the existing wrist-rotation/finger solvers run; the
replay actor default manifest now points at v2. Arm placement is protected: BodyFusion pose
writes keep all Quest-wrist arm fallbacks disabled during replay.

Diagnostics: throttled `mp.MediaPipeLegScaffold` rows per actor with every source
contribution (HMD baseline/drop/lean/alpha/confidence, mono alpha, fused share, per-leg
flexion measured/target/applied, shin tilt + redistribution, foot pitch, contact state,
pelvis/root offsets); the armed `mp.MediaPipeLegSolveDebugOnce` row carries scaffold values.

New verification tooling in `Tools/`: `aim_leg_camera.py`, `aim_hand_camera.py`,
`kellan_replay_hand_sampler.py`, `check_foot_pitch.py`, `check_leg_proportions.py`,
`summarize_replay_motion.py` (incl. grounded-slide metric), `find_squat_windows.py`,
`find_leg_motion_moments.py`, `dump_foot_bones.py`, plus dataset scanners
(`scan_replay_hmd_legs.py`, `scan_replay_hands.py`, `scan_replay_hand_joints.py`,
`scan_full_dataset_hands.py`).

## Verification (final binary)

- Builds via `Tools\BuildTestingKit5EditorFast.ps1`; automation
  `Automation RunTests TestingKit5.MediaPipe` = 117/117 (was 112; 5 new math/loader tests);
  `TestAnalyzeTrackingFusionDataset.py` 20/20; plot selftest OK.
- Live PIE on `/Game/MetaHumanRooms/L_MetaHumanRecordedQuestMediaPipeReplay_01`, canonical
  recording, 30 Hz samplers (sample counts exact):
  - Squats metric + natural: knee fraction at squat bottom 0.44 -> 0.57, femur/shin tilt
    28/44 -> 53/35 deg, sustained knee bend p05 145.5 -> 130.6 deg, standing returns to the
    exact reference extension (fraction 0.493, pelvis 90.6 vs rig 91.35).
  - Feet flat: grounded foot pitch delta vs natural slope = 0.0 deg on every grounded frame
    (was +26.2 on all); ankle properly ~8 cm above ball; heel raises preserved.
  - Hands move: middle-finger curl 6-118 deg (L) / 11-121 deg (R), index ~104 deg range,
    wrist flexion 45/37 deg range over the hands block (all previously rigid).
  - Invariants: proportions exact every frame (thigh:calf 1.025, 0.0000 cm drift), knee
    extension preserved (175.0), foot lifts preserved, 0 penetration frames, grounded ball
    median 0.80 cm, grounded-slide p95 +8 cm/s (no skating); legs-with-hands-enabled vs
    legs-only = equivalence gate PASS.
- Screenshots in `Saved/CodexAgent/Screenshots/`: `legshot_fixed_squat_141.png`,
  `legshot_fixed_kick_170.png`, `legshot_fixed_standing_152.png`, `handshot_fist_40.png`,
  `handshot_open_45.png` (each captured with paired joint-angle logs).

## Known limitations

- A replay seek resets solver continuity including the HMD baseline window; a seek landing
  mid-squat under-corrects until the user stands once (~15 s confidence ramp).
- The HMD scaffold assumes world Z-up with the tracking floor near Z=0 (true for this
  project's recordings).
- Live (non-replay) paths keep prior behavior: all new corrections default off and are
  enabled by the replay policy layer; enabling them live is a CVar flip away but unvalidated.
- The v2 cache and `Saved/` measurement evidence are derived artifacts (not in git);
  regenerate the cache with `python Tools/BuildTrackingFusionReplayCache.py <full dataset
  manifest> --output <..._replay_source_v2.jsonl>`.
