#pragma once

#include "CoreMinimal.h"
#include "MediaPipeTrackingFusionDatasetReplay.h"

struct FEmbodiedFusionSourceObservations;

// DYADIC_STUDY_PLAN Phase 0: per-mesh row-stream drive.
//
// The global dataset replay (FMediaPipeTrackingFusionDatasetReplayRuntime::Get()) switches
// EVERY pose-driven mesh in the process to the same recording. The dyad platform needs the
// opposite shape: ONE mesh (the ghost/partner avatar) driven from a row stream while every
// other mesh stays live-driven. FMediaPipeDyadRowStream wraps a private replay-runtime
// instance (same parser, same sample store) with segment/loop pacing owned here, and
// FMediaPipeDyadRowStreamRegistry binds streams to skeletal-mesh component keys (the same
// uint32 GetUniqueID() keys the solver's keyed runtime-state stores use; key 0 is invalid,
// matching the keyed-store rule).
//
// With no bindings registered the registry is a single atomic read on the anim-node hot
// path — byte-identical behavior to the pre-dyad build.

// Anything that can hand a mesh its observations for "now": a recorded row stream
// (ghost/recorded partner), the local live pose tee (lobby partner-preview puppeting),
// or — Phase 3 — the network wire. Game-thread contract.
class MEDIAPIPEDRIVER_API FMediaPipeDyadObservationSource
{
public:
	virtual ~FMediaPipeDyadObservationSource() = default;
	virtual bool GetObservationsNow(
		double WorldNowSeconds,
		FEmbodiedFusionSourceObservations& OutObservations,
		FString* OutPhaseName = nullptr) = 0;
};

// The local live pose, re-published: the lobby's partner-preview consumes the SAME
// observations the live pawn's fusion just polled from its sensors, so one participant
// puppets both preview avatars. Publish() each frame from whoever owns the tee.
class MEDIAPIPEDRIVER_API FMediaPipeDyadLiveObservationTee : public FMediaPipeDyadObservationSource
{
public:
	void Publish(const FEmbodiedFusionSourceObservations& Observations)
	{
		Latest = Observations;
		bHasObservations = true;
	}

	virtual bool GetObservationsNow(
		double WorldNowSeconds,
		FEmbodiedFusionSourceObservations& OutObservations,
		FString* OutPhaseName = nullptr) override
	{
		if (!bHasObservations)
		{
			return false;
		}
		OutObservations = Latest;
		if (OutPhaseName)
		{
			*OutPhaseName = TEXT("live_tee");
		}
		return true;
	}

private:
	FEmbodiedFusionSourceObservations Latest;
	bool bHasObservations = false;
};

class MEDIAPIPEDRIVER_API FMediaPipeDyadRowStream : public FMediaPipeDyadObservationSource
{
public:
	// Loads a schema-v2 replay cache (manifest .json or direct .jsonl) through the proven
	// replay parser. Synchronous; call at spawn/bind time, not per frame.
	bool Load(const FString& Path, FString& OutError);

	// Selects the looping segment. StartSeconds clamps into the recording; DurationSeconds
	// <= 0 means "to the end of the recording". Safe to call before or after StartAt.
	void ConfigureSegment(double StartSeconds, double DurationSeconds);

	// Latches the world-clock origin the loop pacing runs against.
	void StartAt(double WorldNowSeconds);

	// Maps world time into the configured segment (looping seamlessly) and returns that
	// row's observations restamped to WorldNowSeconds, so downstream freshness checks see
	// a live-paced signal across loop seams. Returns false only when not loaded.
	virtual bool GetObservationsNow(
		double WorldNowSeconds,
		FEmbodiedFusionSourceObservations& OutObservations,
		FString* OutPhaseName = nullptr) override;

	// Pacing math exposed for unit tests: the dataset time a given world time maps to.
	double ResolveDatasetTimeSeconds(double WorldNowSeconds) const;

	bool IsLoaded() const { return bLoaded; }
	double GetRecordingDurationSeconds() const { return RecordingDurationSeconds; }
	double GetSegmentStartSeconds() const;
	double GetSegmentDurationSeconds() const;

private:
	FMediaPipeTrackingFusionDatasetReplayRuntime Runtime;
	bool bLoaded = false;
	double RecordingDurationSeconds = 0.0;
	double RequestedSegmentStartSeconds = 0.0;
	double RequestedSegmentDurationSeconds = 0.0;
	double StartWorldSeconds = -1.0;
};

// Outcome of a registry fetch on the anim-node path. A bound mesh NEVER falls through to
// live sensor polling or the global replay: on a stream gap it still reports Bound with
// whatever the stream returned (empty observations park the mesh instead of letting it
// grab the local user's sensors — the seat-B partner must not puppet from the host's
// webcam when its stream hiccups).
enum class EMediaPipeDyadRowStreamFetch : uint8
{
	NotBound,
	Bound,
};

class MEDIAPIPEDRIVER_API FMediaPipeDyadRowStreamRegistry
{
public:
	// Key = skeletal mesh component GetUniqueID(). Key 0 is rejected (keyed-store rule).
	static void BindMesh(uint32 MeshKey, const TSharedPtr<FMediaPipeDyadObservationSource>& Stream);
	static void UnbindMesh(uint32 MeshKey);
	static bool IsMeshBound(uint32 MeshKey);
	static int32 GetBoundMeshCount();

	// Hot-path fetch. With zero bindings this is one relaxed atomic load and returns
	// NotBound. For a bound mesh, fills OutObservations (possibly empty on stream gap)
	// and returns Bound.
	static EMediaPipeDyadRowStreamFetch Fetch(
		uint32 MeshKey,
		double WorldNowSeconds,
		FEmbodiedFusionSourceObservations& OutObservations,
		FString* OutPhaseName = nullptr);
};

// DYADIC_STUDY_PLAN Phase 0: per-mesh active-profile override.
//
// The profile resolver marks a resolved MetaHuman target bIsActiveProfile only when its
// matched profile equals the process-global active profile — correct for the single-avatar
// mirror, but it hard-disables per-avatar arm retargeting for a second avatar that differs
// from the mirror's. A dyad-owned mesh registers its declared avatar here and the resolver
// treats that profile as active for that mesh only. No overrides = no behavior change.
class MEDIAPIPEDRIVER_API FMediaPipeDyadAvatarProfileOverrides
{
public:
	static void SetMeshProfileOverride(uint32 MeshKey, FName ProfileId);
	static void ClearMeshProfileOverride(uint32 MeshKey);
	static bool GetMeshProfileOverride(uint32 MeshKey, FName& OutProfileId);
	static int32 GetOverrideCount();
};
