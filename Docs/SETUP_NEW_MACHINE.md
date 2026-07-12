# Moving TestingKit5 to a new machine

Written 2026-07-11, when the project moved from the home machine to work.
Verified against the actual repo state at commit `03dd676` (post corrector-refactor merge).

**The one-sentence version:** GitHub has the code, docs, tests, plugins, and even the
MediaPipe model files — but NOT the avatars, the camera-tracking DLLs, or the replay
dataset. You must carry roughly **9 GB by hand** on a drive; everything else installs
or clones.

---

## 1. What GitHub gives you (`git clone https://github.com/adfras/MPQ_fusion.git`)

- All C++ source (`Source/`, including the `Correctors/` folder), the three local
  plugins (`CodexAgent`, `McpAutomationBridge`, `UnrealMCP`), `Config/`, `Tools/`,
  `Tests/`, `AgentBridge/` (minus node_modules), all of `Docs/` including the
  refactor goldens and baseline fingerprint.
- The MediaPipe model files — `Content/MediaPipe/*.task`, including
  `pose_landmarker_heavy.task` (the live model).
- These Content folders only: `Codex`, `CodexAgentTests`, `MCPBench`, `MediaPipe`,
  `MetaHumanRooms`, `Mirror`, `Python` (which includes `init_unreal.py`, the boot
  script that arms the gold standard).

## 2. What GitHub does NOT have — the carry-by-hand payload

| Path (relative to project root) | Size | Why you need it |
| --- | --- | --- |
| `Content\MetaHumans\` | **7.8 GB** | **The entire MetaHuman cast: Kellan (the mirror avatar), Maria, Wallace, Emory, Hudson, Payton, plus the shared `Common` assets they all depend on. Without this, nothing works and there is nothing to showcase.** |
| `Content\Characters\` | 126 MB | Manny (the debug avatar — also part of the showcase). |
| `Content\MHAGroundTruth\` | 298 MB | Ground-truth capture assets. |
| `Content\__ExternalActors__\` + `Content\__ExternalObjects__\` | 5 MB | Actor data for the MCPBench maps (the mirror/preview-room maps do NOT need these, they carry their actors internally). |
| `Content\Input\`, `LevelPrototyping\`, `ThirdPerson\`, `Variant_*`, `CaptureManager\`, `Collections\`, `Developers\` | small | Template + input assets; the copy command below brings them automatically. |
| `ThirdParty\mediapipe_wrapper\ump_shared.dll`, `opencv_world3410.dll`, `opencv_ffmpeg3410_64.dll` | 80 MB | **The camera-tracking engine.** The project builds without them, but the webcam does nothing. |
| `ThirdParty\mediapipe_wrapper\_mediapipe\` | 126 MB | Only needed to REBUILD the DLL from source. Cheap — bring it. |
| Canonical replay dataset (`tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656_replay_source*` in `Saved\CodexAgent\Diagnostics\`) | 0.7 GB | The replay tests and measurement tools read this. A hash-verified backup lives at `D:\Backups\TestingKit5_CanonicalReplayDataset` — carry the backup, restore it into `Saved\CodexAgent\Diagnostics\` at work. |
| (optional) the rest of `Saved\CodexAgent\Diagnostics\` | ~11 GB | Every worn-session capture from June–July. Not needed to operate; bring it if you want the full debugging history available. |

## 3. The copy commands (run on the OLD machine, drive letter `E:` = your USB drive)

```bat
robocopy D:\Epic\Unreal_Projects\TestingKit5 E:\TestingKit5 /E /XD Binaries Intermediate DerivedDataCache .vs node_modules __pycache__ Saved
robocopy D:\Backups\TestingKit5_CanonicalReplayDataset E:\TestingKit5_CanonicalReplayDataset /E
```

The first command copies the whole project (tracked + untracked) minus everything
regenerable. The second brings the dataset backup. Together: ~10 GB.

## 4. What to install on the NEW machine (in this order)

1. **Hardware/OS prerequisites**: Windows 11, admin rights (or IT willing to install
   the list below), a proper DirectX-12 GPU, ~150 GB free disk (the engine alone is
   ~60 GB), a USB webcam, your Quest headset + Link cable.
2. **Epic Games Launcher → Unreal Engine 5.8.** Install to `D:\Epic\UE_5.8` if at all
   possible — every build command and script references that path.
3. **MetaHuman plugins for UE 5.8** (from the Epic launcher / Fab library): the project
   requires `MetaHuman`, `MetaHuman Character`, and `MetaHuman Body Tracker`. Without
   them the editor will refuse the project or Kellan won't load.
4. **Visual Studio 2022** (Community is fine) with the **"Game development with C++"**
   workload (brings the Windows SDK).
5. **Git for Windows**, signed in with access to `github.com/adfras/MPQ_fusion`.
6. **Python 3.11+**: `pip install numpy matplotlib opencv-python mediapipe pillow`
   (that covers every tool that matters; `torch`/`transformers`/`record3d` are only
   for archived depth experiments — skip them).
7. **Node.js 18+**, then `npm install` inside `AgentBridge\` (only needed for the
   editor-control bridge, not for the mirror itself).
8. **Meta Quest Link app** (from meta.com), signed into your Meta account, headset
   paired over Link cable or Air Link.
9. **Claude Code** if you want the same working setup as home.

## 5. Placing the project

Put it at **`D:\Epic\Unreal_Projects\TestingKit5`** — the same path as the old
machine. These files hardcode it: `AGENTS.md`, `Start_Codex_Unreal_Agent.bat`,
`Content/Python/viewer_sync.py`, `Tools/kellan_replay_bone_sampler.py`,
`Tools/kellan_replay_hand_sampler.py`, `Tools/mha_take_score.py`,
`Tools/sample_anim_sequence.py`. If the work machine has no D: drive, either edit
those files or make a junction that fakes the path.

Steps:
1. Copy `E:\TestingKit5` → `D:\Epic\Unreal_Projects\TestingKit5`.
2. Restore the dataset: copy the contents of `E:\TestingKit5_CanonicalReplayDataset`
   into `D:\Epic\Unreal_Projects\TestingKit5\Saved\CodexAgent\Diagnostics\` and keep a
   second copy at `D:\Backups\TestingKit5_CanonicalReplayDataset` (AGENTS.md rule:
   never operate on the dataset without a verified backup).
3. Verify against GitHub: `git fetch origin && git status` inside the project — it
   should say up to date with `origin/main`, working tree clean apart from the
   untracked payload folders. That proves the copy is complete and uncorrupted.

## 6. First build and proof it works (no VR gear needed)

1. Make sure no Unreal editor is running, then build (never use Live Coding):
   ```bat
   D:\Epic\UE_5.8\Engine\Build\BatchFiles\Build.bat TestingKit5Editor Win64 Development -Project="D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject" -WaitMutex -NoUBA -UBANoDetour -MaxParallelActions=4
   ```
   Or use the stall-aware wrapper: `Tools\BuildTestingKit5EditorFast.ps1`. The first
   build is cold — expect several minutes and a ~2.5 GB shared PCH; long silent
   stretches on the big anim-instance file are NORMAL (see AGENTS.md).
2. Run the automation suite headless:
   ```bat
   D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe "D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject" -ExecCmds="Automation RunTests MediaPipe; Quit" -unattended -nopause -nosplash -log
   ```
   Count `Test Completed. Result={Success}` lines in the log: **expect 164, zero
   failures.** This exercises the corrector goldens byte-for-byte — if the copy or
   toolchain were broken, this catches it.
3. Open the editor normally once. Checklist: no missing-plugin dialog, the Content
   Browser shows `Content/MetaHumans` (Kellan), the preview-room map opens without
   missing-actor errors.

## 7. Live mirror bring-up (the real test)

1. Webcam plugged in; Quest connected and Link running.
2. Launch the editor interactively. `Content/Python/init_unreal.py` arms the gold
   standard automatically: preview room + Kellan + candidate settings variant + heavy
   pose model + tracers.
3. In the editor console run `mp.DumpLiveProfileSettings` — you want the **candidate**
   variant with the rescue/legs/panel CVars at 1. A bare boot without the trial armed
   is the RAW stack and looks broken — always verify before judging anything.
4. VR Preview, stand in front of the webcam, judge in the mirror. The mirror avatar
   is the only judge that counts.

### 7b. Choosing the mirror avatar (Kellan / Maria / Wallace / Emory / Hudson / Payton / Manny)

The selector is a PROPERTY ON THE PLACED PAWN, not a console variable. Verified
2026-07-12 (PIE spawned `MP_LiveMetaHumanEmory` + self-view, profile resolved
`active=1 valid=1`).

**The one-line way (any machine, no agent, no editor UI):** in the editor console,
while NOT playing:

```
mp.MirrorAvatar Emory
```

then press VR Preview. The command writes the placed pawn's properties (the real
selector) and aligns the runtime CVars. Run it with no argument to print the current
selection and the valid ids (`Wallace`, `Emory`, `Hudson`, `Kellan`, `Maria`,
`Payton`, `Manny`). `mp.MirrorAvatar Manny` switches the pawn to the internal Manny
baseline (`Avatar Type` non-MetaHuman, `mp.AutoQuestAvatar 0`); any cast name
switches back to MetaHuman mode automatically. If a session is already running the
command stores the choice for the NEXT preview and deliberately leaves the live
session alone.

Equivalent manual path: select **`MP_PlacedEmbodiedMetaHumanPawn`** in the Outliner
and set its **`Avatar Type`** / **`MetaHuman Profile Id`** in the Details panel. The
placed `MP_LiveMediaPipeManny` reference runs alongside every session regardless of
this selection.

### 7c. The showcase loop (zero-setup, any machine)

A bare interactive editor boot needs NOTHING typed before the first preview: the
editor opens straight into the showcase room (`EditorStartupMap`), and
`Content/Python/init_unreal.py` arms the whole gold standard — candidate variant,
live trial, heavy pose model, and every tracer — at startup (interactive boots only;
automation keeps raw defaults). The loop is:

1. Open the editor (double-click the `.uproject`). Optional sanity check:
   `mp.DumpLiveProfileSettings` → candidate variant, rescue/legs/panel at 1.
2. **VR Preview** → showcase the current avatar → **Esc** to stop.
3. `mp.MirrorAvatar Maria` (or any cast id) → **VR Preview** again. Repeat.
4. Nothing needs re-arming between sessions — variant, trial, and tracers survive
   preview stop/start. The avatar choice resets to the level's saved default on the
   next editor restart unless you save the level (Ctrl+S) after switching.

**Why `mp.MetaHumanActiveProfile` alone does NOT work here, and is dangerous
mid-session:** the placed pawn re-applies its own property to that CVar at every play
start (`ApplySelectedAvatarProfileToRuntimeCVars`), so a console pre-set is silently
overwritten. Worse, changing the CVar DURING a session makes the spawned avatar's
profile "not active", which hard-disables the full arm-chain retargeter
(`MediaPipeMetaHumanArmRetargeter.cpp` rejects with "profile X is not active") — the
avatar stays visible but its shoulders/arms collapse to the legacy path and look
mangled. If you ever see that: `mp.MetaHumanActiveProfile <the avatar actually in the
room>` restores it instantly, no restart needed.

### 7d. Avatar sizes are native (and the metric lock, 2026-07-12)

Every cast member drives at its own authored stature - the AVATAR_METRIC_LOCK_PLAN
Phase 0 audit measured driven == reference pose within a few percent on the whole
cast (`Docs/avatar_metric_lock_baseline/phase4_cast_audit.md` has the per-avatar
bone-Z table; Emory is an authored SHORT ADULT at 94.5% of Kellan - the old "child
at 130%" reading was an imported-bounds artifact). `mp.AvatarMetricLock` (candidate
stack, default 0) additionally maps the fused-pose height targets about the floor by
a once-per-session embodiment scale S = avatar eye height / your standing HMD
baseline, so an avatar keeps ITS stature under a taller/shorter user whenever the
fused pose writer (`mp.BodyFusion.WritePose`) is armed; the accepted live stack runs
that writer off, so the lock is dark there. Bisect live, no restart:
`mp.AvatarMetricLock 0` <-> `1`. Per-actor evidence rows: `mp.EmbodimentScaleTrace`
(boot-armed, native-vs-driven spans + the latched S and its inputs).

## 8. Getting Claude (or any agent) up to speed at work

Session memory does not move between machines. Everything essential lives in this
repo — point a fresh agent at, in order:

1. `AGENTS.md` — operational rules: build wrapper, no Live Coding, stall handling,
   bridge ports, dataset protection.
2. `Docs/README.md` — the documentation index (which docs are active vs historical).
3. `Docs/REFACTOR_PLAN.md` §9 — the current corrector architecture, the execution
   log, and the deferred Phase-7 deletion with its re-arm condition.
4. The field notes below.

## 9. Field notes — hard-won lessons, so nobody re-learns them the slow way

- **The mirror avatar is the only judge.** Log rows and tests gate changes; only the
  worn mirror accepts them. Keep worn checks under 30 seconds each and batch them.
- **Node member state does not survive frames in live VR.** `CacheBones` wipes anim
  node members every frame when worn. All cross-frame state goes in the keyed store
  (`GetQuestWristRuntimeState(key)`); key 0 means never write. This class of bug
  passes every desk test and fails only worn.
- **Switching `mp.MediaPipeSettingsVariant` live does NOT reset candidate-only CVars
  that were set directly.** Restart or re-arm before an A/B.
- **The baseline variant deliberately keeps the old webcam-hand takeover**
  (`mp.MediaPipeHandRotationOnQuestLoss=1`, `mp.MediaPipeFingersOnQuestLoss=1`).
  It is not dead code — do not "clean it up". See `Docs/REFACTOR_PLAN.md` §9.
- **ripgrep silently returns nothing on `Saved/Logs`** — use GNU grep or Python for
  log mining.
- **In any arm-quality session, read `mp.ArmJumpTrace` and `mp.ArmDirCorrection` rows
  FIRST** — jumps self-attribute per stage, drift is the correction-angle curve.
- **Editor-wedge detection**: watch the log file's modification time, not MCP acks.
- **Log throttles must be keyed per actor+side** — a shared static throttle lets
  Manny starve Kellan of the exact rows you are judging Kellan with.
- **Any signal fusing hand-tracking vs body-chain positions must assume different
  latencies** — gate on sustained agreement, cap by plausible steady-state deficit.
  Every slow "bias eraser" needs: magnitude bound + quiet-gated learning +
  motion-faded application + slow learner tau.
- **GPU**: on the home 5070 Ti, long DirectML solves could hang the GPU device
  (driver-level, not thermal/OOM, `TdrDelay` does not fix it). The crash-proof path
  for long offline solves is the chunked `SetProcessingRange` solve+export workflow.
  A different work GPU may not have this problem — but if the machine hard-freezes
  mid-solve, this is what it is.
- **Corporate AV warning**: `ump_shared.dll` is a locally built, unsigned DLL. If
  webcam tracking silently does nothing at work, check whether antivirus quarantined
  it before debugging anything else.
- **AgentBridge port**: default 8765 may belong to another project's bridge — check
  `bridge.projectRoot` in `GET /status`, and start this project's bridge on 8766
  (`CODEX_AGENT_PORT=8766`) if so. Restart the bridge after relaunching the editor.
