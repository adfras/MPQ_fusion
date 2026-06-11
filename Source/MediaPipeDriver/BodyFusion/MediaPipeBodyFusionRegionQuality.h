#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "MediaPipeBodyFusionAuthorityPolicy.h"
#include "MediaPipeFusedAvatarPose.h"

struct FEmbodiedFusionFrame;

// Coarse body regions used for evidence/quality scoring and ownership diagnostics.
// These intentionally aggregate the fine-grained fused points so logs and plots stay readable.
enum class EMediaPipeBodyFusionQualityRegion : uint8
{
	Head,
	Hands,
	Arms,
	Shoulders,
	ChestSpine,
	PelvisHips,
	Legs,
	Feet,
	Count
};

static constexpr int32 MediaPipeBodyFusionQualityRegionCount =
	static_cast<int32>(EMediaPipeBodyFusionQualityRegion::Count);

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionRegionQualityStats
{
	EMediaPipeBodyFusionOwner Owner = EMediaPipeBodyFusionOwner::None;
	EMediaPipeBodyFusionSourceState SourceState = EMediaPipeBodyFusionSourceState::Missing;
	float Confidence = 0.0f;
	bool bValid = false;

	// Rolling-window evidence (window length comes from mp.BodyFusion.RegionQualityWindowSeconds).
	float AmplitudeCm = 0.0f;
	float MeanSpeedCmPerSecond = 0.0f;
	int32 DropoutCount = 0;
	float FreshRatio = 0.0f;
	int32 WindowSampleCount = 0;

	// Monocular-depth heuristic: variance along the avatar forward axis over variance along the
	// lateral axis. Front-facing MediaPipe video observes lateral/vertical motion well and depth
	// poorly, so a high ratio on a MediaPipe-owned region marks depth-dominated (weak) evidence.
	float DepthVarianceRatio = 0.0f;
	bool bDepthWeak = false;

	// Whether BodyFusion may currently influence this region's visible pose. For avatar-locked
	// MetaHuman replay, pelvis/hips/legs/feet must stay diagnostics-only (avatar-local solve owns
	// the visible lower body), and this flag makes that policy observable in logs and captures.
	bool bMayInfluencePose = false;
	FString InfluenceReason;
};

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionRegionQualityUpdateInput
{
	const FEmbodiedFusionFrame* Frame = nullptr;
	double NowSeconds = 0.0;
	FName TargetActorName;
	bool bPoseWriteEnabled = false;
	bool bAvatarLockedReplayActive = false;
	FVector AvatarForwardWorld = FVector::ForwardVector;
	FVector AvatarUpWorld = FVector::UpVector;
};

class MEDIAPIPEDRIVER_API FMediaPipeBodyFusionRegionQualityTracker
{
public:
	void Update(const FMediaPipeBodyFusionRegionQualityUpdateInput& Input);
	const FMediaPipeBodyFusionRegionQualityStats& GetStats(EMediaPipeBodyFusionQualityRegion Region) const;
	void Reset();

	static const TCHAR* RegionName(EMediaPipeBodyFusionQualityRegion Region);
	static const TCHAR* OwnerName(EMediaPipeBodyFusionOwner Owner);

	// Emits one throttled log row per region when mp.BodyFusion.RegionQualityLog is enabled and
	// appends JSONL rows when mp.BodyFusion.RegionQualityCapture is enabled. Safe to call every
	// fusion update; interval gating happens inside.
	void EmitDiagnostics(const FMediaPipeBodyFusionRegionQualityUpdateInput& Input);

private:
	struct FRegionHistorySample
	{
		double TimeSeconds = 0.0;
		FVector PositionWorld = FVector::ZeroVector;
		bool bValid = false;
		bool bFresh = false;
		float Confidence = 0.0f;
	};

	struct FRegionHistory
	{
		TArray<FRegionHistorySample> Samples;
	};

	void BuildRegionSample(
		const FMediaPipeBodyFusionRegionQualityUpdateInput& Input,
		EMediaPipeBodyFusionQualityRegion Region,
		FRegionHistorySample& OutSample,
		EMediaPipeBodyFusionOwner& OutOwner,
		EMediaPipeBodyFusionSourceState& OutState) const;
	void RecomputeWindowStats(
		const FMediaPipeBodyFusionRegionQualityUpdateInput& Input,
		EMediaPipeBodyFusionQualityRegion Region);
	void ResolveInfluencePolicy(
		const FMediaPipeBodyFusionRegionQualityUpdateInput& Input,
		EMediaPipeBodyFusionQualityRegion Region,
		FMediaPipeBodyFusionRegionQualityStats& InOutStats) const;

	TStaticArray<FRegionHistory, MediaPipeBodyFusionQualityRegionCount> Histories;
	TStaticArray<FMediaPipeBodyFusionRegionQualityStats, MediaPipeBodyFusionQualityRegionCount> Stats;
	double LastLogTimeSeconds = -1.0;
	double LastCaptureTimeSeconds = -1.0;
	FString CapturePath;
};
