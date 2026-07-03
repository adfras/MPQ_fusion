# MetaHuman Animator Body Capture as an Offline Reference Solve

**Status: installed and loading (2026-07-03); capture/solve not yet
executed.** Step 0 is DONE: the Markerless Motion Capture plugin
(`MetaHumanBodyTracker`, "MetaHuman Animator Markerless Motion Capture"
v1.0.0) is installed at `Engine/Plugins/Marketplace/MetaHumanBodyTracker_5.8`
and enabled in TestingKit5.uproject together with `MetaHuman` (Animator);
all four modules (MetaHumanBodyTracker, BodyTracker, Segmentation,
MetaHumanBodyOptimizer) load with zero errors. Note the install pulled a UE
5.8.0 hotfix (BuildId 53629095 → 55116800) which required a project module
rebuild. Next: Step 1 (capture a take). Nothing here touches the live
solvers or the replay gate — this is evaluation-side only.

## Why

MetaHuman Animator (UE 5.8) can solve body animation offline from a single
video. That is a third, independent estimate of the performer's joint
trajectories, from the same class of input the live pipeline already consumes
(monocular iPhone video). It is most valuable exactly where our evaluation is
weakest: the camera depth axis, where MediaPipe's 2D is trustworthy but its Z
routinely lies, and overhead windows where Quest synthesizes.

**It is a reference, not ground truth.** The MHA solve is also monocular and
also offline-smoothed. Disagreement between the fused live output and the MHA
solve is an investigation lead backed by log evidence, not a verdict. The HMD
remains the metric authority for height and squat depth.

## Step 0 — install (manual, one time)

1. In Fab / Epic Games Launcher, install **MetaHuman Animator Markerless
   Motion Capture Plugin** into UE 5.8 (Windows-only; we are on Windows).
   As of 2026-07-03 it is NOT present in `D:\Epic\UE_5.8\Engine\Plugins`
   (`Marketplace` folder is empty).
2. Enable in TestingKit5.uproject: `MetaHuman` (MetaHuman Animator, ships with
   the engine) plus the installed markerless plugin. Deliberately NOT enabled
   yet — no reason to carry editor-startup weight before the Fab plugin exists.

## Step 1 — capture

One command prepares everything editor-side:

```text
py prepare_mha_groundtruth_session.py
```

(`Content/Python/prepare_mha_groundtruth_session.py` — loads the live trial
map, restores the Kellan profile, and arms
`mp.PrepareAvatarLockedSyncCalibrationCapture label=mha_groundtruth analyze=1`:
30 Hz, all bones, 210 s, the standard seven guided 30-second phases.)

Then the human protocol, in order:

1. iPhone on the usual mount, Camo running (rear Wide 1x, full body in frame,
   camera static — same framing rules as the MediaPipe baseline).
2. **Start the raw video recording.** Preferred: Camo Studio's record button
   on the desktop (records at the hub, independent of the virtual-camera
   consumer). MHA needs the actual video file, not landmarks. Fallback if
   Camo Studio can't record while UE consumes the feed: any screen/OBS
   capture of the Camo Studio preview window at full frame rate.
3. Press **VR Preview**.
4. One sharp full-arm **sync clap** facing the camera — the time-alignment
   event between the dataset clock and the video.
5. Follow the seven green phase prompts. The standard phases already contain
   every hard case this comparison needs (legs block = knee lifts + squats,
   arms block = reaches, feet block = heel/toe work).
6. After the 210 s run ends, stop the video recording. Note the video
   filename next to the dataset label `mha_groundtruth`.

## Step 2 — offline solve

In the editor (MetaHuman Animator workflow):

1. Create a Capture Source pointing at the recorded video; ingest the take.
2. Create a body performance from the take and process it (offline solve —
   minutes, not real-time).
3. Export the result as an Anim Sequence on the MetaHuman skeleton.

## Step 3 — compare

1. Sample bone trajectories from the exported Anim Sequence
   (`Tools/kellan_replay_bone_sampler.py` is the pattern — a sibling sampler
   reading an AnimSequence instead of replay output is a small addition).
2. Align both series on the sync clap (wrist-distance spike is unambiguous in
   both).
3. Diff against the replay-gate measurements via the
   `Tools/compare_replay_measurements.py` conventions. Priority metrics:
   - knee flexion amplitude (the half-height knee-raise class of bug),
   - pelvis height during squat (HMD metric authority cross-check),
   - wrist position error during overhead windows (Quest-synthesis class),
   - camera-depth-axis excursions (MediaPipe Z-lie class).

## Standing rule

Same as everything else in this project: the MHA reference gets believed only
after its solve has been eyeballed against the evidence video for the same
take. Offline solvers fail silently too.
