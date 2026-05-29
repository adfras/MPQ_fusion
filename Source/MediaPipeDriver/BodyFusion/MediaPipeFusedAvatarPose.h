#pragma once

#include "CoreMinimal.h"
#include "MediaPipeBodyFusionAuthorityPolicy.h"
#include "MediaPipeTrackingSourceTypes.h"

struct MEDIAPIPEDRIVER_API FMediaPipeFusedBodyPoint
{
	FVector LocationWorld = FVector::ZeroVector;
	FQuat RotationWorld = FQuat::Identity;
	EMediaPipeBodyFusionOwner Owner = EMediaPipeBodyFusionOwner::None;
	EMediaPipeBodyFusionSourceState SourceState = EMediaPipeBodyFusionSourceState::Missing;
	float Confidence = 0.0f;
	bool bValid = false;
};

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionDebugErrors
{
	float CameraToEyeCm = 0.0f;
	float CameraToChestCm = 0.0f;
	float HeadToChestCm = 0.0f;
	float ChestToPelvisCm = 0.0f;
	float HmdHorizontalOffsetCm = 0.0f;
	float LeftWristReachCm = 0.0f;
	float RightWristReachCm = 0.0f;
	float LeftFootReliability = 0.0f;
	float RightFootReliability = 0.0f;
	EMediaPipeBodyFusionAuthorityState BodyAuthorityState = EMediaPipeBodyFusionAuthorityState::NoMediaPipe;
	uint8 bMediaPipePoseAuthorityAllowed = 0;

	void Reset();
};

struct MEDIAPIPEDRIVER_API FMediaPipeFusedAvatarPose
{
	FMediaPipeFusedBodyPoint Root;
	FMediaPipeFusedBodyPoint Eye;
	FMediaPipeFusedBodyPoint Head;
	FMediaPipeFusedBodyPoint Neck;
	FMediaPipeFusedBodyPoint Chest;
	FMediaPipeFusedBodyPoint Spine;
	FMediaPipeFusedBodyPoint Pelvis;
	FMediaPipeFusedBodyPoint LeftShoulder;
	FMediaPipeFusedBodyPoint LeftElbow;
	FMediaPipeFusedBodyPoint LeftWrist;
	FMediaPipeFusedBodyPoint RightShoulder;
	FMediaPipeFusedBodyPoint RightElbow;
	FMediaPipeFusedBodyPoint RightWrist;
	FMediaPipeFusedBodyPoint LeftHip;
	FMediaPipeFusedBodyPoint LeftKnee;
	FMediaPipeFusedBodyPoint LeftAnkle;
	FMediaPipeFusedBodyPoint LeftFoot;
	FMediaPipeFusedBodyPoint RightHip;
	FMediaPipeFusedBodyPoint RightKnee;
	FMediaPipeFusedBodyPoint RightAnkle;
	FMediaPipeFusedBodyPoint RightFoot;
	FMediaPipeBodyFusionDebugErrors DebugErrors;

	void Reset();
	bool IsUsable() const;
	const FMediaPipeFusedBodyPoint* GetPoint(EMediaPipeBodyFusionRegion Region) const;
	FMediaPipeFusedBodyPoint* GetMutablePoint(EMediaPipeBodyFusionRegion Region);
	void SetPoint(EMediaPipeBodyFusionRegion Region, const FMediaPipeFusedBodyPoint& Point);
};
