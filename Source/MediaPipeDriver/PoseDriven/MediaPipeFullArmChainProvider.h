#pragma once

#include "CoreMinimal.h"

enum class EMediaPipeFullArmChainSource : uint8
{
	None = 0,
	OpenXRBodyTracking = 1,
	TrackingFusionReplay = 2
};

struct FMediaPipeFullArmChainJointSnapshot
{
	FTransform WorldTransform = FTransform::Identity;
	uint8 bValid = 0;
	uint8 bPositionValid = 0;
	uint8 bOrientationValid = 0;

	void Reset()
	{
		WorldTransform = FTransform::Identity;
		bValid = 0;
		bPositionValid = 0;
		bOrientationValid = 0;
	}
};

struct FMediaPipeFullArmChainSideSnapshot
{
	FMediaPipeFullArmChainJointSnapshot Shoulder;
	FMediaPipeFullArmChainJointSnapshot UpperArm;
	FMediaPipeFullArmChainJointSnapshot LowerArm;
	FMediaPipeFullArmChainJointSnapshot WristOrPalm;
	uint8 bActive = 0;
	uint8 bWristOrPalmUsesPalm = 0;
	float Confidence = 0.0f;
	double TimestampSeconds = -1.0;

	void Reset()
	{
		Shoulder.Reset();
		UpperArm.Reset();
		LowerArm.Reset();
		WristOrPalm.Reset();
		bActive = 0;
		bWristOrPalmUsesPalm = 0;
		Confidence = 0.0f;
		TimestampSeconds = -1.0;
	}

	bool HasRequiredPositionChain() const
	{
		return Shoulder.bPositionValid != 0 &&
			UpperArm.bPositionValid != 0 &&
			LowerArm.bPositionValid != 0 &&
			WristOrPalm.bPositionValid != 0;
	}
};

struct FMediaPipeFullArmChainSnapshot
{
	EMediaPipeFullArmChainSource Source = EMediaPipeFullArmChainSource::None;
	FMediaPipeFullArmChainSideSnapshot Left;
	FMediaPipeFullArmChainSideSnapshot Right;
	// The runtime's own hips joint (XR_FB_body_tracking): on Quest 3 the headset's inside-out
	// body tracking observes the torso directly, making this far stronger for pelvis yaw than
	// any monocular camera estimate. Located in the same joint query as the arms.
	FMediaPipeFullArmChainJointSnapshot Hips;
	uint8 bActive = 0;
	float Confidence = 0.0f;
	double TimestampSeconds = -1.0;
	uint32 Sequence = 0;

	void Reset()
	{
		Source = EMediaPipeFullArmChainSource::None;
		Left.Reset();
		Right.Reset();
		Hips.Reset();
		bActive = 0;
		Confidence = 0.0f;
		TimestampSeconds = -1.0;
		Sequence = 0;
	}

	const FMediaPipeFullArmChainSideSnapshot& GetSide(const bool bIsLeft) const
	{
		return bIsLeft ? Left : Right;
	}

	bool HasFreshRequiredSide(const bool bIsLeft, const double NowSeconds, const float MaxAgeSeconds, float& OutAgeSeconds) const
	{
		const FMediaPipeFullArmChainSideSnapshot& Side = GetSide(bIsLeft);
		OutAgeSeconds = Side.TimestampSeconds >= 0.0
			? static_cast<float>(NowSeconds - Side.TimestampSeconds)
			: -1.0f;
		return bActive != 0 &&
			Side.bActive != 0 &&
			Side.HasRequiredPositionChain() &&
			OutAgeSeconds >= 0.0f &&
			(MaxAgeSeconds <= 0.0f || OutAgeSeconds <= MaxAgeSeconds);
	}
};

struct FMediaPipeMetaHumanFullArmChainLogInput
{
	FName TargetActorName = NAME_None;
	FName ProfileId = NAME_None;
	bool bIsLeft = false;
	bool bChainActive = false;
	bool bShoulderValid = false;
	bool bUpperArmValid = false;
	bool bLowerArmValid = false;
	bool bWristOrPalmValid = false;
	bool bMediaPipeArmUsed = false;
	bool bQuestHandUsed = false;
	bool bWristOrPalmUsesPalm = false;
	float TargetReachCm = 0.0f;
	float ElbowBendDeg = 0.0f;
	float Confidence = 0.0f;
	float ChainAgeSeconds = -1.0f;
	FVector HandWorld = FVector::ZeroVector;
};

using FMediaPipeWallaceFullArmChainLogInput = FMediaPipeMetaHumanFullArmChainLogInput;

MEDIAPIPEDRIVER_API void StartupMediaPipeFullArmChainProvider();
MEDIAPIPEDRIVER_API void ShutdownMediaPipeFullArmChainProvider();
MEDIAPIPEDRIVER_API bool ReadLatestMediaPipeFullArmChainSnapshot(FMediaPipeFullArmChainSnapshot& OutSnapshot);
MEDIAPIPEDRIVER_API float ComputeMediaPipeArmChainElbowBendDegrees(
	const FVector& ShoulderWorld,
	const FVector& ElbowWorld,
	const FVector& WristWorld);
MEDIAPIPEDRIVER_API FString FormatMediaPipeMetaHumanFullArmChainLog(
	const FMediaPipeMetaHumanFullArmChainLogInput& Input);
MEDIAPIPEDRIVER_API FString FormatMediaPipeWallaceFullArmChainLog(
	const FMediaPipeWallaceFullArmChainLogInput& Input);
