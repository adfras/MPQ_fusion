#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "MediaPipeTrackingSourceTypes.h"

enum class EMediaPipeBodyFusionOwner : uint8
{
	None,
	Hmd,
	Quest,
	MediaPipe,
	AvatarProfile,
	Fused
};

enum class EMediaPipeBodyFusionAuthorityState : uint8
{
	NoMediaPipe,
	MediaPipeCalibrating,
	MediaPipeStable,
	MediaPipeRejected
};

enum class EMediaPipeBodyFusionRegion : uint8
{
	Root,
	Eye,
	Head,
	Neck,
	Chest,
	Spine,
	Pelvis,
	LeftShoulder,
	LeftElbow,
	LeftWrist,
	RightShoulder,
	RightElbow,
	RightWrist,
	LeftHip,
	LeftKnee,
	LeftAnkle,
	LeftHeel,
	LeftFoot,
	RightHip,
	RightKnee,
	RightAnkle,
	RightHeel,
	RightFoot,
	Count
};

static constexpr int32 MediaPipeBodyFusionRegionCount = static_cast<int32>(EMediaPipeBodyFusionRegion::Count);

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionAuthority
{
	TStaticArray<EMediaPipeBodyFusionOwner, MediaPipeBodyFusionRegionCount> RegionOwners;

	FMediaPipeBodyFusionAuthority();

	static FMediaPipeBodyFusionAuthority DefaultHybrid();
	static FMediaPipeBodyFusionAuthority DefaultEmbodiedUpperBody();
	static FMediaPipeBodyFusionAuthority DefaultEmbodiedHipsOnly();
	EMediaPipeBodyFusionOwner GetOwner(EMediaPipeBodyFusionRegion Region) const;
	void SetOwner(EMediaPipeBodyFusionRegion Region, EMediaPipeBodyFusionOwner Owner);

	static EMediaPipeBodyFusionOwner ResolveUpperLimbOwner(
		const FMediaPipeBodyFusionSourceStatus& QuestStatus,
		const FMediaPipeBodyFusionSourceStatus& MediaPipeStatus);
	static EMediaPipeBodyFusionOwner ResolveLowerBodyOwner(
		const FMediaPipeBodyFusionSourceStatus& MediaPipeStatus,
		const FMediaPipeBodyFusionSourceStatus& QuestStatus);
};

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionAuthorityGateInput
{
	int32 MediaPipeAuthorityMode = 0;
	bool bAllowFullBodyMediaPipeAuthority = false;
	bool bCalibrationUsable = false;
	FString CalibrationRejectReason;
	FMediaPipeBodyFusionSourceStatus BodyPoseStatus;
};

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionAuthorityGateDecision
{
	FMediaPipeBodyFusionAuthority Authority = FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly();
	EMediaPipeBodyFusionAuthorityState AuthorityState = EMediaPipeBodyFusionAuthorityState::NoMediaPipe;
	FString Reason;
	uint8 bAllowMediaPipePoseAuthority = 0;
};

class MEDIAPIPEDRIVER_API FMediaPipeBodyFusionAuthorityPolicy
{
public:
	static FMediaPipeBodyFusionAuthorityGateDecision ResolveMediaPipePoseAuthorityGate(
		const FMediaPipeBodyFusionAuthorityGateInput& Input);
};
