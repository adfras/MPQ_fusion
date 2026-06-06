# MPQ Shadow Fusion Review Deliverables

**Date:** 2026-06-05  
**Package reviewed:** `TestingKit5_docs_source_scripts_20260605_081634.zip` plus `MPQ_SHADOW_FUSION_REVIEW_BRIEF_20260605.md`  
**Review mode:** source/evidence review only. I did not build the Unreal project or run a VR Preview session.

---

## 1. Executive conclusion

The immediate blocker is **capture-start reliability and observability**, not MediaPipe landmark availability.

The failed torso-forward-only trial was prepared and VR Preview ran, but no recorder-start line, no recorder-finished/wrote line, and no JSON file appeared. The current auto-start path can silently return before starting for multiple reasons, and the most suspicious design flaw is that the “armed” state is represented mainly by CVars while recorder state is a global singleton with a persistent `LastAutoStartWorldId` de-duplication gate.

Do **not** enable active MediaPipe authority yet. Keep the package in the requested shadow-only posture:

```text
mp.BodyFusion.Enable 1
mp.BodyFusion.Debug 1
mp.BodyFusion.MediaPipeAuthority 0
mp.BodyFusion.WritePose 0
```

Quest/HMD authority should remain in place for head, wrists, hands, fingers, and reliable arm endpoints. MediaPipe arm fallback remains out of scope.

---

## 2. Priority-ranked deliverables

| Priority | Deliverable | Output |
|---:|---|---|
| P0 | Make MPQ capture auto-start observable | Add explicit `armed`, `start`, `skip`, `finish`, and `write` logs with reason codes. |
| P0 | Remove or scope the world-id silent de-dupe hazard | Reset/replace `LastAutoStartWorldId` when a new MPQ trial is prepared; preferably scope it to an arm serial. |
| P0 | Make the pending capture a real state object | Replace “CVar-only armed state” with a one-shot pending request containing serial, path, duration, analyze flag, prepared time, and start world. |
| P1 | Record finish metadata | Write actual elapsed seconds, sample time span, end reason, start wall time, end wall time, and write/analyze outcome. |
| P1 | Separate MPQ recorder state from Manny head recorder state | The MPQ recorder and default head-trace recorder share `GMannyBoneTimeseriesRecorder`; split or make the shared state mode-aware. |
| P1 | Add candidate-shadow torso/pelvis signals | Do not validate Stage 1 against actual output bones that are intentionally HMD/Profile/Quest-driven while `WritePose=0` and `MediaPipeAuthority=0`. |
| P2 | Add automation tests for auto-start decisions | Cover skip reasons, new arm serial behavior, shadow-only CVar preservation, and finish metadata. |
| P2 | Run one clean torso-forward-only calibration after P0/P1 | Only evaluate Stage 1 after capture start/finish is reliable and non-flat candidate signals are recorded. |

---

## 3. Current capture-start path

The current path is:

```text
mp.PrepareMPQShadowLatencyTrial
  -> applies shadow-only CVars
  -> sets mp.RecordMPQShadowFusionOnPlay=1
  -> sets duration/analyze/path CVars
  -> logs prepare line

VR Preview / PIE begins
  -> SpawnAutoQuestWebcamHandsNextTick(...)
  -> SpawnAutoQuestWebcamHands(...)
  -> source/Manny may be found or spawned
  -> spawned Manny receives LiveMannyTag
  -> Manny Tick
  -> RecordMannyBoneTimeseriesSample(this, DrivenMesh, DeltaSeconds)
  -> TryAutoStartMPQShadowFusionTimeseries(Actor)
  -> StartMannyBoneTimeseriesRecording(...)
```

Important source points:

- MPQ auto-start is controlled by `mp.RecordMPQShadowFusionOnPlay`, duration, output path, and analyze CVars in `Source/MediaPipeDriver/PoseDriven/MediaPipePoseDrivenSkeletalActor.cpp:74-96`.
- The recorder is global singleton state in `FMannyBoneTimeseriesRecorder`, including `bActive`, `Mode`, `AutoStartWorldId`, and `LastAutoStartWorldId`, at `MediaPipePoseDrivenSkeletalActor.cpp:195-210`.
- `PrepareMPQShadowLatencyTrial` applies shadow-only CVars and arms auto-start by setting `mp.RecordMPQShadowFusionOnPlay=1`, duration, analyze, and output path at `MediaPipePoseDrivenSkeletalActor.cpp:830-906`.
- Actual auto-start only occurs from `TryAutoStartMPQShadowFusionTimeseries`, called by the Manny sample recorder path, at `MediaPipePoseDrivenSkeletalActor.cpp:952-992` and `MediaPipePoseDrivenSkeletalActor.cpp:1040-1048`.
- The sample recorder path returns before auto-start if `Actor` is null, `DrivenMesh` is null, or the actor lacks `LiveMannyTag`; these early returns are silent at `MediaPipePoseDrivenSkeletalActor.cpp:1040-1044`.
- `EndPlay` only stops/writes if the actor has `LiveMannyTag`, at `MediaPipePoseDrivenSkeletalActor.cpp:1564-1569`.
- `Tick` is the caller, at `MediaPipePoseDrivenSkeletalActor.cpp:1981`.

---

## 4. Most likely reasons the prepared trial can fail silently

### 4.1 Auto-start is dependent on a tagged Manny actor tick

The MPQ auto-start helper is not called directly from PIE/VR Preview start. It is called after the live Manny actor ticks and passes the `Actor`, `DrivenMesh`, and `LiveMannyTag` gate. If the Manny actor is not spawned, not tagged, not ticking, or lacks a driven mesh, the capture never starts and there is no MPQ-specific skip reason.

The runtime spawn path adds `LiveMannyTag` when it spawns the Manny actor at `Source/MediaPipeDriver/Runtime/MediaPipeDriverRuntime.cpp:3663-3684`, but if the runtime returns before spawn, the recorder path may never execute. Relevant return paths include:

- Auto Quest webcam disabled or non-auto world: `MediaPipeDriverRuntime.cpp:3568-3573`.
- Existing source/Manny/MetaHuman actors already found: `MediaPipeDriverRuntime.cpp:3613-3617`; this return has no recorder-arm diagnostic.
- Capture device resolution failure: `MediaPipeDriverRuntime.cpp:3620-3625`.
- Deferred next-tick PIE startup: `MediaPipeDriverRuntime.cpp:3785-3805`.

### 4.2 Multiple auto-start gates return without logging

`TryAutoStartMPQShadowFusionTimeseries` currently returns silently for:

```cpp
if (!Actor || GMannyBoneTimeseriesRecorder.bActive || CVarRecordMPQShadowFusionOnPlay.GetValueOnAnyThread() == 0)
{
    return;
}

if (!World || (World->WorldType != EWorldType::PIE && World->WorldType != EWorldType::Game))
{
    return;
}

if (WorldId != 0 && GMannyBoneTimeseriesRecorder.LastAutoStartWorldId == WorldId)
{
    return;
}
```

Source: `MediaPipePoseDrivenSkeletalActor.cpp:952-969`.

The third gate is especially risky because `LastAutoStartWorldId` persists in the global recorder across preview sessions and is not reset in `PrepareMPQShadowLatencyTrial`. If Unreal reuses a world ID, or if the same world ID remains visible to the next armed attempt, the new trial can be rejected without any log line.

### 4.3 MPQ and head-trace modes share one recorder singleton

`mp.RecordMannyHeadOnPlay` defaults to `1` at `MediaPipePoseDrivenSkeletalActor.cpp:38-42`. The head recorder and MPQ recorder share `GMannyBoneTimeseriesRecorder` and the same `LastAutoStartWorldId` field. MPQ is attempted before head trace in `RecordMannyBoneTimeseriesSample`, but this is still a coupling risk:

- a head trace can make the recorder active,
- either mode can update the shared world-id de-dupe field,
- a skip caused by `bActive` or `LastAutoStartWorldId` currently has no log.

Source: `MediaPipePoseDrivenSkeletalActor.cpp:994-1038`.

### 4.4 The prepare command does not create a real pending request

`PrepareMPQShadowLatencyTrial` logs that a capture is prepared, but the pending request is spread across CVars. It does not store a serial, reset the world de-dupe state, track a prepared timestamp, track a “must start by” deadline, or emit a terminal failure if no start occurs.

That means the system can be “armed” from the user’s perspective while the code has no single pending capture object to diagnose.

---

## 5. Evidence summary

### 5.1 Log evidence

| Trial | Prepare line | VR Preview | Recorder start | Recorder write | JSON evidence |
|---|---:|---:|---:|---:|---|
| Camo 384 | `2026.06.05-05.39.26` | `07.01.18` | `07.01.24`, world `65025` | `1322 samples`, requested `45.000s` | Present in evidence reports |
| Camo 256 | `07.07.28` | `07.08.50` | `07.08.51`, world `83617` | `1032 samples`, requested `45.000s` | Present in logs |
| Calibration torso/shoulder | `07.29.10` | `07.31.06` | `07.31.07`, world `83615` | `1328 samples`, requested `60.000s` | Present in evidence reports |
| Failed torso-forward-only | `07.36.04` | `07.37.38` | **missing** | **missing** | `No mpq_shadow_latency_calib_torso_forward_only_*.json` |

Source: `Evidence/MPQShadowFusion/Logs/TestingKit5_mpq_shadow_excerpt.txt:1-21` and `Evidence/MPQShadowFusion/Logs/missing_torso_forward_capture_check.txt:1`.

The calibration torso/shoulder run requested 60 seconds, but the evidence report contains about 47.439 seconds of usable samples. The log shows VR Preview teardown at `07.31.55`, about 47.5 seconds after recorder start at `07.31.07`, so early preview teardown is the most likely reason for the shorter-than-requested capture. The current write log prints requested duration, not actual elapsed duration, so this is easy to misread.

### 5.2 Analysis metrics

| Evidence folder | Samples | Usable duration | MP body coverage | Estimated lag | Fitted alignment | Stage readiness |
|---|---:|---:|---:|---:|---:|---|
| `camo384_raw_analysis` | 1322 | 44.996s | all-33 median 1.0, min 1.0 | median 175.591 ms; p05 -29.265 ms; p95 197.539 ms; n=26 | not run in this folder | Stage 1 false; Stage 2 false |
| `camo384_fit_advance176_v2` | 1322 | 44.996s | all-33 median 1.0, min 1.0 | median 14.629 ms after 175.5906 ms advance; n=26 | 0/20 rows pass; Stage 1 0; Stage 2 0 | Stage 1 false; pair-level Stage 2 true, but fitted Stage 2 false |
| `calib_iso_torso_shoulder_raw` | 1328 | 47.439s | all-33 median 1.0, min 1.0 | median 71.059 ms; p05 -192.527 ms; p95 244.156 ms; n=4 | 0/20 rows pass | Stage 1 false; Stage 2 false |
| `calib_iso_torso_shoulder_fit_advance071` | 1328 | 47.439s | all-33 median 1.0, min 1.0 | median 17.736 ms after 71.0588 ms advance; p05 -197.802 ms; p95 142.510 ms; n=4 | 0/20 rows pass; Stage 1 0; Stage 2 0 | Stage 1 false; Stage 2 false |

The strongest Camo evidence does not support “MediaPipe is missing landmarks” as the main issue. The Camo capture has complete body coverage in the report and fresh HMD/Quest arm chain samples for essentially the full capture:

| Run | HMD freshness | Left arm chain | Right arm chain | Authority state |
|---|---:|---:|---:|---|
| Camo 384 | 1321 fresh, 1 missing | 1321 fresh, 1 missing | 1321 fresh, 1 missing | 1322 `no_mediapipe`; 1321 `trace-only` |
| Calibration torso/shoulder | 1327 fresh, 1 missing | 1224 fresh, 103 stale, 1 missing | 1224 fresh, 103 stale, 1 missing | 1328 `no_mediapipe`; 1327 `trace-only` |

### 5.3 Timing evidence

| Run | Capture-to-sample p50 | Capture-to-sample p95 | Capture-to-publish p50 | Native process p50 | Publish-to-sample p50 |
|---|---:|---:|---:|---:|---:|
| Camo 384 | 76.645 ms | 91.970 ms | 51.371 ms | 35.202 ms | 17.097 ms |
| Calibration torso/shoulder | 75.879 ms | 87.348 ms | 49.303 ms | 34.677 ms | 16.487 ms |

The Camo raw waveform lag estimate is about 175.6 ms, but internal capture-to-sample timing is roughly 76.6 ms median and 92.0 ms p95. After applying the 175.5906 ms fitted advance, residual median lag drops, but fitted alignment still fails 0/20 rows. This points away from raw MediaPipe processing time as the sole issue and toward some combination of timeline semantics, sample pairing, coordinate mapping, scale/origin, axis signs, calibration quality, or unsuitable comparison targets.

---

## 6. Why flat fused/output pelvis or torso targets are expected under shadow-only capture

The current shadow-only posture intentionally prevents MediaPipe from owning visible output:

- `ApplyMPQShadowFusionCaptureCVars` sets `mp.BodyFusion.WritePose=0` and `mp.BodyFusion.MediaPipeAuthority=0` at `MediaPipePoseDrivenSkeletalActor.cpp:810-828`.
- The authority policy returns `trace-only`, `NoMediaPipe`, and `bAllowMediaPipePoseAuthority=0` when authority mode is `0` at `Source/MediaPipeDriver/BodyFusion/MediaPipeBodyFusionAuthorityPolicy.cpp:139-152`.
- The BodyFusion solver only treats MediaPipe as lower-body owner when `bAllowMediaPipePoseAuthority` is true and calibration is usable at `Source/MediaPipeDriver/BodyFusion/MediaPipeBodyFusion.cpp:303-318`.
- The best-available output pose uses BodyFusion only when `WritePose` is enabled; otherwise HMD, arm chain, hand tracking, or motion controller sources remain authoritative at `Source/MediaPipeDriver/Embodiment/EmbodiedFusionComponent.cpp:612-730`.

Therefore some `fused.*`, `output.*`, or Manny bone comparison targets may be flat or low-motion by design during shadow-only capture. They should not be used as proof that active pelvis/torso authority is safe.

Recommended fix: record separate candidate-shadow signals, for example:

```text
shadow_candidate.pelvis.loc.{x,y,z}
shadow_candidate.chest.loc.{x,y,z}
shadow_candidate.torso_forward_proxy
shadow_candidate.shoulder_mid.{x,y,z}
```

These should be computed from MediaPipe under the candidate calibration transform but never written to visible bones. Then score candidate-shadow signals against HMD/Quest/body-fusion reference signals separately from actual output authority.

---

## 7. Patch plan

### 7.1 Add an explicit pending request

Add a pending MPQ request separate from the generic recorder:

```cpp
struct FMPQShadowAutoStartRequest
{
    bool bArmed = false;
    uint32 ArmSerial = 0;
    double PreparedAtSeconds = 0.0;
    double StartDeadlineSeconds = 0.0;
    double DurationSeconds = 0.0;
    bool bAnalyzeAfterWrite = true;
    FString OutputPath;
    uint32 StartedWorldId = 0;
    FString Label;
};

static FMPQShadowAutoStartRequest GMPQShadowAutoStartRequest;
static uint32 GMPQShadowNextArmSerial = 0;
```

On `mp.PrepareMPQShadowLatencyTrial`:

```cpp
++GMPQShadowNextArmSerial;
GMPQShadowAutoStartRequest.bArmed = true;
GMPQShadowAutoStartRequest.ArmSerial = GMPQShadowNextArmSerial;
GMPQShadowAutoStartRequest.PreparedAtSeconds = FPlatformTime::Seconds();
GMPQShadowAutoStartRequest.StartDeadlineSeconds = GMPQShadowAutoStartRequest.PreparedAtSeconds + 10.0;
GMPQShadowAutoStartRequest.DurationSeconds = DurationSeconds;
GMPQShadowAutoStartRequest.bAnalyzeAfterWrite = true;
GMPQShadowAutoStartRequest.OutputPath = OutputPath;
GMPQShadowAutoStartRequest.Label = Label;

// Minimum safety fix if LastAutoStartWorldId remains:
GMannyBoneTimeseriesRecorder.LastAutoStartWorldId = 0;

UE_LOG(LogMediaPipePose, Log,
    TEXT("mp.MPQShadowAutoStart: armed serial=%u duration=%.3fs path=%s shadowOnly=1 authority=0 writePose=0 armFallbacks=off"),
    GMPQShadowAutoStartRequest.ArmSerial,
    DurationSeconds,
    *OutputPath);
```

Better than resetting world ID is to replace world-id-only de-dupe with `(ArmSerial, WorldId)` or a `StartedArmSerial` field. A newly prepared serial should never be silently blocked just because the world ID matches a previous session.

### 7.2 Log every skip reason while armed

Add a small reason enum and a once/throttled skip logger:

```cpp
enum class EMPQShadowAutoStartSkipReason : uint8
{
    NotArmed,
    NoActor,
    NoDrivenMesh,
    MissingLiveMannyTag,
    RecorderAlreadyActive,
    WrongWorldType,
    DuplicateWorldForSameArm,
    CaptureDeviceNotReady,
    MannyNotSpawned,
    SourceActorMissing,
    WarmupNotReady,
};

static void LogMPQShadowAutoStartSkip(
    EMPQShadowAutoStartSkipReason Reason,
    const UWorld* World,
    const AActor* Actor,
    const USkeletalMeshComponent* DrivenMesh,
    const TCHAR* Detail)
{
    if (!GMPQShadowAutoStartRequest.bArmed)
    {
        return;
    }

    UE_LOG(LogMediaPipePose, Warning,
        TEXT("mp.MPQShadowAutoStart: skipped serial=%u reason=%s worldId=%u worldType=%s actor=%s hasDrivenMesh=%d recorderActive=%d recorderMode=%s path=%s detail=%s"),
        GMPQShadowAutoStartRequest.ArmSerial,
        *LexToString(Reason),
        World ? World->GetUniqueID() : 0,
        World ? *UEnum::GetValueAsString(World->WorldType) : TEXT("<none>"),
        *GetNameSafe(Actor),
        DrivenMesh ? 1 : 0,
        GMannyBoneTimeseriesRecorder.bActive ? 1 : 0,
        *GMannyBoneTimeseriesRecorder.Mode,
        *GMPQShadowAutoStartRequest.OutputPath,
        Detail ? Detail : TEXT(""));
}
```

Implementation note: Unreal will need a real `LexToString(EMPQShadowAutoStartSkipReason)` helper or `switch` because `LexToString` will not exist automatically for this custom enum.

### 7.3 Move tag/mesh skip logging before the current early return

Current code silently returns before auto-start if actor, mesh, or tag is missing. Change the shape of `RecordMannyBoneTimeseriesSample` so an armed MPQ request can report why it did not reach start:

```cpp
void RecordMannyBoneTimeseriesSample(
    const AMediaPipePoseDrivenSkeletalActor* Actor,
    USkeletalMeshComponent* DrivenMesh,
    const float DeltaSeconds)
{
    const bool bMPQArmed = GMPQShadowAutoStartRequest.bArmed ||
        CVarRecordMPQShadowFusionOnPlay.GetValueOnAnyThread() != 0;

    if (!Actor)
    {
        if (bMPQArmed) { LogMPQShadowAutoStartSkip(EMPQShadowAutoStartSkipReason::NoActor, nullptr, nullptr, DrivenMesh, TEXT("RecordMannyBoneTimeseriesSample")); }
        return;
    }

    const UWorld* World = Actor->GetWorld();
    if (!DrivenMesh)
    {
        if (bMPQArmed) { LogMPQShadowAutoStartSkip(EMPQShadowAutoStartSkipReason::NoDrivenMesh, World, Actor, nullptr, TEXT("DrivenMesh missing before auto-start")); }
        return;
    }

    if (!Actor->Tags.Contains(LiveMannyTag))
    {
        if (bMPQArmed) { LogMPQShadowAutoStartSkip(EMPQShadowAutoStartSkipReason::MissingLiveMannyTag, World, Actor, DrivenMesh, TEXT("Actor did not contain LiveMannyTag")); }
        return;
    }

    TryAutoStartMPQShadowFusionTimeseries(Actor, DrivenMesh);
    TryAutoStartMannyHeadTimeseries(Actor);
    ...
}
```

### 7.4 Make the MPQ auto-start helper mode-aware

Change `TryAutoStartMPQShadowFusionTimeseries` to accept the mesh and pending request, then log rather than silently return:

```cpp
void TryAutoStartMPQShadowFusionTimeseries(
    const AMediaPipePoseDrivenSkeletalActor* Actor,
    const USkeletalMeshComponent* DrivenMesh)
{
    if (!GMPQShadowAutoStartRequest.bArmed &&
        CVarRecordMPQShadowFusionOnPlay.GetValueOnAnyThread() == 0)
    {
        return;
    }

    if (!Actor)
    {
        LogMPQShadowAutoStartSkip(EMPQShadowAutoStartSkipReason::NoActor, nullptr, nullptr, DrivenMesh, TEXT("auto-start helper"));
        return;
    }

    const UWorld* World = Actor->GetWorld();
    if (!World || (World->WorldType != EWorldType::PIE && World->WorldType != EWorldType::Game))
    {
        LogMPQShadowAutoStartSkip(EMPQShadowAutoStartSkipReason::WrongWorldType, World, Actor, DrivenMesh, TEXT("expected PIE or Game"));
        return;
    }

    if (GMannyBoneTimeseriesRecorder.bActive)
    {
        LogMPQShadowAutoStartSkip(EMPQShadowAutoStartSkipReason::RecorderAlreadyActive, World, Actor, DrivenMesh, TEXT("recorder already active"));
        return;
    }

    const uint32 WorldId = World->GetUniqueID();
    if (WorldId != 0 &&
        GMPQShadowAutoStartRequest.StartedWorldId == WorldId &&
        GMPQShadowAutoStartRequest.ArmSerial == GMPQShadowStartedArmSerial)
    {
        LogMPQShadowAutoStartSkip(EMPQShadowAutoStartSkipReason::DuplicateWorldForSameArm, World, Actor, DrivenMesh, TEXT("same arm serial already started in this world"));
        return;
    }

    ApplyMPQShadowFusionCaptureCVars();

    StartMannyBoneTimeseriesRecording(
        GMPQShadowAutoStartRequest.DurationSeconds,
        GMPQShadowAutoStartRequest.OutputPath,
        true,
        WorldId,
        TEXT("auto_mpq_shadow_fusion"),
        GMPQShadowAutoStartRequest.bAnalyzeAfterWrite ? FString(TEXT("Tools/AnalyzeMPQShadowFusionCapture.py")) : FString());

    GMPQShadowAutoStartRequest.StartedWorldId = WorldId;
    GMPQShadowStartedArmSerial = GMPQShadowAutoStartRequest.ArmSerial;
    GMPQShadowAutoStartRequest.bArmed = false;

    UE_LOG(LogMediaPipePose, Log,
        TEXT("mp.MPQShadowAutoStart: started serial=%u worldId=%u duration=%.3fs path=%s analyzer=%d"),
        GMPQShadowStartedArmSerial,
        WorldId,
        GMannyBoneTimeseriesRecorder.DurationSeconds,
        *GMannyBoneTimeseriesRecorder.OutputPath,
        GMPQShadowAutoStartRequest.bAnalyzeAfterWrite ? 1 : 0);
}
```

### 7.5 Add PIE-ready / spawn-path diagnostics

At PIE ready and after `SpawnAutoQuestWebcamHands`, log the armed state and whether the runtime found or created the source/Manny actors:

```text
mp.MPQShadowAutoStart: pieReady serial=17 worldId=83615 armed=1
mp.MPQShadowAutoStart: spawnProbe serial=17 source=MP_AutoQuestSource manny=MP_LiveMediaPipeManny hasLiveMannyTag=1 captureDevice=Camo index=1
```

If MPQ remains armed for more than a few seconds after PIE start, emit one warning:

```text
mp.MPQShadowAutoStart: skipped serial=17 reason=MannyNotSpawned elapsedSincePie=2.0s path=...
```

This closes the current blind spot where the next VR Preview can begin and end without proving whether the recorder path was ever reached.

### 7.6 Finish/write metadata

Change `StopMannyBoneTimeseries()` to accept an end reason:

```cpp
enum class EMannyBoneTimeseriesEndReason : uint8
{
    DurationReached,
    EndPlay,
    ManualStop,
    SerializeFailed,
    WriteFailed,
};

void StopMannyBoneTimeseries(EMannyBoneTimeseriesEndReason Reason)
{
    if (!GMannyBoneTimeseriesRecorder.bActive)
    {
        UE_LOG(LogMediaPipePose, Verbose,
            TEXT("mp.RecordMannyBoneTimeseries: stop ignored inactive reason=%s mode=%s"),
            *LexToString(Reason),
            *GMannyBoneTimeseriesRecorder.Mode);
        return;
    }

    const double EndSeconds = FPlatformTime::Seconds();
    const double ActualElapsedSeconds = FMath::Max(0.0, EndSeconds - GMannyBoneTimeseriesRecorder.StartSeconds);
    GMannyBoneTimeseriesRecorder.bActive = false;

    UE_LOG(LogMediaPipePose, Log,
        TEXT("mp.RecordMannyBoneTimeseries: finished mode=%s reason=%s actualElapsed=%.3fs requested=%.3fs samples=%d path=%s"),
        *GMannyBoneTimeseriesRecorder.Mode,
        *LexToString(Reason),
        ActualElapsedSeconds,
        GMannyBoneTimeseriesRecorder.DurationSeconds,
        GMannyBoneTimeseriesRecorder.SampleCount,
        *GMannyBoneTimeseriesRecorder.OutputPath);

    WriteMannyBoneTimeseries(Reason, ActualElapsedSeconds);
}
```

Also write the following fields into the JSON root:

```json
{
  "requested_duration_seconds": 45.0,
  "actual_elapsed_seconds": 44.98,
  "sample_time_span_seconds": 44.95,
  "end_reason": "duration_reached",
  "start_wall_seconds": 123456.0,
  "end_wall_seconds": 123500.98,
  "arm_serial": 17,
  "write_status": "ok",
  "analyzer_status": "ok"
}
```

---

## 8. Tests to add

The current diagnostics test only verifies registration/defaults for MPQ commands and CVars at `Source/MediaPipeDriver/Tests/MediaPipeDiagnosticsTests.cpp:1121-1146`. Add tests for behavior, not just registration.

### 8.1 Pure decision tests

Create a small pure helper, for example `ResolveMPQShadowAutoStartDecision(Input)`, and test:

| Case | Expected decision |
|---|---|
| MPQ not armed | `NotArmed`, no warning required |
| Armed but no actor | `NoActor`, warning log |
| Armed but no driven mesh | `NoDrivenMesh`, warning log |
| Armed but missing `LiveMannyTag` | `MissingLiveMannyTag`, warning log |
| Armed but wrong world type | `WrongWorldType`, warning log |
| Armed and recorder active in `auto_head_trace` | `RecorderAlreadyActive`, includes existing mode/path |
| New arm serial in same world ID | **start allowed** |
| Same arm serial already started in same world ID | `DuplicateWorldForSameArm` |

### 8.2 CVar preservation test

After `mp.PrepareMPQShadowLatencyTrial`, assert:

```text
mp.BodyFusion.Enable == 1
mp.BodyFusion.Debug == 1
mp.BodyFusion.WritePose == 0
mp.BodyFusion.MediaPipeAuthority == 0
mp.QuestArmDropoutDownFallback == 0
mp.QuestConstrainedArmBodyFallback == 0
mp.MediaPipeArmHoldOnQuestHandLoss == 0
```

### 8.3 Finish metadata test

Start a recorder with a short duration, stop it with `DurationReached`, and assert the JSON root contains:

```text
requested_duration_seconds
actual_elapsed_seconds
sample_time_span_seconds
end_reason
sample_count
mode
arm_serial or auto_start_world_id
```

### 8.4 Log contract test

A prepared trial must produce exactly one terminal outcome:

```text
armed -> started -> finished -> wrote
```

or

```text
armed -> skipped(reason=...)
```

A run that produces `armed` but neither `started` nor `skipped` should fail the diagnostic test.

---

## 9. Alignment and synchronization review

### 9.1 What the evidence already rules out

The Camo 384 capture had complete body landmark coverage and fresh HMD/Quest data for nearly all samples. The issue is not primarily “MediaPipe has no body landmarks.”

### 9.2 What remains plausible

The failures still point to:

- timeline mismatch between MediaPipe capture time, publish time, sample time, HMD time, and Quest time;
- sample pairing mismatch;
- coordinate-space mismatch;
- axis sign inversion;
- scale mismatch;
- origin/height calibration mismatch;
- comparison targets that are flat because shadow-only output is intentionally not MediaPipe-driven;
- calibration motion that is not sufficiently isolated.

### 9.3 Timeline instrumentation to add

Every sample should record these fields side by side:

```text
sample_index
sample_wall_seconds
world_time_seconds
hmd_sample_wall_seconds
quest_left_sample_wall_seconds
quest_right_sample_wall_seconds
mp_capture_wall_seconds
mp_enqueue_wall_seconds
mp_worker_start_wall_seconds
mp_publish_wall_seconds
mp_pose_timestamp_seconds
mpq_recorder_start_wall_seconds
```

Then the analyzer should compute deltas from the same origin and flag mixed-clock comparisons. The current gap between internal capture-to-sample timing and waveform lag makes this necessary before fitting authority behavior.

---

## 10. Minimum clean trial before Stage 1 pelvis/torso hints

Run this only after the P0 capture-start patch is in place:

```text
mp.PrepareMPQShadowLatencyTrial maxdim=384 duration=45 prediction=1 maxPredictionMs=50 label=calib_torso_forward_only_fix1
```

Expected log contract:

```text
mp.MPQShadowAutoStart: armed serial=<N> ... shadowOnly=1 authority=0 writePose=0 armFallbacks=off
mp.MPQShadowAutoStart: started serial=<N> worldId=<W> ...
mp.RecordMannyBoneTimeseries: recording ... mode=auto_mpq_shadow_fusion ...
mp.RecordMannyBoneTimeseries: finished mode=auto_mpq_shadow_fusion reason=duration_reached actualElapsed=...
mp.RecordMannyBoneTimeseries: wrote <samples> requested=45.000s actualElapsed=... path=...
```

Trial motion:

1. 5 seconds neutral stillness.
2. 30-35 seconds torso forward/back only, slow and repeatable, 3 cycles, no deliberate arm/shoulder motion.
3. 5 seconds neutral stillness.
4. Full body visible to Camo; HMD and Quest hands/wrists tracked throughout.

Minimum pass gates before considering Stage 1 candidate hints:

| Gate | Required result |
|---|---|
| Recorder observability | explicit armed, started, finished, wrote lines and JSON file |
| Duration | actual elapsed/sample span within 1 second of request, or explicit teardown reason |
| Landmark coverage | MediaPipe body all-33 valid fraction >= 0.99 |
| HMD/Quest freshness | HMD and both Quest arm chains >= 0.99 fresh; no long stale segment |
| Timing | capture-to-sample p95 under about 120 ms, with all clocks recorded from a common basis |
| Residual lag | Stage 1 fitted residual lag within 100 ms, ideally much lower |
| Candidate signals | torso/pelvis candidate-shadow targets are non-flat |
| Fit quality | Stage 1 torso and pelvis rows pass fitted alignment; no axis inversion/low-correlation/high-error flags |
| Safety posture | `MediaPipeAuthority=0`, `WritePose=0`, arm fallbacks off |

Do not use this trial to enable visible authority. Use it to validate the capture system and the offline candidate-shadow transform.

---

## 11. Direct answers to the reviewer questions

### Why can a prepared MPQ shadow-fusion capture fail to start silently when the next VR Preview begins?

Because the prepare command only arms CVars and the actual start occurs later from a tagged live Manny actor tick. Several gates before and inside `TryAutoStartMPQShadowFusionTimeseries` return without logging. If the live Manny path is not reached, the recorder is already active, the world is rejected, or the world-id de-dupe fires, the trial can end with no start/write line.

### Is capture start gated by runtime session identity, world identity, actor tick, tag lookup, or previous recorder state?

Yes. It is gated by actor tick, `LiveMannyTag`, driven mesh availability, PIE/Game world type, global recorder `bActive`, and `LastAutoStartWorldId`. Runtime spawn/source availability can also prevent the tick path from ever being reached.

### Can the recorder log an explicit skip reason whenever auto-start is armed but not started?

Yes, and it should. Add a pending request serial and log each armed skip reason once or throttled. The log must include serial, world ID/type, actor, mesh present, LiveManny tag present, recorder active/mode/path, output path, and reason.

### Are MediaPipe timestamps being compared on the same wall-clock/sample timeline as HMD and Quest samples?

The current evidence is not sufficient to prove that. Internal MediaPipe timing and waveform lag disagree enough that all sample clocks should be recorded side by side and normalized in the analyzer.

### Is the MediaPipe body pose being converted into the same coordinate space, axis signs, scale, and origin as the HMD/Quest/body-fusion frame?

Not proven. The post-advance fitted alignment still fails 0/20 rows in both fitted analysis folders, with low correlation, high fit error, residual lag, and flat target flags. Coordinate transform, scale, origin, and axis sign checks remain necessary.

### Why are some fused/output pelvis or torso comparison targets flat during shadow-only capture?

Because shadow-only mode intentionally prevents MediaPipe from writing visible pose or owning lower body authority. Some actual output/fused targets are therefore HMD/Profile/Quest-derived or otherwise unsuitable as MediaPipe authority validation targets. Add candidate-shadow torso/pelvis signals for offline validation.

### Is the shorter-than-requested calibration capture caused by VR Preview teardown, recorder duration handling, source warmup, or timestamp filtering?

The log strongly suggests VR Preview teardown: recorder start at `07.31.07`, teardown at `07.31.55`, and report duration about `47.439s`. However, the recorder write log reports requested duration rather than actual elapsed duration, so finish metadata should be added to make this unambiguous.

### What minimum single-motion calibration trial should be required before Stage 1 pelvis/torso hints are allowed?

A clean torso-forward-only trial with explicit start/finish/write logs, JSON output, complete MediaPipe body coverage, stable HMD/Quest freshness, timeline fields recorded on a common basis, non-flat candidate-shadow torso/pelvis signals, low residual lag, and fitted Stage 1 pass rows. Even after that, enable only candidate-shadow diagnostics first; visible pelvis/torso authority should remain off until repeatability is proven.

---

## 12. Recommended immediate patch checklist

```text
[ ] Add MPQ arm serial and pending request state.
[ ] Reset or replace LastAutoStartWorldId on every new prepared trial.
[ ] Emit mp.MPQShadowAutoStart: armed log.
[ ] Emit mp.MPQShadowAutoStart: started log with serial and world ID.
[ ] Emit mp.MPQShadowAutoStart: skipped log for every armed early return.
[ ] Log from PIE-ready/spawn path when MPQ is armed but Manny/source is missing.
[ ] Split MPQ/head recorder state or make shared recorder mode-aware.
[ ] Add end reason and actual elapsed duration to Stop/Write JSON.
[ ] Add candidate-shadow pelvis/torso signals.
[ ] Add automation tests for skip decisions and shadow-only CVar preservation.
[ ] Re-run torso-forward-only calibration after patch.
```

---

## 13. One-line reviewer response

The package should remain shadow-only; fix the MPQ recorder as a real one-shot, serial-scoped, fully logged state machine first, because the latest failure is a silent auto-start/recorder observability failure and the existing alignment reports still do not prove safe Stage 1 pelvis/torso authority.
