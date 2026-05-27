#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "MediaPipeAvatarEmbodimentProfile.h"
#include "MediaPipePoseTypes.h"

enum class EMediaPipeBodyFusionSourceState : uint8
{
	Missing,
	Stale,
	Invalid,
	Fresh
};

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
	LeftFoot,
	RightHip,
	RightKnee,
	RightAnkle,
	RightFoot,
	Count
};

static constexpr int32 MediaPipeBodyFusionRegionCount = static_cast<int32>(EMediaPipeBodyFusionRegion::Count);

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionSourceStatus
{
	EMediaPipeBodyFusionSourceState State = EMediaPipeBodyFusionSourceState::Missing;
	float AgeSeconds = -1.0f;
	float Confidence = 0.0f;

	bool IsFresh() const;
	bool IsUsable() const;
};

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionFreshnessThresholds
{
	float HmdMaxAgeSeconds = 0.25f;
	float QuestHandMaxAgeSeconds = 0.25f;
	float QuestFullArmChainMaxAgeSeconds = 0.25f;
	float MediaPipePoseMaxAgeSeconds = 0.25f;
	float MinHmdConfidence = 0.01f;
	float MinQuestConfidence = 0.01f;
	float MinMediaPipeConfidence = 0.45f;
};

struct MEDIAPIPEDRIVER_API FMediaPipeTrackingSourceFrame
{
	double FrameTimeSeconds = -1.0;

	bool bHasHmdPose = false;
	FVector HmdLocationWorld = FVector::ZeroVector;
	FQuat HmdRotationWorld = FQuat::Identity;
	FVector TrackingUpWorld = FVector::UpVector;
	double HmdTimestampSeconds = -1.0;
	float HmdConfidence = 1.0f;
	FMediaPipeBodyFusionSourceStatus HmdStatus;

	bool bHasQuestLeftHand = false;
	bool bHasQuestRightHand = false;
	FVector QuestLeftHandWorld = FVector::ZeroVector;
	FVector QuestRightHandWorld = FVector::ZeroVector;
	double QuestLeftHandTimestampSeconds = -1.0;
	double QuestRightHandTimestampSeconds = -1.0;
	float QuestLeftHandConfidence = 0.0f;
	float QuestRightHandConfidence = 0.0f;
	FMediaPipeBodyFusionSourceStatus QuestLeftHandStatus;
	FMediaPipeBodyFusionSourceStatus QuestRightHandStatus;

	bool bHasQuestLeftFullArmChain = false;
	bool bHasQuestRightFullArmChain = false;
	FVector QuestLeftShoulderWorld = FVector::ZeroVector;
	FVector QuestLeftElbowWorld = FVector::ZeroVector;
	FVector QuestLeftWristWorld = FVector::ZeroVector;
	FVector QuestRightShoulderWorld = FVector::ZeroVector;
	FVector QuestRightElbowWorld = FVector::ZeroVector;
	FVector QuestRightWristWorld = FVector::ZeroVector;
	double QuestLeftFullArmChainTimestampSeconds = -1.0;
	double QuestRightFullArmChainTimestampSeconds = -1.0;
	float QuestLeftFullArmChainConfidence = 0.0f;
	float QuestRightFullArmChainConfidence = 0.0f;
	FMediaPipeBodyFusionSourceStatus QuestLeftFullArmChainStatus;
	FMediaPipeBodyFusionSourceStatus QuestRightFullArmChainStatus;

	bool bHasMediaPipePose = false;
	double MediaPipePoseTimestampSeconds = -1.0;
	float MediaPipePoseConfidence = 0.0f;
	TStaticArray<FVector, MediaPipePoseLandmarkCount> MediaPipeLandmarksWorld;
	TStaticArray<float, MediaPipePoseLandmarkCount> MediaPipeLandmarkReliability;
	TStaticArray<uint8, MediaPipePoseLandmarkCount> MediaPipeLandmarkValid;
	FMediaPipeBodyFusionSourceStatus MediaPipePoseStatus;

	FMediaPipeTrackingSourceFrame();

	void Reset();
	void SetMediaPipeLandmark(EMediaPipePoseLandmark Landmark, const FVector& LocationWorld, float Reliability);
	bool TryGetMediaPipeLandmark(EMediaPipePoseLandmark Landmark, FVector& OutLocationWorld, float* OutReliability = nullptr) const;
	void UpdateFreshness(const FMediaPipeBodyFusionFreshnessThresholds& Thresholds);

	static FMediaPipeBodyFusionSourceStatus ClassifySource(
		bool bHasSample,
		bool bSampleValid,
		double SampleTimestampSeconds,
		double NowSeconds,
		float MaxAgeSeconds,
		float Confidence,
		float MinConfidence);
};

struct MEDIAPIPEDRIVER_API FMediaPipeEmbodimentCalibrationInput
{
	FVector MediaPipeHipCenterWorld = FVector::ZeroVector;
	FVector MediaPipeForwardWorld = FVector::ForwardVector;
	FVector AvatarPelvisAnchorWorld = FVector::ZeroVector;
	FVector AvatarForwardWorld = FVector::ForwardVector;
	FVector AvatarUpWorld = FVector::UpVector;
	FVector HmdWorld = FVector::ZeroVector;
	float ObservedBodyHeightCm = 0.0f;
	float AvatarBodyHeightCm = 0.0f;
	float Confidence = 0.0f;
	bool bHmdStable = false;
	bool bMediaPipeStable = false;
	double TimestampSeconds = -1.0;
};

struct MEDIAPIPEDRIVER_API FMediaPipeEmbodimentCalibration
{
	bool bHasCalibration = false;
	FQuat YawRotation = FQuat::Identity;
	FVector Translation = FVector::ZeroVector;
	float Scale = 1.0f;
	float Confidence = 0.0f;
	double TimestampSeconds = -1.0;
	FString LastRejectReason;

	void Reset();
	bool IsUsable(float MinConfidence = 0.5f) const;
	FTransform GetMediaPipeToAvatarTransform() const;
	FVector TransformMediaPipePoint(const FVector& MediaPipePointWorld) const;

	static bool TryBuildNeutralCalibration(
		const FMediaPipeEmbodimentCalibrationInput& Input,
		FMediaPipeEmbodimentCalibration& OutCalibration);
};

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

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionSolveInput
{
	FMediaPipeTrackingSourceFrame SourceFrame;
	FMediaPipeEmbodimentCalibration Calibration;
	FMediaPipeBodyFusionAuthority Authority = FMediaPipeBodyFusionAuthority::DefaultHybrid();
	FMediaPipeAvatarEmbodimentProfile Profile;
	FTransform AvatarWorldTransform = FTransform::Identity;
	float UserCameraForwardOffsetCm = 0.0f;
	float ExpectedHeadToChestCm = 0.0f;
	float ExpectedChestToPelvisCm = 0.0f;
	bool bAllowMediaPipePoseAuthority = true;
	EMediaPipeBodyFusionAuthorityState BodyAuthorityState = EMediaPipeBodyFusionAuthorityState::NoMediaPipe;
};

class MEDIAPIPEDRIVER_API FMediaPipeBodyFusionSolver
{
public:
	static bool Solve(const FMediaPipeBodyFusionSolveInput& Input, FMediaPipeFusedAvatarPose& OutPose);
};

struct MEDIAPIPEDRIVER_API FMediaPipeAvatarPoseWritePlan
{
	TArray<EMediaPipeBodyFusionRegion> OrderedRegions;
	bool bKeepProfileDrivenBoneNames = true;
};

struct MEDIAPIPEDRIVER_API FMediaPipeFusedLowerBodySide
{
	FVector HipWorld = FVector::ZeroVector;
	FVector KneeWorld = FVector::ZeroVector;
	FVector AnkleWorld = FVector::ZeroVector;
	FVector FootWorld = FVector::ZeroVector;
	bool bHasFoot = false;
};

class MEDIAPIPEDRIVER_API FMediaPipeAvatarPoseWriter
{
public:
	static FMediaPipeAvatarPoseWritePlan BuildDefaultWritePlan(const FMediaPipeBodyFusionAuthority& Authority);
	static bool CanWritePose(const FMediaPipeFusedAvatarPose& Pose, const FMediaPipeAvatarEmbodimentProfile& Profile);
	static bool TryGetMediaPipeLowerBodySide(
		const FMediaPipeFusedAvatarPose& Pose,
		bool bIsLeft,
		FMediaPipeFusedLowerBodySide& OutSide);
};
