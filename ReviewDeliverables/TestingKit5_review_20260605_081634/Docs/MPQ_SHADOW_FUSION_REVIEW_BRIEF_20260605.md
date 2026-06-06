# MPQ Shadow Fusion Reviewer Brief - 2026-06-05

## Purpose

This package is for reviewing the TestingKit5 MediaPipe + Quest shadow-fusion diagnostics. The goal is to understand why MediaPipe and Quest are not yet safe to actively fuse, without enabling visible MediaPipe authority or arm fallback behavior.

Keep the current test posture shadow-only:

```text
mp.BodyFusion.Enable 1
mp.BodyFusion.Debug 1
mp.BodyFusion.MediaPipeAuthority 0
mp.BodyFusion.WritePose 0
```

Quest and HMD should remain authoritative for head, wrists, hands, fingers, and reliable arm endpoints. MediaPipe arm fallback is out of scope for this review.

## What Is Wrong

1. The latest clean trial was armed correctly but produced no capture file.

   A torso-forward-only trial was prepared, VR Preview was started, and the preview session ended. The log has the prepare line and the VR Preview lifecycle, but there is no recorder-start line, no recorder-finished line, and no JSON output for that trial. That means the capture system can be configured correctly yet silently fail to start on the next VR Preview.

   This is the immediate blocker. Until capture start is reliable and observable, repeated calibration trials cannot be trusted.

2. The current evidence does not support "MediaPipe is missing landmarks" as the main issue.

   In the strongest Camo capture, MediaPipe body coverage was complete: all 33 body landmarks were present for the analyzed body samples. Body, HMD, and Quest arm samples were also fresh for nearly the entire capture. The problem is more likely synchronization, sample pairing, coordinate alignment, scale, authority target selection, or calibration quality.

3. The internal processing timing and the waveform alignment do not agree.

   The Camo 384 capture showed median capture-to-sample timing around 77 ms, with p95 around 92 ms. The waveform lag estimate from the same capture was about 176 ms before applying a fitted advance. That gap suggests the visible motion mismatch is not explained by raw MediaPipe processing time alone.

4. Applying the measured lag offset did not make fitted alignment pass.

   After applying the about-176 ms MediaPipe advance to the Camo capture, fitted alignment still produced 0 passing rows out of 20. Stage 1 torso/pelvis rows did not pass. Stage 2 shoulder rows did not pass. Head diagnostic rows did not pass. The failures were still dominated by low correlation, amplitude mismatch, flat source or target signals, and residual lag.

5. The later calibration capture was not clean enough to resolve the issue.

   The isolated torso/shoulder calibration capture wrote samples, but it recorded only about 47.4 seconds of usable wall time from a 60 second request. Its lag estimate was unstable, with only 4 usable lag pairs and a wide range from negative to positive lag. Quest arm chains also went stale for part of that run. It still produced 0 passing fitted-alignment rows.

6. Stage 1 active fusion is not ready.

   The first active fusion target was supposed to be pelvis/torso hints only, but the available fitted analysis does not prove a stable torso or pelvis mapping. Some output/fused targets are flat or unsuitable for validating active pelvis or torso authority. Enabling pelvis or torso authority now would risk MediaPipe fighting the current HMD/Quest behavior.

7. Stage 2 shoulder hints are not ready.

   Shoulder and clavicle hints remain diagnostic only. Quest wrist endpoints and the arm chain should stay authoritative. The fitted shoulder rows do not yet prove that MediaPipe shoulder/clavicle motion is synchronized, scaled, and axis-aligned well enough to influence the avatar.

## Questions For Review

- Why can a prepared MPQ shadow-fusion capture fail to start silently when the next VR Preview begins?
- Is capture start gated by a runtime session identity, world identity, actor tick, tag lookup, or previous recorder state that can persist across preview sessions?
- Can the recorder log an explicit skip reason whenever auto-start is armed but not started?
- Are MediaPipe timestamps being compared on the same wall-clock/sample timeline as HMD and Quest samples?
- Is the MediaPipe body pose being converted into the same coordinate space, axis signs, scale, and origin as the HMD/Quest/body-fusion frame?
- Why are some fused/output pelvis or torso comparison targets flat during shadow-only capture?
- Is the shorter-than-requested calibration capture caused by VR Preview teardown, recorder duration handling, source warmup, or timestamp filtering?
- What minimum single-motion calibration trial should be required before Stage 1 pelvis/torso hints are allowed?

## Evidence Included

The review package includes compact evidence under `Evidence/MPQShadowFusion`:

- `camo384_raw_analysis`: original Camo 384 shadow capture analysis.
- `camo384_fit_advance176_v2`: fitted alignment after applying the measured MediaPipe advance.
- `calib_iso_torso_shoulder_raw`: later torso/shoulder calibration analysis without lag advance.
- `calib_iso_torso_shoulder_fit_advance071`: same calibration capture with fitted advance.
- `Logs`: excerpt showing the successful captures and the failed torso-forward-only auto-start.

The raw capture JSON files are intentionally not included because they are very large. The reports, CSVs, plots, source, scripts, and documentation are included.

## Implementation Status

The P0/P1 recorder observability fix has been implemented after this review brief was first written:

- prepared MPQ trials now create a serial-scoped one-shot pending request,
- a new prepared trial resets the stale world-id de-duplication hazard,
- armed trials now log an explicit `mp.MPQShadowAutoStart: armed` line,
- successful auto-start now logs `mp.MPQShadowAutoStart: started`,
- armed early returns now log `mp.MPQShadowAutoStart: skipped` with a reason,
- runtime PIE/spawn probes now log when an armed trial reaches or misses the source/Manny startup path,
- recorder finish/write logs now include end reason, actual elapsed time, and sample span,
- capture JSON now includes requested duration, actual elapsed duration, sample span, start/end wall seconds, end reason, and arm serial,
- the runtime CVar automation test now executes the prepare command and verifies shadow-only CVars plus arm fallback disabled state.

Build verification completed with the editor closed, using the standard UnrealBuildTool editor build. The focused runtime CVar automation test passed.

## Safe Next Step

Do not activate MediaPipe authority yet. Fix the capture-start reliability first, then collect one clean single-motion torso-forward-only trial that produces:

- an explicit recorder-start log line,
- an explicit recorder-finished log line,
- a JSON output file,
- complete MediaPipe body landmark coverage,
- stable HMD/Quest freshness,
- stable fitted alignment with low residual lag,
- non-flat torso/pelvis output comparison signals.

Only after that should Stage 1 pelvis/torso hinting be considered.
