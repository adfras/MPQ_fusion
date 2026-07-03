# Face Channel via MetaHuman Live Link Mono-Video — Bounded Experiment

**Status: scaffold.** The `MetaHumanLiveLink` engine plugin is now enabled in
TestingKit5.uproject (2026-07-03). No runtime wiring exists yet; this document
is the experiment protocol. Per project policy, everything here ships
defaulted OFF and is judged in the headset.

## Goal

Give Kellan a live face using the 5.8 **MetaHuman Live Link** mono-video
source, driven by the *same Camo webcam feed* that already feeds MediaPipe —
no new hardware. Today the face model output is underused
(`mp.MediaPipeHeadFaceBlend` defaults to `0.0`); Epic's face solver on the
same video should beat MediaPipe face landmarks on quality.

## Hard constraint: one iPhone

- Live Link Face (the app) needs the front TrueDepth camera; Camo owns the
  rear Wide lens for the body pipeline. Both apps concurrently on one phone is
  not realistic (exclusivity + thermals). **The mono-video Live Link source on
  the existing feed is the only single-device path.**
- The 5.8 RTSP streaming feature only becomes interesting if a second iOS
  device is added for the face; Camo over USB beats RTSP over Wi-Fi on latency
  anyway. Not pursued.

## Gate 0 — device sharing (run this before anything else)

The MediaPipe pipeline already holds the Camo virtual camera. Windows virtual
cameras usually allow multiple consumers, but verify before building anything:

1. Start the body pipeline as usual (`mp.PlayMediaPipeWebcam ...`).
2. In Live Link, add a MetaHuman Live Link video source on the same device.
3. Confirm in the same session: (a) the Live Link source produces face frames,
   and (b) the in-VR tracking panel still shows live camera frames at rate —
   check the per-frame diagnostic rows for dropped-frame evidence.

If the device is exclusive, the experiment is dead on one phone; stop here and
record that verdict in this file.

## Wiring plan (only after Gate 0 passes)

1. Live Link subject → Kellan's face board via the standard MetaHuman face
   anim blueprint path. Do not touch the body anim node chain.
2. Add gating CVar `mp.FaceLiveLink.Enable` (default `0`), enabled only by the
   trial policy layer, same pattern as the other 476. The face channel must be
   forced OFF during replay evaluation — the replay gate stays byte-stable and
   knows nothing about faces.
3. Add one per-frame diagnostic row (subject present, frame age ms, solve
   confidence) so failures are traceable like every other subsystem.

## Headset verification criteria

- Mirror check: mouth open/close, brow raise, blink land on Kellan with no
  visible lag against body motion (face lag reads as dubbing, body desync
  reads as possession — note which).
- Body regressions: none. Arm/hand/leg behavior with the face source running
  must be indistinguishable from the current baseline (log-row diff, not
  vibes).
- Latency: eyeball face-vs-body sync in the mirror; if face trails
  noticeably, record by how much before tuning anything.

## Non-goals

- Replacing `mp.MediaPipeHeadFaceBlend` head-pose blending — head rotation
  stays owned by the existing fusion.
- Audio-driven fallback, emotion detection — out of scope for the first pass.
