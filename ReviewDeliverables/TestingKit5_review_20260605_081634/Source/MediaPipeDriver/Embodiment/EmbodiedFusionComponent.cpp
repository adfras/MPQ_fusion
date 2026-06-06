#include "EmbodiedFusionComponent.h"

#include "MediaPipeBodyFusionDebugFormatter.h"
#include "MediaPipeFullArmChainProvider.h"
#include "MediaPipePoseDiagnosticReporter.h"
#include "MediaPipePoseLog.h"
#include "MediaPipePoseTrackerComponent.h"
#include "MediaPipeQuestHandTrackingSource.h"
#include "MediaPipeQuestRuntimeDebugService.h"
#include "MediaPipeSkeletonPoseAdapter.h"

#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "MotionControllerComponent.h"

namespace
{
float ResolveBodyLandmarkReliability(const FMediaPipePoseFrame& Frame, const int32 LandmarkIndex)
{
	if (LandmarkIndex < 0 ||
		LandmarkIndex >= MediaPipePoseLandmarkCount ||
		!Frame.World.IsValidIndex(LandmarkIndex) ||
		!Frame.Normalized.IsValidIndex(LandmarkIndex))
	{
		return 0.0f;
	}

	const FMediaPipePoseLandmark& WorldLandmark = Frame.World.Points[LandmarkIndex];
	const FMediaPipePoseLandmark& NormalizedLandmark = Frame.Normalized.Points[LandmarkIndex];
	const float ExplicitReliability = FMath::Max(WorldLandmark.Reliability, NormalizedLandmark.Reliability);
	if (ExplicitReliability > 0.0f || Frame.bSourceConditioned)
	{
		return FMath::Clamp(ExplicitReliability, 0.0f, 1.0f);
	}

	return FMath::Clamp(
		FMath::Max(
			WorldLandmark.Visibility * WorldLandmark.Presence,
			NormalizedLandmark.Visibility * NormalizedLandmark.Presence),
		0.0f,
		1.0f);
}

FMediaPipeTrackingHandSourceSnapshot ConvertQuestHandsToGenericHands(const FQuestHandTrackingSnapshot& Snapshot)
{
	FMediaPipeTrackingHandSourceSnapshot Generic;
	Generic.ProviderCount = Snapshot.HandTrackerCount;
	Generic.ValidProviderCount = Snapshot.ValidHandTrackerCount;
	Generic.bHasLeft = Snapshot.bHasLeft;
	Generic.bHasRight = Snapshot.bHasRight;
	Generic.bLeftTracked = Snapshot.bLeftTracked;
	Generic.bRightTracked = Snapshot.bRightTracked;
	for (int32 Index = 0; Index < MediaPipeTrackingHandKeypointCount; ++Index)
	{
		Generic.LeftPositionsWorld[Index] = Snapshot.LeftPositionsWorld[Index];
		Generic.LeftRotationsWorld[Index] = Snapshot.LeftRotationsWorld[Index];
		Generic.LeftRadii[Index] = Snapshot.LeftRadii[Index];
		Generic.RightPositionsWorld[Index] = Snapshot.RightPositionsWorld[Index];
		Generic.RightRotationsWorld[Index] = Snapshot.RightRotationsWorld[Index];
		Generic.RightRadii[Index] = Snapshot.RightRadii[Index];
	}
	return Generic;
}

FMediaPipeTrackingArmChainSourceSnapshot ConvertFullArmChainToGenericArmChain(
	const FMediaPipeFullArmChainSnapshot& Snapshot)
{
	FMediaPipeTrackingArmChainSourceSnapshot Generic;
	auto ConvertSide = [&Snapshot](
		const bool bLeft,
		FMediaPipeTrackingArmChainSideSnapshot& OutSide)
	{
		const FMediaPipeFullArmChainSideSnapshot& Side = Snapshot.GetSide(bLeft);
		if (Snapshot.bActive == 0 || Side.bActive == 0 || !Side.HasRequiredPositionChain())
		{
			return;
		}

		OutSide.bHasChain = true;
		OutSide.ShoulderWorld = Side.Shoulder.WorldTransform.GetLocation();
		OutSide.ElbowWorld = Side.LowerArm.WorldTransform.GetLocation();
		OutSide.WristWorld = Side.WristOrPalm.WorldTransform.GetLocation();
		OutSide.TimestampSeconds = Side.TimestampSeconds;
		OutSide.Confidence = FMath::Max(Side.Confidence, Snapshot.Confidence);
	};

	ConvertSide(true, Generic.Left);
	ConvertSide(false, Generic.Right);
	return Generic;
}

bool IsUsableHandTargetLocation(const FVector& LocationWorld)
{
	return !LocationWorld.ContainsNaN() && !LocationWorld.IsNearlyZero();
}

void CopyHandJointsToBestPose(
	const FMediaPipeTrackingHandSourceSnapshot& Snapshot,
	const bool bLeft,
	FEmbodiedFusionHandJointPose& OutJoints)
{
	OutJoints.Reset();
	const bool bHasSide = bLeft ? Snapshot.bHasLeft != 0 : Snapshot.bHasRight != 0;
	if (!bHasSide)
	{
		return;
	}

	OutJoints.bHasJoints = true;
	OutJoints.bTracked = bLeft ? Snapshot.bLeftTracked != 0 : Snapshot.bRightTracked != 0;
	const TStaticArray<FVector, MediaPipeTrackingHandKeypointCount>& Positions =
		bLeft ? Snapshot.LeftPositionsWorld : Snapshot.RightPositionsWorld;
	const TStaticArray<FQuat, MediaPipeTrackingHandKeypointCount>& Rotations =
		bLeft ? Snapshot.LeftRotationsWorld : Snapshot.RightRotationsWorld;
	for (int32 Index = 0; Index < MediaPipeTrackingHandKeypointCount; ++Index)
	{
		OutJoints.PositionsWorld[Index] = Positions[Index];
		OutJoints.RotationsWorld[Index] = Rotations[Index];
	}
}

bool TryGetHandTrackingTarget(
	const FMediaPipeTrackingHandSourceSnapshot& Snapshot,
	const bool bLeft,
	FTransform& OutHandTargetWorld,
	bool& bOutTracked)
{
	const bool bHasSide = bLeft ? Snapshot.bHasLeft != 0 : Snapshot.bHasRight != 0;
	if (!bHasSide)
	{
		return false;
	}

	const TStaticArray<FVector, MediaPipeTrackingHandKeypointCount>& Positions =
		bLeft ? Snapshot.LeftPositionsWorld : Snapshot.RightPositionsWorld;
	const TStaticArray<FQuat, MediaPipeTrackingHandKeypointCount>& Rotations =
		bLeft ? Snapshot.LeftRotationsWorld : Snapshot.RightRotationsWorld;

	const int32 WristIndex = static_cast<int32>(EHandKeypoint::Wrist);
	const int32 PalmIndex = static_cast<int32>(EHandKeypoint::Palm);
	int32 TargetIndex = INDEX_NONE;
	if (IsUsableHandTargetLocation(Positions[WristIndex]))
	{
		TargetIndex = WristIndex;
	}
	else if (IsUsableHandTargetLocation(Positions[PalmIndex]))
	{
		TargetIndex = PalmIndex;
	}

	if (TargetIndex == INDEX_NONE)
	{
		return false;
	}

	OutHandTargetWorld = FTransform::Identity;
	OutHandTargetWorld.SetLocation(Positions[TargetIndex]);
	if (!Rotations[TargetIndex].ContainsNaN())
	{
		OutHandTargetWorld.SetRotation(Rotations[TargetIndex].GetNormalized());
	}
	bOutTracked = bLeft ? Snapshot.bLeftTracked != 0 : Snapshot.bRightTracked != 0;
	return true;
}

bool TryGetMotionControllerTarget(
	const UMotionControllerComponent* MotionController,
	FTransform& OutHandTargetWorld)
{
	if (!MotionController || !MotionController->IsTracked())
	{
		return false;
	}

	OutHandTargetWorld = MotionController->GetComponentTransform();
	return true;
}

void SetUpperLimbFromFusionPointChain(
	FEmbodiedFusionUpperLimbPose& OutLimb,
	const FVector& ShoulderWorld,
	const FVector& ElbowWorld,
	const FVector& WristWorld,
	const FName Source,
	const FMediaPipeBodyFusionSourceStatus& Status)
{
	OutLimb.bHasJointChain = true;
	OutLimb.bHasHandTarget = true;
	OutLimb.bHandTargetTracked = Status.IsFresh();
	OutLimb.ShoulderWorld = ShoulderWorld;
	OutLimb.ElbowWorld = ElbowWorld;
	OutLimb.WristWorld = WristWorld;
	OutLimb.HandTargetWorld = FTransform::Identity;
	OutLimb.HandTargetWorld.SetLocation(WristWorld);
	OutLimb.Source = Source;
	OutLimb.Status = Status;
}
}

void FEmbodiedFusionHandJointPose::Reset()
{
	bHasJoints = false;
	bTracked = false;
	for (int32 Index = 0; Index < MediaPipeTrackingHandKeypointCount; ++Index)
	{
		PositionsWorld[Index] = FVector::ZeroVector;
		RotationsWorld[Index] = FQuat::Identity;
	}
}

void FEmbodiedFusionUpperLimbPose::Reset()
{
	bHasJointChain = false;
	bHasHandTarget = false;
	bHandTargetTracked = false;
	ShoulderWorld = FVector::ZeroVector;
	ElbowWorld = FVector::ZeroVector;
	WristWorld = FVector::ZeroVector;
	HandTargetWorld = FTransform::Identity;
	Source = NAME_None;
	Status = FMediaPipeBodyFusionSourceStatus();
	HandJoints.Reset();
}

void FEmbodiedFusionBestAvailablePose::Reset()
{
	bHasHead = false;
	HeadLocationWorld = FVector::ZeroVector;
	HeadRotationWorld = FQuat::Identity;
	HeadSource = NAME_None;
	HeadStatus = FMediaPipeBodyFusionSourceStatus();
	LeftUpperLimb.Reset();
	RightUpperLimb.Reset();
}

FEmbodiedFusionUpperLimbPose& FEmbodiedFusionBestAvailablePose::GetMutableUpperLimb(const bool bLeft)
{
	return bLeft ? LeftUpperLimb : RightUpperLimb;
}

const FEmbodiedFusionUpperLimbPose& FEmbodiedFusionBestAvailablePose::GetUpperLimb(const bool bLeft) const
{
	return bLeft ? LeftUpperLimb : RightUpperLimb;
}

void FEmbodiedFusionFrame::ResetTransient()
{
	SourceFrame.Reset();
	FreshnessThresholds = FMediaPipeBodyFusionFreshnessThresholds();
	Pose.Reset();
	Authority = FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly();
	AuthorityState = EMediaPipeBodyFusionAuthorityState::NoMediaPipe;
	AuthorityReason.Reset();
	Calibration = FMediaPipeEmbodimentCalibration();
	CalibrationStableFrameCount = 0;
	CalibrationStableSeconds = 0.0f;
	bMediaPipeAuthorityAllowed = 0;
	bRuntimeEnabled = 0;
	BestAvailablePose.Reset();
}

UEmbodiedFusionComponent::UEmbodiedFusionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	LastQuestHandsForDebug.Reset();
}

void UEmbodiedFusionComponent::ResetFusionState()
{
	LatestFrame.ResetTransient();
	SourceObservations = FEmbodiedFusionSourceObservations();
	Calibration.Reset();
	LastCalibrationResetSerial = FMediaPipeEmbodimentDebugCommands::GetBodyFusionCalibrationResetSerial();
	CalibrationStableFrameCount = 0;
	CalibrationStableSeconds = 0.0f;
	LastCalibrationUpdateTimeSeconds = -1.0;
	LastDebugLogTimeSeconds = -1.0;
	LastCalibrationLogTimeSeconds = -1.0;
	LastQuestHandsForDebug.Reset();
	LastAcceptedHmdPose = FMediaPipeQuestHmdPoseSnapshot();
	LastAcceptedHmdPoseTimeSeconds = -1.0;
	HmdPoseConditionedFrameCount = 0;
}

FMediaPipeQuestHmdPoseSnapshot UEmbodiedFusionComponent::ConditionHmdPose_GameThread(
	const FMediaPipeQuestHmdPoseSnapshot& RawPose,
	const double NowSeconds,
	const FName TargetActorName)
{
	if (!RawPose.bHasPose || RawPose.LocationWorld.ContainsNaN())
	{
		HmdPoseConditionedFrameCount = 0;
		return RawPose;
	}

	constexpr double MaxHmdPoseHistoryGapSeconds = 0.50;
	constexpr float MaxHmdPoseSpeedCmPerSecond = 350.0f;
	constexpr float MinHmdPoseStepClampCm = 8.0f;
	constexpr float LoggedHmdPoseStepCm = 25.0f;

	const bool bHasUsableHistory =
		LastAcceptedHmdPose.bHasPose &&
		!LastAcceptedHmdPose.LocationWorld.ContainsNaN() &&
		LastAcceptedHmdPoseTimeSeconds >= 0.0 &&
		NowSeconds > LastAcceptedHmdPoseTimeSeconds &&
		NowSeconds - LastAcceptedHmdPoseTimeSeconds <= MaxHmdPoseHistoryGapSeconds;
	if (!bHasUsableHistory)
	{
		LastAcceptedHmdPose = RawPose;
		LastAcceptedHmdPoseTimeSeconds = NowSeconds;
		HmdPoseConditionedFrameCount = 0;
		return RawPose;
	}

	const double DeltaSeconds = FMath::Max(NowSeconds - LastAcceptedHmdPoseTimeSeconds, UE_SMALL_NUMBER);
	const float StepCm = FVector::Dist(RawPose.LocationWorld, LastAcceptedHmdPose.LocationWorld);
	const float MaxStepCm =
		FMath::Max(MinHmdPoseStepClampCm, MaxHmdPoseSpeedCmPerSecond * static_cast<float>(DeltaSeconds));
	if (StepCm <= MaxStepCm)
	{
		LastAcceptedHmdPose = RawPose;
		LastAcceptedHmdPoseTimeSeconds = NowSeconds;
		HmdPoseConditionedFrameCount = 0;
		return RawPose;
	}

	FMediaPipeQuestHmdPoseSnapshot ConditionedPose = RawPose;
	const FVector AcceptedToRaw = RawPose.LocationWorld - LastAcceptedHmdPose.LocationWorld;
	ConditionedPose.LocationWorld =
		LastAcceptedHmdPose.LocationWorld + AcceptedToRaw.GetClampedToMaxSize(MaxStepCm);

	++HmdPoseConditionedFrameCount;
	if (StepCm >= LoggedHmdPoseStepCm || HmdPoseConditionedFrameCount == 1)
	{
		UE_LOG(LogMediaPipePose, Log,
			TEXT("mp.BodyFusion.HmdPoseConditioned actor=%s rawStepCm=%.1f maxStepCm=%.1f deltaSeconds=%.3f conditionedFrames=%d raw=%s conditioned=%s"),
			*TargetActorName.ToString(),
			StepCm,
			MaxStepCm,
			DeltaSeconds,
			HmdPoseConditionedFrameCount,
			*RawPose.LocationWorld.ToCompactString(),
			*ConditionedPose.LocationWorld.ToCompactString());
	}

	LastAcceptedHmdPose = ConditionedPose;
	LastAcceptedHmdPoseTimeSeconds = NowSeconds;
	return ConditionedPose;
}

FEmbodiedFusionQuestSourcePollResult UEmbodiedFusionComponent::PollQuestRuntimeSources_GameThread(
	const FEmbodiedFusionQuestSourcePollInput& Input,
	FMediaPipeDiagnosticsState& DiagnosticsState)
{
	FMediaPipeQuestRuntimeTickInput QuestRuntimeInput;
	QuestRuntimeInput.World = Input.World;
	QuestRuntimeInput.TargetComponent = Input.TargetComponent;
	QuestRuntimeInput.TargetActorName = Input.TargetActorName;
	QuestRuntimeInput.bUseQuestHandTracking = Input.bUseQuestHandTracking;
	QuestRuntimeInput.bBodyFusionRuntimeActive = Input.bBodyFusionRuntimeActive;

	const FMediaPipeQuestRuntimeTickOutput QuestRuntimeOutput =
		FMediaPipeQuestRuntimeDebugService::TickSourcesAndDebug(QuestRuntimeInput, DiagnosticsState);

	FEmbodiedFusionQuestSourcePollResult Result;
	Result.QuestHands.Reset();
	if (QuestRuntimeOutput.bQuestHandsPolled)
	{
		Result.QuestHands = QuestRuntimeOutput.QuestHands;
	}
	Result.HmdPose = QuestRuntimeOutput.HmdPose;
	Result.bQuestHandsPolled = QuestRuntimeOutput.bQuestHandsPolled;
	Result.bUsingQuestHandReplay = QuestRuntimeOutput.bUsingQuestHandReplay;
	Result.QuestHandReplayPath = QuestRuntimeOutput.QuestHandReplayPath;
	LastQuestHandsForDebug = Result.QuestHands;
	if (Input.bForceHmdPose && !Result.HmdPose.bHasPose)
	{
		FMediaPipeQuestHmdTrackingSource::TryReadWorldPose(Result.HmdPose);
	}
	Result.HmdPose = ConditionHmdPose_GameThread(Result.HmdPose, FPlatformTime::Seconds(), Input.TargetActorName);
	if (Input.bReadFullArmChain)
	{
		Result.bHasFullArmChain = ReadLatestMediaPipeFullArmChainSnapshot(Result.FullArmChain);
	}
	else
	{
		Result.FullArmChain.Reset();
		Result.bHasFullArmChain = false;
	}

	SourceObservations.HmdPose = Result.HmdPose;
	SourceObservations.Hands = ConvertQuestHandsToGenericHands(Result.QuestHands);
	SourceObservations.ArmChain = ConvertFullArmChainToGenericArmChain(Result.FullArmChain);
	return Result;
}

bool UEmbodiedFusionComponent::ReadMediaPipeSourceFrame_GameThread(
	UWorld* World,
	AActor* PreferredSourceActor,
	FEmbodiedFusionMediaPipeSourceRead& OutRead) const
{
	OutRead = FEmbodiedFusionMediaPipeSourceRead();
	if (!World)
	{
		return false;
	}

	AActor* ResolvedSourceActor = PreferredSourceActor;
	if (!ResolvedSourceActor)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Candidate = *It;
			if (Candidate && Candidate->FindComponentByClass<UMediaPipePoseTrackerComponent>())
			{
				ResolvedSourceActor = Candidate;
				break;
			}
		}
	}

	UMediaPipePoseTrackerComponent* TrackerComp = ResolvedSourceActor
		? ResolvedSourceActor->FindComponentByClass<UMediaPipePoseTrackerComponent>()
		: nullptr;
	OutRead.SourceActor = ResolvedSourceActor;
	OutRead.TrackerComponent = TrackerComp;
	if (!TrackerComp)
	{
		return false;
	}

	OutRead.PoseToWorldTransform = ResolvedSourceActor->GetActorTransform();
	OutRead.WorldScale = TrackerComp->WorldScale;
	OutRead.bMirrorLandmarksLR = TrackerComp->bMirrorLandmarksLR;
	OutRead.bHasLivePoseFrame = TrackerComp->GetLatestFrame(OutRead.Frame) && OutRead.Frame.bValid;
	return OutRead.bHasLivePoseFrame;
}

void UEmbodiedFusionComponent::DisplayQuestCalibrationHuds_GameThread(
	const FEmbodiedFusionQuestCalibrationDebugInput& Input) const
{
	FMediaPipeQuestCalibrationHudInput QuestCalibrationHudInput;
	QuestCalibrationHudInput.World = Input.World;
	QuestCalibrationHudInput.TargetComponent = Input.TargetComponent;
	QuestCalibrationHudInput.WristState = Input.WristState;
	QuestCalibrationHudInput.QuestHands = &LastQuestHandsForDebug;
	QuestCalibrationHudInput.HmdPose = SourceObservations.HmdPose;
	QuestCalibrationHudInput.bArmLengthCalibrationHudOwner =
		Input.bArmLengthCalibrationHudOwner;
	QuestCalibrationHudInput.bHasRefArmL = Input.bHasRefArmL;
	QuestCalibrationHudInput.bHasRefArmR = Input.bHasRefArmR;
	QuestCalibrationHudInput.RefUpperLenCompL = Input.RefUpperLenCompL;
	QuestCalibrationHudInput.RefLowerLenCompL = Input.RefLowerLenCompL;
	QuestCalibrationHudInput.RefUpperLenCompR = Input.RefUpperLenCompR;
	QuestCalibrationHudInput.RefLowerLenCompR = Input.RefLowerLenCompR;
	FMediaPipeQuestRuntimeDebugService::DisplayCalibrationHuds(QuestCalibrationHudInput);
}

void UEmbodiedFusionComponent::DrawHmdRelativeAvatarComparison_GameThread(
	const FEmbodiedFusionHmdRelativeAvatarDebugInput& Input) const
{
	FMediaPipeQuestHmdRelativeAvatarDebugInput QuestAvatarDebugInput;
	QuestAvatarDebugInput.World = Input.World;
	QuestAvatarDebugInput.QuestHands = &LastQuestHandsForDebug;
	QuestAvatarDebugInput.HmdPose = SourceObservations.HmdPose;
	QuestAvatarDebugInput.TargetCompTransform = Input.TargetCompTransform;
	QuestAvatarDebugInput.bUseQuestHandTracking = Input.bUseHandTracking;
	QuestAvatarDebugInput.bHasTargetEmbodimentProfile = Input.bHasTargetEmbodimentProfile;
	QuestAvatarDebugInput.TargetEmbodimentProfile = Input.TargetEmbodimentProfile;
	QuestAvatarDebugInput.bUseTargetFaceForwardAxis = Input.bUseTargetFaceForwardAxis;
	QuestAvatarDebugInput.bHasTargetEyeLocalOffset = Input.bHasTargetEyeLocalOffset;
	QuestAvatarDebugInput.TargetEyeLocalOffset = Input.TargetEyeLocalOffset;
	QuestAvatarDebugInput.TargetEmbodiedCameraForwardOffsetCm =
		Input.TargetEmbodiedCameraForwardOffsetCm;
	FMediaPipeQuestRuntimeDebugService::DrawHmdRelativeAvatarComparison(QuestAvatarDebugInput);
}

void UEmbodiedFusionComponent::UpdateBodyPoseObservation_GameThread(
	const double TimestampSeconds,
	const FMediaPipePoseFrame& Frame,
	const TStaticArray<FVector, MediaPipePoseLandmarkCount>& LandmarksWorld,
	const TStaticArray<uint8, MediaPipePoseLandmarkCount>& LandmarkValid)
{
	SourceObservations.NowSeconds = TimestampSeconds;
	SourceObservations.BodyPose.Reset();
	SourceObservations.BodyPose.TimestampSeconds = Frame.PublishWallSeconds >= 0.0
		? Frame.PublishWallSeconds
		: (Frame.LandmarkEndWallSeconds >= 0.0
			? Frame.LandmarkEndWallSeconds
			: TimestampSeconds);
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		if (LandmarkValid[Index] == 0)
		{
			continue;
		}

		SourceObservations.BodyPose.SetLandmark(
			static_cast<EMediaPipePoseLandmark>(Index),
			LandmarksWorld[Index],
			ResolveBodyLandmarkReliability(Frame, Index));
	}
}

bool UEmbodiedFusionComponent::UpdateFusion_GameThread(const FEmbodiedFusionUpdateInput& Input)
{
	const FMediaPipeBodyFusionRuntimePolicySnapshot RuntimePolicy =
		FMediaPipeBodyFusionRuntimePolicy::ReadGameThread();
	LatestFrame.ResetTransient();
	LatestFrame.bRuntimeEnabled = RuntimePolicy.bBodyFusionEnabled ? 1 : 0;

	if (!RuntimePolicy.bBodyFusionEnabled)
	{
		return false;
	}

	const double NowSeconds = Input.NowSeconds >= 0.0
		? Input.NowSeconds
		: (SourceObservations.NowSeconds >= 0.0
			? SourceObservations.NowSeconds
			: FPlatformTime::Seconds());

	FMediaPipeTrackingSourceFrameBuilderInput SourceFrameInput;
	SourceFrameInput.NowSeconds = NowSeconds;
	SourceFrameInput.bHasHmdPose = SourceObservations.HmdPose.bHasPose;
	SourceFrameInput.HmdLocationWorld = SourceObservations.HmdPose.LocationWorld;
	SourceFrameInput.HmdRotationWorld = SourceObservations.HmdPose.RotationWorld;
	SourceFrameInput.HmdTrackingUpWorld = SourceObservations.HmdPose.TrackingUpWorld;
	SourceFrameInput.Hands = SourceObservations.Hands;
	SourceFrameInput.ArmChain = SourceObservations.ArmChain;
	SourceFrameInput.BodyPose = SourceObservations.BodyPose;
	SourceFrameInput.bOverrideArmChainMaxAgeSeconds =
		Input.bOverrideArmChainMaxAgeSeconds;
	SourceFrameInput.ArmChainMaxAgeSeconds =
		Input.ArmChainMaxAgeSeconds;

	FMediaPipeTrackingSourceFrameBuilder::BuildSourceFrame(
		SourceFrameInput,
		LatestFrame.SourceFrame,
		LatestFrame.FreshnessThresholds);

	TryUpdateCalibration_GameThread(Input, RuntimePolicy, NowSeconds);

	FMediaPipeBodyFusionAuthorityGateInput AuthorityGateInput;
	AuthorityGateInput.MediaPipeAuthorityMode = RuntimePolicy.MediaPipeAuthorityMode;
	AuthorityGateInput.bCalibrationUsable = Calibration.IsUsable();
	AuthorityGateInput.CalibrationRejectReason = Calibration.LastRejectReason;
	AuthorityGateInput.BodyPoseStatus = LatestFrame.SourceFrame.BodyPoseStatus;
	const FMediaPipeBodyFusionAuthorityGateDecision AuthorityGateDecision =
		FMediaPipeBodyFusionAuthorityPolicy::ResolveMediaPipePoseAuthorityGate(AuthorityGateInput);

	LatestFrame.Authority = AuthorityGateDecision.Authority;
	LatestFrame.AuthorityState = AuthorityGateDecision.AuthorityState;
	LatestFrame.AuthorityReason = AuthorityGateDecision.Reason;
	LatestFrame.bMediaPipeAuthorityAllowed = AuthorityGateDecision.bAllowMediaPipePoseAuthority ? 1 : 0;
	LatestFrame.Calibration = Calibration;
	LatestFrame.CalibrationStableFrameCount = CalibrationStableFrameCount;
	LatestFrame.CalibrationStableSeconds = CalibrationStableSeconds;

	FMediaPipeBodyFusionSolveInput SolveInput;
	SolveInput.SourceFrame = LatestFrame.SourceFrame;
	SolveInput.Calibration = Calibration;
	SolveInput.Authority = LatestFrame.Authority;
	SolveInput.Profile = Input.AvatarProfile;
	SolveInput.AvatarWorldTransform = Input.TargetComponentTransform;
	SolveInput.UserCameraForwardOffsetCm = 0.0f;
	SolveInput.bAllowMediaPipePoseAuthority = LatestFrame.bMediaPipeAuthorityAllowed != 0;
	SolveInput.BodyAuthorityState = LatestFrame.AuthorityState;

	LatestFrame.Pose.Reset();
	FMediaPipeBodyFusionSolver::Solve(SolveInput, LatestFrame.Pose);
	RefreshBestAvailablePose_GameThread();

	if (RuntimePolicy.bDebugEnabled)
	{
		EmitDebugLog_GameThread(Input, NowSeconds);
	}

	return LatestFrame.Pose.IsUsable();
}

void UEmbodiedFusionComponent::UpdateMovementReplicaPose_GameThread(
	const FEmbodiedFusionMovementReplicaPoseInput& Input)
{
	FEmbodiedFusionQuestSourcePollInput PollInput;
	PollInput.World = Input.World;
	PollInput.TargetComponent = Input.TargetComponent;
	PollInput.TargetActorName = Input.TargetActorName;
	PollInput.bUseQuestHandTracking = Input.bUseHandTracking;
	PollInput.bBodyFusionRuntimeActive = true;
	PollInput.bForceHmdPose = true;
	PollInput.bReadFullArmChain = false;
	PollQuestRuntimeSources_GameThread(PollInput, ComponentDiagnosticsState);
	const double NowSeconds = FPlatformTime::Seconds();
	FMediaPipeTrackingSourceFrameBuilderInput SourceFrameInput;
	SourceFrameInput.NowSeconds = NowSeconds;
	SourceFrameInput.bHasHmdPose = SourceObservations.HmdPose.bHasPose;
	SourceFrameInput.HmdLocationWorld = SourceObservations.HmdPose.LocationWorld;
	SourceFrameInput.HmdRotationWorld = SourceObservations.HmdPose.RotationWorld;
	SourceFrameInput.HmdTrackingUpWorld = SourceObservations.HmdPose.TrackingUpWorld;
	SourceFrameInput.Hands = SourceObservations.Hands;
	SourceFrameInput.ArmChain = SourceObservations.ArmChain;
	SourceFrameInput.BodyPose = SourceObservations.BodyPose;
	FMediaPipeTrackingSourceFrameBuilder::BuildSourceFrame(
		SourceFrameInput,
		LatestFrame.SourceFrame,
		LatestFrame.FreshnessThresholds);
	RefreshBestAvailablePose_GameThread(&Input);
}

void UEmbodiedFusionComponent::RefreshBestAvailablePose_GameThread(
	const FEmbodiedFusionMovementReplicaPoseInput* MovementInput)
{
	LatestFrame.BestAvailablePose.Reset();
	FEmbodiedFusionBestAvailablePose& BestPose = LatestFrame.BestAvailablePose;
	const bool bUseFusedPoseForOutput =
		FMediaPipeBodyFusionRuntimePolicy::IsPoseWriteEnabledGameThread();

	if (bUseFusedPoseForOutput &&
		LatestFrame.Pose.IsUsable() &&
		LatestFrame.Pose.Head.bValid &&
		!LatestFrame.Pose.Head.RotationWorld.ContainsNaN())
	{
		BestPose.bHasHead = true;
		BestPose.HeadLocationWorld = LatestFrame.Pose.Head.LocationWorld;
		BestPose.HeadRotationWorld = LatestFrame.Pose.Head.RotationWorld;
		BestPose.HeadSource = FName(TEXT("BodyFusion"));
		BestPose.HeadStatus.State = EMediaPipeBodyFusionSourceState::Fresh;
		BestPose.HeadStatus.Confidence = LatestFrame.Pose.Head.Confidence;
		BestPose.HeadStatus.AgeSeconds = 0.0f;
	}
	else if (SourceObservations.HmdPose.bHasPose &&
		!SourceObservations.HmdPose.RotationWorld.ContainsNaN())
	{
		BestPose.bHasHead = true;
		BestPose.HeadLocationWorld = SourceObservations.HmdPose.LocationWorld;
		BestPose.HeadRotationWorld = SourceObservations.HmdPose.RotationWorld;
		BestPose.HeadSource = FName(TEXT("HMD"));
		BestPose.HeadStatus.State = EMediaPipeBodyFusionSourceState::Fresh;
		BestPose.HeadStatus.Confidence = 1.0f;
		BestPose.HeadStatus.AgeSeconds = 0.0f;
	}
	else if (MovementInput && MovementInput->FallbackHeadComponent)
	{
		BestPose.bHasHead = true;
		BestPose.HeadLocationWorld = MovementInput->FallbackHeadComponent->GetComponentLocation();
		BestPose.HeadRotationWorld = MovementInput->FallbackHeadComponent->GetComponentQuat();
		BestPose.HeadSource = FName(TEXT("FallbackHeadComponent"));
		BestPose.HeadStatus.State = EMediaPipeBodyFusionSourceState::Fresh;
		BestPose.HeadStatus.Confidence = 0.25f;
		BestPose.HeadStatus.AgeSeconds = 0.0f;
	}

	auto PopulateSide = [&](const bool bLeft)
	{
		FEmbodiedFusionUpperLimbPose& OutLimb = BestPose.GetMutableUpperLimb(bLeft);
		CopyHandJointsToBestPose(SourceObservations.Hands, bLeft, OutLimb.HandJoints);

		FMediaPipeFusedUpperLimbSide FusedSide;
		if (bUseFusedPoseForOutput &&
			LatestFrame.Pose.IsUsable() &&
			FMediaPipeAvatarPoseWriter::TryGetUpperLimbSide(LatestFrame.Pose, bLeft, FusedSide))
		{
			FMediaPipeBodyFusionSourceStatus Status;
			Status.State = EMediaPipeBodyFusionSourceState::Fresh;
			Status.Confidence = 1.0f;
			Status.AgeSeconds = 0.0f;
			SetUpperLimbFromFusionPointChain(
				OutLimb,
				FusedSide.ShoulderWorld,
				FusedSide.ElbowWorld,
				FusedSide.WristWorld,
				FName(TEXT("BodyFusion")),
				Status);
			return;
		}

		const FMediaPipeBodyFusionSourceStatus& ArmChainStatus =
			bLeft ? LatestFrame.SourceFrame.LeftArmChainStatus : LatestFrame.SourceFrame.RightArmChainStatus;
		const bool bHasArmChain =
			bLeft ? LatestFrame.SourceFrame.bHasLeftArmChain : LatestFrame.SourceFrame.bHasRightArmChain;
		if (bHasArmChain && ArmChainStatus.IsFresh())
		{
			SetUpperLimbFromFusionPointChain(
				OutLimb,
				bLeft ? LatestFrame.SourceFrame.LeftArmShoulderWorld : LatestFrame.SourceFrame.RightArmShoulderWorld,
				bLeft ? LatestFrame.SourceFrame.LeftArmElbowWorld : LatestFrame.SourceFrame.RightArmElbowWorld,
				bLeft ? LatestFrame.SourceFrame.LeftArmWristWorld : LatestFrame.SourceFrame.RightArmWristWorld,
				FName(TEXT("ArmChain")),
				ArmChainStatus);
			return;
		}

		FTransform HandTargetWorld = FTransform::Identity;
		bool bTracked = false;
		if (TryGetHandTrackingTarget(SourceObservations.Hands, bLeft, HandTargetWorld, bTracked))
		{
			OutLimb.bHasHandTarget = true;
			OutLimb.bHandTargetTracked = bTracked;
			OutLimb.HandTargetWorld = HandTargetWorld;
			OutLimb.WristWorld = HandTargetWorld.GetLocation();
			OutLimb.Source = FName(TEXT("HandTracking"));
			OutLimb.Status.State = EMediaPipeBodyFusionSourceState::Fresh;
			OutLimb.Status.Confidence = bTracked ? 1.0f : 0.5f;
			OutLimb.Status.AgeSeconds = 0.0f;
			return;
		}

		if (MovementInput)
		{
			const UMotionControllerComponent* MotionController =
				bLeft ? MovementInput->LeftMotionController : MovementInput->RightMotionController;
			if (TryGetMotionControllerTarget(MotionController, HandTargetWorld))
			{
				OutLimb.bHasHandTarget = true;
				OutLimb.bHandTargetTracked = true;
				OutLimb.HandTargetWorld = HandTargetWorld;
				OutLimb.WristWorld = HandTargetWorld.GetLocation();
				OutLimb.Source = FName(TEXT("MotionController"));
				OutLimb.Status.State = EMediaPipeBodyFusionSourceState::Fresh;
				OutLimb.Status.Confidence = 1.0f;
				OutLimb.Status.AgeSeconds = 0.0f;
			}
		}
	};

	PopulateSide(true);
	PopulateSide(false);
}

FVector UEmbodiedFusionComponent::LockVectorToHemisphere(const FVector& Vector, const FVector& Reference)
{
	return FVector::DotProduct(Vector, Reference) < 0.0f ? -Vector : Vector;
}

float UEmbodiedFusionComponent::EstimateObservedHeightCm(const FMediaPipeTrackingSourceFrame& SourceFrame)
{
	FVector HeadWorld = FVector::ZeroVector;
	float HeadReliability = 0.0f;
	if (!SourceFrame.TryGetBodyLandmark(EMediaPipePoseLandmark::Nose, HeadWorld, &HeadReliability))
	{
		return 0.0f;
	}

	float FloorZ = TNumericLimits<float>::Max();
	bool bHasFloor = false;
	auto ConsiderFloorLandmark = [&](const EMediaPipePoseLandmark Landmark)
	{
		FVector PointWorld = FVector::ZeroVector;
		float Reliability = 0.0f;
		if (SourceFrame.TryGetBodyLandmark(Landmark, PointWorld, &Reliability))
		{
			FloorZ = bHasFloor ? FMath::Min(FloorZ, PointWorld.Z) : PointWorld.Z;
			bHasFloor = true;
		}
	};

	ConsiderFloorLandmark(EMediaPipePoseLandmark::LeftAnkle);
	ConsiderFloorLandmark(EMediaPipePoseLandmark::RightAnkle);
	ConsiderFloorLandmark(EMediaPipePoseLandmark::LeftHeel);
	ConsiderFloorLandmark(EMediaPipePoseLandmark::RightHeel);
	ConsiderFloorLandmark(EMediaPipePoseLandmark::LeftFootIndex);
	ConsiderFloorLandmark(EMediaPipePoseLandmark::RightFootIndex);

	return bHasFloor ? FMath::Max(0.0f, HeadWorld.Z - FloorZ) : 0.0f;
}

bool UEmbodiedFusionComponent::TryUpdateCalibration_GameThread(
	const FEmbodiedFusionUpdateInput& Input,
	const FMediaPipeBodyFusionRuntimePolicySnapshot& RuntimePolicy,
	const double NowSeconds)
{
	const int32 ResetSerial = FMediaPipeEmbodimentDebugCommands::GetBodyFusionCalibrationResetSerial();
	if (LastCalibrationResetSerial != ResetSerial)
	{
		LastCalibrationResetSerial = ResetSerial;
		Calibration.Reset();
		CalibrationStableFrameCount = 0;
		CalibrationStableSeconds = 0.0f;
		LastCalibrationUpdateTimeSeconds = -1.0;
		if (RuntimePolicy.bDebugEnabled)
		{
			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.BodyFusion.Calibration actor=%s reset serial=%d"),
				*Input.TargetActorName.ToString(),
				ResetSerial);
		}
	}

	if (Calibration.IsUsable())
	{
		return true;
	}

	FVector HipCenterWorld = FVector::ZeroVector;
	FVector ShoulderCenterWorld = FVector::ZeroVector;
	float HipReliability = 0.0f;
	float ShoulderReliability = 0.0f;
	const bool bHasHipCenter = FMediaPipeBodyFusionDebugFormatter::TryLandmarkMidpoint(
		LatestFrame.SourceFrame,
		EMediaPipePoseLandmark::LeftHip,
		EMediaPipePoseLandmark::RightHip,
		HipCenterWorld,
		&HipReliability);
	const bool bHasShoulderCenter = FMediaPipeBodyFusionDebugFormatter::TryLandmarkMidpoint(
		LatestFrame.SourceFrame,
		EMediaPipePoseLandmark::LeftShoulder,
		EMediaPipePoseLandmark::RightShoulder,
		ShoulderCenterWorld,
		&ShoulderReliability);

	FVector LeftHipWorld = FVector::ZeroVector;
	FVector RightHipWorld = FVector::ZeroVector;
	float LeftHipReliability = 0.0f;
	float RightHipReliability = 0.0f;
	const bool bHasLeftHip = LatestFrame.SourceFrame.TryGetBodyLandmark(
		EMediaPipePoseLandmark::LeftHip,
		LeftHipWorld,
		&LeftHipReliability);
	const bool bHasRightHip = LatestFrame.SourceFrame.TryGetBodyLandmark(
		EMediaPipePoseLandmark::RightHip,
		RightHipWorld,
		&RightHipReliability);

	FVector NoseWorld = FVector::ZeroVector;
	float NoseReliability = 0.0f;
	LatestFrame.SourceFrame.TryGetBodyLandmark(EMediaPipePoseLandmark::Nose, NoseWorld, &NoseReliability);

	FMediaPipeAvatarEmbodimentProfile CalibrationProfile = Input.AvatarProfile;
	const FVector AvatarForwardWorld = Input.TargetForwardWorld.GetSafeNormal();
	FVector AvatarUpWorld = FMediaPipeAvatarEmbodimentSolver::GetAvatarUpWorld(
		Input.TargetComponentTransform,
		AvatarForwardWorld).GetSafeNormal();
	if (AvatarUpWorld.IsNearlyZero())
	{
		AvatarUpWorld = FVector::UpVector;
	}

	FVector MediaPipeForwardWorld = AvatarForwardWorld;
	if (bHasHipCenter && bHasShoulderCenter && bHasLeftHip && bHasRightHip)
	{
		const FVector MediaPipeUpWorld = (ShoulderCenterWorld - HipCenterWorld).GetSafeNormal();
		FVector MediaPipeRightWorld = (RightHipWorld - LeftHipWorld).GetSafeNormal();
		MediaPipeRightWorld = (MediaPipeRightWorld - FVector::DotProduct(MediaPipeRightWorld, MediaPipeUpWorld) * MediaPipeUpWorld).GetSafeNormal();
		FVector CandidateForwardWorld = FVector::CrossProduct(MediaPipeRightWorld, MediaPipeUpWorld).GetSafeNormal();
		if (!CandidateForwardWorld.IsNearlyZero())
		{
			if (!AvatarForwardWorld.IsNearlyZero())
			{
				CandidateForwardWorld = LockVectorToHemisphere(CandidateForwardWorld, AvatarForwardWorld);
			}
			MediaPipeForwardWorld = CandidateForwardWorld;
		}
	}

	const FVector AvatarPelvisAnchorWorld = !Input.RefPelvisTranslationComp.IsNearlyZero()
		? Input.TargetComponentTransform.TransformPosition(Input.RefPelvisTranslationComp)
		: Input.TargetComponentTransform.TransformPosition(CalibrationProfile.DefaultPelvisLocalOffset);
	const float Confidence = FMath::Min(
		LatestFrame.SourceFrame.BodyPoseStatus.Confidence,
		FMath::Min(HipReliability, ShoulderReliability));
	const float ObservedBodyHeightCm = EstimateObservedHeightCm(LatestFrame.SourceFrame);
	const float AvatarBodyHeightCm = FMath::Max(CalibrationProfile.DefaultEyeLocalOffset.Z, ObservedBodyHeightCm);

	FMediaPipeEmbodimentCalibrationInput CalibrationInput;
	CalibrationInput.MediaPipeHipCenterWorld = HipCenterWorld;
	CalibrationInput.MediaPipeForwardWorld = MediaPipeForwardWorld;
	CalibrationInput.AvatarPelvisAnchorWorld = AvatarPelvisAnchorWorld;
	CalibrationInput.AvatarForwardWorld = AvatarForwardWorld.IsNearlyZero() ? FVector::ForwardVector : AvatarForwardWorld;
	CalibrationInput.AvatarUpWorld = AvatarUpWorld;
	CalibrationInput.HmdWorld = LatestFrame.SourceFrame.HmdLocationWorld;
	CalibrationInput.ObservedBodyHeightCm = ObservedBodyHeightCm;
	CalibrationInput.AvatarBodyHeightCm = AvatarBodyHeightCm;
	CalibrationInput.Confidence = Confidence;
	CalibrationInput.bHmdStable = LatestFrame.SourceFrame.HmdStatus.IsFresh();
	CalibrationInput.bMediaPipeStable =
		LatestFrame.SourceFrame.BodyPoseStatus.IsFresh() &&
		bHasHipCenter &&
		bHasShoulderCenter &&
		!MediaPipeForwardWorld.IsNearlyZero();
	CalibrationInput.TimestampSeconds = NowSeconds;

	const float CalibrationDeltaSeconds = LastCalibrationUpdateTimeSeconds >= 0.0
		? FMath::Clamp(static_cast<float>(NowSeconds - LastCalibrationUpdateTimeSeconds), 0.0f, 0.25f)
		: 0.0f;
	LastCalibrationUpdateTimeSeconds = NowSeconds;
	const bool bCalibrationSampleStable =
		CalibrationInput.bHmdStable &&
		CalibrationInput.bMediaPipeStable &&
		CalibrationInput.Confidence >= 0.5f &&
		ObservedBodyHeightCm > KINDA_SMALL_NUMBER;
	if (bCalibrationSampleStable)
	{
		++CalibrationStableFrameCount;
		CalibrationStableSeconds += CalibrationDeltaSeconds;
	}
	else
	{
		CalibrationStableFrameCount = 0;
		CalibrationStableSeconds = 0.0f;
	}

	const int32 RequiredStableFrames = RuntimePolicy.RequiredCalibrationStableFrames;
	const float RequiredStableSeconds = RuntimePolicy.RequiredCalibrationStableSeconds;
	const bool bStableGateSatisfied =
		CalibrationStableFrameCount >= RequiredStableFrames &&
		CalibrationStableSeconds >= RequiredStableSeconds;

	if (!bStableGateSatisfied)
	{
		Calibration.Reset();
		Calibration.LastRejectReason = bCalibrationSampleStable
			? TEXT("Waiting for stable MediaPipe calibration")
			: (CalibrationInput.bMediaPipeStable ? TEXT("Low MediaPipe confidence") : TEXT("MediaPipe unstable"));
		if (RuntimePolicy.bDebugEnabled &&
			FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(
				NowSeconds,
				1.0,
				LastCalibrationLogTimeSeconds))
		{
			FMediaPipeBodyFusionDebugFormatter::EmitCalibrationRejected(
				Input.TargetActorName,
				Calibration.LastRejectReason,
				Confidence,
				LatestFrame.SourceFrame,
				bHasHipCenter,
				bHasShoulderCenter,
				ObservedBodyHeightCm,
				CalibrationStableFrameCount,
				RequiredStableFrames,
				CalibrationStableSeconds,
				RequiredStableSeconds);
		}
		return false;
	}

	const bool bAccepted = FMediaPipeEmbodimentCalibration::TryBuildNeutralCalibration(
		CalibrationInput,
		Calibration);
	if (RuntimePolicy.bDebugEnabled &&
		FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(
			NowSeconds,
			1.0,
			LastCalibrationLogTimeSeconds))
	{
		if (bAccepted)
		{
			FMediaPipeBodyFusionDebugFormatter::EmitCalibrationAccepted(
				Input.TargetActorName,
				Calibration.Confidence,
				ObservedBodyHeightCm,
				AvatarBodyHeightCm,
				CalibrationStableFrameCount,
				RequiredStableFrames,
				CalibrationStableSeconds,
				RequiredStableSeconds,
				Calibration.YawRotation,
				Calibration.Translation,
				Calibration.Scale,
				LatestFrame.SourceFrame,
				HipCenterWorld,
				ShoulderCenterWorld,
				MediaPipeForwardWorld);
		}
		else
		{
			FMediaPipeBodyFusionDebugFormatter::EmitCalibrationRejected(
				Input.TargetActorName,
				Calibration.LastRejectReason,
				Confidence,
				LatestFrame.SourceFrame,
				bHasHipCenter,
				bHasShoulderCenter,
				ObservedBodyHeightCm,
				CalibrationStableFrameCount,
				RequiredStableFrames,
				CalibrationStableSeconds,
				RequiredStableSeconds);
		}
	}

	return bAccepted;
}

void UEmbodiedFusionComponent::EmitDebugLog_GameThread(
	const FEmbodiedFusionUpdateInput& Input,
	const double NowSeconds)
{
	if (!FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(
		NowSeconds,
		1.0,
		LastDebugLogTimeSeconds))
	{
		return;
	}

	const FMediaPipeAvatarEmbodimentProfile BodyFusionProfile = Input.AvatarProfile;
	const FVector AvatarForwardWorld = Input.TargetForwardWorld.GetSafeNormal();
	FVector AvatarUpWorld = FMediaPipeAvatarEmbodimentSolver::GetAvatarUpWorld(
		Input.TargetComponentTransform,
		AvatarForwardWorld).GetSafeNormal();
	if (AvatarUpWorld.IsNearlyZero())
	{
		AvatarUpWorld = FVector::UpVector;
	}

	const FVector AvatarRootWorld = Input.TargetComponentTransform.GetLocation();
	const FVector AvatarEyeWorld = Input.TargetComponentTransform.TransformPosition(BodyFusionProfile.DefaultEyeLocalOffset);
	const FVector AvatarHeadWorld = !Input.RefHeadPosComp.IsNearlyZero()
		? Input.TargetComponentTransform.TransformPosition(Input.RefHeadPosComp)
		: Input.TargetComponentTransform.TransformPosition(ResolveMediaPipeAvatarProfileHeadLocal(BodyFusionProfile));
	const FVector AvatarPelvisWorld = !Input.RefPelvisTranslationComp.IsNearlyZero()
		? Input.TargetComponentTransform.TransformPosition(Input.RefPelvisTranslationComp)
		: Input.TargetComponentTransform.TransformPosition(BodyFusionProfile.DefaultPelvisLocalOffset);
	const FVector AvatarChestWorld = Input.bHasRefChestPosComp
		? Input.TargetComponentTransform.TransformPosition(Input.RefChestPosComp)
		: Input.TargetComponentTransform.TransformPosition(BodyFusionProfile.DefaultChestLocalOffset);
	const FVector AvatarNeckWorld = Input.TargetComponentTransform.TransformPosition(BodyFusionProfile.DefaultNeckLocalOffset);

	FVector MediaPipeHipCenterWorld = FVector::ZeroVector;
	FVector MediaPipeShoulderCenterWorld = FVector::ZeroVector;
	FVector MediaPipeHeadWorld = FVector::ZeroVector;
	float MediaPipeHipReliability = 0.0f;
	float MediaPipeShoulderReliability = 0.0f;
	const bool bHasMediaPipeHip = FMediaPipeBodyFusionDebugFormatter::TryLandmarkMidpoint(
		LatestFrame.SourceFrame,
		EMediaPipePoseLandmark::LeftHip,
		EMediaPipePoseLandmark::RightHip,
		MediaPipeHipCenterWorld,
		&MediaPipeHipReliability);
	const bool bHasMediaPipeShoulder = FMediaPipeBodyFusionDebugFormatter::TryLandmarkMidpoint(
		LatestFrame.SourceFrame,
		EMediaPipePoseLandmark::LeftShoulder,
		EMediaPipePoseLandmark::RightShoulder,
		MediaPipeShoulderCenterWorld,
		&MediaPipeShoulderReliability);
	float MediaPipeHeadReliability = 0.0f;
	const bool bHasMediaPipeHead = LatestFrame.SourceFrame.TryGetBodyLandmark(
		EMediaPipePoseLandmark::Nose,
		MediaPipeHeadWorld,
		&MediaPipeHeadReliability);

	const bool bHasHmd = LatestFrame.SourceFrame.HmdStatus.IsFresh();
	const float CameraToEyeCm = bHasHmd ? FVector::Distance(LatestFrame.SourceFrame.HmdLocationWorld, AvatarEyeWorld) : -1.0f;
	const float CameraToChestCm = bHasHmd ? FVector::Distance(LatestFrame.SourceFrame.HmdLocationWorld, AvatarChestWorld) : -1.0f;
	const float HeadToChestCm = FVector::Distance(AvatarHeadWorld, AvatarChestWorld);
	const float ChestToPelvisCm = FVector::Distance(AvatarChestWorld, AvatarPelvisWorld);
	const float HmdYawDeg = bHasHmd ? LatestFrame.SourceFrame.HmdRotationWorld.Rotator().Yaw : 0.0f;

	UE_LOG(LogMediaPipePose, Log,
		TEXT("mp.BodyFusion.Debug actor=%s bodyAuthority=%s mediaPipeAuthority=%d reason=\"%s\" stableFrames=%d stableSeconds=%.2f hmd=%s qHandL=%s qHandR=%s fullChainL=%s fullChainR=%s mediaPipe=%s hmdYaw=%.1f hmd=%s trackingUp=%s avatarRoot=%s eye=%s head=%s neck=%s chest=%s pelvis=%s forward=%s mpHip=%s hipRel=%.2f mpShoulder=%s shoulderRel=%.2f mpHead=%s headRel=%.2f dist(cameraEye=%.1f cameraChest=%.1f headChest=%.1f chestPelvis=%.1f) hmdPlanar(offset=%.1f) solve=%d solveEye=%s solveHead=%s solveChest=%s solvePelvis=%s"),
		*Input.TargetActorName.ToString(),
		FMediaPipeBodyFusionDebugFormatter::AuthorityStateName(LatestFrame.AuthorityState),
		LatestFrame.bMediaPipeAuthorityAllowed ? 1 : 0,
		*LatestFrame.AuthorityReason,
		CalibrationStableFrameCount,
		CalibrationStableSeconds,
		*FMediaPipeBodyFusionDebugFormatter::StatusString(LatestFrame.SourceFrame.HmdStatus),
		*FMediaPipeBodyFusionDebugFormatter::StatusString(LatestFrame.SourceFrame.LeftHandStatus),
		*FMediaPipeBodyFusionDebugFormatter::StatusString(LatestFrame.SourceFrame.RightHandStatus),
		*FMediaPipeBodyFusionDebugFormatter::StatusString(LatestFrame.SourceFrame.LeftArmChainStatus),
		*FMediaPipeBodyFusionDebugFormatter::StatusString(LatestFrame.SourceFrame.RightArmChainStatus),
		*FMediaPipeBodyFusionDebugFormatter::StatusString(LatestFrame.SourceFrame.BodyPoseStatus),
		HmdYawDeg,
		*FMediaPipeBodyFusionDebugFormatter::VectorString(LatestFrame.SourceFrame.HmdLocationWorld),
		*FMediaPipeBodyFusionDebugFormatter::VectorString(LatestFrame.SourceFrame.TrackingUpWorld),
		*FMediaPipeBodyFusionDebugFormatter::VectorString(AvatarRootWorld),
		*FMediaPipeBodyFusionDebugFormatter::VectorString(AvatarEyeWorld),
		*FMediaPipeBodyFusionDebugFormatter::VectorString(AvatarHeadWorld),
		*FMediaPipeBodyFusionDebugFormatter::VectorString(AvatarNeckWorld),
		*FMediaPipeBodyFusionDebugFormatter::VectorString(AvatarChestWorld),
		*FMediaPipeBodyFusionDebugFormatter::VectorString(AvatarPelvisWorld),
		*FMediaPipeBodyFusionDebugFormatter::VectorString(AvatarForwardWorld),
		bHasMediaPipeHip ? *FMediaPipeBodyFusionDebugFormatter::VectorString(MediaPipeHipCenterWorld) : TEXT("(missing)"),
		MediaPipeHipReliability,
		bHasMediaPipeShoulder ? *FMediaPipeBodyFusionDebugFormatter::VectorString(MediaPipeShoulderCenterWorld) : TEXT("(missing)"),
		MediaPipeShoulderReliability,
		bHasMediaPipeHead ? *FMediaPipeBodyFusionDebugFormatter::VectorString(MediaPipeHeadWorld) : TEXT("(missing)"),
		MediaPipeHeadReliability,
		CameraToEyeCm,
		CameraToChestCm,
		HeadToChestCm,
		ChestToPelvisCm,
		LatestFrame.Pose.DebugErrors.HmdHorizontalOffsetCm,
		LatestFrame.Pose.IsUsable() ? 1 : 0,
		*FMediaPipeBodyFusionDebugFormatter::VectorString(LatestFrame.Pose.Eye.LocationWorld),
		*FMediaPipeBodyFusionDebugFormatter::VectorString(LatestFrame.Pose.Head.LocationWorld),
		*FMediaPipeBodyFusionDebugFormatter::VectorString(LatestFrame.Pose.Chest.LocationWorld),
		*FMediaPipeBodyFusionDebugFormatter::VectorString(LatestFrame.Pose.Pelvis.LocationWorld));
}
