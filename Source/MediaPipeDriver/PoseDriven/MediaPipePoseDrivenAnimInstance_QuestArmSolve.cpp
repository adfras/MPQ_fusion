#include "MediaPipePoseDrivenAnimInstance.h"

#include "MediaPipeArmDirectionCorrector.h"
#include "MediaPipeArmLossOverride.h"
#include "MediaPipeArmGuardPolicy.h"
#include "MediaPipeArmTwistSolver.h"
#include "MediaPipeBodyDiagnostics.h"
#include "MediaPipeBodyFusionDebugFormatter.h"
#include "MediaPipeBodyFusionPoseWriteContext.h"
#include "MediaPipeBodyFusionRuntime.h"
#include "MediaPipeTrackingSourceFrameBuilder.h"
#include "MediaPipeBodySolverMath.h"
#include "MediaPipeMetaHumanArmRetargeter.h"
#include "MediaPipeMetaHumanPoseAdapter.h"
#include "MediaPipePoseCoordinate.h"
#include "MediaPipePoseDiagnosticReporter.h"
#include "MediaPipePoseDiagnostics.h"
#include "MediaPipePoseFrameContinuity.h"
#include "MediaPipeRuntimeCVars.h"
#include "MediaPipeStage2ShoulderEvidence.h"
#include "MediaPipeTrackingFusionDatasetReplay.h"
#include "MediaPipeTrackingQualityMetrics.h"
#include "MediaPipeQuestHandDebugReporter.h"
#include "MediaPipeQuestHandCompareDiagnostics.h"
#include "MediaPipeQuestFingerSolver.h"
#include "MediaPipeQuestConstrainedArmSolver.h"
#include "MediaPipeQuestWristApplyPolicy.h"
#include "MediaPipeReachExtender.h"
#include "MediaPipeQuestWristDebugReporter.h"
#include "MediaPipeQuestWristCalibrationState.h"
#include "MediaPipeQuestWristDiagnosticFormatter.h"
#include "MediaPipeSkeletonPoseAdapter.h"
#include "MediaPipeShoulderRollbackDiagnostics.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeSolvedPose.h"

#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"

#include <atomic>
#include "HAL/IConsoleManager.h"
#include "HeadMountedDisplayTypes.h"
#include "Math/RotationMatrix.h"

#include "MediaPipePoseDrivenAnimInstanceShared.h"

namespace
{
// Maps the 21-landmark MediaPipe hand onto the 26-joint OpenXR hand snapshot layout so the
// existing user-validated segment-direction finger solver can consume CAMERA hands unchanged
// while a Quest hand is untracked (USER FEEDBACK 2026-07-03: overhead, frozen Quest hands
// looked broken). Positions only - the accepted segmentDirection finger mode solves from
// joint POSITIONS; identity rotations make the dormant jointOrientation mode veto itself.
// MediaPipe hand world landmarks are HAND-CENTERED, not body-frame: every point is written as
// a wrist-relative delta re-anchored on the avatar's solved wrist, matching the wrist-rotation
// basis path above.
void FillSyntheticQuestHandSideFromMediaPipe(
	const FMediaPipeRawHandLandmarks& HandWorldLandmarks,
	const FVector& AnchorWristWorld,
	const FTransform& PoseToWorldTransform,
	const float WorldScale,
	const bool bMirrorLandmarksLR,
	const bool bFillLeftSide,
	FQuestHandTrackingSnapshot& InOutSnapshot)
{
	const FMediaPipeRawHandLandmark& WristRaw = HandWorldLandmarks.Landmarks[0];
	auto LandmarkWorld = [&](const int32 HandLmIndex) -> FVector
	{
		const FMediaPipeRawHandLandmark& PointRaw = HandWorldLandmarks.Landmarks[HandLmIndex];
		const FVector DeltaMp(
			PointRaw.X - WristRaw.X,
			PointRaw.Y - WristRaw.Y,
			PointRaw.Z - WristRaw.Z);
		FVector DeltaUe = MediaPipePoseCoordinate::MpWorldToUeLocalUnscaled(DeltaMp, bMirrorLandmarksLR) * WorldScale;
		DeltaUe = PoseToWorldTransform.TransformVectorNoScale(DeltaUe);
		return AnchorWristWorld + DeltaUe;
	};

	TStaticArray<FVector, QuestHandKeypointCount>& Positions =
		bFillLeftSide ? InOutSnapshot.LeftPositionsWorld : InOutSnapshot.RightPositionsWorld;
	TStaticArray<FQuat, QuestHandKeypointCount>& Rotations =
		bFillLeftSide ? InOutSnapshot.LeftRotationsWorld : InOutSnapshot.RightRotationsWorld;
	TStaticArray<float, QuestHandKeypointCount>& Radii =
		bFillLeftSide ? InOutSnapshot.LeftRadii : InOutSnapshot.RightRadii;
	auto SetJoint = [&](const EHandKeypoint Keypoint, const FVector& World)
	{
		const int32 JointIndex = static_cast<int32>(Keypoint);
		Positions[JointIndex] = World;
		Rotations[JointIndex] = FQuat::Identity;
		Radii[JointIndex] = 1.0f;
	};

	// 21-landmark MediaPipe hand layout: 0 wrist; thumb 1 CMC, 2 MCP, 3 IP, 4 tip;
	// then per finger (index 5.., middle 9.., ring 13.., little 17..): MCP, PIP, DIP, tip.
	const FVector MiddleMcpWorld = LandmarkWorld(9);
	SetJoint(EHandKeypoint::Wrist, AnchorWristWorld);
	SetJoint(EHandKeypoint::Palm, (AnchorWristWorld + MiddleMcpWorld) * 0.5f);
	SetJoint(EHandKeypoint::ThumbMetacarpal, LandmarkWorld(1));
	SetJoint(EHandKeypoint::ThumbProximal, LandmarkWorld(2));
	SetJoint(EHandKeypoint::ThumbDistal, LandmarkWorld(3));
	SetJoint(EHandKeypoint::ThumbTip, LandmarkWorld(4));
	struct FSyntheticFingerMap
	{
		EHandKeypoint Metacarpal;
		EHandKeypoint Proximal;
		EHandKeypoint Intermediate;
		EHandKeypoint Distal;
		EHandKeypoint Tip;
		int32 McpLandmark;
	};
	const FSyntheticFingerMap Fingers[4] = {
		{EHandKeypoint::IndexMetacarpal, EHandKeypoint::IndexProximal, EHandKeypoint::IndexIntermediate, EHandKeypoint::IndexDistal, EHandKeypoint::IndexTip, 5},
		{EHandKeypoint::MiddleMetacarpal, EHandKeypoint::MiddleProximal, EHandKeypoint::MiddleIntermediate, EHandKeypoint::MiddleDistal, EHandKeypoint::MiddleTip, 9},
		{EHandKeypoint::RingMetacarpal, EHandKeypoint::RingProximal, EHandKeypoint::RingIntermediate, EHandKeypoint::RingDistal, EHandKeypoint::RingTip, 13},
		{EHandKeypoint::LittleMetacarpal, EHandKeypoint::LittleProximal, EHandKeypoint::LittleIntermediate, EHandKeypoint::LittleDistal, EHandKeypoint::LittleTip, 17},
	};
	for (const FSyntheticFingerMap& Finger : Fingers)
	{
		const FVector McpWorld = LandmarkWorld(Finger.McpLandmark);
		// The 21-landmark set has no metacarpal joints; approximate them partway from the wrist
		// toward each knuckle so metacarpal segment directions stay anatomically sane.
		SetJoint(Finger.Metacarpal, FMath::Lerp(AnchorWristWorld, McpWorld, 0.4f));
		SetJoint(Finger.Proximal, McpWorld);
		SetJoint(Finger.Intermediate, LandmarkWorld(Finger.McpLandmark + 1));
		SetJoint(Finger.Distal, LandmarkWorld(Finger.McpLandmark + 2));
		SetJoint(Finger.Tip, LandmarkWorld(Finger.McpLandmark + 3));
	}

	if (bFillLeftSide)
	{
		InOutSnapshot.bHasLeft = 1;
		InOutSnapshot.bLeftTracked = 1;
		InOutSnapshot.LeftTimestampSeconds = FPlatformTime::Seconds();
	}
	else
	{
		InOutSnapshot.bHasRight = 1;
		InOutSnapshot.bRightTracked = 1;
		InOutSnapshot.RightTimestampSeconds = FPlatformTime::Seconds();
	}
	InOutSnapshot.HandTrackerCount = 1;
	InOutSnapshot.ValidHandTrackerCount = 1;
}
} // namespace

// ORCHESTRATOR MAP (refactor/correctors Phase 8; the call sequence below is the frozen
// application order - moving a socket changes behavior and is out of bounds):
//   1. BUILD      - source resolution, tracked-recency bookkeeping, chain/fused/landmark arm
//                   candidates, MediaPipe source snapshot.
//   2. OVERRIDE   - FMediaPipeArmLossOverride (Correctors/): trigger/hold/release latch, then
//                   the source demotion effect applied here.
//   3. SOCKETS    - in the existing order on the chain-direct path:
//                   ApplyMediaPipeArmDirectionCorrection (Correctors/), then
//                   ApplyMediaPipeReachExtension (Correctors/).
//   4. GUARD      - the torso-capsule guard (inline below; not a phase target).
//   5. TRACER     - mp.ArmJumpTrace stage attribution (inline below; instruments the sockets).
//   6. ASSEMBLY   - wrist solve, hand rotation dispatch, fingers. The camera-hand ownership
//                   block (~lines 4040-4870, gated on mp.MediaPipeHandRotationOnQuestLoss /
//                   mp.MediaPipeFingersOnQuestLoss) is BASELINE-LIVE and deliberately frozen
//                   in place - see Docs/REFACTOR_PLAN.md section 9 (Phase 7 deferred).
void FAnimNode_MediaPipePoseDriven::DriveArmCS(FCSPose<FCompactPose>& CSPose, bool bIsLeft, float DeltaSeconds)
{
	if (!bDriveArms)
	{
		return;
	}

	const bool bHasRef = bIsLeft ? bHasRefArmL : bHasRefArmR;
	if (!bHasRef)
	{
		return;
	}

	const int32 ShoulderLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftShoulder : (int32)EMediaPipePoseLandmark::RightShoulder;
	const int32 ElbowLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftElbow : (int32)EMediaPipePoseLandmark::RightElbow;
	const int32 WristLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftWrist : (int32)EMediaPipePoseLandmark::RightWrist;
	const int32 IndexLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftIndex : (int32)EMediaPipePoseLandmark::RightIndex;
	const int32 PinkyLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftPinky : (int32)EMediaPipePoseLandmark::RightPinky;

	const FBoneReference& UpperBone = bIsLeft ? UpperArmL : UpperArmR;
	const FBoneReference& LowerBone = bIsLeft ? LowerArmL : LowerArmR;
	const FBoneReference& HandBone = bIsLeft ? HandL : HandR;

	const bool bQuestHandTrackingEnabled =
		bUseQuestHandTracking &&
		CVarQuestHandTracking.GetValueOnAnyThread() != 0;
	FQuestWristRuntimeState& QuestWristRuntimeState = GetQuestWristRuntimeState(RuntimeStateKey);
	FQuestWristSideRuntimeState& QuestWristSideState = bIsLeft ? QuestWristRuntimeState.Left : QuestWristRuntimeState.Right;
	const bool bMetaHumanFullArmChainRequested =
		TargetMetaHumanProfile.IsValidForPoseDriving() &&
		TargetMetaHumanProfile.bIsActiveProfile &&
		ResolveMediaPipeMetaHumanArmSourceMode(TargetMetaHumanProfile) == 1;
	const FMediaPipeFullArmChainSideSnapshot& FullArmChainSide = FullArmChain.GetSide(bIsLeft);
	const double FullArmChainNowSeconds = FPlatformTime::Seconds();
	const float FullArmChainMaxAgeSeconds = ResolveMediaPipeMetaHumanFullArmChainMaxAgeSeconds(TargetMetaHumanProfile);
	const float RefUpperLen = bIsLeft ? RefUpperLenCompL : RefUpperLenCompR;
	const float RefLowerLen = bIsLeft ? RefLowerLenCompL : RefLowerLenCompR;
	FMediaPipeMetaHumanFullArmChainRetargetInput FullArmChainInput;
	FullArmChainInput.Target = &TargetMetaHumanProfile;
	FullArmChainInput.Snapshot = &FullArmChain;
	FullArmChainInput.TargetComponentTransform = TargetCompTransform;
	FullArmChainInput.bIsLeft = bIsLeft;
	FullArmChainInput.NowSeconds = FullArmChainNowSeconds;
	FullArmChainInput.MaxAgeSeconds = FullArmChainMaxAgeSeconds;
	FullArmChainInput.FallbackUpperArmLengthCm = RefUpperLen;
	FullArmChainInput.FallbackLowerArmLengthCm = RefLowerLen;
	const FMediaPipeMetaHumanFullArmChainRetargetResult FullArmChainResult =
		FMediaPipeMetaHumanFullArmChainRetargeter::ResolveFullArmChainSource(FullArmChainInput);
	const float FullArmChainAgeSeconds = FullArmChainResult.ChainAgeSeconds;
	const bool bMetaHumanFullArmChainFresh =
		bMetaHumanFullArmChainRequested &&
		FullArmChainResult.bFresh;
	const bool bBodyFusionPoseWriteActive = ShouldUseBodyFusionPoseForEvaluation();
	// Live-parity replays must run the live solver paths: the rescue/camera-hand/direct-chain
	// gates below all treat "replay active" as "suppress live behavior", which is correct for
	// the byte-stable evaluation policy but defeats the point of parity scoring (2026-07-04:
	// the overhead rescue never fired in a parity replay because of this flag; the recorded
	// Quest hands stayed "fresh" through the entire overhead window while synthesizing).
	const bool bTrackingFusionReplayActive =
		FMediaPipeTrackingFusionDatasetReplayRuntime::Get().IsActive() &&
		!FMediaPipeTrackingFusionDatasetReplayRuntime::IsLiveParityEnabled();
	bool bMetaHumanFullArmChainDirectFresh =
		bMetaHumanFullArmChainFresh &&
		(!bBodyFusionPoseWriteActive || bTrackingFusionReplayActive);
	auto EmitMetaHumanFullArmChainLog = [&](
		const bool bChainActive,
		const bool bQuestHandUsed,
		const float TargetReachCm,
		const float ElbowBendDeg,
		const FVector& HandWorld)
	{
		if (!bMetaHumanFullArmChainRequested ||
			!ShouldTraceMediaPipeMetaHumanFullArmChain(TargetMetaHumanProfile))
		{
			return;
		}

		double& LastLogTimeSeconds = bIsLeft
			? DiagnosticsState.LastMetaHumanFullArmChainLogTimeSecondsL
			: DiagnosticsState.LastMetaHumanFullArmChainLogTimeSecondsR;
		const double NowSeconds = FPlatformTime::Seconds();
		const double RequiredLogIntervalSeconds = static_cast<double>(
			FMath::Clamp(ResolveMediaPipeMetaHumanFullArmChainTraceIntervalSeconds(TargetMetaHumanProfile), 0.05f, 5.0f));
		if (!FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, RequiredLogIntervalSeconds, LastLogTimeSeconds))
		{
			return;
		}

		FMediaPipeMetaHumanFullArmChainLogInput FullArmChainLogInput;
		FullArmChainLogInput.ProfileId = TargetMetaHumanProfile.ProfileId;
		FullArmChainLogInput.TargetActorName = TargetActorName;
		FullArmChainLogInput.bIsLeft = bIsLeft;
		FullArmChainLogInput.bChainActive = bChainActive;
		FullArmChainLogInput.bShoulderValid = FullArmChainSide.Shoulder.bPositionValid != 0;
		FullArmChainLogInput.bUpperArmValid = FullArmChainSide.UpperArm.bPositionValid != 0;
		FullArmChainLogInput.bLowerArmValid = FullArmChainSide.LowerArm.bPositionValid != 0;
		FullArmChainLogInput.bWristOrPalmValid = FullArmChainSide.WristOrPalm.bPositionValid != 0;
		FullArmChainLogInput.bMediaPipeArmUsed = false;
		FullArmChainLogInput.bQuestHandUsed = bQuestHandUsed;
		FullArmChainLogInput.bWristOrPalmUsesPalm = FullArmChainSide.bWristOrPalmUsesPalm != 0;
		FullArmChainLogInput.TargetReachCm = TargetReachCm;
		FullArmChainLogInput.ElbowBendDeg = ElbowBendDeg;
		FullArmChainLogInput.HandWorld = HandWorld;
		FullArmChainLogInput.Confidence = FullArmChainSide.Confidence;
		FullArmChainLogInput.ChainAgeSeconds = FullArmChainAgeSeconds;
		UE_LOG(LogMediaPipePose, Log, TEXT("%s"), *FormatMediaPipeMetaHumanFullArmChainLog(FullArmChainLogInput));
	};
	const bool bQuestSideTrackedForArm = bQuestHandTrackingEnabled && IsQuestHandSideTracked(QuestHands, bIsLeft);
	// Tracked-flag flicker grace (2026-07-09): the flag drops for sub-second bursts at the
	// FOV edge during side raises; a single dropped frame must not hand the arm or the hand
	// rotation to the camera. Recency lives in the keyed state (RuntimeStateKey==0 skips the
	// write - pre-PreUpdate evaluations must not touch the shared key-0 bucket).
	const double QuestTrackedNowSeconds = FPlatformTime::Seconds();
	if (bQuestSideTrackedForArm && RuntimeStateKey != 0)
	{
		QuestWristSideState.LastQuestSideTrackedTimeSeconds = QuestTrackedNowSeconds;
	}
	const float QuestTrackedGraceSeconds = FMath::Max(0.0f, CVarQuestWristLostTrackingGraceSeconds.GetValueOnAnyThread());
	const bool bQuestSideRecentlyTrackedForArm =
		bQuestSideTrackedForArm ||
		(RuntimeStateKey != 0 &&
			QuestWristSideState.LastQuestSideTrackedTimeSeconds >= 0.0 &&
			QuestTrackedNowSeconds - QuestWristSideState.LastQuestSideTrackedTimeSeconds <= QuestTrackedGraceSeconds);
	const bool bQuestSideUsableForArm = bQuestHandTrackingEnabled && IsQuestHandSideUsableForWrist(QuestHands, bIsLeft);
	const int32 QuestArmMode = FMath::Clamp(CVarQuestArmMode.GetValueOnAnyThread(), 0, 3);
	const bool bQuestArmFallbackAllowed =
		!bBodyFusionPoseWriteActive &&
		!bMetaHumanFullArmChainDirectFresh;
	bool bQuestArmUsesWristEndpoint = QuestArmMode >= 1 && bQuestArmFallbackAllowed;
	bool bQuestArmUsesConstrainedSolve = QuestArmMode >= 2 && bQuestArmFallbackAllowed;
	bool bQuestArmUsesLegacyReachAssist = QuestArmMode == 1 && bQuestArmFallbackAllowed;
	bool bUseHmdRelativeAvatarArmFrame = QuestArmMode >= 3 && bQuestArmFallbackAllowed;
	FMediaPipeFusedUpperLimbSide FusedArmSide;
	const bool bUseBodyFusionArm =
		bBodyFusionPoseWriteActive &&
		!bMetaHumanFullArmChainDirectFresh &&
		FMediaPipeAvatarPoseWriter::TryGetUpperLimbSide(BodyFusionFrame.Pose, bIsLeft, FusedArmSide);

	auto TryGetBoneWorld = [&](const FBoneReference& Bone, FVector& OutWorld) -> bool
	{
		if (!Bone.IsValidToEvaluate())
		{
			return false;
		}

		const FVector BoneComp = CSPose.GetComponentSpaceTransform(Bone.CachedCompactPoseIndex).GetTranslation();
		OutWorld = TargetCompTransform.TransformPosition(BoneComp);
		return !OutWorld.ContainsNaN();
	};

	FVector ShoulderWorld = FVector::ZeroVector;
	FVector ElbowWorld = FVector::ZeroVector;
	FVector WristWorld = FVector::ZeroVector;
	const bool bHasMediaPipeArmWorld =
		IsMeasured(ShoulderLm) &&
		IsMeasured(ElbowLm) &&
		IsMeasured(WristLm) &&
		TryGetLmWorld(ShoulderLm, ShoulderWorld) &&
		TryGetLmWorld(ElbowLm, ElbowWorld) &&
		TryGetLmWorld(WristLm, WristWorld);
	const FVector MediaPipeSourceShoulderWorld = bHasMediaPipeArmWorld ? ShoulderWorld : FVector::ZeroVector;
	const FVector MediaPipeSourceElbowWorld = bHasMediaPipeArmWorld ? ElbowWorld : FVector::ZeroVector;
	const FVector MediaPipeSourceWristWorld = bHasMediaPipeArmWorld ? WristWorld : FVector::ZeroVector;

	// Overhead MediaPipe rescue: hands above the head leave the headset cameras' view - Quest
	// hand tracking drops and the body-tracking chain keeps SYNTHESIZING a guess that sags the
	// arms down - while the phone camera still measures the whole arm (observed live
	// 2026-07-02). When the Quest side is untracked and MediaPipe reliably sees the wrist above
	// the shoulder, the camera takes the arm; dwell-time hysteresis prevents source flapping
	// and the rotation smoothing blends the handover.
	// The dwell/latch lives in the keyed runtime state (QuestWristSideState), NOT in node
	// members: node state measurably does not survive pose-state resets (2026-07-03 root-cause
	// session), and a latch that forgets its dwell every reset can never fire. RuntimeStateKey=0
	// means this evaluation ran before the first PreUpdate - skip the rescue for that frame so
	// the shared key-0 bucket is never read or written.
	if (CVarMediaPipeArmOverheadRescue.GetValueOnAnyThread() != 0 &&
		RuntimeStateKey != 0 &&
		!bBodyFusionPoseWriteActive &&
		!bTrackingFusionReplayActive)
	{
		// Extracted verbatim to FMediaPipeArmLossOverride::Update
		// (Correctors/MediaPipeArmLossOverride; refactor/correctors Phase 6). Inputs are
		// the exact locals the inline block read; the wall clock is hoisted here.
		FMediaPipeArmLossOverrideInputs RescueIn;
		RescueIn.bIsLeft = bIsLeft;
		RescueIn.TargetActorName = TargetActorName;
		RescueIn.bHasMediaPipeArmWorld = bHasMediaPipeArmWorld;
		RescueIn.RescueReliability = bHasMediaPipeArmWorld
			? FMath::Min3(
				GetLandmarkReliability(ShoulderLm),
				GetLandmarkReliability(ElbowLm),
				GetLandmarkReliability(WristLm))
			: 0.0f;
		RescueIn.bQuestSideTrackedForArm = bQuestSideTrackedForArm;
		RescueIn.bQuestSideRecentlyTrackedForArm = bQuestSideRecentlyTrackedForArm;
		RescueIn.bMetaHumanFullArmChainFresh = bMetaHumanFullArmChainFresh;
		RescueIn.CamShoulderWorld = MediaPipeSourceShoulderWorld;
		RescueIn.CamWristWorld = MediaPipeSourceWristWorld;
		RescueIn.ChainResultShoulderWorld = FullArmChainResult.ShoulderWorld;
		RescueIn.ChainResultWristWorld = FullArmChainResult.WristWorld;
		RescueIn.NowSeconds = FPlatformTime::Seconds();
		RescueIn.FallbackDeltaSeconds = FMath::Max(DeltaSeconds, CachedDeltaTimeSeconds);
		RescueIn.NodeDiagSerial = NodeDiagSerial;
		RescueIn.RuntimeStateKey = RuntimeStateKey;
		RescueIn.DeltaSeconds = DeltaSeconds;
		RescueIn.PoseStateResetCount = PoseStateResetCount;
		FMediaPipeArmLossOverride::Update(RescueIn, QuestWristSideState);
	}
	else if (RuntimeStateKey != 0)
	{
		FMediaPipeArmLossOverride::Reset(QuestWristSideState);
	}
	const bool bMediaPipeArmRescue = QuestWristSideState.bArmRescueActive && bHasMediaPipeArmWorld;
	if (bMediaPipeArmRescue)
	{
		// Demote every Quest-driven arm source: the plain MediaPipe landmark path below owns
		// this arm until the Quest hand is tracked again.
		bMetaHumanFullArmChainDirectFresh = false;
		bQuestArmUsesWristEndpoint = false;
		bQuestArmUsesConstrainedSolve = false;
		bQuestArmUsesLegacyReachAssist = false;
		bUseHmdRelativeAvatarArmFrame = false;
	}

	if (bMetaHumanFullArmChainRequested && !bMetaHumanFullArmChainFresh)
	{
		EmitMetaHumanFullArmChainLog(false, false, 0.0f, 0.0f, FVector::ZeroVector);
	}

	if (bUseBodyFusionArm)
	{
		ShoulderWorld = FusedArmSide.ShoulderWorld;
		ElbowWorld = FusedArmSide.ElbowWorld;
		WristWorld = FusedArmSide.WristWorld;
	}
	else if (bMetaHumanFullArmChainDirectFresh)
	{
		ShoulderWorld = FullArmChainResult.ShoulderWorld;
		ElbowWorld = FullArmChainResult.ElbowWorld;
		WristWorld = FullArmChainResult.WristWorld;

		// Camera arm-direction transplant (take-3 referee 2026-07-05, supersedes the narrower
		// elbow swivel): the chain synthesizes the whole HANGING arm too wide - elbow/wrist
		// 22-24cm horizontal of the shoulder vs the camera's 7-10cm; the fused avatar hung its
		// upper arms at 34-45deg from vertical vs the referee's 15-21deg ("arms panned out",
		// the user's top repeat report). The camera owns arm DIRECTION from the shoulder (both
		// segments); the chain keeps ownership of reach (segment lengths) and the shoulder
		// anchor. Directions are shoulder-relative within each source's own frame, so the
		// camera frame's translation bias cancels by construction.
		const float ArmDirectionWeight = FMath::Clamp(
			CVarMediaPipeArmDirectionFromCamera.GetValueOnAnyThread(), 0.0f, 1.0f);
		if (ArmDirectionWeight > 0.0f && RuntimeStateKey != 0)
		{
			// Extracted verbatim to ApplyMediaPipeArmDirectionCorrection
			// (Correctors/MediaPipeArmDirectionCorrector; refactor/correctors Phase 2).
			// The inputs are the exact locals the inline block read; the wall clock and
			// the other-shoulder landmark lookup are hoisted here (pure reads).
			FMediaPipeArmDirectionCorrectorInputs DirIn;
			DirIn.bIsLeft = bIsLeft;
			DirIn.TargetActorName = TargetActorName;
			DirIn.Weight = ArmDirectionWeight;
			DirIn.bHasMediaPipeArmWorld = bHasMediaPipeArmWorld;
			DirIn.Reliability = bHasMediaPipeArmWorld
				? FMath::Min3(
					GetLandmarkReliability(ShoulderLm),
					GetLandmarkReliability(ElbowLm),
					GetLandmarkReliability(WristLm))
				: 0.0f;
			DirIn.NowSeconds = FPlatformTime::Seconds();
			DirIn.FallbackDeltaSeconds = FMath::Max(DeltaSeconds, CachedDeltaTimeSeconds);
			DirIn.ChainShoulderWorld = ShoulderWorld;
			DirIn.LearnChainWristWorld = FullArmChainResult.WristWorld;
			DirIn.CamShoulderWorld = MediaPipeSourceShoulderWorld;
			DirIn.CamElbowWorld = MediaPipeSourceElbowWorld;
			DirIn.CamWristWorld = MediaPipeSourceWristWorld;
			const int32 DirOtherShoulderLmIdx = bIsLeft
				? (int32)EMediaPipePoseLandmark::RightShoulder
				: (int32)EMediaPipePoseLandmark::LeftShoulder;
			DirIn.bHasOtherShoulderWorld =
				IsMeasured(DirOtherShoulderLmIdx) &&
				TryGetLmWorld(DirOtherShoulderLmIdx, DirIn.OtherShoulderWorld);
			// Timestamp-aligned residuals (TRACKING_QUALITY_PLAN Phase 1, 2026-07-11): push
			// the RAW chain arm into the keyed history ring, then look up the chain at this
			// measurement's effective capture time (capture timestamp + conditioner forward
			// prediction). Pushes and lookups run ONLY while the CVar is armed, so the
			// disarmed path writes nothing and the corrector sees inputs identical to the
			// golden-locked original. Degenerate aligned offsets (<1cm) stay unaligned.
			if (CVarMediaPipeTimestampAlignedResiduals.GetValueOnAnyThread() != 0)
			{
				MediaPipePoseHistory::FArmChainHistorySample ChainNowSample;
				ChainNowSample.ShoulderWorld = ShoulderWorld;
				ChainNowSample.ElbowWorld = ElbowWorld;
				ChainNowSample.WristWorld = WristWorld;
				QuestWristSideState.ArmChainHistory.Push(DirIn.NowSeconds, ChainNowSample);
				double EffectiveMeasTimeSeconds = 0.0;
				if (DirIn.bHasMediaPipeArmWorld &&
					bHasPoseFrame &&
					MediaPipePoseHistory::TryGetEffectiveMeasurementTimeSeconds(
						static_cast<double>(PoseFrame.TimestampUs) * 1.0e-6,
						PoseFrame.ConditioningDiagnostics.PredictionHorizonMs,
						EffectiveMeasTimeSeconds))
				{
					MediaPipePoseHistory::FArmChainHistorySample AlignedSample;
					if (QuestWristSideState.ArmChainHistory.TrySample(EffectiveMeasTimeSeconds, AlignedSample))
					{
						const FVector AlignedElbowOffset = AlignedSample.ElbowWorld - AlignedSample.ShoulderWorld;
						const FVector AlignedWristOffset = AlignedSample.WristWorld - AlignedSample.ShoulderWorld;
						if (!AlignedElbowOffset.IsNearlyZero(1.0) && !AlignedWristOffset.IsNearlyZero(1.0))
						{
							DirIn.bHasAlignedChainArm = true;
							DirIn.AlignedChainShoulderWorld = AlignedSample.ShoulderWorld;
							DirIn.AlignedChainElbowWorld = AlignedSample.ElbowWorld;
							DirIn.AlignedChainWristWorld = AlignedSample.WristWorld;
						}
					}
				}
			}
			// mp.WebcamAgeTrace (TRACKING_QUALITY_PLAN Phase 0, 2026-07-11): report-only row
			// emitted BEFORE the corrector so the residuals are the raw camera-vs-chain
			// disagreement against the CURRENT pose (pre-correction), on EVERY reliable frame
			// - the corrector's own learning is quiet-gated, so its 1Hz drift row never shows
			// the motion-window residual that Phase 1's timestamp alignment must shrink. The
			// speed reads the corrector's keyed baseline before the corrector updates it, so
			// motion windows classify identically to the quiet gate. Only writes: the keyed
			// log throttle (RuntimeStateKey != 0 is guaranteed by the enclosing gate).
			if (CVarWebcamAgeTrace.GetValueOnAnyThread() != 0 &&
				FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(
					DirIn.NowSeconds, 0.25, QuestWristSideState.WebcamAgeTraceLastLogTimeSeconds))
			{
				const double CaptureSeconds = bHasPoseFrame
					? static_cast<double>(PoseFrame.TimestampUs) * 1.0e-6
					: -1.0;
				const float PredMs = bHasPoseFrame ? PoseFrame.ConditioningDiagnostics.PredictionHorizonMs : -1.0f;
				const float SrcAgeMs = bHasPoseFrame ? PoseFrame.ConditioningDiagnostics.SourceAgeMs : -1.0f;
				const float AgeMs = CaptureSeconds > 0.0
					? static_cast<float>((DirIn.NowSeconds - CaptureSeconds) * 1000.0)
					: -1.0f;
				const float EffAgeMs = MediaPipeTrackingQualityMetrics::ComputeEffectiveWebcamAgeMs(
					CaptureSeconds, DirIn.NowSeconds, PredMs);
				float ElbowResidDeg = -1.0f;
				float WristResidDeg = -1.0f;
				if (DirIn.bHasMediaPipeArmWorld)
				{
					const FVector ChainElbowDir = (ElbowWorld - DirIn.ChainShoulderWorld).GetSafeNormal();
					const FVector ChainWristDir = (WristWorld - DirIn.ChainShoulderWorld).GetSafeNormal();
					const FVector CamElbowDir = (DirIn.CamElbowWorld - DirIn.CamShoulderWorld).GetSafeNormal();
					const FVector CamWristDir = (DirIn.CamWristWorld - DirIn.CamShoulderWorld).GetSafeNormal();
					if (!ChainElbowDir.IsNearlyZero() && !CamElbowDir.IsNearlyZero())
					{
						ElbowResidDeg = FMath::RadiansToDegrees(FMath::Acos(
							FMath::Clamp(FVector::DotProduct(ChainElbowDir, CamElbowDir), -1.0f, 1.0f)));
					}
					if (!ChainWristDir.IsNearlyZero() && !CamWristDir.IsNearlyZero())
					{
						WristResidDeg = FMath::RadiansToDegrees(FMath::Acos(
							FMath::Clamp(FVector::DotProduct(ChainWristDir, CamWristDir), -1.0f, 1.0f)));
					}
				}
				float AgeTraceSpdCmS = -1.0f;
				if (QuestWristSideState.ArmDirLearnLastTimeSeconds >= 0.0)
				{
					const float SpeedDtSeconds = static_cast<float>(
						DirIn.NowSeconds - QuestWristSideState.ArmDirLearnLastTimeSeconds);
					if (SpeedDtSeconds > KINDA_SMALL_NUMBER && SpeedDtSeconds <= 0.1f)
					{
						AgeTraceSpdCmS = FVector::Dist(
							DirIn.LearnChainWristWorld,
							QuestWristSideState.ArmDirLearnLastChainWristWorld) / SpeedDtSeconds;
					}
				}
				// Aligned residuals (Phase 1): same camera dirs against the history-ring chain
				// pose - the raw-vs-aligned pair in ONE row is the Phase 1 gate instrument.
				float AlignedElbowResidDeg = -1.0f;
				float AlignedWristResidDeg = -1.0f;
				if (DirIn.bHasAlignedChainArm && DirIn.bHasMediaPipeArmWorld)
				{
					const FVector AlChainElbowDir =
						(DirIn.AlignedChainElbowWorld - DirIn.AlignedChainShoulderWorld).GetSafeNormal();
					const FVector AlChainWristDir =
						(DirIn.AlignedChainWristWorld - DirIn.AlignedChainShoulderWorld).GetSafeNormal();
					const FVector AlCamElbowDir = (DirIn.CamElbowWorld - DirIn.CamShoulderWorld).GetSafeNormal();
					const FVector AlCamWristDir = (DirIn.CamWristWorld - DirIn.CamShoulderWorld).GetSafeNormal();
					if (!AlChainElbowDir.IsNearlyZero() && !AlCamElbowDir.IsNearlyZero())
					{
						AlignedElbowResidDeg = FMath::RadiansToDegrees(FMath::Acos(
							FMath::Clamp(FVector::DotProduct(AlChainElbowDir, AlCamElbowDir), -1.0f, 1.0f)));
					}
					if (!AlChainWristDir.IsNearlyZero() && !AlCamWristDir.IsNearlyZero())
					{
						AlignedWristResidDeg = FMath::RadiansToDegrees(FMath::Acos(
							FMath::Clamp(FVector::DotProduct(AlChainWristDir, AlCamWristDir), -1.0f, 1.0f)));
					}
				}
				UE_LOG(LogMediaPipePose, Log,
					TEXT("mp.WebcamAgeTrace: actor=%s side=%s ageMs=%.1f srcAgeMs=%.1f predMs=%.1f effAgeMs=%.1f predicted=%d rel=%.2f hasMpArm=%d elbowResidDeg=%.1f wristResidDeg=%.1f spdCmS=%.1f quiet=%d engaged=%d aligned=%d alElbowResidDeg=%.1f alWristResidDeg=%.1f histN=%d key=%u"),
					*TargetActorName.ToString(),
					bIsLeft ? TEXT("L") : TEXT("R"),
					AgeMs,
					SrcAgeMs,
					PredMs,
					EffAgeMs,
					(bHasPoseFrame && PoseFrame.ConditioningDiagnostics.bPredicted != 0) ? 1 : 0,
					DirIn.Reliability,
					DirIn.bHasMediaPipeArmWorld ? 1 : 0,
					ElbowResidDeg,
					WristResidDeg,
					AgeTraceSpdCmS,
					(AgeTraceSpdCmS >= 0.0f && AgeTraceSpdCmS <= 15.0f) ? 1 : 0,
					QuestWristSideState.bArmDirCameraVoteEngaged ? 1 : 0,
					DirIn.bHasAlignedChainArm ? 1 : 0,
					AlignedElbowResidDeg,
					AlignedWristResidDeg,
					QuestWristSideState.ArmChainHistory.Count,
					RuntimeStateKey);
			}
			ApplyMediaPipeArmDirectionCorrection(DirIn, QuestWristSideState, ElbowWorld, WristWorld);
		}
		const FVector ArmTraceWristAfterDirWorld = WristWorld;

		// QUEST-REACH EXTENSION (2026-07-10, restores what the old quest-wrist solve's
		// reach-scale calibration provided before the chain path gated it off): the chain
		// retargeter rebuilds the arm from the body-tracking chain's segment DIRECTIONS at the
		// avatar's fixed segment lengths, and the synthesized elbow never fully straightens -
		// rendered reach saturates ~41-46cm (max ~52) while the avatar's straight arm reaches
		// ~52cm, so full extensions read as bent arms ("arms are not extending fully like they
		// used to"). The REAL Quest hand-tracking wrist knows the true extension: its distance
		// from the chain's own shoulder over the chain's own segment-length sum is the user's
		// actual straightness fraction, dimensionless and frame-bias-free (both measured in the
		// same tracking space). Map that fraction onto the avatar's full reach and extend the
		// chain wrist radially (stretch-only), re-solving the elbow with the two-bone cosine
		// rule in the existing swivel plane so segment lengths stay exact.
		const float ChainReachWeight = FMath::Clamp(
			CVarMediaPipeChainReachFromQuestHand.GetValueOnAnyThread(), 0.0f, 1.0f);
		if (ChainReachWeight > 0.0f && RuntimeStateKey != 0)
		{
			// Extracted verbatim to ApplyMediaPipeReachExtension
			// (Correctors/MediaPipeReachExtender; refactor/correctors Phase 5). Inputs are
			// the exact locals the inline block read; the wall clock, chain-source joints,
			// and real hand wrist are hoisted here (pure reads).
			FMediaPipeReachExtenderInputs ReachIn;
			ReachIn.bIsLeft = bIsLeft;
			ReachIn.TargetActorName = TargetActorName;
			ReachIn.Weight = ChainReachWeight;
			ReachIn.bQuestSideTrackedForArm = bQuestSideTrackedForArm;
			ReachIn.NowSeconds = FPlatformTime::Seconds();
			ReachIn.ChainShoulderWorld = ShoulderWorld;
			ReachIn.ChainResultShoulderWorld = FullArmChainResult.ShoulderWorld;
			ReachIn.ChainResultElbowWorld = FullArmChainResult.ElbowWorld;
			ReachIn.ChainResultWristWorld = FullArmChainResult.WristWorld;
			ReachIn.ChainSrcShoulderWorld = FullArmChainSide.UpperArm.WorldTransform.GetLocation();
			ReachIn.ChainSrcElbowWorld = FullArmChainSide.LowerArm.WorldTransform.GetLocation();
			ReachIn.ChainSrcWristWorld = FullArmChainSide.WristOrPalm.WorldTransform.GetLocation();
			ReachIn.RealHandWristWorld =
				GetQuestHandPositions(QuestHands, bIsLeft)[static_cast<int32>(EHandKeypoint::Wrist)];
			ApplyMediaPipeReachExtension(ReachIn, QuestWristSideState, ElbowWorld, WristWorld);
		}
		else if (RuntimeStateKey != 0)
		{
			ResetMediaPipeReachExtension(QuestWristSideState);
		}
		const FVector ArmTraceWristAfterExtWorld = WristWorld;

		// Torso-capsule guard (take-3 forensics 2026-07-05: the fused right wrist passed
		// within 2.0cm of the spine axis at 66s - inside the body - while the offline referee
		// never dipped below 14.8cm; Epic's solver enforces non-penetration, this stack had no
		// such constraint). Clamp elbow/wrist targets to a minimum horizontal distance from
		// the torso axis, approximated as the vertical line half a shoulder-width inboard of
		// this side's chain shoulder along the camera's shoulder line.
		const float TorsoGuardCm = CVarMediaPipeArmTorsoGuardCm.GetValueOnAnyThread();
		if (TorsoGuardCm > 0.0f && bHasMediaPipeArmWorld)
		{
			const int32 GuardOtherShoulderLm = bIsLeft
				? (int32)EMediaPipePoseLandmark::RightShoulder
				: (int32)EMediaPipePoseLandmark::LeftShoulder;
			FVector GuardOtherShoulderWorld = FVector::ZeroVector;
			if (IsMeasured(GuardOtherShoulderLm) &&
				TryGetLmWorld(GuardOtherShoulderLm, GuardOtherShoulderWorld))
			{
				FVector TowardCenter = GuardOtherShoulderWorld - MediaPipeSourceShoulderWorld;
				TowardCenter.Z = 0.0f;
				TowardCenter = TowardCenter.GetSafeNormal();
				if (!TowardCenter.IsNearlyZero())
				{
					const FVector TorsoAxisPoint = ShoulderWorld + TowardCenter * 18.0f;
					auto GuardPoint = [&](FVector& Point)
					{
						FVector Planar = Point - TorsoAxisPoint;
						const float PointZ = Planar.Z;
						Planar.Z = 0.0f;
						const float Dist = Planar.Size();
						if (Dist < TorsoGuardCm && Dist > KINDA_SMALL_NUMBER)
						{
							Point = TorsoAxisPoint + Planar * (TorsoGuardCm / Dist);
							Point.Z = TorsoAxisPoint.Z + PointZ;
						}
					};
					GuardPoint(ElbowWorld);
					GuardPoint(WristWorld);
				}
			}
		}

		// JUMP-ATTRIBUTION TRACER (2026-07-10, worn verdict "arms still jump sometimes"): the
		// 1Hz stage rows alias sub-second events and cannot say WHICH stage moved the arm. Per
		// frame, compare the final wrist target's motion against the RAW chain source's motion -
		// real body movement cancels out, so any residual above the threshold is solver-injected.
		// On trigger, one row names each stage's contribution (direction correction, reach
		// extension, torso guard) and how much it CHANGED since the previous frame - the culprit
		// is the largest change. Event-driven and rate-limited; costs nothing between events.
		if (RuntimeStateKey != 0 && CVarMediaPipeCameraHandTrace.GetValueOnAnyThread() != 0)
		{
			const double ArmJumpNowSeconds = FPlatformTime::Seconds();
			const FVector ChainRawWristWorld = FullArmChainResult.WristWorld;
			if (QuestWristSideState.bHasArmJumpBaseline &&
				QuestWristSideState.ArmJumpLastTimeSeconds >= 0.0)
			{
				const float ArmJumpDtSeconds = static_cast<float>(
					ArmJumpNowSeconds - QuestWristSideState.ArmJumpLastTimeSeconds);
				if (ArmJumpDtSeconds > 0.0f && ArmJumpDtSeconds <= 0.1f)
				{
					const FVector FinalDelta = WristWorld - QuestWristSideState.ArmJumpLastFinalWristWorld;
					const FVector SourceDelta = ChainRawWristWorld - QuestWristSideState.ArmJumpLastChainWristWorld;
					const float ResidualCm = (FinalDelta - SourceDelta).Size();
					if (ResidualCm > 1.2f &&
						FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(
							ArmJumpNowSeconds, 0.25, QuestWristSideState.ArmJumpLastLogTimeSeconds))
					{
						const float DirOffCm = (ArmTraceWristAfterDirWorld - ChainRawWristWorld).Size();
						const float PrevDirOffCm = (QuestWristSideState.ArmJumpLastDirWristWorld - QuestWristSideState.ArmJumpLastChainWristWorld).Size();
						const float ExtOffCm = (ArmTraceWristAfterExtWorld - ArmTraceWristAfterDirWorld).Size();
						const float PrevExtOffCm = (QuestWristSideState.ArmJumpLastExtWristWorld - QuestWristSideState.ArmJumpLastDirWristWorld).Size();
						const float GuardOffCm = (WristWorld - ArmTraceWristAfterExtWorld).Size();
						const float PrevGuardOffCm = (QuestWristSideState.ArmJumpLastFinalWristWorld - QuestWristSideState.ArmJumpLastExtWristWorld).Size();
						UE_LOG(LogMediaPipePose, Log,
							TEXT("mp.ArmJumpTrace: actor=%s side=%s residCm=%.1f dFinalCm=%.1f dSourceCm=%.1f dt=%.3f dirOffCm=%.1f dDirCm=%+.1f extOffCm=%.1f dExtCm=%+.1f guardOffCm=%.1f dGuardCm=%+.1f dirAlpha=%.2f appliedExtCm=%.1f questTracked=%d"),
							*TargetActorName.ToString(),
							bIsLeft ? TEXT("L") : TEXT("R"),
							ResidualCm,
							FinalDelta.Size(),
							SourceDelta.Size(),
							ArmJumpDtSeconds,
							DirOffCm,
							DirOffCm - PrevDirOffCm,
							ExtOffCm,
							ExtOffCm - PrevExtOffCm,
							GuardOffCm,
							GuardOffCm - PrevGuardOffCm,
							QuestWristSideState.ArmDirectionBlendAlpha,
							QuestWristSideState.ChainReachExtensionCm,
							bQuestSideTrackedForArm ? 1 : 0);
					}
				}
			}
			QuestWristSideState.ArmJumpLastChainWristWorld = ChainRawWristWorld;
			QuestWristSideState.ArmJumpLastDirWristWorld = ArmTraceWristAfterDirWorld;
			QuestWristSideState.ArmJumpLastExtWristWorld = ArmTraceWristAfterExtWorld;
			QuestWristSideState.ArmJumpLastFinalWristWorld = WristWorld;
			QuestWristSideState.ArmJumpLastTimeSeconds = ArmJumpNowSeconds;
			QuestWristSideState.bHasArmJumpBaseline = true;
		}

		// Camera elbow swivel: narrower correction retained for configurations that keep the
		// direction transplant off (the transplant subsumes the swivel; the trial layer now
		// enables the transplant instead - running both would double-correct the azimuth).
		const float ElbowSwivelWeight = ArmDirectionWeight > 0.0f
			? 0.0f
			: FMath::Clamp(CVarMediaPipeArmElbowSwivelFromCamera.GetValueOnAnyThread(), 0.0f, 1.0f);
		if (ElbowSwivelWeight > 0.0f && bHasMediaPipeArmWorld && RuntimeStateKey != 0)
		{
			const float SwivelReliability = FMath::Min3(
				GetLandmarkReliability(ShoulderLm),
				GetLandmarkReliability(ElbowLm),
				GetLandmarkReliability(WristLm));
			auto ElbowAzimuthDeg = [](const FVector& S, const FVector& E, const FVector& W,
				bool& bOutValid) -> float
			{
				const FVector Chord = (W - S).GetSafeNormal();
				if (Chord.IsNearlyZero())
				{
					bOutValid = false;
					return 0.0f;
				}
				// Shared reference: world up projected off the chord; fall back to forward
				// when the arm points nearly straight up/down.
				FVector Ref = FVector::UpVector - Chord * FVector::DotProduct(FVector::UpVector, Chord);
				if (Ref.SizeSquared() < 0.01f)
				{
					Ref = FVector::ForwardVector - Chord * FVector::DotProduct(FVector::ForwardVector, Chord);
				}
				Ref = Ref.GetSafeNormal();
				const FVector RefOrtho = FVector::CrossProduct(Chord, Ref);
				const FVector ElbowOffChord = (E - S) - Chord * FVector::DotProduct(E - S, Chord);
				if (ElbowOffChord.SizeSquared() < 1.0f)
				{
					// Elbow within 1cm of the chord: azimuth undefined (arm straight).
					bOutValid = false;
					return 0.0f;
				}
				bOutValid = true;
				return FMath::RadiansToDegrees(FMath::Atan2(
					FVector::DotProduct(ElbowOffChord, RefOrtho),
					FVector::DotProduct(ElbowOffChord, Ref)));
			};
			bool bChainAzimuthValid = false;
			bool bCameraAzimuthValid = false;
			const float ChainAzimuthDeg = ElbowAzimuthDeg(
				ShoulderWorld, ElbowWorld, WristWorld, bChainAzimuthValid);
			const float CameraAzimuthDeg = ElbowAzimuthDeg(
				MediaPipeSourceShoulderWorld, MediaPipeSourceElbowWorld, MediaPipeSourceWristWorld,
				bCameraAzimuthValid);
			const bool bSwivelMeasurable =
				bChainAzimuthValid && bCameraAzimuthValid && SwivelReliability >= 0.5f;
			const double SwivelNowSeconds = FPlatformTime::Seconds();
			float SwivelStepSeconds = QuestWristSideState.ElbowSwivelLastUpdateTimeSeconds >= 0.0
				? static_cast<float>(SwivelNowSeconds - QuestWristSideState.ElbowSwivelLastUpdateTimeSeconds)
				: FMath::Max(DeltaSeconds, CachedDeltaTimeSeconds);
			SwivelStepSeconds = FMath::Clamp(SwivelStepSeconds, 0.0f, 0.1f);
			QuestWristSideState.ElbowSwivelLastUpdateTimeSeconds = SwivelNowSeconds;
			// Smooth toward the measured delta while measurable; decay toward zero when the
			// camera cannot vote so the correction releases instead of freezing.
			const float SwivelTargetDeg = bSwivelMeasurable
				? FMath::FindDeltaAngleDegrees(ChainAzimuthDeg, CameraAzimuthDeg)
				: 0.0f;
			if (!QuestWristSideState.bHasSmoothedElbowSwivel)
			{
				QuestWristSideState.SmoothedElbowSwivelDeltaDeg = 0.0f;
				QuestWristSideState.bHasSmoothedElbowSwivel = true;
			}
			const float SwivelSmoothAlpha = 1.0f - FMath::Exp(-SwivelStepSeconds / 0.15f);
			QuestWristSideState.SmoothedElbowSwivelDeltaDeg = FMath::Lerp(
				QuestWristSideState.SmoothedElbowSwivelDeltaDeg, SwivelTargetDeg, SwivelSmoothAlpha);
			const float AppliedSwivelDeg =
				QuestWristSideState.SmoothedElbowSwivelDeltaDeg * ElbowSwivelWeight;
			if (!FMath::IsNearlyZero(AppliedSwivelDeg, 0.1f))
			{
				const FVector Chord = (WristWorld - ShoulderWorld).GetSafeNormal();
				if (!Chord.IsNearlyZero())
				{
					ElbowWorld = ShoulderWorld +
						(ElbowWorld - ShoulderWorld).RotateAngleAxis(AppliedSwivelDeg, Chord);
				}
			}
		}
	}
	else if (bBodyFusionPoseWriteActive)
	{
		return;
	}
	else if (bUseHmdRelativeAvatarArmFrame)
	{
		FVector AvatarShoulderWorld = FVector::ZeroVector;
		FVector AvatarElbowWorld = FVector::ZeroVector;
		FVector AvatarWristWorld = FVector::ZeroVector;
		if (!TryGetBoneWorld(UpperBone, AvatarShoulderWorld) ||
			!TryGetBoneWorld(LowerBone, AvatarElbowWorld) ||
			!TryGetBoneWorld(HandBone, AvatarWristWorld))
		{
			return;
		}

		ShoulderWorld = AvatarShoulderWorld;
		ElbowWorld = AvatarElbowWorld;
		WristWorld = AvatarWristWorld;
	}
	else if (!bHasMediaPipeArmWorld)
	{
		return;
	}

	const FVector& RefUpperDir = bIsLeft ? RefUpperDirCompL : RefUpperDirCompR;
	const FVector& RefLowerDir = bIsLeft ? RefLowerDirCompL : RefLowerDirCompR;
	const FVector& RefPoleDir = bIsLeft ? RefPoleDirCompL : RefPoleDirCompR;
	const FQuat& RefUpperComp = bIsLeft ? RefUpperArmCompL : RefUpperArmCompR;
	const FQuat& RefLowerComp = bIsLeft ? RefLowerArmCompL : RefLowerArmCompR;
	const FQuat& RefUpperBasisComp = bIsLeft ? RefUpperArmBasisCompL : RefUpperArmBasisCompR;
	const FQuat& RefLowerBasisComp = bIsLeft ? RefLowerArmBasisCompL : RefLowerArmBasisCompR;
	const FQuat& RefUpperSurfaceBasisComp = bIsLeft ? RefUpperArmSurfaceBasisCompL : RefUpperArmSurfaceBasisCompR;
	const FQuat& RefLowerSurfaceBasisComp = bIsLeft ? RefLowerArmSurfaceBasisCompL : RefLowerArmSurfaceBasisCompR;
	const FQuat& RefHandComp = bIsLeft ? RefHandCompL : RefHandCompR;
	const FQuat& RefHandBasisComp = bIsLeft ? RefHandBasisCompL : RefHandBasisCompR;
	bool& bHasSmoothedUpperRotCS = bIsLeft ? LeftArmState.bHasSmoothedUpperArmRotCS : RightArmState.bHasSmoothedUpperArmRotCS;
	FQuat& SmoothedUpperRotCS = bIsLeft ? LeftArmState.SmoothedUpperArmRotCS : RightArmState.SmoothedUpperArmRotCS;
	bool& bHasSmoothedLowerRotCS = bIsLeft ? LeftArmState.bHasSmoothedLowerArmRotCS : RightArmState.bHasSmoothedLowerArmRotCS;
	FQuat& SmoothedLowerRotCS = bIsLeft ? LeftArmState.SmoothedLowerArmRotCS : RightArmState.SmoothedLowerArmRotCS;

	bool& bHasLastReliableArmSample = bIsLeft ? LeftArmState.bHasLastReliableArmSample : RightArmState.bHasLastReliableArmSample;
	FVector& LastReliableShoulderWorld = bIsLeft ? LeftArmState.LastReliableShoulderWorld : RightArmState.LastReliableShoulderWorld;
	FVector& LastReliableElbowWorld = bIsLeft ? LeftArmState.LastReliableElbowWorld : RightArmState.LastReliableElbowWorld;
	FVector& LastReliableWristWorld = bIsLeft ? LeftArmState.LastReliableWristWorld : RightArmState.LastReliableWristWorld;

	FMediaPipeQuestWristHeldTargetLossInput HeldTargetLossInput;
	const float HeldTargetGraceSeconds = FMath::Max(0.0f, CVarQuestWristLostTrackingGraceSeconds.GetValueOnAnyThread());
	const double HeldTargetNowSeconds = FPlatformTime::Seconds();
	HeldTargetLossInput.bHasHeldTarget = QuestWristSideState.bHasHeldTarget;
	HeldTargetLossInput.GraceSeconds = HeldTargetGraceSeconds;
	HeldTargetLossInput.LastTargetAgeSeconds = QuestWristSideState.LastTargetTimeSeconds >= 0.0
		? static_cast<float>(HeldTargetNowSeconds - QuestWristSideState.LastTargetTimeSeconds)
		: -1.0f;
	const bool bHasRecentQuestWristHeldTarget =
		FMediaPipeQuestWristApplyPolicy::HasFreshHeldTargetForPositionAttempt(HeldTargetLossInput);

	FMediaPipeQuestWristPositionAttemptInput WristAttemptInput;
	WristAttemptInput.bQuestArmUsesWristEndpoint = bQuestArmUsesWristEndpoint;
	WristAttemptInput.bQuestArmUsesConstrainedSolve = bQuestArmUsesConstrainedSolve;
	WristAttemptInput.bQuestSideUsable = bQuestSideUsableForArm;
	WristAttemptInput.bHasHeldTarget = bHasRecentQuestWristHeldTarget;
	WristAttemptInput.RequestedPositionBlend = CVarQuestWristPositionBlend.GetValueOnAnyThread();
	const bool bQuestWristPositionCandidate =
		FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve(WristAttemptInput);

	FMediaPipeQuestArmHoldOnLossInput ArmHoldInput;
	ArmHoldInput.bHoldOnQuestHandLossEnabled =
		!bUseBodyFusionArm &&
		!bMetaHumanFullArmChainDirectFresh &&
		!bMediaPipeArmRescue &&
		CVarMediaPipeArmHoldOnQuestHandLoss.GetValueOnAnyThread() != 0;
	ArmHoldInput.bQuestHandTrackingEnabled = bQuestHandTrackingEnabled;
	ArmHoldInput.bQuestSideTracked = bQuestSideTrackedForArm;
	ArmHoldInput.bHasLastReliableArmSample = bHasLastReliableArmSample;
	ArmHoldInput.bQuestWristPositionCandidate = bQuestWristPositionCandidate;
	const bool bHoldArmOnQuestHandLoss =
		FMediaPipeQuestWristApplyPolicy::ShouldHoldArmOnQuestHandLoss(ArmHoldInput);

	float ArmObservationAlphaScale = 1.0f;
	if (!bUseBodyFusionArm && !bMetaHumanFullArmChainDirectFresh && bHoldArmOnQuestHandLoss)
	{
		ShoulderWorld = LastReliableShoulderWorld;
		ElbowWorld = LastReliableElbowWorld;
		WristWorld = LastReliableWristWorld;
		ArmObservationAlphaScale = 0.0f;
	}

	// USER RULE (2026-07-02): while the overhead rescue owns this arm, the reliability gate must
	// not run - its hold branch lerps low-confidence samples back to the LAST RELIABLE target,
	// which overhead (reliability 0.2-0.4 at the top of frame) is the lowered arm. The gate
	// holding against the camera is exactly the forced-down failure the rescue exists to stop.
	if (!bUseBodyFusionArm &&
		!bMetaHumanFullArmChainDirectFresh &&
		!bUseHmdRelativeAvatarArmFrame &&
		!bMediaPipeArmRescue &&
		CVarMediaPipeArmReliabilityGate.GetValueOnAnyThread() != 0)
	{
		const float ArmMinReliability = FMath::Clamp(CVarMediaPipeArmMinReliability.GetValueOnAnyThread(), 0.0f, 1.0f);
		const float ShoulderReliability = GetLandmarkReliability(ShoulderLm);
		const float ElbowReliability = GetLandmarkReliability(ElbowLm);
		const float WristReliability = GetLandmarkReliability(WristLm);
		const bool bReliableEnough =
			!bHoldArmOnQuestHandLoss &&
			ShoulderReliability >= ArmMinReliability &&
			ElbowReliability >= ArmMinReliability &&
			(bQuestWristPositionCandidate || WristReliability >= ArmMinReliability);
		const float ArmReliability = bQuestWristPositionCandidate
			? FMath::Min(ShoulderReliability, ElbowReliability)
			: FMath::Min(ShoulderReliability, FMath::Min(ElbowReliability, WristReliability));
		if (ArmMinReliability > 0.0f)
		{
			const float ReliabilityWeight = RemapClamped(ArmReliability, ArmMinReliability, FMath::Min(1.0f, ArmMinReliability + 0.20f));
			ArmObservationAlphaScale = FMath::Lerp(0.35f, 1.0f, ReliabilityWeight);
		}

		bool bStepAcceptable = true;
		bool bSegmentLengthAcceptable = true;
		if (bHasLastReliableArmSample)
		{
			const float MaxElbowStepCm = FMath::Max(0.0f, CVarMediaPipeArmMaxElbowStepCm.GetValueOnAnyThread());
			const float MaxWristStepCm = FMath::Max(0.0f, CVarMediaPipeArmMaxWristStepCm.GetValueOnAnyThread());
			const float ElbowStepCm = FVector::Dist(ElbowWorld, LastReliableElbowWorld);
			const float WristStepCm = FVector::Dist(WristWorld, LastReliableWristWorld);
			bStepAcceptable =
				(MaxElbowStepCm <= 0.0f || ElbowStepCm <= MaxElbowStepCm) &&
				(bQuestWristPositionCandidate || MaxWristStepCm <= 0.0f || WristStepCm <= MaxWristStepCm);

			const float MaxSegmentDeltaFraction = FMath::Max(0.0f, CVarMediaPipeArmMaxSegmentLengthDeltaFraction.GetValueOnAnyThread());
			if (MaxSegmentDeltaFraction > 0.0f)
			{
				const float MaxSegmentDeltaCm = FMath::Max(0.0f, CVarMediaPipeArmMaxSegmentLengthDeltaCm.GetValueOnAnyThread());
				const float UpperLenCm = FVector::Dist(ShoulderWorld, ElbowWorld);
				const float LowerLenCm = FVector::Dist(ElbowWorld, WristWorld);
				const float LastUpperLenCm = FVector::Dist(LastReliableShoulderWorld, LastReliableElbowWorld);
				const float LastLowerLenCm = FVector::Dist(LastReliableElbowWorld, LastReliableWristWorld);
				const float UpperToleranceCm = FMath::Max(MaxSegmentDeltaCm, LastUpperLenCm * MaxSegmentDeltaFraction);
				const float LowerToleranceCm = FMath::Max(MaxSegmentDeltaCm, LastLowerLenCm * MaxSegmentDeltaFraction);
				bSegmentLengthAcceptable =
					LastUpperLenCm <= KINDA_SMALL_NUMBER || FMath::Abs(UpperLenCm - LastUpperLenCm) <= UpperToleranceCm;
				bSegmentLengthAcceptable = bSegmentLengthAcceptable &&
					(bQuestWristPositionCandidate ||
					 LastLowerLenCm <= KINDA_SMALL_NUMBER ||
					 FMath::Abs(LowerLenCm - LastLowerLenCm) <= LowerToleranceCm);
			}
		}

		if (!bStepAcceptable || !bSegmentLengthAcceptable)
		{
			ArmObservationAlphaScale = FMath::Min(ArmObservationAlphaScale, 0.35f);
		}

		if (bReliableEnough && bStepAcceptable && bSegmentLengthAcceptable)
		{
			LastReliableShoulderWorld = ShoulderWorld;
			LastReliableElbowWorld = ElbowWorld;
			LastReliableWristWorld = WristWorld;
			bHasLastReliableArmSample = true;
		}
		else if (bHasLastReliableArmSample)
		{
			const float RejectedSampleAlpha = FMath::Clamp(CVarMediaPipeArmRejectedSampleAlpha.GetValueOnAnyThread(), 0.0f, 1.0f);
			const float HoldAlpha = bReliableEnough ? FMath::Min(ArmObservationAlphaScale, RejectedSampleAlpha) : 0.0f;
			ShoulderWorld = FMath::Lerp(LastReliableShoulderWorld, ShoulderWorld, HoldAlpha);
			ElbowWorld = FMath::Lerp(LastReliableElbowWorld, ElbowWorld, HoldAlpha);
			WristWorld = FMath::Lerp(LastReliableWristWorld, WristWorld, HoldAlpha);
			ArmObservationAlphaScale = FMath::Min(ArmObservationAlphaScale, HoldAlpha);
		}
		else
		{
			return;
		}
	}

	FMediaPipeAvatarArmBasisInput AvatarArmBasisInput;
	AvatarArmBasisInput.TargetComponentTransform = TargetCompTransform;
	AvatarArmBasisInput.bUseTargetFaceForwardAxis = bUseTargetFaceForwardAxis;
	const FMediaPipeAvatarArmBasisResult AvatarArmBasis =
		MediaPipeBodySolverMath::BuildAvatarArmBasis(AvatarArmBasisInput);
	FVector HipRightWorld = AvatarArmBasis.RightWorld;
	FVector ShoulderRightWorld = AvatarArmBasis.RightWorld;
	FVector UpWorld = AvatarArmBasis.UpWorld;
	FVector ForwardWorld = AvatarArmBasis.ForwardWorld;
	const bool bHasTorsoBasis = TryGetTorsoBasisWorld(HipRightWorld, ShoulderRightWorld, UpWorld, ForwardWorld);
	const float QuestConstrainedArmMaxReachFraction = FMath::Clamp(
		CVarQuestConstrainedArmMaxReachFraction.GetValueOnAnyThread(),
		0.50f,
		0.999f);

	FQuestWristMappingTrace QuestWristTrace;
	// Pre-fill the owner-independent facts. When body fusion or the chain-direct path owns
	// this arm the wrist-position mapping never runs and a default trace prints
	// questTracked=0 runtimeKey=0 - which reads as a dead Quest hand stream (2026-07-06:
	// a full handoff chased that ghost while the stream was healthy the whole day).
	QuestWristTrace.RuntimeStateKey = RuntimeStateKey;
	QuestWristTrace.bQuestTracked = bQuestSideTrackedForArm ? 1 : 0;
	bool bHasRecentHmdRelativeReachContinuity = false;
	float PreviousHmdRelativeReachContinuityCm = 0.0f;
	auto StoreHmdRelativeReachContinuity = [&](const FVector& FinalArmWristWorld)
	{
		if (!bUseHmdRelativeAvatarArmFrame ||
			!bQuestArmUsesConstrainedSolve ||
			QuestWristTrace.bMapped == 0 ||
			QuestWristTrace.bConstrainedArmBodyFallbackApplied != 0)
		{
			return;
		}

		const float FinalReachCm = FVector::Dist(ShoulderWorld, FinalArmWristWorld);
		if (FinalReachCm <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		QuestWristSideState.bHasHmdRelativeReachContinuity = true;
		QuestWristSideState.HmdRelativeReachContinuityCm = FinalReachCm;
		QuestWristSideState.HmdRelativeReachContinuityTimeSeconds = FPlatformTime::Seconds();
	};
	bool bQuestWristPositionApplied =
		bQuestArmUsesWristEndpoint &&
		TryApplyQuestWristPositionWorld(CSPose, bIsLeft, WristWorld, WristWorld, &QuestWristTrace);
	if (!bQuestWristPositionApplied &&
		bUseHmdRelativeAvatarArmFrame &&
		bQuestArmUsesConstrainedSolve &&
		CVarQuestConstrainedArmBodyFallback.GetValueOnAnyThread() != 0 &&
		bHasMediaPipeArmWorld &&
		RefUpperLen > KINDA_SMALL_NUMBER &&
		RefLowerLen > KINDA_SMALL_NUMBER)
	{
		FMediaPipeConstrainedArmFallbackInput FallbackInput;
		FallbackInput.SourceShoulderWorld = MediaPipeSourceShoulderWorld;
		FallbackInput.SourceElbowWorld = MediaPipeSourceElbowWorld;
		FallbackInput.SourceWristWorld = MediaPipeSourceWristWorld;
		FallbackInput.TargetShoulderWorld = ShoulderWorld;
		FallbackInput.UpWorld = UpWorld;
		FallbackInput.ShoulderRightWorld = ShoulderRightWorld;
		FallbackInput.bHasTorsoBasis = bHasTorsoBasis;
		FallbackInput.bIsLeft = bIsLeft;
		FallbackInput.TargetUpperLenCm = RefUpperLen;
		FallbackInput.TargetLowerLenCm = RefLowerLen;
		FallbackInput.MaxReachFraction = QuestConstrainedArmMaxReachFraction;
		FallbackInput.bEnableDownStraighten =
			CVarQuestConstrainedArmDownStraighten.GetValueOnAnyThread() != 0;
		FallbackInput.DownStraightenThresholdCm = CVarQuestConstrainedArmDownStraightenThresholdCm.GetValueOnAnyThread();
		FallbackInput.DownStraightenMaxCm = CVarQuestConstrainedArmDownStraightenMaxCm.GetValueOnAnyThread();
		FallbackInput.DownStraightenMinBelowShoulderRatio = CVarQuestConstrainedArmDownStraightenMinBelowShoulderRatio.GetValueOnAnyThread();
		FallbackInput.DownStraightenReachFloorFraction = CVarQuestConstrainedArmDownStraightenReachFloorFraction.GetValueOnAnyThread();
		FallbackInput.DownStraightenMaxReachFraction = CVarQuestConstrainedArmDownStraightenMaxReachFraction.GetValueOnAnyThread();

		FMediaPipeConstrainedArmFallbackResult FallbackResult;
		if (FMediaPipeQuestConstrainedArmSolver::BuildBodyFallbackEndpoint(FallbackInput, FallbackResult))
		{
			FVector FallbackElbowWorld = FallbackResult.TargetElbowWorld;
			FVector FallbackWristWorld = FallbackResult.TargetWristWorld;
			FMediaPipeArmSolverState& ActiveArmState = bIsLeft ? LeftArmState : RightArmState;
			const double NowSeconds = FPlatformTime::Seconds();
			const float LastSolveMaxAgeSeconds = FMath::Max(
				0.50f,
				CVarQuestWristLostTrackingGraceSeconds.GetValueOnAnyThread() + 0.50f);
			FMediaPipeConstrainedArmFallbackContinuityInput ContinuityInput;
			ContinuityInput.FallbackElbowWorld = FallbackResult.TargetElbowWorld;
			ContinuityInput.FallbackWristWorld = FallbackResult.TargetWristWorld;
			ContinuityInput.bConstrainElbowToCurrentSide = true;
			ContinuityInput.bIsLeft = bIsLeft;
			ContinuityInput.TargetShoulderWorld = FallbackInput.TargetShoulderWorld;
			ContinuityInput.ShoulderRightWorld = FallbackInput.ShoulderRightWorld;
			ContinuityInput.bHasLastConstrainedArmSolve = ActiveArmState.bHasLastConstrainedArmSolve;
			ContinuityInput.LastConstrainedArmElbowWorld = ActiveArmState.LastConstrainedArmElbowWorld;
			ContinuityInput.LastConstrainedArmWristWorld = ActiveArmState.LastConstrainedArmWristWorld;
			ContinuityInput.LastSolveAgeSeconds = ActiveArmState.LastConstrainedArmSolveTimeSeconds >= 0.0
				? static_cast<float>(NowSeconds - ActiveArmState.LastConstrainedArmSolveTimeSeconds)
				: -1.0f;
			ContinuityInput.MaxLastSolveAgeSeconds = LastSolveMaxAgeSeconds;
			ContinuityInput.DeltaSeconds = FMath::Max(DeltaSeconds, CachedDeltaTimeSeconds);
			ContinuityInput.WristHalfLifeSeconds = FMath::Max(
				0.0f,
				CVarQuestConstrainedArmBodyFallbackWristHalfLife.GetValueOnAnyThread());
			ContinuityInput.MaxWristStepCm = FMath::Max(
				0.0f,
				CVarQuestConstrainedArmBodyFallbackMaxWristStepCm.GetValueOnAnyThread());
			ContinuityInput.MaxElbowStepCm = ContinuityInput.MaxWristStepCm;

			FMediaPipeConstrainedArmFallbackContinuityResult ContinuityResult;
			if (FMediaPipeQuestConstrainedArmSolver::ApplyBodyFallbackContinuity(ContinuityInput, ContinuityResult))
			{
				FallbackElbowWorld = ContinuityResult.TargetElbowWorld;
				FallbackWristWorld = ContinuityResult.TargetWristWorld;
			}
			if (ContinuityResult.bUsedContinuity)
			{
				QuestWristTrace.bPositionFilterApplied = 1;
				QuestWristTrace.PositionFilterAlpha = ContinuityResult.BlendAlpha;
				QuestWristTrace.PositionFilterTargetDeltaCm = ContinuityResult.RawWristStepCm;
				QuestWristTrace.PositionFilterFilteredDeltaCm = ContinuityResult.FilteredWristStepCm;
				QuestWristTrace.PositionFilterSpeedCmSec = ContinuityResult.FilteredWristSpeedCmSec;
			}

			ElbowWorld = FallbackElbowWorld;
			WristWorld = FallbackWristWorld;
			QuestWristTrace.MediaPipeWristWorld = MediaPipeSourceWristWorld;
			QuestWristTrace.MappedQuestWristWorld = FallbackResult.TargetWristWorld;
			QuestWristTrace.FinalWristWorld = FallbackWristWorld;
			QuestWristTrace.MappedOffsetFromMediaPipeCm = FVector::Dist(FallbackResult.TargetWristWorld, MediaPipeSourceWristWorld);
			QuestWristTrace.CalibrationMode = EQuestMediaSpaceCalibrationMode::HmdRelativeAvatar;
			QuestWristTrace.bMapped = 1;
			QuestWristTrace.bPositionApplied = 1;
			if (FallbackResult.bReachClamped || FallbackResult.bDownStraightened)
			{
				QuestWristTrace.bReachClamped = 1;
			}
			QuestWristTrace.bUsedHeldQuestWrist = 1;
			QuestWristTrace.bConstrainedArmBodyFallbackApplied = 1;
			QuestWristTrace.bConstrainedArmBodyFallbackDownStraightened = FallbackResult.bDownStraightened ? 1 : 0;
			QuestWristTrace.ConstrainedArmBodyFallbackReachFraction = FallbackResult.SourceReachFraction;
			QuestWristTrace.ConstrainedArmBodyFallbackTargetReachCm = FallbackResult.TargetReachCm;
			QuestWristTrace.ConstrainedArmBodyFallbackTargetReachFraction = FallbackResult.TargetReachCm / FMath::Max(RefUpperLen + RefLowerLen, UE_SMALL_NUMBER);
			QuestWristTrace.ConstrainedArmBodyFallbackDownStraightenAlpha = FallbackResult.DownStraightenAdaptiveAlpha;
			QuestWristTrace.bQuestTracked = bQuestSideTrackedForArm ? 1 : 0;
			bQuestWristPositionApplied = true;
		}
	}
	const bool bWasDropoutDownFallbackActive = QuestWristSideState.bDropoutDownFallbackActive;
	if (bQuestSideTrackedForArm)
	{
		if (bWasDropoutDownFallbackActive)
		{
			const double ReacquireNowSeconds = FPlatformTime::Seconds();
			const float ReacquireSuppressSeconds = FMath::Max(
				0.10f,
				CVarQuestArmDropoutDownFallbackBlendHalfLife.GetValueOnAnyThread() * 3.0f);
			QuestWristSideState.DropoutReacquireReachScaleSuppressUntilTimeSeconds = FMath::Max(
				QuestWristSideState.DropoutReacquireReachScaleSuppressUntilTimeSeconds,
				ReacquireNowSeconds + ReacquireSuppressSeconds);
		}
		QuestWristSideState.bDropoutDownFallbackActive = false;
		QuestWristSideState.DropoutDownFallbackLastUpdateTimeSeconds = -1.0;
	}
	const bool bQuestArmCanUseDropoutDownFallback =
		CVarQuestArmDropoutDownFallback.GetValueOnAnyThread() != 0 &&
		bUseHmdRelativeAvatarArmFrame &&
		bQuestArmUsesConstrainedSolve &&
		bQuestHandTrackingEnabled &&
		!bQuestSideTrackedForArm &&
		RefUpperLen > KINDA_SMALL_NUMBER &&
		RefLowerLen > KINDA_SMALL_NUMBER;
	const bool bQuestArmHasOnlyDropoutEndpoint =
		!bQuestWristPositionApplied ||
		QuestWristTrace.bUsedUntrackedJointData != 0 ||
		QuestWristTrace.bUsedHeldQuestWrist != 0 ||
		QuestWristTrace.bRawQuestRejected != 0;
	if (bQuestArmCanUseDropoutDownFallback && bQuestArmHasOnlyDropoutEndpoint)
	{
		FVector DropoutUpWorld = UpWorld.GetSafeNormal();
		if (DropoutUpWorld.IsNearlyZero())
		{
			DropoutUpWorld = TargetCompTransform.GetUnitAxis(EAxis::Z).GetSafeNormal();
		}
		if (DropoutUpWorld.IsNearlyZero())
		{
			DropoutUpWorld = FVector::UpVector;
		}

		const double NowSeconds = FPlatformTime::Seconds();
		const FMediaPipeArmSolverState& DropoutArmState = bIsLeft ? LeftArmState : RightArmState;
		const float RecentTrackedSeconds = FMath::Max(
			0.0f,
			CVarQuestArmDropoutDownFallbackRecentTrackedSeconds.GetValueOnAnyThread());
		const float LastTrackedAgeSeconds =
			QuestWristSideState.LastTrackedQuestArmTimeSeconds >= 0.0
				? static_cast<float>(NowSeconds - QuestWristSideState.LastTrackedQuestArmTimeSeconds)
				: -1.0f;
		const bool bHasRecentTrackedArmPose =
			QuestWristSideState.bHasLastTrackedQuestArmPose &&
			LastTrackedAgeSeconds >= 0.0f &&
			(RecentTrackedSeconds <= KINDA_SMALL_NUMBER || LastTrackedAgeSeconds <= RecentTrackedSeconds);
		const float MinDownDominance = FMath::Clamp(
			CVarQuestArmDropoutDownFallbackMinDownDominance.GetValueOnAnyThread(),
			0.0f,
			1.0f);
		const bool bLastTrackedPoseWasDown =
			bHasRecentTrackedArmPose &&
			QuestWristSideState.LastTrackedQuestArmDownDominance >= MinDownDominance;
		const float LastSolveMaxAgeSeconds = FMath::Max(
			0.50f,
			CVarQuestWristLostTrackingGraceSeconds.GetValueOnAnyThread() + 0.50f);
		const float LastConstrainedAgeSeconds =
			DropoutArmState.LastConstrainedArmSolveTimeSeconds >= 0.0
				? static_cast<float>(NowSeconds - DropoutArmState.LastConstrainedArmSolveTimeSeconds)
				: -1.0f;
		const bool bHasRecentConstrainedArmSolve =
			DropoutArmState.bHasLastConstrainedArmSolve &&
			LastConstrainedAgeSeconds >= 0.0f &&
			LastConstrainedAgeSeconds <= LastSolveMaxAgeSeconds &&
			!DropoutArmState.LastConstrainedArmShoulderWorld.ContainsNaN() &&
			!DropoutArmState.LastConstrainedArmElbowWorld.ContainsNaN() &&
			!DropoutArmState.LastConstrainedArmWristWorld.ContainsNaN();
		const bool bHasLastReliableArmPoseSeed =
			bHasLastReliableArmSample &&
			!LastReliableShoulderWorld.ContainsNaN() &&
			!LastReliableElbowWorld.ContainsNaN() &&
			!LastReliableWristWorld.ContainsNaN();
		const bool bHasAcceptedArmLengthCalibration =
			QuestWristRuntimeState.ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_Accepted &&
			QuestWristSideState.bHasArmLengthCalibrationForwardReach &&
			QuestWristSideState.ArmLengthCalibrationForwardReachCm > KINDA_SMALL_NUMBER &&
			QuestWristSideState.bHasArmLengthCalibrationDownSample &&
			QuestWristSideState.ArmLengthCalibrationDownDropCm > KINDA_SMALL_NUMBER;

		const float FullReachCm = RefUpperLen + RefLowerLen;
		const float MaxReachCm = FullReachCm * QuestConstrainedArmMaxReachFraction;
		const float MinReachCm = FMath::Abs(RefUpperLen - RefLowerLen) + 0.5f;
		float CalibratedDirectReachCm = MaxReachCm;
		if (QuestWristSideState.bHasArmLengthCalibrationForwardReach &&
			QuestWristSideState.ArmLengthCalibrationForwardReachCm > KINDA_SMALL_NUMBER)
		{
			CalibratedDirectReachCm = QuestWristSideState.ArmLengthCalibrationForwardReachCm;
		}
		CalibratedDirectReachCm = FMath::Clamp(
			CalibratedDirectReachCm,
			MinReachCm,
			FMath::Max(MinReachCm, MaxReachCm));
		const float CurrentDropoutEndpointReachCm = FVector::Dist(ShoulderWorld, WristWorld);
		const float InferDownPoseMaxReachFraction = FMath::Clamp(
			CVarQuestArmLengthCalibrationDownMinCorrectedReachFraction.GetValueOnAnyThread(),
			0.50f,
			1.0f);
		const bool bCanInferCalibratedDownPose =
			!bQuestWristPositionApplied ||
			CurrentDropoutEndpointReachCm <= CalibratedDirectReachCm * InferDownPoseMaxReachFraction;

		FVector MediaPipeHorizontalHintWorld = FVector::ZeroVector;
		float MediaPipeDownDominance = 0.0f;
		bool bMediaPipeDownHint = false;
		if (bHasMediaPipeArmWorld)
		{
			const FVector SourceShoulderToWristWorld = MediaPipeSourceWristWorld - MediaPipeSourceShoulderWorld;
			const float SourceReachCm = SourceShoulderToWristWorld.Size();
			const float SourceBelowShoulderCm = FVector::DotProduct(MediaPipeSourceShoulderWorld - MediaPipeSourceWristWorld, DropoutUpWorld);
			MediaPipeDownDominance = SourceReachCm > KINDA_SMALL_NUMBER
				? FMath::Max(0.0f, SourceBelowShoulderCm) / SourceReachCm
				: 0.0f;
			if (MediaPipeDownDominance >= MinDownDominance)
			{
				MediaPipeHorizontalHintWorld =
					SourceShoulderToWristWorld -
					FVector::DotProduct(SourceShoulderToWristWorld, DropoutUpWorld) * DropoutUpWorld;
				bMediaPipeDownHint = true;
			}
		}

		const bool bContinueActiveFallback =
			QuestWristSideState.bDropoutDownFallbackActive;

		FMediaPipeQuestArmDropoutDownFallbackPolicyInput DropoutPolicyInput;
		DropoutPolicyInput.bEnabled = CVarQuestArmDropoutDownFallback.GetValueOnAnyThread() != 0;
		DropoutPolicyInput.bUseHmdRelativeAvatarArmFrame = bUseHmdRelativeAvatarArmFrame;
		DropoutPolicyInput.bQuestArmUsesConstrainedSolve = bQuestArmUsesConstrainedSolve;
		DropoutPolicyInput.bQuestHandTrackingEnabled = bQuestHandTrackingEnabled;
		DropoutPolicyInput.bQuestSideTracked = bQuestSideTrackedForArm;
		DropoutPolicyInput.bHasOnlyDropoutEndpoint = bQuestArmHasOnlyDropoutEndpoint;
		DropoutPolicyInput.bHasRecentTrackedArmPose = bHasRecentTrackedArmPose;
		DropoutPolicyInput.bLastTrackedPoseWasDown = bLastTrackedPoseWasDown;
		DropoutPolicyInput.bHasMediaPipeDownHint = bMediaPipeDownHint;
		DropoutPolicyInput.bContinueActiveFallback = bContinueActiveFallback;
		DropoutPolicyInput.bHasAcceptedArmLengthCalibration = bHasAcceptedArmLengthCalibration;
		DropoutPolicyInput.bCanInferCalibratedDownPose = bCanInferCalibratedDownPose;
		DropoutPolicyInput.bHasRecentConstrainedArmSolve = bHasRecentConstrainedArmSolve;
		DropoutPolicyInput.bHasLastReliableArmSample = bHasLastReliableArmPoseSeed;
		// USER RULE (2026-07-02): the camera outranks the synthetic down pose. Down-dominance
		// below the activation threshold while the whole arm is measured means the camera says
		// the arm is NOT down - the fallback must not run, and an already-active fallback latch
		// is cancelled outright (the measured failure rode the continue latch through a whole
		// overhead raise at full-extension-down while the camera watched the arm go up).
		DropoutPolicyInput.bMediaPipeSeesArmNotDown =
			bHasMediaPipeArmWorld &&
			MediaPipeDownDominance < MinDownDominance;
		const FMediaPipeQuestArmDropoutDownFallbackPolicyResult DropoutPolicy =
			FMediaPipeQuestWristApplyPolicy::ShouldUseDropoutDownFallback(DropoutPolicyInput);

		if (DropoutPolicyInput.bMediaPipeSeesArmNotDown && QuestWristSideState.bDropoutDownFallbackActive)
		{
			QuestWristSideState.bDropoutDownFallbackActive = false;
			QuestWristSideState.DropoutDownFallbackLastUpdateTimeSeconds = -1.0;
		}

		if (DropoutPolicy.bUseFallback)
		{
			FVector SeedShoulderToWristWorld = FVector::ZeroVector;
			FVector SeedShoulderToElbowWorld = FVector::ZeroVector;
			bool bHasSeedArmPose = false;
			if (bHasRecentTrackedArmPose)
			{
				SeedShoulderToWristWorld =
					QuestWristSideState.LastTrackedQuestArmWristWorld -
					QuestWristSideState.LastTrackedQuestArmShoulderWorld;
				SeedShoulderToElbowWorld =
					QuestWristSideState.LastTrackedQuestArmElbowWorld -
					QuestWristSideState.LastTrackedQuestArmShoulderWorld;
				bHasSeedArmPose = true;
			}
			else if (bHasRecentConstrainedArmSolve)
			{
				SeedShoulderToWristWorld =
					DropoutArmState.LastConstrainedArmWristWorld -
					DropoutArmState.LastConstrainedArmShoulderWorld;
				SeedShoulderToElbowWorld =
					DropoutArmState.LastConstrainedArmElbowWorld -
					DropoutArmState.LastConstrainedArmShoulderWorld;
				bHasSeedArmPose = true;
			}
			else if (bHasLastReliableArmPoseSeed)
			{
				SeedShoulderToWristWorld = LastReliableWristWorld - LastReliableShoulderWorld;
				SeedShoulderToElbowWorld = LastReliableElbowWorld - LastReliableShoulderWorld;
				bHasSeedArmPose = true;
			}

			FVector HorizontalHintWorld = FVector::ZeroVector;
			if (!DropoutPolicy.bInferCalibratedDownPose && bHasSeedArmPose)
			{
				HorizontalHintWorld =
					SeedShoulderToWristWorld -
					FVector::DotProduct(SeedShoulderToWristWorld, DropoutUpWorld) * DropoutUpWorld;
			}
			if (bMediaPipeDownHint && !MediaPipeHorizontalHintWorld.IsNearlyZero())
			{
				HorizontalHintWorld = HorizontalHintWorld.IsNearlyZero()
					? MediaPipeHorizontalHintWorld
					: FMath::Lerp(HorizontalHintWorld, MediaPipeHorizontalHintWorld, 0.35f);
			}
			if (HorizontalHintWorld.IsNearlyZero())
			{
				FVector OutwardWorld = bIsLeft ? -ShoulderRightWorld : ShoulderRightWorld;
				OutwardWorld = (OutwardWorld - FVector::DotProduct(OutwardWorld, DropoutUpWorld) * DropoutUpWorld).GetSafeNormal();
				if (OutwardWorld.IsNearlyZero())
				{
					OutwardWorld = (bIsLeft ? -TargetCompTransform.GetUnitAxis(EAxis::Y) : TargetCompTransform.GetUnitAxis(EAxis::Y)).GetSafeNormal();
				}
				HorizontalHintWorld = OutwardWorld * (CalibratedDirectReachCm * 0.20f);
			}
			const float MaxHorizontalCm = CalibratedDirectReachCm * 0.45f;
			if (HorizontalHintWorld.SizeSquared() > FMath::Square(MaxHorizontalCm))
			{
				HorizontalHintWorld = HorizontalHintWorld.GetSafeNormal() * MaxHorizontalCm;
			}

			const float HorizontalCm = HorizontalHintWorld.Size();
			const float TargetDownDropCm = FMath::Sqrt(FMath::Max(
				0.0f,
				CalibratedDirectReachCm * CalibratedDirectReachCm - HorizontalCm * HorizontalCm));
			const FVector RawFallbackWristWorld =
				ShoulderWorld +
				HorizontalHintWorld -
				DropoutUpWorld * TargetDownDropCm;
			const FVector SeedWristWorld =
				QuestWristSideState.bDropoutDownFallbackActive
					? QuestWristSideState.DropoutDownFallbackWristWorld
					: bHasSeedArmPose
						? ShoulderWorld + SeedShoulderToWristWorld
						: WristWorld;
			const FVector SeedElbowWorld =
				QuestWristSideState.bDropoutDownFallbackActive
					? QuestWristSideState.DropoutDownFallbackElbowWorld
					: bHasSeedArmPose
						? ShoulderWorld + SeedShoulderToElbowWorld
						: ElbowWorld;
			float FallbackDeltaSeconds = QuestWristSideState.DropoutDownFallbackLastUpdateTimeSeconds >= 0.0
				? static_cast<float>(NowSeconds - QuestWristSideState.DropoutDownFallbackLastUpdateTimeSeconds)
				: FMath::Max(DeltaSeconds, CachedDeltaTimeSeconds);
			if (!FMath::IsFinite(FallbackDeltaSeconds) || FallbackDeltaSeconds <= 0.0f || FallbackDeltaSeconds > 0.25f)
			{
				FallbackDeltaSeconds = FMath::Max(DeltaSeconds, CachedDeltaTimeSeconds);
			}
			const float FallbackAlpha = HalfLifeToAlpha(
				FMath::Max(0.0f, CVarQuestArmDropoutDownFallbackBlendHalfLife.GetValueOnAnyThread()),
				FallbackDeltaSeconds);

			WristWorld = FMath::Lerp(SeedWristWorld, RawFallbackWristWorld, FallbackAlpha);
			ElbowWorld = SeedElbowWorld;
			QuestWristSideState.bDropoutDownFallbackActive = true;
			QuestWristSideState.DropoutDownFallbackWristWorld = WristWorld;
			QuestWristSideState.DropoutDownFallbackElbowWorld = ElbowWorld;
			QuestWristSideState.DropoutDownFallbackLastUpdateTimeSeconds = NowSeconds;

			QuestWristTrace.bMapped = 1;
			QuestWristTrace.bPositionApplied = 1;
			QuestWristTrace.bUsedHeldQuestWrist = 1;
			QuestWristTrace.bConstrainedArmDropoutDownFallbackApplied = 1;
			QuestWristTrace.bConstrainedArmDropoutMediaPipeHintUsed = bMediaPipeDownHint ? 1 : 0;
			QuestWristTrace.CalibrationMode = EQuestMediaSpaceCalibrationMode::HmdRelativeAvatar;
			const FVector DropoutReferenceWristWorld = bHasMediaPipeArmWorld
				? MediaPipeSourceWristWorld
				: QuestWristTrace.MediaPipeWristWorld;
			QuestWristTrace.MediaPipeWristWorld = DropoutReferenceWristWorld;
			QuestWristTrace.MappedQuestWristWorld = RawFallbackWristWorld;
			QuestWristTrace.FinalWristWorld = WristWorld;
			QuestWristTrace.MappedOffsetFromMediaPipeCm = FVector::Dist(RawFallbackWristWorld, DropoutReferenceWristWorld);
			QuestWristTrace.ConstrainedArmDropoutDownFallbackAlpha = FallbackAlpha;
			QuestWristTrace.ConstrainedArmDropoutDirectReachCm = FVector::Dist(ShoulderWorld, WristWorld);
			QuestWristTrace.ConstrainedArmDropoutTargetReachCm = CalibratedDirectReachCm;
			QuestWristTrace.ConstrainedArmDropoutDownDominance = FMath::Max(
				FMath::Max(
					bHasRecentTrackedArmPose ? QuestWristSideState.LastTrackedQuestArmDownDominance : 0.0f,
					MediaPipeDownDominance),
				DropoutPolicy.bInferCalibratedDownPose ? 1.0f : 0.0f);
			QuestWristTrace.ConstrainedArmDropoutLastTrackedAgeSeconds =
				bHasRecentTrackedArmPose
					? LastTrackedAgeSeconds
					: LastConstrainedAgeSeconds;
			bQuestWristPositionApplied = true;
		}
	}
	FMediaPipeQuestArmLengthCalibrationOwnerPolicyInput ArmLengthOwnerInput;
	ArmLengthOwnerInput.bTargetIsMetaHuman = TargetMetaHumanProfile.bIsMetaHuman;
	ArmLengthOwnerInput.bTargetProfileActive = TargetMetaHumanProfile.bIsActiveProfile;
	ArmLengthOwnerInput.bHasTargetEmbodimentProfile = bHasTargetEmbodimentProfile;
	ArmLengthOwnerInput.bTargetIsMannyLike =
		TargetEmbodimentProfile.SkeletonFamily == EMediaPipeAvatarSkeletonFamily::MannyLike;
	const bool bQuestArmLengthCalibrationOwner =
		FMediaPipeQuestWristApplyPolicy::ShouldOwnArmLengthCalibration(ArmLengthOwnerInput);
	const bool bQuestArmLengthCalibrationAccepted =
		QuestWristRuntimeState.ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_Accepted;

	if (bQuestWristPositionApplied)
	{
		const float MaxReachCm = (RefUpperLen + RefLowerLen) * QuestConstrainedArmMaxReachFraction;
		const float MinReachCm = FMath::Abs(RefUpperLen - RefLowerLen) + 0.5f;
		const float ConfiguredMaxReachStepCm = FMath::Max(0.0f, CVarQuestConstrainedArmMaxReachStepCm.GetValueOnAnyThread());
		const float MaxReachStepCm = ConfiguredMaxReachStepCm;
		FVector ShoulderToWristWorld = WristWorld - ShoulderWorld;
		float WristReachCm = ShoulderToWristWorld.Size();
		const double ReachScaleNowSeconds = FPlatformTime::Seconds();
		const bool bSuppressReachScaleAfterDropout =
			bQuestSideTrackedForArm &&
			QuestWristSideState.DropoutReacquireReachScaleSuppressUntilTimeSeconds >= ReachScaleNowSeconds &&
			QuestWristTrace.bMapped != 0 &&
			QuestWristTrace.bUsedHeldQuestWrist == 0 &&
			QuestWristTrace.bConstrainedArmBodyFallbackApplied == 0 &&
			QuestWristTrace.bConstrainedArmDropoutDownFallbackApplied == 0;
		if (bUseHmdRelativeAvatarArmFrame &&
			CVarQuestConstrainedArmReachScaleCalibration.GetValueOnAnyThread() != 0 &&
			QuestWristTrace.bMapped != 0 &&
			QuestWristTrace.bUsedHeldQuestWrist == 0 &&
			QuestWristTrace.bConstrainedArmBodyFallbackApplied == 0 &&
			bQuestSideTrackedForArm &&
			WristReachCm > KINDA_SMALL_NUMBER &&
			MaxReachCm > KINDA_SMALL_NUMBER)
		{
			const float MinObservedFraction = FMath::Clamp(
				CVarQuestConstrainedArmReachScaleMinObservedFraction.GetValueOnAnyThread(),
				0.0f,
				1.0f);
			const float ReachScaleMin = FMath::Max(
				0.01f,
				CVarQuestConstrainedArmReachScaleMin.GetValueOnAnyThread());
			const float ReachScaleMax = FMath::Max(
				ReachScaleMin,
				CVarQuestConstrainedArmReachScaleMax.GetValueOnAnyThread());
			const float MinObservedReachCm = MaxReachCm * MinObservedFraction;
			const float MaxObservedReachCm = MaxReachCm / ReachScaleMin;
			if (!bSuppressReachScaleAfterDropout &&
				WristReachCm >= MinObservedReachCm &&
				WristReachCm <= MaxObservedReachCm &&
				(!QuestWristSideState.bHasHmdRelativeReachObservedMax ||
				 WristReachCm > QuestWristSideState.HmdRelativeReachObservedMaxCm))
			{
				QuestWristSideState.bHasHmdRelativeReachObservedMax = true;
				QuestWristSideState.HmdRelativeReachObservedMaxCm = WristReachCm;
			}

			if (QuestWristSideState.bHasHmdRelativeReachObservedMax)
			{
				FMediaPipeQuestReachScaleCalibrationInput ReachScaleInput;
				ReachScaleInput.bEnabled = true;
				ReachScaleInput.bSuppressDuringDropoutReacquire =
					bSuppressReachScaleAfterDropout ||
					QuestWristTrace.bUsedHeldQuestWrist != 0 ||
					QuestWristTrace.bUsedUntrackedJointData != 0;
				ReachScaleInput.bAllowScaleBelowOne =
					!bQuestArmLengthCalibrationOwner || bQuestArmLengthCalibrationAccepted;
				ReachScaleInput.CurrentReachCm = WristReachCm;
				ReachScaleInput.ObservedMaxReachCm = QuestWristSideState.HmdRelativeReachObservedMaxCm;
				ReachScaleInput.TargetMinReachCm = MinReachCm;
				ReachScaleInput.TargetMaxReachCm = MaxReachCm;
				ReachScaleInput.MinObservedTargetFraction = MinObservedFraction;
				ReachScaleInput.bApplyUniformScale =
					CVarQuestConstrainedArmReachScaleUniform.GetValueOnAnyThread() != 0;
				ReachScaleInput.ApplyStartObservedFraction =
					CVarQuestConstrainedArmReachScaleApplyStartFraction.GetValueOnAnyThread();
				ReachScaleInput.ApplyFullObservedFraction =
					CVarQuestConstrainedArmReachScaleApplyFullFraction.GetValueOnAnyThread();
				ReachScaleInput.MinScale = ReachScaleMin;
				ReachScaleInput.MaxScale = ReachScaleMax;
				const FMediaPipeQuestReachScaleCalibrationResult ReachScaleResult =
					FMediaPipeQuestWristApplyPolicy::ComputeReachScaleCalibration(ReachScaleInput);
				QuestWristTrace.ConstrainedArmReachScale = ReachScaleResult.Scale;
				QuestWristTrace.ConstrainedArmReachScaleAlpha = ReachScaleResult.ApplyAlpha;
				QuestWristTrace.ConstrainedArmReachScaleObservedMaxCm = QuestWristSideState.HmdRelativeReachObservedMaxCm;
				QuestWristTrace.ConstrainedArmReachScaleTargetReachCm = ReachScaleResult.TargetReachCm;
				if (ReachScaleResult.bApplied)
				{
					WristWorld = ShoulderWorld + ShoulderToWristWorld.GetSafeNormal() * ReachScaleResult.TargetReachCm;
					ShoulderToWristWorld = WristWorld - ShoulderWorld;
					WristReachCm = ReachScaleResult.TargetReachCm;
					QuestWristTrace.FinalWristWorld = WristWorld;
					QuestWristTrace.bConstrainedArmReachScaleApplied = 1;
				}
			}
		}
		if (bQuestArmLengthCalibrationOwner &&
			bUseHmdRelativeAvatarArmFrame &&
			bQuestArmUsesConstrainedSolve &&
			CVarQuestArmLengthCalibrationStartup.GetValueOnAnyThread() != 0 &&
			QuestWristTrace.bMapped != 0 &&
			QuestWristTrace.bUsedHeldQuestWrist == 0 &&
			QuestWristTrace.bConstrainedArmBodyFallbackApplied == 0 &&
			bQuestSideTrackedForArm &&
			WristReachCm > KINDA_SMALL_NUMBER &&
			MaxReachCm > KINDA_SMALL_NUMBER)
		{
			const FVector UpAxisWorld = UpWorld.IsNearlyZero()
				? TargetCompTransform.GetUnitAxis(EAxis::Z).GetSafeNormal()
				: UpWorld.GetSafeNormal();
			const double NowSeconds = FPlatformTime::Seconds();
			const float BelowShoulderCm = FVector::DotProduct(ShoulderWorld - WristWorld, UpAxisWorld);
			const float DownDropCm = FMath::Max(0.0f, BelowShoulderCm);
			const float DownDominance = WristReachCm > KINDA_SMALL_NUMBER ? DownDropCm / WristReachCm : 0.0f;

			if (QuestWristSideState.bHasArmLengthCalibrationLastSample &&
				QuestWristSideState.ArmLengthCalibrationLastSampleTimeSeconds >= 0.0 &&
				NowSeconds > QuestWristSideState.ArmLengthCalibrationLastSampleTimeSeconds)
			{
				const float SampleDeltaSeconds = static_cast<float>(NowSeconds - QuestWristSideState.ArmLengthCalibrationLastSampleTimeSeconds);
				QuestWristSideState.ArmLengthCalibrationLastVelocityCmSec =
					FVector::Dist(WristWorld, QuestWristSideState.ArmLengthCalibrationLastWristWorld) /
					FMath::Max(SampleDeltaSeconds, UE_SMALL_NUMBER);
			}
			else
			{
				QuestWristSideState.ArmLengthCalibrationLastVelocityCmSec = 0.0f;
			}
			QuestWristSideState.bHasArmLengthCalibrationLastSample = true;
			QuestWristSideState.ArmLengthCalibrationLastWristWorld = WristWorld;
			QuestWristSideState.ArmLengthCalibrationLastSampleTimeSeconds = NowSeconds;

			QuestWristSideState.bHasArmLengthCalibrationCandidate = true;
			QuestWristSideState.bArmLengthCalibrationCandidateTracked = true;
			QuestWristSideState.ArmLengthCalibrationCandidateWristWorld = WristWorld;
			QuestWristSideState.ArmLengthCalibrationCandidateShoulderWorld = ShoulderWorld;
			QuestWristSideState.ArmLengthCalibrationCandidateReachCm = WristReachCm;
			QuestWristSideState.ArmLengthCalibrationCandidateBelowShoulderCm = DownDropCm;
			QuestWristSideState.ArmLengthCalibrationCandidateVerticalDominance = DownDominance;
			QuestWristSideState.ArmLengthCalibrationCandidateTimeSeconds = NowSeconds;

			auto ResetArmLengthCalibrationProgress = [&]()
			{
				QuestWristRuntimeState.ArmLengthCalibrationStableFrameCount = 0;
				QuestWristRuntimeState.ArmLengthCalibrationStableSeconds = 0.0f;
				QuestWristRuntimeState.ArmLengthCalibrationLastUpdateTimeSeconds = -1.0;
			};

			auto IsRecentCandidate = [&](const FQuestWristSideRuntimeState& SideState) -> bool
			{
				return SideState.bHasArmLengthCalibrationCandidate &&
					SideState.bArmLengthCalibrationCandidateTracked &&
					SideState.ArmLengthCalibrationCandidateTimeSeconds >= 0.0 &&
					NowSeconds - SideState.ArmLengthCalibrationCandidateTimeSeconds <= 0.25;
			};

			FQuestWristSideRuntimeState& LeftSideState = QuestWristRuntimeState.Left;
			FQuestWristSideRuntimeState& RightSideState = QuestWristRuntimeState.Right;
			if (IsRecentCandidate(LeftSideState) && IsRecentCandidate(RightSideState))
			{
				if (QuestWristRuntimeState.ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_WaitingForHands)
				{
					QuestWristRuntimeState.ArmLengthCalibrationStage = QuestArmLengthCalibrationStage_ForwardReach;
					ResetArmLengthCalibrationProgress();
				}

				const float MaxHandVelocityCmSec = FMath::Max(
					0.0f,
					CVarQuestArmLengthCalibrationMaxHandVelocityCmSec.GetValueOnAnyThread());
				const bool bHandsStable =
					(MaxHandVelocityCmSec <= KINDA_SMALL_NUMBER ||
					 (LeftSideState.ArmLengthCalibrationLastVelocityCmSec <= MaxHandVelocityCmSec &&
					  RightSideState.ArmLengthCalibrationLastVelocityCmSec <= MaxHandVelocityCmSec));

				bool bPoseValid = false;
				float LeftCorrectedDownReachCm = 0.0f;
				float RightCorrectedDownReachCm = 0.0f;
				float LeftCorrectedDownDropCm = 0.0f;
				float RightCorrectedDownDropCm = 0.0f;
				if (QuestWristRuntimeState.ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_ForwardReach)
				{
					const float ForwardMinReachFraction = FMath::Clamp(
						CVarQuestArmLengthCalibrationForwardMinReachFraction.GetValueOnAnyThread(),
						0.0f,
						1.0f);
					const float MinForwardReachCm = MaxReachCm * ForwardMinReachFraction;
					bPoseValid =
						LeftSideState.ArmLengthCalibrationCandidateReachCm >= MinForwardReachCm &&
						RightSideState.ArmLengthCalibrationCandidateReachCm >= MinForwardReachCm;
				}
				else if (QuestWristRuntimeState.ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_DownReach)
				{
					const float DownMinBelowShoulderFraction = FMath::Clamp(
						CVarQuestArmLengthCalibrationDownMinBelowShoulderFraction.GetValueOnAnyThread(),
						0.0f,
						1.0f);
					const float DownMinVerticalDominance = FMath::Clamp(
						CVarQuestArmLengthCalibrationDownMinVerticalDominance.GetValueOnAnyThread(),
						0.0f,
						1.0f);
					const float DownMinCorrectedReachFraction = FMath::Clamp(
						CVarQuestArmLengthCalibrationDownMinCorrectedReachFraction.GetValueOnAnyThread(),
						0.0f,
						1.0f);
					const float MinDownDropCm = MaxReachCm * DownMinBelowShoulderFraction;
					const float MinCorrectedDownReachCm = MaxReachCm * DownMinCorrectedReachFraction;
					auto ComputeCorrectedDownReachCm = [&](
						const FQuestWristSideRuntimeState& SideState,
						float& OutCorrectedDownDropCm) -> float
					{
						const float CandidateReachCm = FMath::Max(0.0f, SideState.ArmLengthCalibrationCandidateReachCm);
						const float CandidateDownDropCm = FMath::Max(0.0f, SideState.ArmLengthCalibrationCandidateBelowShoulderCm);
						if (CandidateReachCm <= KINDA_SMALL_NUMBER || CandidateDownDropCm <= KINDA_SMALL_NUMBER)
						{
							OutCorrectedDownDropCm = 0.0f;
							return 0.0f;
						}

						const float HorizontalCmSq = FMath::Max(
							0.0f,
							CandidateReachCm * CandidateReachCm - CandidateDownDropCm * CandidateDownDropCm);
						const float TargetDownDropCm = FMath::Sqrt(FMath::Max(0.0f, MaxReachCm * MaxReachCm - HorizontalCmSq));
						const float MaxScale = FMath::Max(1.0f, CVarQuestArmDownFrameCorrectionMaxScale.GetValueOnAnyThread());
						const float DownScale = FMath::Clamp(TargetDownDropCm / CandidateDownDropCm, 1.0f, MaxScale);
						const float DirectionAlpha = RemapClamped(
							SideState.ArmLengthCalibrationCandidateVerticalDominance,
							DownMinVerticalDominance,
							1.0f);
						OutCorrectedDownDropCm = CandidateDownDropCm * FMath::Lerp(1.0f, DownScale, DirectionAlpha);
						return FMath::Sqrt(FMath::Max(0.0f, HorizontalCmSq + OutCorrectedDownDropCm * OutCorrectedDownDropCm));
					};
					LeftCorrectedDownReachCm = ComputeCorrectedDownReachCm(LeftSideState, LeftCorrectedDownDropCm);
					RightCorrectedDownReachCm = ComputeCorrectedDownReachCm(RightSideState, RightCorrectedDownDropCm);
					bPoseValid =
						LeftSideState.ArmLengthCalibrationCandidateBelowShoulderCm >= MinDownDropCm &&
						RightSideState.ArmLengthCalibrationCandidateBelowShoulderCm >= MinDownDropCm &&
						LeftSideState.ArmLengthCalibrationCandidateVerticalDominance >= DownMinVerticalDominance &&
						RightSideState.ArmLengthCalibrationCandidateVerticalDominance >= DownMinVerticalDominance &&
						LeftCorrectedDownReachCm >= MinCorrectedDownReachCm &&
						RightCorrectedDownReachCm >= MinCorrectedDownReachCm;
				}

				if (bPoseValid && bHandsStable)
				{
					float StableDeltaSeconds = QuestWristRuntimeState.ArmLengthCalibrationLastUpdateTimeSeconds >= 0.0
						? static_cast<float>(NowSeconds - QuestWristRuntimeState.ArmLengthCalibrationLastUpdateTimeSeconds)
						: FMath::Max(DeltaSeconds, CachedDeltaTimeSeconds);
					if (!FMath::IsFinite(StableDeltaSeconds) || StableDeltaSeconds <= 0.0f || StableDeltaSeconds > 0.25f)
					{
						StableDeltaSeconds = FMath::Max(DeltaSeconds, CachedDeltaTimeSeconds);
					}
					QuestWristRuntimeState.ArmLengthCalibrationStableSeconds += FMath::Clamp(StableDeltaSeconds, 1.0f / 240.0f, 0.10f);
					QuestWristRuntimeState.ArmLengthCalibrationStableFrameCount += 1;
					QuestWristRuntimeState.ArmLengthCalibrationLastUpdateTimeSeconds = NowSeconds;

					const float RequiredHoldSeconds = FMath::Max(0.0f, CVarQuestArmLengthCalibrationHoldSeconds.GetValueOnAnyThread());
					const int32 RequiredStableFrames = FMath::Max(1, CVarQuestArmLengthCalibrationStableFrames.GetValueOnAnyThread());
					if (QuestWristRuntimeState.ArmLengthCalibrationStableSeconds >= RequiredHoldSeconds &&
						QuestWristRuntimeState.ArmLengthCalibrationStableFrameCount >= RequiredStableFrames)
					{
						if (QuestWristRuntimeState.ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_ForwardReach)
						{
							LeftSideState.bHasArmLengthCalibrationForwardReach = true;
							RightSideState.bHasArmLengthCalibrationForwardReach = true;
							LeftSideState.ArmLengthCalibrationForwardReachCm = LeftSideState.ArmLengthCalibrationCandidateReachCm;
							RightSideState.ArmLengthCalibrationForwardReachCm = RightSideState.ArmLengthCalibrationCandidateReachCm;
							QuestWristRuntimeState.ArmLengthCalibrationStage = QuestArmLengthCalibrationStage_DownReach;
							ResetArmLengthCalibrationProgress();
							UE_LOG(
								LogMediaPipePose,
								Log,
								TEXT("mp.QuestArmLengthCalibration: stage=ForwardReach accepted=1 observedForwardReachCmL=%.1f observedForwardReachCmR=%.1f targetMetaHumanReachCm=%.1f next=DownReach"),
								LeftSideState.ArmLengthCalibrationForwardReachCm,
								RightSideState.ArmLengthCalibrationForwardReachCm,
								MaxReachCm);
						}
						else if (QuestWristRuntimeState.ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_DownReach)
						{
							LeftSideState.bHasArmLengthCalibrationDownSample = true;
							RightSideState.bHasArmLengthCalibrationDownSample = true;
							LeftSideState.ArmLengthCalibrationDownDropCm = LeftSideState.ArmLengthCalibrationCandidateBelowShoulderCm;
							RightSideState.ArmLengthCalibrationDownDropCm = RightSideState.ArmLengthCalibrationCandidateBelowShoulderCm;
							LeftSideState.ArmLengthCalibrationDownReachCm = LeftSideState.ArmLengthCalibrationCandidateReachCm;
							RightSideState.ArmLengthCalibrationDownReachCm = RightSideState.ArmLengthCalibrationCandidateReachCm;
							QuestWristRuntimeState.ArmLengthCalibrationStage = QuestArmLengthCalibrationStage_Accepted;
							QuestWristRuntimeState.ArmLengthCalibrationAcceptedTimeSeconds = NowSeconds;
							ResetArmLengthCalibrationProgress();
							UE_LOG(
								LogMediaPipePose,
								Log,
								TEXT("mp.QuestArmLengthCalibration: stage=DownReach accepted=1 observedForwardReachCmL=%.1f observedForwardReachCmR=%.1f observedDownDropCmL=%.1f observedDownDropCmR=%.1f observedDownReachCmL=%.1f observedDownReachCmR=%.1f correctedDownDropCmL=%.1f correctedDownDropCmR=%.1f correctedDownReachCmL=%.1f correctedDownReachCmR=%.1f targetMetaHumanReachCm=%.1f calibrationAccepted=1"),
								LeftSideState.ArmLengthCalibrationForwardReachCm,
								RightSideState.ArmLengthCalibrationForwardReachCm,
								LeftSideState.ArmLengthCalibrationDownDropCm,
								RightSideState.ArmLengthCalibrationDownDropCm,
								LeftSideState.ArmLengthCalibrationDownReachCm,
								RightSideState.ArmLengthCalibrationDownReachCm,
								LeftCorrectedDownDropCm,
								RightCorrectedDownDropCm,
								LeftCorrectedDownReachCm,
								RightCorrectedDownReachCm,
								MaxReachCm);
						}
					}
				}
				else if (QuestWristRuntimeState.ArmLengthCalibrationStage != QuestArmLengthCalibrationStage_Accepted)
				{
					ResetArmLengthCalibrationProgress();
				}
			}
			else if (QuestWristRuntimeState.ArmLengthCalibrationStage != QuestArmLengthCalibrationStage_Accepted)
			{
				QuestWristRuntimeState.ArmLengthCalibrationStage = QuestArmLengthCalibrationStage_WaitingForHands;
				ResetArmLengthCalibrationProgress();
			}

			QuestWristTrace.ArmLengthCalibrationStage = QuestWristRuntimeState.ArmLengthCalibrationStage;
			QuestWristTrace.ArmLengthCalibrationStableSeconds = QuestWristRuntimeState.ArmLengthCalibrationStableSeconds;
			QuestWristTrace.ArmLengthCalibrationForwardReachCm = QuestWristSideState.ArmLengthCalibrationForwardReachCm;
			QuestWristTrace.ArmLengthCalibrationDownDropCm = QuestWristSideState.ArmLengthCalibrationDownDropCm;
			QuestWristTrace.ArmLengthCalibrationTargetReachCm = MaxReachCm;

			if (QuestWristRuntimeState.ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_Accepted &&
				CVarQuestArmDownFrameCorrection.GetValueOnAnyThread() != 0 &&
				QuestWristSideState.bHasArmLengthCalibrationDownSample &&
				QuestWristSideState.ArmLengthCalibrationDownDropCm > KINDA_SMALL_NUMBER &&
				DownDropCm > KINDA_SMALL_NUMBER &&
				DownDominance >= FMath::Clamp(CVarQuestArmLengthCalibrationDownMinVerticalDominance.GetValueOnAnyThread(), 0.0f, 1.0f))
			{
				const float HorizontalCmSq = FMath::Max(0.0f, WristReachCm * WristReachCm - DownDropCm * DownDropCm);
				const float TargetDownDropCm = FMath::Sqrt(FMath::Max(0.0f, MaxReachCm * MaxReachCm - HorizontalCmSq));
				const float MaxScale = FMath::Max(1.0f, CVarQuestArmDownFrameCorrectionMaxScale.GetValueOnAnyThread());
				const float DownScale = FMath::Clamp(
					TargetDownDropCm / QuestWristSideState.ArmLengthCalibrationDownDropCm,
					1.0f,
					MaxScale);
				const float DirectionAlpha = RemapClamped(
					DownDominance,
					FMath::Clamp(CVarQuestArmLengthCalibrationDownMinVerticalDominance.GetValueOnAnyThread(), 0.0f, 1.0f),
					1.0f);
				const float CorrectedDownDropCm = DownDropCm * FMath::Lerp(1.0f, DownScale, DirectionAlpha);
				if (CorrectedDownDropCm > DownDropCm + 0.1f)
				{
					const FVector VerticalWorld = -UpAxisWorld * CorrectedDownDropCm;
					const FVector HorizontalWorld = ShoulderToWristWorld + UpAxisWorld * DownDropCm;
					WristWorld = ShoulderWorld + HorizontalWorld + VerticalWorld;
					ShoulderToWristWorld = WristWorld - ShoulderWorld;
					WristReachCm = ShoulderToWristWorld.Size();
					QuestWristTrace.FinalWristWorld = WristWorld;
					QuestWristTrace.MappedQuestWristWorld = WristWorld;
					QuestWristTrace.bConstrainedArmDownFrameCorrectionApplied = 1;
					QuestWristTrace.ConstrainedArmDownFrameScale = DownScale;
					QuestWristTrace.ConstrainedArmDownFrameAlpha = DirectionAlpha;
					QuestWristTrace.ConstrainedArmDownFrameObservedDropCm = QuestWristSideState.ArmLengthCalibrationDownDropCm;
					QuestWristTrace.ConstrainedArmDownFrameTargetDropCm = TargetDownDropCm;
				}
			}
		}
		if (WristReachCm > KINDA_SMALL_NUMBER)
		{
			const float ClampedReachCm = FMath::Clamp(WristReachCm, MinReachCm, FMath::Max(MinReachCm, MaxReachCm));
			if (!FMath::IsNearlyEqual(ClampedReachCm, WristReachCm, 0.1f))
			{
				WristWorld = ShoulderWorld + ShoulderToWristWorld.GetSafeNormal() * ClampedReachCm;
				ShoulderToWristWorld = WristWorld - ShoulderWorld;
				WristReachCm = ClampedReachCm;
				QuestWristTrace.FinalWristWorld = WristWorld;
				QuestWristTrace.bReachClamped = 1;
			}
		}
		if (bUseHmdRelativeAvatarArmFrame &&
			bQuestArmUsesConstrainedSolve &&
			QuestWristTrace.bMapped != 0 &&
			QuestWristTrace.bConstrainedArmBodyFallbackApplied == 0 &&
			WristReachCm > KINDA_SMALL_NUMBER &&
			MaxReachCm > KINDA_SMALL_NUMBER)
		{
			const double ReachContinuityNowSeconds = FPlatformTime::Seconds();
			const float LastReachContinuityMaxAgeSeconds = FMath::Max(
				0.50f,
				CVarQuestWristLostTrackingGraceSeconds.GetValueOnAnyThread() + 0.50f);
			const float LastReachContinuityAgeSeconds =
				QuestWristSideState.HmdRelativeReachContinuityTimeSeconds >= 0.0
					? static_cast<float>(ReachContinuityNowSeconds - QuestWristSideState.HmdRelativeReachContinuityTimeSeconds)
					: -1.0f;
			bHasRecentHmdRelativeReachContinuity =
				QuestWristSideState.bHasHmdRelativeReachContinuity &&
				LastReachContinuityAgeSeconds >= 0.0f &&
				LastReachContinuityAgeSeconds <= LastReachContinuityMaxAgeSeconds;
			PreviousHmdRelativeReachContinuityCm = QuestWristSideState.HmdRelativeReachContinuityCm;
			FMediaPipeQuestReachStepContinuityInput ReachContinuityInput;
			ReachContinuityInput.bEnabled = MaxReachStepCm > KINDA_SMALL_NUMBER;
			ReachContinuityInput.CurrentReachCm = WristReachCm;
			ReachContinuityInput.bHasPreviousReach = bHasRecentHmdRelativeReachContinuity;
			ReachContinuityInput.PreviousReachCm = PreviousHmdRelativeReachContinuityCm;
			ReachContinuityInput.MaxStepCm = MaxReachStepCm;
			ReachContinuityInput.MinReachCm = MinReachCm;
			ReachContinuityInput.MaxReachCm = FMath::Max(MinReachCm, MaxReachCm);
			const FMediaPipeQuestReachStepContinuityResult ReachContinuityResult =
				FMediaPipeQuestWristApplyPolicy::ApplyReachStepContinuity(ReachContinuityInput);
			QuestWristTrace.ConstrainedArmReachContinuityMaxStepCm = ReachContinuityResult.MaxStepCm;
			if (ReachContinuityResult.bApplied)
			{
				WristWorld = ShoulderWorld + ShoulderToWristWorld.GetSafeNormal() * ReachContinuityResult.TargetReachCm;
				ShoulderToWristWorld = WristWorld - ShoulderWorld;
				WristReachCm = ReachContinuityResult.TargetReachCm;
				QuestWristTrace.FinalWristWorld = WristWorld;
				QuestWristTrace.bConstrainedArmReachContinuityApplied = 1;
				QuestWristTrace.ConstrainedArmReachContinuityRawReachCm = ReachContinuityResult.RawReachCm;
				QuestWristTrace.ConstrainedArmReachContinuityPreviousReachCm = ReachContinuityResult.PreviousReachCm;
				QuestWristTrace.ConstrainedArmReachContinuityMaxStepCm = ReachContinuityResult.MaxStepCm;
			}
		}
		ArmObservationAlphaScale = 1.0f;
	}
	if (!bUseBodyFusionArm &&
		!bMetaHumanFullArmChainDirectFresh &&
		CVarMediaPipeArmHoldOnQuestHandLoss.GetValueOnAnyThread() != 0 &&
		bQuestHandTrackingEnabled &&
		bQuestSideTrackedForArm)
	{
		LastReliableShoulderWorld = ShoulderWorld;
		LastReliableElbowWorld = ElbowWorld;
		LastReliableWristWorld = WristWorld;
		bHasLastReliableArmSample = true;
	}

	float QuestWristDriftGuardAlpha = 0.0f;
	float QuestWristDriftOffsetCm = 0.0f;
	if (bQuestWristPositionApplied &&
		QuestWristTrace.bMapped != 0 &&
		(bQuestArmUsesLegacyReachAssist || bQuestArmUsesConstrainedSolve) &&
		CVarQuestWristDriftGuard.GetValueOnAnyThread() != 0)
	{
		const float DriftGuardStartCm = FMath::Max(0.0f, CVarQuestWristDriftGuardStartCm.GetValueOnAnyThread());
		const float DriftGuardFullCm = FMath::Max(DriftGuardStartCm + 1.0f, CVarQuestWristDriftGuardFullCm.GetValueOnAnyThread());
		QuestWristDriftOffsetCm = QuestWristTrace.MappedOffsetFromMediaPipeCm;
		if (QuestWristDriftOffsetCm <= UE_SMALL_NUMBER)
		{
			QuestWristDriftOffsetCm = FVector::Dist(QuestWristTrace.MappedQuestWristWorld, QuestWristTrace.MediaPipeWristWorld);
		}
		QuestWristDriftGuardAlpha = FMath::Clamp(
			(QuestWristDriftOffsetCm - DriftGuardStartCm) / FMath::Max(DriftGuardFullCm - DriftGuardStartCm, UE_SMALL_NUMBER),
			0.0f,
			1.0f);
		if (QuestWristDriftGuardAlpha > KINDA_SMALL_NUMBER)
		{
			QuestWristTrace.bDriftGuardApplied = 1;
			QuestWristTrace.DriftGuardAlpha = QuestWristDriftGuardAlpha;
			QuestWristTrace.DriftGuardOffsetCm = QuestWristDriftOffsetCm;
		}
	}

	bool bQuestConstrainedArmSolveApplied = false;
	if (bQuestWristPositionApplied &&
		QuestWristTrace.bMapped != 0 &&
		bQuestArmUsesConstrainedSolve &&
		CVarQuestConstrainedArmSolve.GetValueOnAnyThread() != 0 &&
		CVarQuestWristForceArmIK.GetValueOnAnyThread() == 0 &&
		!bUseArmIK &&
		RefUpperLen > KINDA_SMALL_NUMBER &&
		RefLowerLen > KINDA_SMALL_NUMBER)
	{
		const float ConstrainedSolveBlend = FMath::Clamp(CVarQuestConstrainedArmSolveBlend.GetValueOnAnyThread(), 0.0f, 1.0f);
		const float WristAuthority = FMath::Clamp(CVarQuestConstrainedArmWristAuthority.GetValueOnAnyThread(), 0.0f, 1.0f);
		float EffectiveWristAuthority = WristAuthority;
		const float AuthorityFadeStartCm = FMath::Max(0.0f, CVarQuestConstrainedArmWristAuthorityFadeStartCm.GetValueOnAnyThread());
		const float AuthorityFadeFullCm = FMath::Max(AuthorityFadeStartCm + 1.0f, CVarQuestConstrainedArmWristAuthorityFadeFullCm.GetValueOnAnyThread());
		const float MinWristAuthority = FMath::Clamp(CVarQuestConstrainedArmWristAuthorityMin.GetValueOnAnyThread(), 0.0f, WristAuthority);
		if (AuthorityFadeStartCm > KINDA_SMALL_NUMBER && MinWristAuthority < WristAuthority)
		{
			float QuestCorrectionCm = FVector::Dist(QuestWristTrace.FinalWristWorld, QuestWristTrace.MediaPipeWristWorld);
			if (QuestWristTrace.PositionFilterTargetDeltaCm > QuestCorrectionCm)
			{
				QuestCorrectionCm = QuestWristTrace.PositionFilterTargetDeltaCm;
			}
			if (QuestWristTrace.MappedOffsetFromMediaPipeCm > QuestCorrectionCm)
			{
				QuestCorrectionCm = QuestWristTrace.MappedOffsetFromMediaPipeCm;
			}
			const float AuthorityFadeAlpha = FMath::Clamp(
				(QuestCorrectionCm - AuthorityFadeStartCm) / FMath::Max(AuthorityFadeFullCm - AuthorityFadeStartCm, UE_SMALL_NUMBER),
				0.0f,
				1.0f);
			EffectiveWristAuthority = FMath::Lerp(WristAuthority, MinWristAuthority, AuthorityFadeAlpha);
		}
		if (ConstrainedSolveBlend > KINDA_SMALL_NUMBER && EffectiveWristAuthority > KINDA_SMALL_NUMBER)
		{
			const FVector OriginalElbowWorld = ElbowWorld;
			const FVector QuestEndpointWorld = FMath::Lerp(QuestWristTrace.MediaPipeWristWorld, QuestWristTrace.FinalWristWorld, EffectiveWristAuthority);
			FVector ConstrainedSolveElbowHintWorld = ElbowWorld;
			bool bConstrainedSolveSourceElbowHintApplied = false;
			if (bUseHmdRelativeAvatarArmFrame &&
				bHasMediaPipeArmWorld &&
				QuestWristTrace.bConstrainedArmBodyFallbackApplied == 0 &&
				CVarQuestConstrainedArmMediaPipeElbowHint.GetValueOnAnyThread() > KINDA_SMALL_NUMBER)
			{
				bool bMediaPipeSourceElbowHintReliable = true;
				if (CVarMediaPipeArmReliabilityGate.GetValueOnAnyThread() != 0)
				{
					const float ArmMinReliability = FMath::Clamp(CVarMediaPipeArmMinReliability.GetValueOnAnyThread(), 0.0f, 1.0f);
					bMediaPipeSourceElbowHintReliable =
						GetLandmarkReliability(ShoulderLm) >= ArmMinReliability &&
						GetLandmarkReliability(ElbowLm) >= ArmMinReliability &&
						GetLandmarkReliability(WristLm) >= ArmMinReliability;
				}

				if (bMediaPipeSourceElbowHintReliable)
				{
					FMediaPipeConstrainedArmSourceElbowHintInput SourceElbowHintInput;
					SourceElbowHintInput.bIsLeft = bIsLeft;
					SourceElbowHintInput.SourceShoulderWorld = MediaPipeSourceShoulderWorld;
					SourceElbowHintInput.SourceElbowWorld = MediaPipeSourceElbowWorld;
					SourceElbowHintInput.SourceWristWorld = MediaPipeSourceWristWorld;
					SourceElbowHintInput.TargetShoulderWorld = ShoulderWorld;
					SourceElbowHintInput.TargetEndpointWorld = QuestEndpointWorld;
					SourceElbowHintInput.UpWorld = UpWorld;
					SourceElbowHintInput.ShoulderRightWorld = ShoulderRightWorld;
					SourceElbowHintInput.TargetUpperLenCm = RefUpperLen;
					SourceElbowHintInput.TargetLowerLenCm = RefLowerLen;
					SourceElbowHintInput.MaxReachFraction = QuestConstrainedArmMaxReachFraction;

					FMediaPipeConstrainedArmSourceElbowHintResult SourceElbowHintResult;
					if (FMediaPipeQuestConstrainedArmSolver::BuildSourceElbowHint(SourceElbowHintInput, SourceElbowHintResult))
					{
						ConstrainedSolveElbowHintWorld = SourceElbowHintResult.TargetElbowWorld;
						bConstrainedSolveSourceElbowHintApplied = true;
					}
				}
			}

			FMediaPipeArmSolverState& ActiveArmState = bIsLeft ? LeftArmState : RightArmState;
			const double ConstrainedNowSeconds = FPlatformTime::Seconds();
			const float LastSolveMaxAgeSeconds = FMath::Max(
				0.50f,
				CVarQuestWristLostTrackingGraceSeconds.GetValueOnAnyThread() + 0.50f);
			const bool bHasRecentLastConstrainedArmSolve =
				ActiveArmState.bHasLastConstrainedArmSolve &&
				ActiveArmState.LastConstrainedArmSolveTimeSeconds >= 0.0 &&
				static_cast<float>(ConstrainedNowSeconds - ActiveArmState.LastConstrainedArmSolveTimeSeconds) <= LastSolveMaxAgeSeconds;
			if (!bHasRecentLastConstrainedArmSolve)
			{
				ActiveArmState.bHasSmoothedConstrainedArmElbowWorld = false;
			}
			FMediaPipeConstrainedArmSolveInput SolveInput;
			SolveInput.bIsLeft = bIsLeft;
			SolveInput.ShoulderWorld = ShoulderWorld;
			SolveInput.CurrentElbowWorld = ConstrainedSolveElbowHintWorld;
			SolveInput.QuestEndpointWorld = QuestEndpointWorld;
			SolveInput.UpWorld = UpWorld;
			SolveInput.ShoulderRightWorld = ShoulderRightWorld;
			SolveInput.bHasTorsoBasis = bHasTorsoBasis;
			SolveInput.TargetUpperLenCm = RefUpperLen;
			SolveInput.TargetLowerLenCm = RefLowerLen;
			SolveInput.MaxReachFraction = QuestConstrainedArmMaxReachFraction;
			SolveInput.bEnableDownStraighten =
				CVarQuestConstrainedArmDownStraighten.GetValueOnAnyThread() != 0;
			SolveInput.DownStraightenThresholdCm = CVarQuestConstrainedArmDownStraightenThresholdCm.GetValueOnAnyThread();
			SolveInput.DownStraightenMaxCm = CVarQuestConstrainedArmDownStraightenMaxCm.GetValueOnAnyThread();
			SolveInput.DownStraightenMinBelowShoulderRatio = CVarQuestConstrainedArmDownStraightenMinBelowShoulderRatio.GetValueOnAnyThread();
			SolveInput.DownStraightenReachFloorFraction = CVarQuestConstrainedArmDownStraightenReachFloorFraction.GetValueOnAnyThread();
			SolveInput.DownStraightenMaxReachFraction = CVarQuestConstrainedArmDownStraightenMaxReachFraction.GetValueOnAnyThread();
			SolveInput.QuestWristDriftGuardAlpha = QuestWristDriftGuardAlpha;
			SolveInput.MediaPipeElbowHint = CVarQuestConstrainedArmMediaPipeElbowHint.GetValueOnAnyThread();
			SolveInput.StablePoleDown = CVarQuestConstrainedArmStablePoleDown.GetValueOnAnyThread();
			SolveInput.CloseReachStartCm = CVarQuestConstrainedArmCloseReachStartCm.GetValueOnAnyThread();
			SolveInput.CloseReachFullCm = CVarQuestConstrainedArmCloseReachFullCm.GetValueOnAnyThread();
			SolveInput.CloseReachPoleBias = CVarQuestConstrainedArmCloseReachPoleBias.GetValueOnAnyThread();
			SolveInput.CloseReachStablePoleDown = CVarQuestConstrainedArmCloseReachStablePoleDown.GetValueOnAnyThread();
			SolveInput.MaxElbowMoveCm = CVarQuestConstrainedArmMaxElbowMoveCm.GetValueOnAnyThread();
			SolveInput.MaxReachStepCm = bUseHmdRelativeAvatarArmFrame
				? (CVarQuestConstrainedArmMaxReachStepCm.GetValueOnAnyThread() > KINDA_SMALL_NUMBER
					? CVarQuestConstrainedArmMaxReachStepCm.GetValueOnAnyThread()
					: 0.0f)
				: CVarQuestConstrainedArmMaxReachStepCm.GetValueOnAnyThread();
			SolveInput.bHasReachContinuityHistory = bHasRecentHmdRelativeReachContinuity;
			SolveInput.ReachContinuityPreviousReachCm = PreviousHmdRelativeReachContinuityCm;
			SolveInput.bEnableNearFullPoleContinuity = CVarQuestConstrainedArmNearFullPoleContinuity.GetValueOnAnyThread() != 0;
			SolveInput.NearFullPoleStartFraction = CVarQuestConstrainedArmNearFullPoleStartFraction.GetValueOnAnyThread();
			SolveInput.NearFullPoleFullFraction = CVarQuestConstrainedArmNearFullPoleFullFraction.GetValueOnAnyThread();
			SolveInput.bHasLastConstrainedArmSolve = bHasRecentLastConstrainedArmSolve;
			SolveInput.LastConstrainedArmShoulderWorld = ActiveArmState.LastConstrainedArmShoulderWorld;
			SolveInput.LastConstrainedArmElbowWorld = ActiveArmState.LastConstrainedArmElbowWorld;
			SolveInput.LastConstrainedArmWristWorld = ActiveArmState.LastConstrainedArmWristWorld;

			FMediaPipeConstrainedArmSolveResult SolveResult;
			if (FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(SolveInput, SolveResult))
			{
				WristWorld = SolveResult.TargetWristWorld;
				QuestWristTrace.FinalWristWorld = WristWorld;
				if (SolveResult.bReachClamped)
				{
					QuestWristTrace.bReachClamped = 1;
				}
				if (SolveResult.bDownStraightened)
				{
					QuestWristTrace.bConstrainedArmDownStraightened = 1;
					QuestWristTrace.ConstrainedArmDownStraightenAlpha = SolveResult.DownStraightenAdaptiveAlpha;
				}
				if (SolveResult.bPoleContinuityApplied)
				{
					QuestWristTrace.bConstrainedArmPoleContinuityApplied = 1;
				}
				if (SolveResult.bReachContinuityApplied)
				{
					QuestWristTrace.bConstrainedArmReachContinuityApplied = 1;
					QuestWristTrace.ConstrainedArmReachContinuityRawReachCm = SolveResult.ReachContinuityRawReachCm;
					QuestWristTrace.ConstrainedArmReachContinuityPreviousReachCm = SolveResult.ReachContinuityPreviousReachCm;
					QuestWristTrace.ConstrainedArmReachContinuityMaxStepCm = SolveResult.ReachContinuityMaxStepCm;
				}
				QuestWristTrace.ConstrainedArmNearFullPoleAlpha = SolveResult.NearFullPoleAlpha;
				if (QuestWristTrace.ConstrainedArmReachContinuityMaxStepCm <= KINDA_SMALL_NUMBER)
				{
					QuestWristTrace.ConstrainedArmReachContinuityMaxStepCm = SolveInput.MaxReachStepCm;
				}
				QuestWristTrace.ConstrainedArmWristStepCm = SolveResult.ContinuityWristStepCm;
				QuestWristTrace.ConstrainedArmCandidateElbowStepCm = SolveResult.CandidateElbowStepCm;
				QuestWristTrace.ConstrainedArmAllowedElbowStepCm = SolveResult.AllowedElbowStepCm;

				FVector SolvedElbowWorld = SolveResult.TargetElbowWorld;
				const float ConfiguredElbowHalfLifeSeconds = FMath::Max(0.0f, CVarQuestConstrainedArmElbowHalfLife.GetValueOnAnyThread());
				const float ElbowHalfLifeSeconds = ConfiguredElbowHalfLifeSeconds;
				const float ConfiguredMaxElbowStepCm = FMath::Max(0.0f, CVarQuestConstrainedArmMaxElbowStepCm.GetValueOnAnyThread());
				const float MaxElbowStepCm = ConfiguredMaxElbowStepCm;
				if (ElbowHalfLifeSeconds > KINDA_SMALL_NUMBER || MaxElbowStepCm > KINDA_SMALL_NUMBER)
				{
					bool& bHasSmoothedConstrainedElbowWorld = bIsLeft
						? LeftArmState.bHasSmoothedConstrainedArmElbowWorld
						: RightArmState.bHasSmoothedConstrainedArmElbowWorld;
					FVector& SmoothedConstrainedElbowWorld = bIsLeft
						? LeftArmState.SmoothedConstrainedArmElbowWorld
						: RightArmState.SmoothedConstrainedArmElbowWorld;
					if (!bHasSmoothedConstrainedElbowWorld || SmoothedConstrainedElbowWorld.ContainsNaN())
					{
						SmoothedConstrainedElbowWorld = OriginalElbowWorld;
						bHasSmoothedConstrainedElbowWorld = true;
					}

					FVector GuardedElbowWorld = ElbowHalfLifeSeconds > KINDA_SMALL_NUMBER
						? FMath::Lerp(SmoothedConstrainedElbowWorld, SolvedElbowWorld, HalfLifeToAlpha(ElbowHalfLifeSeconds, DeltaSeconds))
						: SolvedElbowWorld;
					FVector GuardedDeltaWorld = GuardedElbowWorld - SmoothedConstrainedElbowWorld;
					const float GuardedStepCm = GuardedDeltaWorld.Size();
					if (MaxElbowStepCm > KINDA_SMALL_NUMBER && GuardedStepCm > MaxElbowStepCm)
					{
						GuardedDeltaWorld = GuardedDeltaWorld.GetSafeNormal() * MaxElbowStepCm;
						GuardedElbowWorld = SmoothedConstrainedElbowWorld + GuardedDeltaWorld;
					}
					SmoothedConstrainedElbowWorld = GuardedElbowWorld;
					SolvedElbowWorld = GuardedElbowWorld;
				}

				ElbowWorld = FMath::Lerp(ElbowWorld, SolvedElbowWorld, ConstrainedSolveBlend);
				QuestWristTrace.bConstrainedArmSolveApplied = 1;
				QuestWristTrace.ConstrainedArmSolveBlend = ConstrainedSolveBlend;
				QuestWristTrace.ConstrainedArmWristAuthority = EffectiveWristAuthority;
				QuestWristTrace.ConstrainedArmMediaPipeElbowHint = FMath::Clamp(CVarQuestConstrainedArmMediaPipeElbowHint.GetValueOnAnyThread(), 0.0f, 1.0f);
				if (bConstrainedSolveSourceElbowHintApplied)
				{
					QuestWristTrace.bConstrainedArmSourceElbowHintApplied = 1;
					QuestWristTrace.ConstrainedArmSourceElbowHintWorld = ConstrainedSolveElbowHintWorld;
				}
				QuestWristTrace.ConstrainedArmCloseReachAlpha = SolveResult.CloseReachPoleAlpha;
				QuestWristTrace.ConstrainedArmStablePoleDown = SolveResult.StablePoleDown;
				QuestWristTrace.ConstrainedArmElbowMoveCm = FVector::Dist(OriginalElbowWorld, ElbowWorld);
				QuestWristTrace.ConstrainedArmElbowWorld = ElbowWorld;
				ActiveArmState.bHasLastConstrainedArmSolve = true;
				ActiveArmState.LastConstrainedArmShoulderWorld = ShoulderWorld;
				ActiveArmState.LastConstrainedArmElbowWorld = ElbowWorld;
				ActiveArmState.LastConstrainedArmWristWorld = WristWorld;
				ActiveArmState.LastConstrainedArmSolveTimeSeconds = FPlatformTime::Seconds();
				if (bQuestSideTrackedForArm &&
					QuestWristTrace.bUsedHeldQuestWrist == 0 &&
					QuestWristTrace.bUsedUntrackedJointData == 0 &&
					QuestWristTrace.bConstrainedArmBodyFallbackApplied == 0 &&
					QuestWristTrace.bConstrainedArmDropoutDownFallbackApplied == 0)
				{
					FVector TrackedUpWorld = UpWorld.GetSafeNormal();
					if (TrackedUpWorld.IsNearlyZero())
					{
						TrackedUpWorld = TargetCompTransform.GetUnitAxis(EAxis::Z).GetSafeNormal();
					}
					if (TrackedUpWorld.IsNearlyZero())
					{
						TrackedUpWorld = FVector::UpVector;
					}
					const float TrackedReachCm = FVector::Dist(ShoulderWorld, WristWorld);
					const float TrackedBelowShoulderCm = FMath::Max(
						0.0f,
						FVector::DotProduct(ShoulderWorld - WristWorld, TrackedUpWorld));
					QuestWristSideState.bHasLastTrackedQuestArmPose = true;
					QuestWristSideState.LastTrackedQuestArmShoulderWorld = ShoulderWorld;
					QuestWristSideState.LastTrackedQuestArmElbowWorld = ElbowWorld;
					QuestWristSideState.LastTrackedQuestArmWristWorld = WristWorld;
					QuestWristSideState.LastTrackedQuestArmReachCm = TrackedReachCm;
					QuestWristSideState.LastTrackedQuestArmBelowShoulderCm = TrackedBelowShoulderCm;
					QuestWristSideState.LastTrackedQuestArmDownDominance = TrackedReachCm > KINDA_SMALL_NUMBER
						? TrackedBelowShoulderCm / TrackedReachCm
						: 0.0f;
					QuestWristSideState.LastTrackedQuestArmTimeSeconds = ActiveArmState.LastConstrainedArmSolveTimeSeconds;
				}
				StoreHmdRelativeReachContinuity(WristWorld);
				bQuestConstrainedArmSolveApplied = true;
			}
		}
	}
	if (!bQuestConstrainedArmSolveApplied)
	{
		auto KeepRecentConstrainedSolveOrReset = [&]()
		{
			FMediaPipeArmSolverState& ActiveArmState = bIsLeft ? LeftArmState : RightArmState;
			const double NowSeconds = FPlatformTime::Seconds();
			const float LastSolveMaxAgeSeconds = FMath::Max(
				0.50f,
				CVarQuestWristLostTrackingGraceSeconds.GetValueOnAnyThread() + 0.50f);
			const bool bKeepRecentSolve =
				ActiveArmState.bHasLastConstrainedArmSolve &&
				ActiveArmState.LastConstrainedArmSolveTimeSeconds >= 0.0 &&
				static_cast<float>(NowSeconds - ActiveArmState.LastConstrainedArmSolveTimeSeconds) <= LastSolveMaxAgeSeconds;
			if (bKeepRecentSolve)
			{
				return;
			}

			ActiveArmState.bHasSmoothedConstrainedArmElbowWorld = false;
			ActiveArmState.bHasLastConstrainedArmSolve = false;
			ActiveArmState.LastConstrainedArmSolveTimeSeconds = -1.0;
		};

		KeepRecentConstrainedSolveOrReset();
	}

	if (bQuestWristPositionApplied &&
		!bQuestConstrainedArmSolveApplied &&
		bQuestArmUsesLegacyReachAssist &&
		CVarQuestWristReachAssist.GetValueOnAnyThread() != 0 &&
		CVarQuestWristForceArmIK.GetValueOnAnyThread() == 0 &&
		!bUseArmIK &&
		RefUpperLen > KINDA_SMALL_NUMBER &&
		RefLowerLen > KINDA_SMALL_NUMBER)
	{
		const float BaseReachAssistBlend = FMath::Clamp(CVarQuestWristReachAssistBlend.GetValueOnAnyThread(), 0.0f, 1.0f);
		float ReachAssistBlend = BaseReachAssistBlend;
		if (QuestWristDriftGuardAlpha > KINDA_SMALL_NUMBER)
		{
			const float DriftReachBoost = FMath::Clamp(CVarQuestWristDriftGuardReachBlendBoost.GetValueOnAnyThread(), 0.0f, 1.0f);
			ReachAssistBlend = FMath::Clamp(BaseReachAssistBlend + QuestWristDriftGuardAlpha * DriftReachBoost, 0.0f, 1.0f);
			QuestWristTrace.DriftGuardReachAssistBlend = ReachAssistBlend;
		}
		if (ReachAssistBlend > KINDA_SMALL_NUMBER)
		{
			const FVector OriginalElbowWorld = ElbowWorld;
			FVector ShoulderToWristWorld = WristWorld - ShoulderWorld;
			float WristReachCm = ShoulderToWristWorld.Size();
			if (WristReachCm > KINDA_SMALL_NUMBER)
			{
				const float MinReachCm = FMath::Abs(RefUpperLen - RefLowerLen) + 0.5f;
				const float MaxReachCm = (RefUpperLen + RefLowerLen) * QuestConstrainedArmMaxReachFraction;
				const float ClampedReachCm = FMath::Clamp(WristReachCm, MinReachCm, FMath::Max(MinReachCm, MaxReachCm));
				const FVector ReachDirWorld = ShoulderToWristWorld / WristReachCm;
				if (!FMath::IsNearlyEqual(ClampedReachCm, WristReachCm, 0.1f))
				{
					WristWorld = ShoulderWorld + ReachDirWorld * ClampedReachCm;
					ShoulderToWristWorld = WristWorld - ShoulderWorld;
					WristReachCm = ClampedReachCm;
					QuestWristTrace.FinalWristWorld = WristWorld;
					QuestWristTrace.bReachClamped = 1;
				}

				const float AlongCm = FMath::Clamp(
					((RefUpperLen * RefUpperLen) + (WristReachCm * WristReachCm) - (RefLowerLen * RefLowerLen)) /
						FMath::Max(2.0f * WristReachCm, UE_SMALL_NUMBER),
					0.0f,
					RefUpperLen);
				const float ElbowOffsetCm = FMath::Sqrt(FMath::Max(0.0f, (RefUpperLen * RefUpperLen) - (AlongCm * AlongCm)));
				FVector ElbowPoleWorld = ElbowWorld - ShoulderWorld;
				ElbowPoleWorld -= FVector::DotProduct(ElbowPoleWorld, ReachDirWorld) * ReachDirWorld;
				FVector StablePoleSideWorld = bIsLeft ? -ShoulderRightWorld : ShoulderRightWorld;
				if (StablePoleSideWorld.IsNearlyZero())
				{
					StablePoleSideWorld = bIsLeft ? -FVector::RightVector : FVector::RightVector;
				}
				FVector StablePoleUpWorld = UpWorld.GetSafeNormal();
				if (StablePoleUpWorld.IsNearlyZero())
				{
					StablePoleUpWorld = FVector::UpVector;
				}
				FVector StablePoleWorld = StablePoleSideWorld - StablePoleUpWorld * 0.25f;
				StablePoleWorld -= FVector::DotProduct(StablePoleWorld, ReachDirWorld) * ReachDirWorld;
				if (ElbowPoleWorld.IsNearlyZero())
				{
					ElbowPoleWorld = StablePoleWorld;
				}
				else if (QuestWristDriftGuardAlpha > KINDA_SMALL_NUMBER && !StablePoleWorld.IsNearlyZero())
				{
					const float DriftPoleBlend = FMath::Clamp(
						QuestWristDriftGuardAlpha * FMath::Clamp(CVarQuestWristDriftGuardPoleBlend.GetValueOnAnyThread(), 0.0f, 1.0f),
						0.0f,
						1.0f);
					if (DriftPoleBlend > KINDA_SMALL_NUMBER)
					{
						const FVector MediaPipePoleDirWorld = ElbowPoleWorld.GetSafeNormal();
						const FVector StablePoleDirWorld = StablePoleWorld.GetSafeNormal();
						if (!MediaPipePoleDirWorld.IsNearlyZero() && !StablePoleDirWorld.IsNearlyZero())
						{
							ElbowPoleWorld = FMath::Lerp(MediaPipePoleDirWorld, StablePoleDirWorld, DriftPoleBlend).GetSafeNormal();
							QuestWristTrace.DriftGuardPoleBlend = DriftPoleBlend;
						}
					}
				}

				const FVector ElbowPoleDirWorld = ElbowPoleWorld.GetSafeNormal();
				if (!ElbowPoleDirWorld.IsNearlyZero())
				{
					FVector AssistedElbowWorld = ShoulderWorld + ReachDirWorld * AlongCm + ElbowPoleDirWorld * ElbowOffsetCm;
					float MaxElbowMoveCm = FMath::Max(0.0f, CVarQuestWristReachAssistMaxElbowMoveCm.GetValueOnAnyThread());
					if (QuestWristDriftGuardAlpha > KINDA_SMALL_NUMBER)
					{
						MaxElbowMoveCm += QuestWristDriftGuardAlpha * FMath::Max(0.0f, CVarQuestWristDriftGuardExtraElbowMoveCm.GetValueOnAnyThread());
					}
					FVector ElbowDeltaWorld = AssistedElbowWorld - ElbowWorld;
					const float ElbowMoveCm = ElbowDeltaWorld.Size();
					if (MaxElbowMoveCm > KINDA_SMALL_NUMBER && ElbowMoveCm > MaxElbowMoveCm)
					{
						ElbowDeltaWorld = ElbowDeltaWorld.GetSafeNormal() * MaxElbowMoveCm;
						AssistedElbowWorld = ElbowWorld + ElbowDeltaWorld;
					}

					ElbowWorld = FMath::Lerp(ElbowWorld, AssistedElbowWorld, ReachAssistBlend);
					QuestWristTrace.bReachAssistApplied = 1;
					QuestWristTrace.ReachAssistBlend = ReachAssistBlend;
					QuestWristTrace.ReachAssistElbowMoveCm = FVector::Dist(OriginalElbowWorld, ElbowWorld);
					QuestWristTrace.ReachAssistElbowWorld = ElbowWorld;
				}
			}
		}
	}

	const FVector PoseUpperWorld = (ElbowWorld - ShoulderWorld).GetSafeNormal();
	const FVector PoseLowerWorld = (WristWorld - ElbowWorld).GetSafeNormal();
	if (PoseUpperWorld.IsNearlyZero() || PoseLowerWorld.IsNearlyZero())
	{
		return;
	}

	const FVector PoseUpperComp = TargetCompTransform.InverseTransformVectorNoScale(PoseUpperWorld).GetSafeNormal();
	const FVector PoseLowerComp = TargetCompTransform.InverseTransformVectorNoScale(PoseLowerWorld).GetSafeNormal();
	if (PoseUpperComp.IsNearlyZero() || PoseLowerComp.IsNearlyZero())
	{
		return;
	}

	FVector AvatarRightComp = TargetCompTransform.InverseTransformVectorNoScale(AvatarArmBasis.RightWorld).GetSafeNormal();
	FVector AvatarUpComp = TargetCompTransform.InverseTransformVectorNoScale(AvatarArmBasis.UpWorld).GetSafeNormal();
	FVector AvatarForwardComp = TargetCompTransform.InverseTransformVectorNoScale(AvatarArmBasis.ForwardWorld).GetSafeNormal();
	if (AvatarRightComp.IsNearlyZero())
	{
		AvatarRightComp = bUseTargetFaceForwardAxis ? -FVector::ForwardVector : FVector::RightVector;
	}
	if (AvatarUpComp.IsNearlyZero())
	{
		AvatarUpComp = FVector::UpVector;
	}
	if (AvatarForwardComp.IsNearlyZero())
	{
		AvatarForwardComp = bUseTargetFaceForwardAxis ? FVector::RightVector : FVector::ForwardVector;
	}

	const FVector HipRightComp = bHasTorsoBasis ? TargetCompTransform.InverseTransformVectorNoScale(HipRightWorld).GetSafeNormal() : AvatarRightComp;
	const FVector ShoulderRightComp = bHasTorsoBasis ? TargetCompTransform.InverseTransformVectorNoScale(ShoulderRightWorld).GetSafeNormal() : AvatarRightComp;
	FVector UpComp = bHasTorsoBasis ? TargetCompTransform.InverseTransformVectorNoScale(UpWorld).GetSafeNormal() : AvatarUpComp;
	FVector ForwardComp = bHasTorsoBasis ? TargetCompTransform.InverseTransformVectorNoScale(ForwardWorld).GetSafeNormal() : AvatarForwardComp;
	if (UpComp.IsNearlyZero())
	{
		UpComp = AvatarUpComp.IsNearlyZero() ? FVector::UpVector : AvatarUpComp;
	}
	if (ForwardComp.IsNearlyZero())
	{
		ForwardComp = AvatarForwardComp.IsNearlyZero()
			? (bUseTargetFaceForwardAxis ? FVector::RightVector : FVector::ForwardVector)
			: AvatarForwardComp;
	}

	FMediaPipeQuestArmPoseWriteInput ArmPoseWriteInput;
	ArmPoseWriteInput.bUseHmdRelativeAvatarArmFrame = bUseHmdRelativeAvatarArmFrame;
	ArmPoseWriteInput.bQuestWristPositionApplied = bQuestWristPositionApplied;
	const bool bFrameCoherentQuestArmPoseWrite =
		bUseBodyFusionArm ||
		bMetaHumanFullArmChainDirectFresh ||
		FMediaPipeQuestWristApplyPolicy::ShouldWriteFrameCoherentQuestArmPose(ArmPoseWriteInput);
	const float EffectiveArmTargetHalfLifeSeconds = bFrameCoherentQuestArmPoseWrite
		? 0.0f
		: ArmIKTargetHalfLifeSeconds;
	const float EffectiveArmRotationHalfLifeSeconds = bFrameCoherentQuestArmPoseWrite
		? 0.0f
		: ArmIKRotationHalfLifeSeconds;

	const float ArmFixedMaxStepDegrees = FMath::Max(0.0f, CVarMediaPipeArmRotationMaxStepDegrees.GetValueOnAnyThread());
	const float ArmSpeedMaxStepDegrees = (DeltaSeconds > 0.0f)
		? FMath::Max(0.0f, CVarMediaPipeArmRotationMaxSpeedDegreesPerSecond.GetValueOnAnyThread()) * DeltaSeconds
		: 0.0f;
	float ArmMaxStepDegrees = bFrameCoherentQuestArmPoseWrite ? 0.0f : ArmFixedMaxStepDegrees;
	if (!bFrameCoherentQuestArmPoseWrite && ArmSpeedMaxStepDegrees > 0.0f)
	{
		ArmMaxStepDegrees = ArmMaxStepDegrees > 0.0f
			? FMath::Min(ArmMaxStepDegrees, ArmSpeedMaxStepDegrees)
			: ArmSpeedMaxStepDegrees;
	}

	const bool bHasRefClav = bIsLeft ? bHasRefClavL : bHasRefClavR;
	double& LastClavicleDiagnosticLogTimeSeconds = bIsLeft
		? DiagnosticsState.LastClavicleDiagnosticLogTimeSecondsL
		: DiagnosticsState.LastClavicleDiagnosticLogTimeSecondsR;
	const TCHAR* ClavicleSideLabel = bIsLeft ? TEXT("L") : TEXT("R");
	const bool bBodyFusionMetaHumanClavicleWrite =
		bUseBodyFusionArm &&
		TargetMetaHumanProfile.IsValidForPoseDriving();
	const bool bDriveMainClavicle =
		bDriveClavicles ||
		bBodyFusionMetaHumanClavicleWrite;
	if (!bDriveMainClavicle)
	{
		if (bIsLeft)
		{
			LeftArmState.bHasSmoothedClavRotCS = false;
		}
		else
		{
			RightArmState.bHasSmoothedClavRotCS = false;
		}
		if (CVarMediaPipeTorsoDebug.GetValueOnAnyThread() != 0)
		{
			const double NowSeconds = FPlatformTime::Seconds();
			if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, LastClavicleDiagnosticLogTimeSeconds))
			{
				UE_LOG(LogMediaPipePose, Log,
					TEXT("mp.ClavicleDebug: actor=%s side=%s enabled=0 hasRef=%d reason=DriveClaviclesOff"),
					*TargetActorName.ToString(),
					ClavicleSideLabel,
					bHasRefClav ? 1 : 0);
			}
		}
	}
	else if (bHasRefClav)
	{
		const FBoneReference& ClavBone = bIsLeft ? ClavicleL : ClavicleR;
		const FVector& RefClavDir = bIsLeft ? RefClavDirCompL : RefClavDirCompR;
		const FQuat& RefClavComp = bIsLeft ? RefClavCompL : RefClavCompR;

		if (ClavBone.IsValidToEvaluate())
		{
			const FVector RightComp = !HipRightComp.IsNearlyZero() ? HipRightComp : ShoulderRightComp;
			FVector Outward = bIsLeft ? -RightComp : RightComp;
			if (Outward.IsNearlyZero())
			{
				Outward = RefClavDir;
			}
			Outward = (Outward - FVector::DotProduct(Outward, UpComp) * UpComp).GetSafeNormal();
			Outward = (Outward - FVector::DotProduct(Outward, ForwardComp) * ForwardComp).GetSafeNormal();
			if (Outward.IsNearlyZero())
			{
				Outward = RefClavDir;
			}

			if (bBodyFusionMetaHumanClavicleWrite)
			{
				const FVector TargetShoulderComp = TargetCompTransform.InverseTransformPosition(ShoulderWorld);
				const FVector ClavicleBaseComp =
					CSPose.GetComponentSpaceTransform(ClavBone.CachedCompactPoseIndex).GetTranslation();
				FVector DesiredClavDir = (TargetShoulderComp - ClavicleBaseComp).GetSafeNormal();
				if (DesiredClavDir.IsNearlyZero())
				{
					DesiredClavDir = Outward;
				}
				if (DesiredClavDir.IsNearlyZero())
				{
					DesiredClavDir = RefClavDir;
				}

				const FQuat TargetClavRotCS =
					(FQuat::FindBetweenNormals(RefClavDir, DesiredClavDir) * RefClavComp).GetNormalized();
				const float ClavAlpha =
					HalfLifeToAlpha(EffectiveArmRotationHalfLifeSeconds, DeltaSeconds) * ArmObservationAlphaScale;
				bool& bHasSmoothedClavRot = bIsLeft ? LeftArmState.bHasSmoothedClavRotCS : RightArmState.bHasSmoothedClavRotCS;
				FQuat& SmoothedClavRotCS = bIsLeft ? LeftArmState.SmoothedClavRotCS : RightArmState.SmoothedClavRotCS;
				UpdateSmoothedRotation(bHasSmoothedClavRot, SmoothedClavRotCS, TargetClavRotCS, ClavAlpha, ArmMaxStepDegrees);
				ApplyRotationCS(CSPose, ClavBone, SmoothedClavRotCS);

				float AppliedShoulderTargetDeltaCm = 0.0f;
				if (UpperBone.IsValidToEvaluate() && !TargetShoulderComp.ContainsNaN())
				{
					const FVector UpperAfterComp =
						CSPose.GetComponentSpaceTransform(UpperBone.CachedCompactPoseIndex).GetTranslation();
					AppliedShoulderTargetDeltaCm = FVector::Dist(UpperAfterComp, TargetShoulderComp);
				}

				LatestSignalSnapshot.bValid = true;
				LatestSignalSnapshot.RuntimeStateKey = RuntimeStateKey;
				LatestSignalSnapshot.PoseTimestampUs = BodyFusionFrame.SourceFrame.BodyPoseTimestampSeconds >= 0.0
					? static_cast<int64>(BodyFusionFrame.SourceFrame.BodyPoseTimestampSeconds * 1000000.0)
					: (bHasPoseFrame ? PoseFrame.TimestampUs : -1);
				FMediaPipePoseDrivenShoulderSignalSnapshot& ShoulderSnapshot =
					bIsLeft ? LatestSignalSnapshot.LeftShoulder : LatestSignalSnapshot.RightShoulder;
				ShoulderSnapshot.bValid = true;
				ShoulderSnapshot.ShoulderRelativeLiftCm =
					FVector::DotProduct(TargetShoulderComp - ClavicleBaseComp, UpComp);
				ShoulderSnapshot.ComputedShrugWeight = 1.0f;
				ShoulderSnapshot.SmoothedShrugWeight = 1.0f;
				ShoulderSnapshot.AppliedClavicleLiftCm = 0.0f;
				ShoulderSnapshot.AppliedUpperLiftCm = AppliedShoulderTargetDeltaCm;
				ShoulderSnapshot.UpWeight = FVector::DotProduct(DesiredClavDir, UpComp);
				ShoulderSnapshot.ForwardWeight = FVector::DotProduct(DesiredClavDir, ForwardComp);

				if (CVarMediaPipeTorsoDebug.GetValueOnAnyThread() != 0)
				{
					const double NowSeconds = FPlatformTime::Seconds();
					if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, LastClavicleDiagnosticLogTimeSeconds))
					{
						UE_LOG(LogMediaPipePose, Log,
							TEXT("mp.ClavicleDebug: actor=%s side=%s enabled=1 valid=1 runtimeKey=%u mode=BodyFusionMetaHuman targetShoulderComp=%s clavicleBaseComp=%s desiredDir=%s shoulderTargetResidualCm=%.1f appliedDeg=%.1f alpha=%.2f helpers=BodyFusionOwned"),
							*TargetActorName.ToString(),
							ClavicleSideLabel,
							RuntimeStateKey,
							*TargetShoulderComp.ToCompactString(),
							*ClavicleBaseComp.ToCompactString(),
							*DesiredClavDir.ToCompactString(),
							AppliedShoulderTargetDeltaCm,
							QuatAngularDistanceDegrees(SmoothedClavRotCS, RefClavComp),
							ClavAlpha);
					}
				}
			}
			else
			{
			const float ArmUp = FMath::Clamp(FVector::DotProduct(PoseUpperComp, UpComp), -1.0f, 1.0f);
			const float ArmForward = FMath::Clamp(FVector::DotProduct(PoseUpperComp, ForwardComp), -1.0f, 1.0f);
			float ShoulderShrugCm = 0.0f;
			float ShoulderShrugW = 0.0f;
			float ShoulderRawShrugW = 0.0f;
			float ShoulderLiftTranslationCm = 0.0f;
			float ShoulderSignedLiftCm = 0.0f;
			float ShoulderSignedLiftW = 0.0f;
			float ShoulderRelativeLiftCm = 0.0f;
			float ShoulderRelativeLiftW = 0.0f;
			float ShoulderScreenLift = 0.0f;
			float ShoulderScreenLiftW = 0.0f;
			float ShoulderPositiveLiftEvidenceCm = 0.0f;
			float ShoulderHeadClearanceCm = 0.0f;
			float ShoulderHeadClearanceShrugCm = 0.0f;
			float ShoulderHeadClearanceShrugCandidateCm = 0.0f;
			float BilateralShoulderHeadClearanceCm = 0.0f;
			float BilateralShoulderHeadClearanceReferenceCm = BodyState.BilateralShoulderHeadClearanceReferenceCm;
			float BilateralShoulderHeadClearanceShrugCm = 0.0f;
			float RigShoulderWidthForClearanceCm = 0.0f;
			float AppliedClavicleLiftCm = 0.0f;
			float AppliedUpperLiftCm = 0.0f;
			int32 SharedClearanceMode = 0;
			int32 SharedClearanceHadReference = 0;
			int32 SharedClearanceInitializedReference = 0;
			float SharedClearanceReferenceBeforeCm = 0.0f;
			bool bAppliedHeadClearanceShrug = false;
			const float ClavicleShrugWeight = FMath::Clamp(CVarMediaPipeClavicleShrugWeight.GetValueOnAnyThread(), 0.0f, 2.0f);
			const float ShrugStartCm = FMath::Max(0.0f, CVarMediaPipeClavicleShrugMinCm.GetValueOnAnyThread());
			const float ShrugFullCm = FMath::Max(
				ShrugStartCm + 0.5f,
				CVarMediaPipeClavicleShrugFullCm.GetValueOnAnyThread());
			const bool bUseHolisticShoulderSolve = CVarMediaPipeHolisticShoulderSolve.GetValueOnAnyThread() != 0;
			auto TryGetNormalizedXY = [&](const int32 LmIdx, FVector2D& Out) -> bool
			{
				if (LmIdx < 0 || !bHasPoseFrame || !IsMeasured(LmIdx) || !PoseFrame.Normalized.IsValidIndex(LmIdx))
				{
					return false;
				}
				const FMediaPipePoseLandmark& Lm = PoseFrame.Normalized.Points[LmIdx];
				if (!FMath::IsFinite(Lm.X) || !FMath::IsFinite(Lm.Y))
				{
					return false;
				}
				Out = FVector2D(Lm.X, Lm.Y);
				return true;
			};
			auto IsNormalizedPointInFrame = [](const FVector2D& Point) -> bool
			{
				return Point.X >= 0.0f && Point.X <= 1.0f && Point.Y >= 0.0f && Point.Y <= 1.0f;
			};
			auto TryGetReliableNormalizedXY = [&](const int32 LmIdx, const float MinReliability, FVector2D& Out) -> bool
			{
				if (GetLandmarkReliability(LmIdx) < MinReliability || !TryGetNormalizedXY(LmIdx, Out))
				{
					return false;
				}
				return IsNormalizedPointInFrame(Out);
			};
			constexpr float MinLiveShrugShoulderReliability = 0.30f;
			constexpr float MinLiveShrugHeadReliability = 0.20f;
			FVector2D ReliableLeftShoulder2D = FVector2D::ZeroVector;
			FVector2D ReliableRightShoulder2D = FVector2D::ZeroVector;
			const bool bHasReliableShrugShoulders2D =
				TryGetReliableNormalizedXY((int32)EMediaPipePoseLandmark::LeftShoulder, MinLiveShrugShoulderReliability, ReliableLeftShoulder2D) &&
				TryGetReliableNormalizedXY((int32)EMediaPipePoseLandmark::RightShoulder, MinLiveShrugShoulderReliability, ReliableRightShoulder2D);
			if (ClavicleShrugWeight > KINDA_SMALL_NUMBER && bHasTorsoBasis)
			{
				FVector LeftHipWorld = FVector::ZeroVector;
				FVector RightHipWorld = FVector::ZeroVector;
				const FVector UpWorldSafe = UpWorld.GetSafeNormal();
				struct FShoulderClearance2DResult
				{
					bool bValid = false;
					float LeftClearanceCm = 0.0f;
					float RightClearanceCm = 0.0f;
					float BilateralClearanceCm = 0.0f;
					float BilateralReferenceCm = 0.0f;
					float BilateralShrugCm = 0.0f;
					float LeftAsymCm = 0.0f;
					float RightAsymCm = 0.0f;
					int32 SourceMode = 0;
				};
				auto ResolveRigShoulderWidthCm = [&](const float SourceShoulderWidthCm) -> float
				{
					float RigShoulderWidthCm = SourceShoulderWidthCm;
					if (UpperArmL.IsValidToEvaluate() && UpperArmR.IsValidToEvaluate())
					{
						const FVector LeftUpperComp =
							CSPose.GetComponentSpaceTransform(UpperArmL.CachedCompactPoseIndex).GetTranslation();
						const FVector RightUpperComp =
							CSPose.GetComponentSpaceTransform(UpperArmR.CachedCompactPoseIndex).GetTranslation();
						const FVector ShoulderRightSafe = ShoulderRightComp.GetSafeNormal();
						const float AcrossShoulderCm = !ShoulderRightSafe.IsNearlyZero()
							? FMath::Abs(FVector::DotProduct(RightUpperComp - LeftUpperComp, ShoulderRightSafe))
							: FVector::Dist(LeftUpperComp, RightUpperComp);
						if (AcrossShoulderCm > 5.0f)
						{
							RigShoulderWidthCm = AcrossShoulderCm;
						}
					}
					return RigShoulderWidthCm;
				};
				auto TryGetDenseFacePoint2D = [&](const int32 FaceIndex, FVector2D& OutPoint) -> bool
				{
					if (!bUseHolisticShoulderSolve ||
						FaceIndex < 0 ||
						!PoseFrame.bHasFace ||
						PoseFrame.Face.bHasFace == 0 ||
						FaceIndex >= PoseFrame.Face.Normalized.Count ||
						FaceIndex >= MediaPipeFaceLandmarkMaxCount)
					{
						return false;
					}

					const FMediaPipeRawHandLandmark& FacePoint = PoseFrame.Face.Normalized.Landmarks[FaceIndex];
					if (!FMath::IsFinite(FacePoint.X) || !FMath::IsFinite(FacePoint.Y))
					{
						return false;
					}

					OutPoint = FVector2D(FacePoint.X, FacePoint.Y);
					return true;
				};
				auto TryGetPairedHeadSidePoints2D = [&](FVector2D& OutLeftHeadSide2D, FVector2D& OutRightHeadSide2D) -> bool
				{
					FVector2D Left2D = FVector2D::ZeroVector;
					FVector2D Right2D = FVector2D::ZeroVector;
					if (TryGetDenseFacePoint2D(33, Left2D) && TryGetDenseFacePoint2D(263, Right2D))
					{
						OutLeftHeadSide2D = Left2D;
						OutRightHeadSide2D = Right2D;
						return true;
					}
					if (TryGetReliableNormalizedXY((int32)EMediaPipePoseLandmark::LeftEar, MinLiveShrugHeadReliability, Left2D) &&
						TryGetReliableNormalizedXY((int32)EMediaPipePoseLandmark::RightEar, MinLiveShrugHeadReliability, Right2D))
					{
						OutLeftHeadSide2D = Left2D;
						OutRightHeadSide2D = Right2D;
						return true;
					}
					if (TryGetReliableNormalizedXY((int32)EMediaPipePoseLandmark::LeftEye, MinLiveShrugHeadReliability, Left2D) &&
						TryGetReliableNormalizedXY((int32)EMediaPipePoseLandmark::RightEye, MinLiveShrugHeadReliability, Right2D))
					{
						OutLeftHeadSide2D = Left2D;
						OutRightHeadSide2D = Right2D;
						return true;
					}
					if (TryGetReliableNormalizedXY((int32)EMediaPipePoseLandmark::Nose, MinLiveShrugHeadReliability, Left2D))
					{
						OutLeftHeadSide2D = Left2D;
						OutRightHeadSide2D = Left2D;
						return true;
					}
					return false;
				};
				auto TryGetPairedHeadSidePointsWorld = [&](FVector& OutLeftHeadSideWorld, FVector& OutRightHeadSideWorld) -> bool
				{
					FVector LeftWorld = FVector::ZeroVector;
					FVector RightWorld = FVector::ZeroVector;
					if (GetLandmarkReliability((int32)EMediaPipePoseLandmark::LeftEar) >= MinLiveShrugHeadReliability &&
						GetLandmarkReliability((int32)EMediaPipePoseLandmark::RightEar) >= MinLiveShrugHeadReliability &&
						TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftEar, LeftWorld) &&
						TryGetLmWorld((int32)EMediaPipePoseLandmark::RightEar, RightWorld))
					{
						OutLeftHeadSideWorld = LeftWorld;
						OutRightHeadSideWorld = RightWorld;
						return true;
					}
					if (GetLandmarkReliability((int32)EMediaPipePoseLandmark::LeftEye) >= MinLiveShrugHeadReliability &&
						GetLandmarkReliability((int32)EMediaPipePoseLandmark::RightEye) >= MinLiveShrugHeadReliability &&
						TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftEye, LeftWorld) &&
						TryGetLmWorld((int32)EMediaPipePoseLandmark::RightEye, RightWorld))
					{
						OutLeftHeadSideWorld = LeftWorld;
						OutRightHeadSideWorld = RightWorld;
						return true;
					}
					if (GetLandmarkReliability((int32)EMediaPipePoseLandmark::Nose) >= MinLiveShrugHeadReliability &&
						TryGetLmWorld((int32)EMediaPipePoseLandmark::Nose, LeftWorld))
					{
						OutLeftHeadSideWorld = LeftWorld;
						OutRightHeadSideWorld = LeftWorld;
						return true;
					}
					return false;
				};
				FShoulderClearance2DResult Clearance2D;
				auto UpdateSharedClearance = [&](
					const float LeftClearanceCm,
					const float RightClearanceCm,
					const float BilateralClearanceCm,
					const int32 SourceMode) -> void
				{
					if (BilateralClearanceCm <= KINDA_SMALL_NUMBER)
					{
						return;
					}

					FDerivedSignalRuntimeState& DerivedSignals = GetDerivedSignalRuntimeState(RuntimeStateKey);
					SharedClearanceHadReference = DerivedSignals.bHasBilateralShoulderHeadClearanceReference ? 1 : 0;
					SharedClearanceReferenceBeforeCm = DerivedSignals.BilateralShoulderHeadClearanceReferenceCm;
					float BilateralReferenceForShrugCm = BilateralClearanceCm;
					if (!DerivedSignals.bHasBilateralShoulderHeadClearanceReference ||
						DerivedSignals.BilateralShoulderHeadClearanceReferenceCm <= KINDA_SMALL_NUMBER)
					{
						SharedClearanceInitializedReference = 1;
						DerivedSignals.bHasBilateralShoulderHeadClearanceReference = true;
						DerivedSignals.BilateralShoulderHeadClearanceReferenceCm = BilateralClearanceCm;
						DerivedSignals.LastBilateralShoulderHeadClearanceReferencePoseTimestampUs = PoseFrame.TimestampUs;
					}
					else
					{
						BilateralReferenceForShrugCm = DerivedSignals.BilateralShoulderHeadClearanceReferenceCm;
					}

					Clearance2D.bValid = true;
					Clearance2D.LeftClearanceCm = LeftClearanceCm;
					Clearance2D.RightClearanceCm = RightClearanceCm;
					Clearance2D.BilateralClearanceCm = BilateralClearanceCm;
					Clearance2D.BilateralReferenceCm = BilateralReferenceForShrugCm;
					Clearance2D.BilateralShrugCm = FMath::Max(
						0.0f,
						BilateralReferenceForShrugCm - BilateralClearanceCm);
					Clearance2D.LeftAsymCm = RightClearanceCm - LeftClearanceCm;
					Clearance2D.RightAsymCm = LeftClearanceCm - RightClearanceCm;
					Clearance2D.SourceMode = SourceMode;

					if (DerivedSignals.LastBilateralShoulderHeadClearanceReferencePoseTimestampUs != PoseFrame.TimestampUs)
					{
						const float ClearanceReferenceHalfLifeSeconds =
							BilateralClearanceCm > DerivedSignals.BilateralShoulderHeadClearanceReferenceCm ? 0.20f : 8.00f;
						const float ClearanceReferenceAlpha = HalfLifeToAlpha(ClearanceReferenceHalfLifeSeconds, DeltaSeconds);
						DerivedSignals.BilateralShoulderHeadClearanceReferenceCm = FMath::Lerp(
							DerivedSignals.BilateralShoulderHeadClearanceReferenceCm,
							BilateralClearanceCm,
							ClearanceReferenceAlpha);
						DerivedSignals.LastBilateralShoulderHeadClearanceReferencePoseTimestampUs = PoseFrame.TimestampUs;
					}
					BodyState.bHasBilateralShoulderHeadClearanceReference =
						DerivedSignals.bHasBilateralShoulderHeadClearanceReference;
					BodyState.BilateralShoulderHeadClearanceReferenceCm =
						DerivedSignals.BilateralShoulderHeadClearanceReferenceCm;
					BodyState.LastBilateralShoulderHeadClearanceReferencePoseTimestampUs =
						DerivedSignals.LastBilateralShoulderHeadClearanceReferencePoseTimestampUs;
				};
				FVector LeftShoulderWorldForClearance = FVector::ZeroVector;
				FVector RightShoulderWorldForClearance = FVector::ZeroVector;
				FVector LeftHeadSideWorld = FVector::ZeroVector;
				FVector RightHeadSideWorld = FVector::ZeroVector;
				if (!UpWorldSafe.IsNearlyZero() &&
					bHasReliableShrugShoulders2D &&
					TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftShoulder, LeftShoulderWorldForClearance) &&
					TryGetLmWorld((int32)EMediaPipePoseLandmark::RightShoulder, RightShoulderWorldForClearance) &&
					TryGetPairedHeadSidePointsWorld(LeftHeadSideWorld, RightHeadSideWorld))
				{
					const float SourceShoulderWidthCm = FVector::Dist(LeftShoulderWorldForClearance, RightShoulderWorldForClearance);
					const float RigShoulderWidthCm = ResolveRigShoulderWidthCm(SourceShoulderWidthCm);
					RigShoulderWidthForClearanceCm = RigShoulderWidthCm;
					if (SourceShoulderWidthCm > KINDA_SMALL_NUMBER && RigShoulderWidthCm > KINDA_SMALL_NUMBER)
					{
						const float SourceToRigScale = RigShoulderWidthCm / SourceShoulderWidthCm;
						const float LeftClearanceCm =
							FVector::DotProduct(LeftHeadSideWorld - LeftShoulderWorldForClearance, UpWorldSafe) * SourceToRigScale;
						const float RightClearanceCm =
							FVector::DotProduct(RightHeadSideWorld - RightShoulderWorldForClearance, UpWorldSafe) * SourceToRigScale;
						UpdateSharedClearance(
							LeftClearanceCm,
							RightClearanceCm,
							(LeftClearanceCm + RightClearanceCm) * 0.5f,
							3);
					}
				}
				FVector2D LeftShoulder2D = FVector2D::ZeroVector;
				FVector2D RightShoulder2D = FVector2D::ZeroVector;
				FVector2D LeftHeadSide2D = FVector2D::ZeroVector;
				FVector2D RightHeadSide2D = FVector2D::ZeroVector;
				if (!Clearance2D.bValid &&
					bHasReliableShrugShoulders2D &&
					TryGetPairedHeadSidePoints2D(LeftHeadSide2D, RightHeadSide2D))
				{
					LeftShoulder2D = ReliableLeftShoulder2D;
					RightShoulder2D = ReliableRightShoulder2D;
					FVector LeftShoulderWorldForWidth = FVector::ZeroVector;
					FVector RightShoulderWorldForWidth = FVector::ZeroVector;
					float SourceShoulderWidthCm = 0.0f;
					if (TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftShoulder, LeftShoulderWorldForWidth) &&
						TryGetLmWorld((int32)EMediaPipePoseLandmark::RightShoulder, RightShoulderWorldForWidth))
					{
						SourceShoulderWidthCm = FVector::Dist(LeftShoulderWorldForWidth, RightShoulderWorldForWidth);
					}
					const float RigShoulderWidthCm = ResolveRigShoulderWidthCm(SourceShoulderWidthCm);
					RigShoulderWidthForClearanceCm = RigShoulderWidthCm;
					const float ShoulderSpan2D = FMath::Max(FVector2D::Distance(LeftShoulder2D, RightShoulder2D), 0.05f);
					if (RigShoulderWidthCm > KINDA_SMALL_NUMBER)
					{
						const float LeftClearanceCm =
							((LeftShoulder2D.Y - LeftHeadSide2D.Y) / ShoulderSpan2D) * RigShoulderWidthCm;
						const float RightClearanceCm =
							((RightShoulder2D.Y - RightHeadSide2D.Y) / ShoulderSpan2D) * RigShoulderWidthCm;
						const float BilateralClearanceCm = (LeftClearanceCm + RightClearanceCm) * 0.5f;
						UpdateSharedClearance(LeftClearanceCm, RightClearanceCm, BilateralClearanceCm, 2);
					}
				}
				if (Clearance2D.bValid)
				{
					const float BilateralShrugCm = Clearance2D.BilateralShrugCm;
					ShoulderHeadClearanceCm = bIsLeft ? Clearance2D.LeftClearanceCm : Clearance2D.RightClearanceCm;
					ShoulderHeadClearanceShrugCm = BilateralShrugCm;
					ShoulderShrugCm = FMath::Max(ShoulderShrugCm, BilateralShrugCm);
					ShoulderShrugW +=
						RemapPositiveUnbounded(BilateralShrugCm, ShrugStartCm, ShrugFullCm) *
						ClavicleShrugWeight *
						0.35f;
					ShoulderLiftTranslationCm += BilateralShrugCm * 0.35f;
					BilateralShoulderHeadClearanceCm = Clearance2D.BilateralClearanceCm;
					BilateralShoulderHeadClearanceReferenceCm = Clearance2D.BilateralReferenceCm;
					BilateralShoulderHeadClearanceShrugCm = BilateralShrugCm;
					SharedClearanceMode = Clearance2D.SourceMode;
					bAppliedHeadClearanceShrug = true;
				}
				const float LegacyShoulderDriverScale = 1.0f;
				if (!UpWorldSafe.IsNearlyZero() &&
					bHasReliableShrugShoulders2D &&
					GetLandmarkReliability((int32)EMediaPipePoseLandmark::LeftHip) >= MinLiveShrugShoulderReliability &&
					GetLandmarkReliability((int32)EMediaPipePoseLandmark::RightHip) >= MinLiveShrugShoulderReliability &&
					TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftHip, LeftHipWorld) &&
					TryGetLmWorld((int32)EMediaPipePoseLandmark::RightHip, RightHipWorld))
				{
					const FVector HipMidWorld = (LeftHipWorld + RightHipWorld) * 0.5f;
					// Shrug signal source (2026-07-06): by this point ShoulderWorld has been
					// reassigned to the ARM CHAIN's shoulder (line ~487), which is synthesized
					// from the HMD and cannot see shrugs (1.5cm amplitude measured vs the
					// camera's 7.7). The camera's own measurement was preserved at line ~292;
					// the shrug height must come from it - it is the only source that sees
					// the shoulders rise.
					const FVector ShrugShoulderWorld =
						(CVarMediaPipeClavicleShrugDirect.GetValueOnAnyThread() != 0 &&
							!MediaPipeSourceShoulderWorld.IsNearlyZero())
						? MediaPipeSourceShoulderWorld
						: ShoulderWorld;
					const float ShoulderHeightCm = FVector::DotProduct(ShrugShoulderWorld - HipMidWorld, UpWorldSafe);
					if (ShoulderHeightCm > KINDA_SMALL_NUMBER)
					{
						FMediaPipeArmSolverState& ClavicleArmState = bIsLeft ? LeftArmState : RightArmState;
						if (!ClavicleArmState.bHasShoulderHeightReference || ClavicleArmState.ShoulderHeightReferenceCm <= KINDA_SMALL_NUMBER)
						{
							ClavicleArmState.bHasShoulderHeightReference = true;
							ClavicleArmState.ShoulderHeightReferenceCm = ShoulderHeightCm;
						}

						// Asymmetric rest reference (shrug redesign 2026-07-06): the symmetric 8s
						// lowpass absorbed HELD shrugs into the baseline - the signal decayed while
						// the wearer was still shrugging (referee: fused shrug 1.5-2cm vs Epic
						// 11.5). Shoulders REST at their minimum height; shrugs only go up. Adapt
						// DOWN fast (posture drop, sitting) and UP over minutes, so the reference
						// tracks true rest and a shrug can never eat its own baseline.
						const bool bDirectShrug = CVarMediaPipeClavicleShrugDirect.GetValueOnAnyThread() != 0;
						const float ReferenceHalfLifeSeconds = bDirectShrug
							? (ShoulderHeightCm < ClavicleArmState.ShoulderHeightReferenceCm ? 0.7f : 90.0f)
							: 8.0f;
						const float ReferenceAlpha = HalfLifeToAlpha(ReferenceHalfLifeSeconds, DeltaSeconds);
						ClavicleArmState.ShoulderHeightReferenceCm = FMath::Lerp(
							ClavicleArmState.ShoulderHeightReferenceCm,
							ShoulderHeightCm,
							ReferenceAlpha);

						ShoulderSignedLiftCm = ShoulderHeightCm - ClavicleArmState.ShoulderHeightReferenceCm;
						ShoulderPositiveLiftEvidenceCm = FMath::Max(ShoulderPositiveLiftEvidenceCm, ShoulderSignedLiftCm);
						ShoulderSignedLiftW =
							RemapSignedUnbounded(ShoulderSignedLiftCm, ShrugStartCm, ShrugFullCm) *
							ClavicleShrugWeight *
							0.30f *
							LegacyShoulderDriverScale;
						ShoulderShrugCm = FMath::Max(ShoulderShrugCm, FMath::Abs(ShoulderSignedLiftCm) * LegacyShoulderDriverScale);
						ShoulderShrugW += ShoulderSignedLiftW;
						ShoulderLiftTranslationCm += ShoulderSignedLiftCm * 0.28f * LegacyShoulderDriverScale;
					}

					const int32 OppositeShoulderLm = bIsLeft
						? (int32)EMediaPipePoseLandmark::RightShoulder
						: (int32)EMediaPipePoseLandmark::LeftShoulder;
					const int32 EarLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftEar : (int32)EMediaPipePoseLandmark::RightEar;
					const int32 EyeLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftEye : (int32)EMediaPipePoseLandmark::RightEye;
					FVector OppositeShoulderWorld = FVector::ZeroVector;
					if (bHasReliableShrugShoulders2D && TryGetLmWorld(OppositeShoulderLm, OppositeShoulderWorld))
					{
						const float ShoulderWidthCm = FVector::Dist(ShoulderWorld, OppositeShoulderWorld);
						const float RelativeLiftFullCm = FMath::Max(
							2.5f,
							FMath::Min(6.0f, ShoulderWidthCm * 0.14f));
						const float RelativeLiftStartCm = FMath::Max(1.0f, ShrugStartCm * 0.5f);
						const float SignedRelativeLiftCm = FVector::DotProduct(ShoulderWorld - OppositeShoulderWorld, UpWorldSafe);
						ShoulderRelativeLiftCm = SignedRelativeLiftCm;
						ShoulderRelativeLiftW =
							RemapSignedUnbounded(ShoulderRelativeLiftCm, RelativeLiftStartCm, RelativeLiftFullCm) *
							ClavicleShrugWeight *
							0.12f *
							LegacyShoulderDriverScale;
						ShoulderShrugCm = FMath::Max(ShoulderShrugCm, FMath::Abs(ShoulderRelativeLiftCm) * LegacyShoulderDriverScale);
						ShoulderShrugW += ShoulderRelativeLiftW;

						FVector2D Shoulder2D = FVector2D::ZeroVector;
						FVector2D OppositeShoulder2D = FVector2D::ZeroVector;
						FVector2D LeftHip2D = FVector2D::ZeroVector;
						FVector2D RightHip2D = FVector2D::ZeroVector;
						if (TryGetReliableNormalizedXY(ShoulderLm, MinLiveShrugShoulderReliability, Shoulder2D) &&
							TryGetReliableNormalizedXY(OppositeShoulderLm, MinLiveShrugShoulderReliability, OppositeShoulder2D) &&
							TryGetReliableNormalizedXY((int32)EMediaPipePoseLandmark::LeftHip, MinLiveShrugShoulderReliability, LeftHip2D) &&
							TryGetReliableNormalizedXY((int32)EMediaPipePoseLandmark::RightHip, MinLiveShrugShoulderReliability, RightHip2D))
						{
							const float ShoulderSpan2D = FMath::Max(FVector2D::Distance(Shoulder2D, OppositeShoulder2D), 0.05f);
							const FVector2D HipMid2D = (LeftHip2D + RightHip2D) * 0.5f;
							const float ShoulderHeight2D = (HipMid2D.Y - Shoulder2D.Y) / ShoulderSpan2D;
							FMediaPipeArmSolverState& ClavicleArmState = bIsLeft ? LeftArmState : RightArmState;
							if (!ClavicleArmState.bHasShoulderScreenHeightReference)
							{
								ClavicleArmState.bHasShoulderScreenHeightReference = true;
								ClavicleArmState.ShoulderScreenHeightReference = ShoulderHeight2D;
							}

							const float ScreenReferenceHalfLifeSeconds = 10.0f;
							const float ScreenReferenceAlpha = HalfLifeToAlpha(ScreenReferenceHalfLifeSeconds, DeltaSeconds);
							ClavicleArmState.ShoulderScreenHeightReference = FMath::Lerp(
								ClavicleArmState.ShoulderScreenHeightReference,
								ShoulderHeight2D,
								ScreenReferenceAlpha);

							const float AbsoluteScreenLift = ShoulderHeight2D - ClavicleArmState.ShoulderScreenHeightReference;
							const float RelativeScreenLift = (OppositeShoulder2D.Y - Shoulder2D.Y) / ShoulderSpan2D;
							const float AbsoluteScreenLiftCm = AbsoluteScreenLift * ShoulderWidthCm;
							const float RelativeScreenLiftCm = RelativeScreenLift * ShoulderWidthCm;
							ShoulderPositiveLiftEvidenceCm = FMath::Max(ShoulderPositiveLiftEvidenceCm, AbsoluteScreenLiftCm);
							const float AbsoluteScreenLiftW =
								RemapSignedUnbounded(AbsoluteScreenLiftCm, ShrugStartCm, ShrugFullCm) *
								ClavicleShrugWeight;
							const float RelativeScreenLiftW =
								RemapSignedUnbounded(RelativeScreenLiftCm, ShrugStartCm, ShrugFullCm) *
								ClavicleShrugWeight;
							ShoulderScreenLift = AbsoluteScreenLift;
							ShoulderScreenLiftW = (AbsoluteScreenLiftW * 0.85f + RelativeScreenLiftW * 0.10f) * LegacyShoulderDriverScale;
							ShoulderShrugCm = FMath::Max(ShoulderShrugCm, FMath::Max(FMath::Abs(AbsoluteScreenLiftCm), FMath::Abs(RelativeScreenLiftCm)) * LegacyShoulderDriverScale);
							ShoulderShrugW += ShoulderScreenLiftW;
							ShoulderLiftTranslationCm += AbsoluteScreenLiftCm * 0.35f * LegacyShoulderDriverScale;

							if (Clearance2D.bValid)
							{
								ShoulderHeadClearanceCm = bIsLeft ? Clearance2D.LeftClearanceCm : Clearance2D.RightClearanceCm;
							}

							FVector2D HeadAnchor2D = FVector2D::ZeroVector;
							FVector2D LeftEar2D = FVector2D::ZeroVector;
							FVector2D RightEar2D = FVector2D::ZeroVector;
							FVector2D LeftEye2D = FVector2D::ZeroVector;
							FVector2D RightEye2D = FVector2D::ZeroVector;
							bool bHasHeadAnchor2D = false;
							if (TryGetDenseFacePoint2D(bIsLeft ? 33 : 263, HeadAnchor2D))
							{
								bHasHeadAnchor2D = true;
							}
							else if (TryGetReliableNormalizedXY((int32)EMediaPipePoseLandmark::LeftEar, MinLiveShrugHeadReliability, LeftEar2D) &&
								TryGetReliableNormalizedXY((int32)EMediaPipePoseLandmark::RightEar, MinLiveShrugHeadReliability, RightEar2D))
							{
								HeadAnchor2D = (LeftEar2D + RightEar2D) * 0.5f;
								bHasHeadAnchor2D = true;
							}
							else if (TryGetReliableNormalizedXY((int32)EMediaPipePoseLandmark::LeftEye, MinLiveShrugHeadReliability, LeftEye2D) &&
								TryGetReliableNormalizedXY((int32)EMediaPipePoseLandmark::RightEye, MinLiveShrugHeadReliability, RightEye2D))
							{
								HeadAnchor2D = (LeftEye2D + RightEye2D) * 0.5f;
								bHasHeadAnchor2D = true;
							}
							else if (TryGetReliableNormalizedXY(EarLm, MinLiveShrugHeadReliability, HeadAnchor2D) ||
								TryGetReliableNormalizedXY(EyeLm, MinLiveShrugHeadReliability, HeadAnchor2D) ||
								TryGetReliableNormalizedXY((int32)EMediaPipePoseLandmark::Nose, MinLiveShrugHeadReliability, HeadAnchor2D))
							{
								bHasHeadAnchor2D = true;
							}

							if (!bAppliedHeadClearanceShrug && bHasHeadAnchor2D)
							{
								FMediaPipeArmSolverState& ClearanceArmState = bIsLeft ? LeftArmState : RightArmState;
								const float ScreenHeadClearanceCm = ((Shoulder2D.Y - HeadAnchor2D.Y) / ShoulderSpan2D) * ShoulderWidthCm;
								ShoulderHeadClearanceCm = ScreenHeadClearanceCm;
								if (ScreenHeadClearanceCm > KINDA_SMALL_NUMBER)
								{
									if (!ClearanceArmState.bHasShoulderHeadClearanceReference ||
										ClearanceArmState.ShoulderHeadClearanceReferenceCm <= KINDA_SMALL_NUMBER)
									{
										ClearanceArmState.bHasShoulderHeadClearanceReference = true;
										ClearanceArmState.ShoulderHeadClearanceReferenceCm = ScreenHeadClearanceCm;
									}

									const float ClearanceReferenceHalfLifeSeconds =
										ScreenHeadClearanceCm > ClearanceArmState.ShoulderHeadClearanceReferenceCm ? 0.25f : 8.0f;
									const float ClearanceReferenceAlpha = HalfLifeToAlpha(ClearanceReferenceHalfLifeSeconds, DeltaSeconds);
									ClearanceArmState.ShoulderHeadClearanceReferenceCm = FMath::Lerp(
										ClearanceArmState.ShoulderHeadClearanceReferenceCm,
										ScreenHeadClearanceCm,
										ClearanceReferenceAlpha);

									const float ClearanceShrugCm = FMath::Max(
										0.0f,
										ClearanceArmState.ShoulderHeadClearanceReferenceCm - ScreenHeadClearanceCm);
									ShoulderHeadClearanceShrugCm = ClearanceShrugCm;
									ShoulderHeadClearanceShrugCandidateCm = FMath::Max(ShoulderHeadClearanceShrugCandidateCm, ClearanceShrugCm);
									bAppliedHeadClearanceShrug = true;
								}
							}
						}
					}

					FVector HeadAnchorWorld = FVector::ZeroVector;
					FVector LeftEarWorldForClearance = FVector::ZeroVector;
					FVector RightEarWorldForClearance = FVector::ZeroVector;
					FVector LeftEyeWorldForClearance = FVector::ZeroVector;
					FVector RightEyeWorldForClearance = FVector::ZeroVector;
					if (GetLandmarkReliability((int32)EMediaPipePoseLandmark::LeftEar) >= MinLiveShrugHeadReliability &&
						GetLandmarkReliability((int32)EMediaPipePoseLandmark::RightEar) >= MinLiveShrugHeadReliability &&
						TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftEar, LeftEarWorldForClearance) &&
						TryGetLmWorld((int32)EMediaPipePoseLandmark::RightEar, RightEarWorldForClearance))
					{
						HeadAnchorWorld = (LeftEarWorldForClearance + RightEarWorldForClearance) * 0.5f;
					}
					else if (GetLandmarkReliability((int32)EMediaPipePoseLandmark::LeftEye) >= MinLiveShrugHeadReliability &&
						GetLandmarkReliability((int32)EMediaPipePoseLandmark::RightEye) >= MinLiveShrugHeadReliability &&
						TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftEye, LeftEyeWorldForClearance) &&
						TryGetLmWorld((int32)EMediaPipePoseLandmark::RightEye, RightEyeWorldForClearance))
					{
						HeadAnchorWorld = (LeftEyeWorldForClearance + RightEyeWorldForClearance) * 0.5f;
					}
					else if ((GetLandmarkReliability(EarLm) < MinLiveShrugHeadReliability || !TryGetLmWorld(EarLm, HeadAnchorWorld)) &&
						(GetLandmarkReliability(EyeLm) < MinLiveShrugHeadReliability || !TryGetLmWorld(EyeLm, HeadAnchorWorld)) &&
						(GetLandmarkReliability((int32)EMediaPipePoseLandmark::Nose) < MinLiveShrugHeadReliability ||
							!TryGetLmWorld((int32)EMediaPipePoseLandmark::Nose, HeadAnchorWorld)))
					{
						HeadAnchorWorld = FVector::ZeroVector;
					}

					if (!bAppliedHeadClearanceShrug && !HeadAnchorWorld.IsNearlyZero())
					{
						FMediaPipeArmSolverState& ClavicleArmState = bIsLeft ? LeftArmState : RightArmState;
						const float HeadClearanceCm = FVector::DotProduct(HeadAnchorWorld - ShoulderWorld, UpWorldSafe);
						ShoulderHeadClearanceCm = HeadClearanceCm;
						if (HeadClearanceCm > KINDA_SMALL_NUMBER)
						{
							if (!ClavicleArmState.bHasShoulderHeadClearanceReference ||
								ClavicleArmState.ShoulderHeadClearanceReferenceCm <= KINDA_SMALL_NUMBER)
							{
								ClavicleArmState.bHasShoulderHeadClearanceReference = true;
								ClavicleArmState.ShoulderHeadClearanceReferenceCm = HeadClearanceCm;
							}

							const float ClearanceReferenceHalfLifeSeconds =
								HeadClearanceCm > ClavicleArmState.ShoulderHeadClearanceReferenceCm ? 0.25f : 8.0f;
							const float ClearanceReferenceAlpha = HalfLifeToAlpha(ClearanceReferenceHalfLifeSeconds, DeltaSeconds);
							ClavicleArmState.ShoulderHeadClearanceReferenceCm = FMath::Lerp(
								ClavicleArmState.ShoulderHeadClearanceReferenceCm,
								HeadClearanceCm,
								ClearanceReferenceAlpha);

							const float ClearanceShrugCm = FMath::Max(
								0.0f,
								ClavicleArmState.ShoulderHeadClearanceReferenceCm - HeadClearanceCm);
							ShoulderHeadClearanceShrugCm = ClearanceShrugCm;
							ShoulderHeadClearanceShrugCandidateCm = FMath::Max(ShoulderHeadClearanceShrugCandidateCm, ClearanceShrugCm);
						}
					}
				}
			}

			if (ShoulderHeadClearanceShrugCandidateCm > KINDA_SMALL_NUMBER)
			{
				// A smaller head-to-shoulder clearance can also be a head nod. Require independent shoulder-up evidence.
				const float ShoulderIntentW = FMath::Clamp(
					RemapPositiveUnbounded(ShoulderPositiveLiftEvidenceCm, ShrugStartCm, ShrugFullCm),
					0.0f,
					1.0f);
				const float GatedClearanceShrugCm = ShoulderHeadClearanceShrugCandidateCm * ShoulderIntentW;
				ShoulderHeadClearanceShrugCm = GatedClearanceShrugCm;
				if (GatedClearanceShrugCm > KINDA_SMALL_NUMBER)
				{
					ShoulderShrugCm = FMath::Max(ShoulderShrugCm, GatedClearanceShrugCm);
					ShoulderShrugW +=
						RemapPositiveUnbounded(GatedClearanceShrugCm, ShrugStartCm, ShrugFullCm) *
						ClavicleShrugWeight *
						0.45f;
					ShoulderLiftTranslationCm += GatedClearanceShrugCm * 0.45f;
				}
			}

			const float ShoulderLiftTranslationScale = FMath::Clamp(
				CVarMediaPipeShoulderLiftTranslationScale.GetValueOnAnyThread(),
				0.0f,
				8.0f);
			ShoulderLiftTranslationCm = FMath::Clamp(ShoulderLiftTranslationCm * ShoulderLiftTranslationScale, -7.0f, 14.0f);
			ShoulderRawShrugW = ShoulderShrugW;
			FMediaPipeArmSolverState& ClavicleArmState = bIsLeft ? LeftArmState : RightArmState;
			const float ShrugWeightAlpha = HalfLifeToAlpha(
				ShoulderShrugW > ClavicleArmState.SmoothedClavicleShrugWeight ? 0.12f : 0.18f,
				DeltaSeconds);
			if (!ClavicleArmState.bHasSmoothedClavicleShrugWeight)
			{
				ClavicleArmState.bHasSmoothedClavicleShrugWeight = true;
				ClavicleArmState.SmoothedClavicleShrugWeight = ShoulderShrugW;
			}
			else
			{
				const float PreviousShrugWeight = ClavicleArmState.SmoothedClavicleShrugWeight;
				const float DesiredShrugWeight = FMath::Lerp(
					ClavicleArmState.SmoothedClavicleShrugWeight,
					ShoulderShrugW,
					ShrugWeightAlpha);
				const float MaxShrugStep = FMath::Max(0.015f, 1.4f * FMath::Max(DeltaSeconds, 0.0f));
				ClavicleArmState.SmoothedClavicleShrugWeight = FMath::Clamp(
					DesiredShrugWeight,
					PreviousShrugWeight - MaxShrugStep,
					PreviousShrugWeight + MaxShrugStep);
			}
			ShoulderShrugW = ClavicleArmState.SmoothedClavicleShrugWeight;

			const float LiftTranslationAlpha = HalfLifeToAlpha(
				FMath::Abs(ShoulderLiftTranslationCm) > FMath::Abs(ClavicleArmState.SmoothedClavicleLiftTranslationCm) ? 0.05f : 0.12f,
				DeltaSeconds);
			if (!ClavicleArmState.bHasSmoothedClavicleLiftTranslation)
			{
				ClavicleArmState.bHasSmoothedClavicleLiftTranslation = true;
				ClavicleArmState.SmoothedClavicleLiftTranslationCm = ShoulderLiftTranslationCm;
			}
			else
			{
				ClavicleArmState.SmoothedClavicleLiftTranslationCm = FMath::Lerp(
					ClavicleArmState.SmoothedClavicleLiftTranslationCm,
					ShoulderLiftTranslationCm,
					LiftTranslationAlpha);
			}

			const float ArmDrivenClavicleUpW = FMath::Pow(FMath::Clamp(ArmUp, 0.0f, 1.0f), 1.35f) * 0.18f;
			float ShrugDrivenClavicleUpW = FMath::Clamp(ShoulderShrugW * 0.35f, -0.08f, 0.25f);
			// Direct metric shrug (2026-07-06): the weights-of-evidence path above saturates at
			// the 0.25 clamp (~2cm of rise) no matter the signal - the referee measured the
			// camera seeing 7.7cm shrugs while the avatar produced 1.5-2 vs Epic's 11.5. Drive
			// the clavicle BY GEOMETRY instead: the measured metric lift over the rig's
			// clavicle length gives the direction-blend weight whose rotation reproduces that
			// rise (rise ~= clavLen * UpW / sqrt(1+UpW^2)). Rig-scaled via the hip-height
			// calibration ratio; the asymmetric rest reference above keeps the lift honest.
			if (CVarMediaPipeClavicleShrugDirect.GetValueOnAnyThread() != 0 &&
				ClavBone.IsValidToEvaluate() && UpperBone.IsValidToEvaluate())
			{
				// Rig scale from shoulder widths (source landmarks vs posed rig), the same
				// correspondence the clearance path uses; falls back to 1.0 when either side
				// is unmeasurable this frame.
				float RigScale = 1.0f;
				FVector ScaleLS = FVector::ZeroVector;
				FVector ScaleRS = FVector::ZeroVector;
				if (UpperArmL.IsValidToEvaluate() && UpperArmR.IsValidToEvaluate() &&
					TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftShoulder, ScaleLS) &&
					TryGetLmWorld((int32)EMediaPipePoseLandmark::RightShoulder, ScaleRS))
				{
					const float SourceWidthCm = FVector::Dist(ScaleLS, ScaleRS);
					const float RigWidthCm = FVector::Dist(
						CSPose.GetComponentSpaceTransform(UpperArmL.CachedCompactPoseIndex).GetTranslation(),
						CSPose.GetComponentSpaceTransform(UpperArmR.CachedCompactPoseIndex).GetTranslation());
					if (SourceWidthCm > 10.0f && RigWidthCm > 10.0f)
					{
						RigScale = FMath::Clamp(RigWidthCm / SourceWidthCm, 0.5f, 2.0f);
					}
				}
				const float LiftRigCm = FMath::Clamp(ShoulderSignedLiftCm * RigScale, 0.0f, 14.0f);
				const float ClavLenCm = FMath::Max(8.0f, FVector::Dist(
					CSPose.GetComponentSpaceTransform(ClavBone.CachedCompactPoseIndex).GetTranslation(),
					CSPose.GetComponentSpaceTransform(UpperBone.CachedCompactPoseIndex).GetTranslation()));
				const float DirectUpW = FMath::Clamp(LiftRigCm / ClavLenCm, 0.0f, 0.8f) *
					FMath::Clamp(CVarMediaPipeClavicleShrugWeight.GetValueOnAnyThread(), 0.0f, 2.0f);
				ShrugDrivenClavicleUpW = FMath::Max(ShrugDrivenClavicleUpW, DirectUpW);
			}
			const float UpW = FMath::Clamp(ArmDrivenClavicleUpW + ShrugDrivenClavicleUpW, -0.25f, 0.85f);
			const float FwdW = FMath::Clamp(ArmForward, 0.0f, 1.0f) * 0.14f;

			FVector DesiredClavDir = (Outward + UpComp * UpW + ForwardComp * FwdW).GetSafeNormal();
			if (DesiredClavDir.IsNearlyZero())
			{
				DesiredClavDir = Outward;
			}

			const FQuat TargetClavRotCS = (FQuat::FindBetweenNormals(RefClavDir, DesiredClavDir) * RefClavComp).GetNormalized();
			const float ClavAlpha = HalfLifeToAlpha(EffectiveArmRotationHalfLifeSeconds, DeltaSeconds) * ArmObservationAlphaScale;
			bool& bHasSmoothedClavRot = bIsLeft ? LeftArmState.bHasSmoothedClavRotCS : RightArmState.bHasSmoothedClavRotCS;
			FQuat& SmoothedClavRotCS = bIsLeft ? LeftArmState.SmoothedClavRotCS : RightArmState.SmoothedClavRotCS;
			UpdateSmoothedRotation(bHasSmoothedClavRot, SmoothedClavRotCS, TargetClavRotCS, ClavAlpha, ArmMaxStepDegrees);
			ApplyRotationCS(CSPose, ClavBone, SmoothedClavRotCS);
			FVector ClavicleLiftDeltaComp = UpComp.GetSafeNormal();
			if (!ClavicleLiftDeltaComp.IsNearlyZero())
			{
				const FVector LiftDirComp = ClavicleLiftDeltaComp;
				FVector ClavicleBefore = FVector::ZeroVector;
				FVector UpperBefore = FVector::ZeroVector;
				if (ClavBone.IsValidToEvaluate())
				{
					ClavicleBefore = CSPose.GetComponentSpaceTransform(ClavBone.CachedCompactPoseIndex).GetTranslation();
				}
				if (UpperBone.IsValidToEvaluate())
				{
					UpperBefore = CSPose.GetComponentSpaceTransform(UpperBone.CachedCompactPoseIndex).GetTranslation();
				}
				ClavicleLiftDeltaComp *= ClavicleArmState.SmoothedClavicleLiftTranslationCm;
				ApplyTranslationDeltaCS(CSPose, ClavBone, ClavicleLiftDeltaComp);
				ApplyTranslationDeltaCS(CSPose, UpperBone, ClavicleLiftDeltaComp);
				ApplyTranslationDeltaCS(CSPose, LowerBone, ClavicleLiftDeltaComp);
				ApplyTranslationDeltaCS(CSPose, HandBone, ClavicleLiftDeltaComp);
				if (ClavBone.IsValidToEvaluate())
				{
					const FVector ClavicleAfter = CSPose.GetComponentSpaceTransform(ClavBone.CachedCompactPoseIndex).GetTranslation();
					AppliedClavicleLiftCm = FVector::DotProduct(ClavicleAfter - ClavicleBefore, LiftDirComp);
				}
				if (UpperBone.IsValidToEvaluate())
				{
					const FVector UpperAfter = CSPose.GetComponentSpaceTransform(UpperBone.CachedCompactPoseIndex).GetTranslation();
					AppliedUpperLiftCm = FVector::DotProduct(UpperAfter - UpperBefore, LiftDirComp);
				}
			}
			LatestSignalSnapshot.bValid = true;
			LatestSignalSnapshot.RuntimeStateKey = RuntimeStateKey;
			LatestSignalSnapshot.PoseTimestampUs = bHasPoseFrame ? PoseFrame.TimestampUs : -1;
			FMediaPipePoseDrivenShoulderSignalSnapshot& ShoulderSnapshot =
				bIsLeft ? LatestSignalSnapshot.LeftShoulder : LatestSignalSnapshot.RightShoulder;
			ShoulderSnapshot.bValid = true;
			ShoulderSnapshot.ShoulderSignedLiftCm = ShoulderSignedLiftCm;
			ShoulderSnapshot.ShoulderRelativeLiftCm = ShoulderRelativeLiftCm;
			ShoulderSnapshot.ShoulderPositiveLiftEvidenceCm = ShoulderPositiveLiftEvidenceCm;
			ShoulderSnapshot.ShoulderHeadClearanceCm = ShoulderHeadClearanceCm;
			ShoulderSnapshot.ShoulderHeadClearanceShrugCm = ShoulderHeadClearanceShrugCm;
			ShoulderSnapshot.BilateralShoulderHeadClearanceCm = BilateralShoulderHeadClearanceCm;
			ShoulderSnapshot.BilateralShoulderHeadClearanceReferenceCm = BilateralShoulderHeadClearanceReferenceCm;
			ShoulderSnapshot.BilateralShoulderHeadClearanceShrugCm = BilateralShoulderHeadClearanceShrugCm;
			ShoulderSnapshot.ComputedShrugWeight = ShoulderRawShrugW;
			ShoulderSnapshot.SmoothedShrugWeight = ShoulderShrugW;
			ShoulderSnapshot.ComputedLiftTranslationCm = ShoulderLiftTranslationCm;
			ShoulderSnapshot.SmoothedLiftTranslationCm = ClavicleArmState.SmoothedClavicleLiftTranslationCm;
			ShoulderSnapshot.AppliedClavicleLiftCm = AppliedClavicleLiftCm;
			ShoulderSnapshot.AppliedUpperLiftCm = AppliedUpperLiftCm;
			ShoulderSnapshot.UpWeight = UpW;
			ShoulderSnapshot.ForwardWeight = FwdW;

			if (CVarMediaPipeTorsoDebug.GetValueOnAnyThread() != 0)
			{
				const double NowSeconds = FPlatformTime::Seconds();
				if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, LastClavicleDiagnosticLogTimeSeconds))
				{
					UE_LOG(LogMediaPipePose, Log,
						TEXT("mp.ClavicleDebug: actor=%s side=%s enabled=1 valid=1 runtimeKey=%u sharedClearance=%d refHad=%d refInit=%d refBeforeCm=%.1f rigWidthCm=%.1f bilateralClearanceCm=%.1f bilateralRefCm=%.1f bilateralShrugCm=%.1f armUp=%.2f armForward=%.2f shrugCm=%.1f shrugWeight=%.2f liftTranslateCm=%.1f appliedClavLiftCm=%.1f appliedUpperLiftCm=%.1f headClearanceCm=%.1f clearanceShrugCm=%.1f relativeLiftCm=%.1f relativeLiftWeight=%.2f screenLift=%.3f screenLiftWeight=%.2f upWeight=%.2f forwardWeight=%.2f targetDeg=%.1f appliedDeg=%.1f alpha=%.2f maxStepDeg=%.1f"),
						*TargetActorName.ToString(),
						ClavicleSideLabel,
						RuntimeStateKey,
						SharedClearanceMode,
						SharedClearanceHadReference,
						SharedClearanceInitializedReference,
						SharedClearanceReferenceBeforeCm,
						RigShoulderWidthForClearanceCm,
						BilateralShoulderHeadClearanceCm,
						BilateralShoulderHeadClearanceReferenceCm,
						BilateralShoulderHeadClearanceShrugCm,
						ArmUp,
						ArmForward,
						ShoulderShrugCm,
						ShoulderShrugW,
						ClavicleArmState.SmoothedClavicleLiftTranslationCm,
						AppliedClavicleLiftCm,
						AppliedUpperLiftCm,
						ShoulderHeadClearanceCm,
						ShoulderHeadClearanceShrugCm,
						ShoulderRelativeLiftCm,
						ShoulderRelativeLiftW,
						ShoulderScreenLift,
						ShoulderScreenLiftW,
						UpW,
						FwdW,
						QuatAngularDistanceDegrees(TargetClavRotCS, RefClavComp),
						QuatAngularDistanceDegrees(SmoothedClavRotCS, RefClavComp),
						ClavAlpha,
						ArmMaxStepDegrees);
				}
			}
			}
		}
		else if (CVarMediaPipeTorsoDebug.GetValueOnAnyThread() != 0)
		{
			const double NowSeconds = FPlatformTime::Seconds();
			if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, LastClavicleDiagnosticLogTimeSeconds))
			{
				UE_LOG(LogMediaPipePose, Log,
					TEXT("mp.ClavicleDebug: actor=%s side=%s enabled=1 valid=0 hasRef=1 reason=BoneInvalid"),
					*TargetActorName.ToString(),
					ClavicleSideLabel);
			}
		}
	}
	else if (CVarMediaPipeTorsoDebug.GetValueOnAnyThread() != 0)
	{
		const double NowSeconds = FPlatformTime::Seconds();
		if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, LastClavicleDiagnosticLogTimeSeconds))
		{
			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.ClavicleDebug: actor=%s side=%s enabled=1 valid=0 hasRef=0 reason=NoReferenceClavicle"),
				*TargetActorName.ToString(),
				ClavicleSideLabel);
		}
	}

	const float EffectiveUpperArmTwistWeight = FMath::Clamp(CVarMediaPipeUpperArmTwistWeight.GetValueOnAnyThread(), 0.0f, 1.0f);
	const float EffectiveLowerArmTwistWeight = FMath::Clamp(CVarMediaPipeLowerArmTwistWeight.GetValueOnAnyThread(), 0.0f, 1.0f);
	const float EffectiveUpperArmTwistClampDegrees = FMath::Clamp(CVarMediaPipeUpperArmTwistClampDegrees.GetValueOnAnyThread(), 0.0f, 180.0f);
	const float EffectiveLowerArmTwistClampDegrees = FMath::Clamp(CVarMediaPipeLowerArmTwistClampDegrees.GetValueOnAnyThread(), 0.0f, 180.0f);
	const bool bShouldApplyShoulderRollbackGuard =
		!bMetaHumanFullArmChainDirectFresh &&
		FMediaPipeArmGuardPolicy::ShouldApplyShoulderRollbackGuard(
			CVarMediaPipeShoulderRollbackGuard.GetValueOnAnyThread() != 0,
			bQuestConstrainedArmSolveApplied,
			bUseHmdRelativeAvatarArmFrame);

	auto BuildArmBasisRotation = [&](const FVector& RefDir, const FVector& TargetDir, const FVector& TargetPole, const FQuat& RefRot, const FQuat& RefBasis) -> FQuat
	{
		FMediaPipeArmBasisRotationInput BasisInput;
		BasisInput.RefDir = RefDir;
		BasisInput.TargetDir = TargetDir;
		BasisInput.TargetPole = TargetPole;
		BasisInput.RefRot = RefRot;
		BasisInput.RefBasis = RefBasis;
		BasisInput.bHasRefBasis = bHasRef;

		FQuat TargetRotCS = FQuat::Identity;
		if (!MediaPipeBodySolverMath::TryBuildArmBasisRotation(BasisInput, TargetRotCS))
		{
			return (FQuat::FindBetweenNormals(RefDir, TargetDir) * RefRot).GetNormalized();
		}

		return TargetRotCS;
	};

	if (ArmObservationAlphaScale <= KINDA_SMALL_NUMBER && (!bHasSmoothedUpperRotCS || !bHasSmoothedLowerRotCS))
	{
		return;
	}

	const bool bHadUpperRotBeforeUpdate = bHasSmoothedUpperRotCS;
	const bool bHadLowerRotBeforeUpdate = bHasSmoothedLowerRotCS;
	const FQuat UpperRotBeforeUpdateCS = SmoothedUpperRotCS;
	const FQuat LowerRotBeforeUpdateCS = SmoothedLowerRotCS;
	FQuat ShoulderRollbackTargetUpperCS = FQuat::Identity;
	FQuat ShoulderRollbackTargetLowerCS = FQuat::Identity;
	bool bHasShoulderRollbackTargets = false;
	bool bShoulderRollbackUsesStableRigPole = false;
	bool bShoulderRollbackUsesElbowPlaneRoll = false;
	bool bShoulderRollbackGuardApplied = false;
	float ShoulderRollbackGuardBlendUsed = 0.0f;
	const float ShoulderRollbackElbowReliability = GetLandmarkReliability(ElbowLm);
	const float ShoulderRollbackWristReliability = GetLandmarkReliability(WristLm);

	bool bArmIKBranchEntered = false;
	FVector LoggedArmIKTargetComp = FVector::ZeroVector;
	FVector LoggedArmIKWristComp = FVector::ZeroVector;
	const bool bQuestWristForcesArmIK =
		bQuestWristPositionApplied &&
		CVarQuestWristForceArmIK.GetValueOnAnyThread() != 0;
	if ((bUseArmIK || bQuestWristForcesArmIK) && RefUpperLen > KINDA_SMALL_NUMBER && RefLowerLen > KINDA_SMALL_NUMBER && UpperBone.IsValidToEvaluate() && LowerBone.IsValidToEvaluate())
	{
		bArmIKBranchEntered = true;
		const float TargetAlpha = HalfLifeToAlpha(EffectiveArmTargetHalfLifeSeconds, DeltaSeconds) * ArmObservationAlphaScale;
		const float RotAlpha = HalfLifeToAlpha(EffectiveArmRotationHalfLifeSeconds, DeltaSeconds) * ArmObservationAlphaScale;

		const FVector ShoulderPosComp = CSPose.GetComponentSpaceTransform(UpperBone.CachedCompactPoseIndex).GetTranslation();
		FVector PoseToWristVecComp = TargetCompTransform.InverseTransformVectorNoScale(WristWorld - ShoulderWorld);
		if (PoseToWristVecComp.IsNearlyZero())
		{
			PoseToWristVecComp = PoseUpperComp * (RefUpperLen + RefLowerLen);
		}

		FVector DirToWristComp = PoseToWristVecComp.GetSafeNormal();
		if (DirToWristComp.IsNearlyZero())
		{
			DirToWristComp = PoseUpperComp;
		}

		FVector ArmSolveRightComp = !ShoulderRightComp.IsNearlyZero() ? ShoulderRightComp : HipRightComp;
		if (ArmSolveRightComp.IsNearlyZero())
		{
			ArmSolveRightComp = FVector::RightVector;
		}
		const FVector ArmOutwardComp = (bIsLeft ? -ArmSolveRightComp : ArmSolveRightComp).GetSafeNormal();

		FVector LeftHipWorld = FVector::ZeroVector;
		FVector RightHipWorld = FVector::ZeroVector;
		const bool bHasHipMidWorld =
			TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftHip, LeftHipWorld) &&
			TryGetLmWorld((int32)EMediaPipePoseLandmark::RightHip, RightHipWorld);
		const FVector HipMidWorld = bHasHipMidWorld ? 0.5f * (LeftHipWorld + RightHipWorld) : FVector::ZeroVector;
		const float ArmReach = FMath::Max(RefUpperLen + RefLowerLen, 1.0f);
		const float UpperDownMetric = RemapClamped(FVector::DotProduct(PoseUpperComp, -UpComp), 0.30f, 0.85f);
		float WristBelowWaistMetric = 0.0f;
		if (bHasHipMidWorld)
		{
			const float WristHeightAlongUp = FVector::DotProduct(WristWorld - HipMidWorld, UpWorld);
			WristBelowWaistMetric = RemapClamped((-WristHeightAlongUp) / ArmReach, 0.00f, 0.30f);
		}
		const float DownPosePoleAlpha = UpperDownMetric * WristBelowWaistMetric;
		const FVector DownPosePoleHintComp = (-ForwardComp + 0.25f * ArmOutwardComp).GetSafeNormal();
		const FVector DownPosePoleReference = ProjectPoleReferenceToPlane(
			DownPosePoleHintComp,
			DirToWristComp,
			-ForwardComp,
			!ArmOutwardComp.IsNearlyZero() ? ArmOutwardComp : RefPoleDir);
		const FVector ActivePoleReference =
			(DownPosePoleAlpha > KINDA_SMALL_NUMBER && !DownPosePoleReference.IsNearlyZero())
				? LerpNormalized(RefPoleDir, DownPosePoleReference, DownPosePoleAlpha)
				: RefPoleDir;

		const FVector TargetWristComp = ShoulderPosComp + PoseToWristVecComp;
		LoggedArmIKTargetComp = TargetWristComp;

		const FVector PoseToWristDirWorld = (WristWorld - ShoulderWorld).GetSafeNormal();
		const FVector ToElbowWorld = (ElbowWorld - ShoulderWorld);
		FVector PoleDirWorld = ToElbowWorld - FVector::DotProduct(ToElbowWorld, PoseToWristDirWorld) * PoseToWristDirWorld;
		const float MeasuredPoleFraction = ArmReach > KINDA_SMALL_NUMBER ? (PoleDirWorld.Size() / ArmReach) : 0.0f;
		FVector PoleDirComp = TargetCompTransform.InverseTransformVectorNoScale(PoleDirWorld).GetSafeNormal();
		if (PoleDirComp.IsNearlyZero())
		{
			PoleDirComp = ActivePoleReference;
		}

		PoleDirComp = (PoleDirComp - FVector::DotProduct(PoleDirComp, DirToWristComp) * DirToWristComp).GetSafeNormal();
		if (PoleDirComp.IsNearlyZero())
		{
			PoleDirComp = ActivePoleReference;
		}
		PoleDirComp = LockVectorToHemisphere(PoleDirComp, ActivePoleReference);

		bool& bHasSmoothedIK = bIsLeft ? LeftArmState.bHasSmoothedArmIK : RightArmState.bHasSmoothedArmIK;
		FVector& SmoothedWristTargetComp = bIsLeft ? LeftArmState.SmoothedWristTargetComp : RightArmState.SmoothedWristTargetComp;
		FVector& SmoothedPoleDirComp = bIsLeft ? LeftArmState.SmoothedPoleDirComp : RightArmState.SmoothedPoleDirComp;

		if (!bHasSmoothedIK)
		{
			SmoothedWristTargetComp = TargetWristComp;
			SmoothedPoleDirComp = PoleDirComp;
			bHasSmoothedIK = true;
		}
		else
		{
			SmoothedWristTargetComp = FMath::Lerp(SmoothedWristTargetComp, TargetWristComp, TargetAlpha);
			SmoothedPoleDirComp = LockVectorToHemisphere(SmoothedPoleDirComp, ActivePoleReference);
			SmoothedPoleDirComp = LerpNormalized(SmoothedPoleDirComp, PoleDirComp, TargetAlpha);
			SmoothedPoleDirComp = LockVectorToHemisphere(SmoothedPoleDirComp, ActivePoleReference);
		}

		FVector TargetDelta = (SmoothedWristTargetComp - ShoulderPosComp);
		float Dist = TargetDelta.Size();
		if (Dist <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		Dist = FMath::Clamp(Dist, FMath::Abs(RefUpperLen - RefLowerLen) + 0.1f, (RefUpperLen + RefLowerLen) - 0.1f);

		const FVector DirToTarget = (TargetDelta / TargetDelta.Size());
		const FVector WristPosComp = ShoulderPosComp + DirToTarget * Dist;
		LoggedArmIKWristComp = WristPosComp;
		FVector Pole = (SmoothedPoleDirComp - FVector::DotProduct(SmoothedPoleDirComp, DirToTarget) * DirToTarget).GetSafeNormal();
		if (Pole.IsNearlyZero())
		{
			Pole = ActivePoleReference;
			Pole = (Pole - FVector::DotProduct(Pole, DirToTarget) * DirToTarget).GetSafeNormal();
		}
		if (Pole.IsNearlyZero())
		{
			Pole = FVector::UpVector;
			Pole = (Pole - FVector::DotProduct(Pole, DirToTarget) * DirToTarget).GetSafeNormal();
		}
		Pole = LockVectorToHemisphere(Pole, ActivePoleReference);

		FVector PlaneNormal = FVector::CrossProduct(DirToTarget, Pole).GetSafeNormal();
		if (PlaneNormal.IsNearlyZero())
		{
			PlaneNormal = FVector::CrossProduct(DirToTarget, FVector::UpVector).GetSafeNormal();
		}
		const FVector DirPerp = FVector::CrossProduct(PlaneNormal, DirToTarget).GetSafeNormal();
		if (DirPerp.IsNearlyZero())
		{
			return;
		}

		const float L1 = RefUpperLen;
		const float L2 = RefLowerLen;
		const float CosA = FMath::Clamp(((L1 * L1) + (Dist * Dist) - (L2 * L2)) / (2.0f * L1 * Dist), -1.0f, 1.0f);
		const float SinA = FMath::Sqrt(FMath::Max(0.0f, 1.0f - (CosA * CosA)));

		const FVector ElbowPosA = ShoulderPosComp + DirToTarget * (L1 * CosA) + DirPerp * (L1 * SinA);
		const FVector ElbowPosB = ShoulderPosComp + DirToTarget * (L1 * CosA) - DirPerp * (L1 * SinA);

		const FVector UpperDirA = (ElbowPosA - ShoulderPosComp).GetSafeNormal();
		const FVector LowerDirA = (WristPosComp - ElbowPosA).GetSafeNormal();
		const FVector PoleA = (UpperDirA - FVector::DotProduct(UpperDirA, DirToTarget) * DirToTarget).GetSafeNormal();
		FVector MeasuredPoleForBranch = TargetCompTransform.InverseTransformVectorNoScale(PoleDirWorld);
		MeasuredPoleForBranch = (MeasuredPoleForBranch - FVector::DotProduct(MeasuredPoleForBranch, DirToTarget) * DirToTarget).GetSafeNormal();
		const FVector BranchPoleReference = !MeasuredPoleForBranch.IsNearlyZero() ? MeasuredPoleForBranch : Pole;
		const float BranchPoleWeight = !MeasuredPoleForBranch.IsNearlyZero()
			? (1.10f * RemapClamped(MeasuredPoleFraction, 0.02f, 0.12f))
			: (0.35f * DownPosePoleAlpha);
		const float ScoreA =
			FVector::DotProduct(UpperDirA, PoseUpperComp) +
			FVector::DotProduct(LowerDirA, PoseLowerComp) +
			FVector::DotProduct(PoleA, BranchPoleReference) * BranchPoleWeight;

		const FVector UpperDirB = (ElbowPosB - ShoulderPosComp).GetSafeNormal();
		const FVector LowerDirB = (WristPosComp - ElbowPosB).GetSafeNormal();
		const FVector PoleB = (UpperDirB - FVector::DotProduct(UpperDirB, DirToTarget) * DirToTarget).GetSafeNormal();
		const float ScoreB =
			FVector::DotProduct(UpperDirB, PoseUpperComp) +
			FVector::DotProduct(LowerDirB, PoseLowerComp) +
			FVector::DotProduct(PoleB, BranchPoleReference) * BranchPoleWeight;

		double& LastArmDiagnosticLogTimeSeconds = bIsLeft ? DiagnosticsState.LastArmDiagnosticLogTimeSecondsL : DiagnosticsState.LastArmDiagnosticLogTimeSecondsR;
		const bool bUseA = (ScoreA >= ScoreB);
		if (TargetActorName == FName(TEXT("MP_MediaPipeMannyLike")) &&
			FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(PoseTimestampSeconds, 1.0, LastArmDiagnosticLogTimeSeconds))
		{
			const FVector PlaneWorld = FVector::CrossProduct(ElbowWorld - ShoulderWorld, WristWorld - ElbowWorld).GetSafeNormal();
			const FVector PlaneComp = FVector::CrossProduct(PoseUpperComp, PoseLowerComp).GetSafeNormal();
			FMediaPipeMannyLikeArmSolveLogInput ArmLogInput;
			ArmLogInput.TargetActorName = TargetActorName;
			ArmLogInput.bIsLeft = bIsLeft;
			ArmLogInput.ShoulderWorld = ShoulderWorld;
			ArmLogInput.ElbowWorld = ElbowWorld;
			ArmLogInput.WristWorld = WristWorld;
			ArmLogInput.PoseUpperComp = PoseUpperComp;
			ArmLogInput.PoseLowerComp = PoseLowerComp;
			ArmLogInput.PlaneWorld = PlaneWorld;
			ArmLogInput.ForwardWorld = ForwardWorld;
			ArmLogInput.PlaneComp = PlaneComp;
			ArmLogInput.ForwardComp = ForwardComp;
			ArmLogInput.UpComp = UpComp;
			ArmLogInput.MeasuredPoleFraction = MeasuredPoleFraction;
			ArmLogInput.UpperDownMetric = UpperDownMetric;
			ArmLogInput.WristBelowWaistMetric = WristBelowWaistMetric;
			ArmLogInput.DownPosePoleAlpha = DownPosePoleAlpha;
			ArmLogInput.Pole = Pole;
			ArmLogInput.BranchPoleReference = BranchPoleReference;
			ArmLogInput.ActivePoleReference = ActivePoleReference;
			ArmLogInput.BranchPoleWeight = BranchPoleWeight;
			ArmLogInput.ScoreA = ScoreA;
			ArmLogInput.ScoreB = ScoreB;
			ArmLogInput.bUseA = bUseA;
			FMediaPipeBodyDiagnostics::EmitMannyLikeArmSolveLog(ArmLogInput);
		}
		const FVector UpperDirComp = bUseA ? UpperDirA : UpperDirB;
		const FVector LowerDirComp = bUseA ? LowerDirA : LowerDirB;

		auto OrthoToForward = [](const FVector& V, const FVector& Forward) -> FVector
		{
			const FVector Fwd = Forward.GetSafeNormal();
			if (Fwd.IsNearlyZero())
			{
				return FVector::UpVector;
			}
			const FVector Ortho = V - FVector::DotProduct(V, Fwd) * Fwd;
			return Ortho.IsNearlyZero() ? FVector::UpVector : Ortho.GetSafeNormal();
		};

		auto TargetFromPole = [&](const FVector& RefDir, const FVector& TargetDir, const FVector& RefPole, const FVector& TargetPole, const FQuat& RefRot) -> FQuat
		{
			const FVector RefUp = OrthoToForward(RefPole, RefDir);
			const FVector TargetUp = OrthoToForward(TargetPole, TargetDir);
			if (!RefUp.IsNearlyZero() && !TargetUp.IsNearlyZero() &&
				!FVector::CrossProduct(RefDir, RefUp).IsNearlyZero() &&
				!FVector::CrossProduct(TargetDir, TargetUp).IsNearlyZero())
			{
				const FQuat RefBasis = MakeQuatFromForwardUp(RefDir, RefUp);
				const FQuat TargetBasis = MakeQuatFromForwardUp(TargetDir, TargetUp);
				if (!RefBasis.IsIdentity() && !TargetBasis.IsIdentity())
				{
					const FQuat Delta = TargetBasis * RefBasis.Inverse();
					return (Delta * RefRot).GetNormalized();
				}
			}

			const FQuat DeltaDir = FQuat::FindBetweenNormals(RefDir, TargetDir);
			return (DeltaDir * RefRot).GetNormalized();
		};

		const FQuat FallbackUpperComp = TargetFromPole(RefUpperDir, UpperDirComp, ActivePoleReference, Pole, RefUpperComp);
		const FQuat FallbackLowerComp = TargetFromPole(RefLowerDir, LowerDirComp, ActivePoleReference, Pole, RefLowerComp);
		FQuat FilteredUpperComp = FilterTargetRotationSwingTwist(
			RefUpperComp,
			RefUpperDir,
			FallbackUpperComp,
			EffectiveUpperArmTwistWeight,
			EffectiveUpperArmTwistClampDegrees);
		FQuat FilteredLowerComp = FilterTargetRotationSwingTwist(
			RefLowerComp,
			RefLowerDir,
			FallbackLowerComp,
			EffectiveLowerArmTwistWeight,
			EffectiveLowerArmTwistClampDegrees);

		ShoulderRollbackTargetUpperCS = FilteredUpperComp;
		ShoulderRollbackTargetLowerCS = FilteredLowerComp;
		bHasShoulderRollbackTargets = true;

		if (bShouldApplyShoulderRollbackGuard && bHadUpperRotBeforeUpdate)
		{
			FVector WristFromShoulderComp = TargetCompTransform.InverseTransformVectorNoScale(WristWorld - ShoulderWorld).GetSafeNormal();
			if (WristFromShoulderComp.IsNearlyZero())
			{
				WristFromShoulderComp = PoseUpperComp;
			}

			const float BackDotThreshold = FMath::Clamp(CVarMediaPipeShoulderRollbackBackDotThreshold.GetValueOnAnyThread(), -1.0f, 1.0f);
			const float UpperForwardDotForGuard = FVector::DotProduct(PoseUpperComp, ForwardComp);
			const float WristForwardDotForGuard = FVector::DotProduct(WristFromShoulderComp, ForwardComp);
			const float MaxTargetFromRefDegrees = FMath::Clamp(CVarMediaPipeShoulderRollbackGuardMaxTargetFromRefDegrees.GetValueOnAnyThread(), 1.0f, 180.0f);
			const float UpperTargetFromRefDegrees = FMath::RadiansToDegrees(RefUpperComp.AngularDistance(FilteredUpperComp));
			const float MinReliability = FMath::Clamp(CVarMediaPipeShoulderRollbackGuardMinReliability.GetValueOnAnyThread(), 0.0f, 1.0f);
			const bool bLowArmReliability = FMath::Min(ShoulderRollbackElbowReliability, ShoulderRollbackWristReliability) < MinReliability;
			const bool bGuardThisTarget =
				UpperForwardDotForGuard <= BackDotThreshold &&
				(WristForwardDotForGuard <= BackDotThreshold || bLowArmReliability || UpperTargetFromRefDegrees >= MaxTargetFromRefDegrees);
			if (bGuardThisTarget)
			{
				ShoulderRollbackGuardBlendUsed = FMath::Clamp(CVarMediaPipeShoulderRollbackGuardBlend.GetValueOnAnyThread(), 0.0f, 1.0f);
				FilteredUpperComp = FQuat::Slerp(UpperRotBeforeUpdateCS, FilteredUpperComp, ShoulderRollbackGuardBlendUsed).GetNormalized();
				if (bHadLowerRotBeforeUpdate)
				{
					FilteredLowerComp = FQuat::Slerp(LowerRotBeforeUpdateCS, FilteredLowerComp, ShoulderRollbackGuardBlendUsed).GetNormalized();
				}
				bShoulderRollbackGuardApplied = true;
			}
		}

		UpdateSmoothedRotation(bHasSmoothedUpperRotCS, SmoothedUpperRotCS, FilteredUpperComp, RotAlpha, ArmMaxStepDegrees);
		ApplyRotationCS(CSPose, UpperBone, SmoothedUpperRotCS);
		UpdateSmoothedRotation(bHasSmoothedLowerRotCS, SmoothedLowerRotCS, FilteredLowerComp, RotAlpha, ArmMaxStepDegrees);
		ApplyRotationCS(CSPose, LowerBone, SmoothedLowerRotCS);
	}
	else
	{
		const float RotAlpha = HalfLifeToAlpha(EffectiveArmRotationHalfLifeSeconds, DeltaSeconds) * ArmObservationAlphaScale;
		FQuat FallbackUpperComp = (FQuat::FindBetweenNormals(RefUpperDir, PoseUpperComp) * RefUpperComp).GetNormalized();
		FQuat FallbackLowerComp = (FQuat::FindBetweenNormals(RefLowerDir, PoseLowerComp) * RefLowerComp).GetNormalized();
		bool bUsesStableRigPole = false;
		bool bUsesElbowPlaneRoll = false;
		const float ElbowPlaneSin = FVector::CrossProduct(PoseUpperComp, PoseLowerComp).Size();
		FMediaPipeSolvedElbowPlaneArmInput SolvedPlaneInput;
		SolvedPlaneInput.RefUpperDir = RefUpperDir;
		SolvedPlaneInput.RefLowerDir = RefLowerDir;
		SolvedPlaneInput.TargetUpperDir = PoseUpperComp;
		SolvedPlaneInput.TargetLowerDir = PoseLowerComp;
		SolvedPlaneInput.RefUpperRot = RefUpperComp;
		SolvedPlaneInput.RefLowerRot = RefLowerComp;
		SolvedPlaneInput.RefUpperBasis = RefUpperBasisComp;
		SolvedPlaneInput.RefLowerBasis = RefLowerBasisComp;
		SolvedPlaneInput.bHasRefUpperBasis = bHasRef;
		SolvedPlaneInput.bHasRefLowerBasis = bHasRef;
		const float DirectArmPlaneMinSin = FMath::Clamp(CVarMediaPipeArmElbowPlaneMinSin.GetValueOnAnyThread(), 0.0f, 1.0f);
		SolvedPlaneInput.MinElbowPlaneSin = (bUseBodyFusionArm || bQuestConstrainedArmSolveApplied || bMetaHumanFullArmChainDirectFresh)
			? FMath::Clamp(CVarQuestConstrainedArmSolvedPlaneMinSin.GetValueOnAnyThread(), 0.0f, 1.0f)
			: DirectArmPlaneMinSin;

		FMediaPipeSolvedElbowPlaneArmResult SolvedPlaneResult;
		const bool bShouldUseSolvedElbowPlane =
			bUseBodyFusionArm ||
			bMetaHumanFullArmChainDirectFresh ||
			bQuestConstrainedArmSolveApplied ||
			CVarMediaPipeArmUseElbowPlaneRoll.GetValueOnAnyThread() != 0;
		if (bShouldUseSolvedElbowPlane &&
			ElbowPlaneSin >= SolvedPlaneInput.MinElbowPlaneSin &&
			MediaPipeBodySolverMath::TryBuildSolvedElbowPlaneArmRotations(SolvedPlaneInput, SolvedPlaneResult))
		{
			bUsesElbowPlaneRoll = true;
			FallbackUpperComp = SolvedPlaneResult.UpperRotCS;
			FallbackLowerComp = SolvedPlaneResult.LowerRotCS;
		}
		else
		{
			const FVector UpperSurfaceHint = BuildArmSurfaceUpHint(UpComp, ForwardComp, PoseUpperComp);
			const FVector StableUpperSurfaceUp = ProjectPoleReferenceToPlane(UpperSurfaceHint, PoseUpperComp, ForwardComp, RefPoleDir);
			if (!StableUpperSurfaceUp.IsNearlyZero())
			{
				const FVector StableLowerSurfaceUp = ProjectPoleReferenceToPlane(StableUpperSurfaceUp, PoseLowerComp, ForwardComp, RefPoleDir);
				FallbackUpperComp = BuildArmBasisRotation(RefUpperDir, PoseUpperComp, StableUpperSurfaceUp, RefUpperComp, RefUpperSurfaceBasisComp);
				if (!StableLowerSurfaceUp.IsNearlyZero())
				{
					FallbackLowerComp = BuildArmBasisRotation(RefLowerDir, PoseLowerComp, StableLowerSurfaceUp, RefLowerComp, RefLowerSurfaceBasisComp);
				}
				bUsesStableRigPole = true;
			}
		}
		FQuat FilteredUpperComp = bUsesStableRigPole
			? FallbackUpperComp
			: FilterTargetRotationSwingTwist(
				RefUpperComp,
				RefUpperDir,
				FallbackUpperComp,
				EffectiveUpperArmTwistWeight,
				EffectiveUpperArmTwistClampDegrees);
		FQuat FilteredLowerComp = bUsesStableRigPole
			? FallbackLowerComp
			: FilterTargetRotationSwingTwist(
				RefLowerComp,
				RefLowerDir,
				FallbackLowerComp,
				EffectiveLowerArmTwistWeight,
				EffectiveLowerArmTwistClampDegrees);

		ShoulderRollbackTargetUpperCS = FilteredUpperComp;
		ShoulderRollbackTargetLowerCS = FilteredLowerComp;
		bHasShoulderRollbackTargets = true;
		bShoulderRollbackUsesStableRigPole = bUsesStableRigPole;
		bShoulderRollbackUsesElbowPlaneRoll = bUsesElbowPlaneRoll;

		if (bShouldApplyShoulderRollbackGuard && bHadUpperRotBeforeUpdate)
		{
			FVector WristFromShoulderComp = TargetCompTransform.InverseTransformVectorNoScale(WristWorld - ShoulderWorld).GetSafeNormal();
			if (WristFromShoulderComp.IsNearlyZero())
			{
				WristFromShoulderComp = PoseUpperComp;
			}

			const float BackDotThreshold = FMath::Clamp(CVarMediaPipeShoulderRollbackBackDotThreshold.GetValueOnAnyThread(), -1.0f, 1.0f);
			const float UpperForwardDotForGuard = FVector::DotProduct(PoseUpperComp, ForwardComp);
			const float WristForwardDotForGuard = FVector::DotProduct(WristFromShoulderComp, ForwardComp);
			const float MaxTargetFromRefDegrees = FMath::Clamp(CVarMediaPipeShoulderRollbackGuardMaxTargetFromRefDegrees.GetValueOnAnyThread(), 1.0f, 180.0f);
			const float UpperTargetFromRefDegrees = FMath::RadiansToDegrees(RefUpperComp.AngularDistance(FilteredUpperComp));
			const float MinReliability = FMath::Clamp(CVarMediaPipeShoulderRollbackGuardMinReliability.GetValueOnAnyThread(), 0.0f, 1.0f);
			const bool bLowArmReliability = FMath::Min(ShoulderRollbackElbowReliability, ShoulderRollbackWristReliability) < MinReliability;
			const bool bGuardThisTarget =
				UpperForwardDotForGuard <= BackDotThreshold &&
				(WristForwardDotForGuard <= BackDotThreshold || bLowArmReliability || UpperTargetFromRefDegrees >= MaxTargetFromRefDegrees);
			if (bGuardThisTarget)
			{
				ShoulderRollbackGuardBlendUsed = FMath::Clamp(CVarMediaPipeShoulderRollbackGuardBlend.GetValueOnAnyThread(), 0.0f, 1.0f);
				FilteredUpperComp = FQuat::Slerp(UpperRotBeforeUpdateCS, FilteredUpperComp, ShoulderRollbackGuardBlendUsed).GetNormalized();
				if (bHadLowerRotBeforeUpdate)
				{
					FilteredLowerComp = FQuat::Slerp(LowerRotBeforeUpdateCS, FilteredLowerComp, ShoulderRollbackGuardBlendUsed).GetNormalized();
				}
				bShoulderRollbackGuardApplied = true;
			}
		}

		UpdateSmoothedRotation(bHasSmoothedUpperRotCS, SmoothedUpperRotCS, FilteredUpperComp, RotAlpha, ArmMaxStepDegrees);
		ApplyRotationCS(CSPose, UpperBone, SmoothedUpperRotCS);
		UpdateSmoothedRotation(bHasSmoothedLowerRotCS, SmoothedLowerRotCS, FilteredLowerComp, RotAlpha, ArmMaxStepDegrees);
		ApplyRotationCS(CSPose, LowerBone, SmoothedLowerRotCS);
	}

	if (CVarMediaPipeShoulderRollbackTrace.GetValueOnAnyThread() != 0 && bHasShoulderRollbackTargets)
	{
		FVector WristFromShoulderComp = TargetCompTransform.InverseTransformVectorNoScale(WristWorld - ShoulderWorld).GetSafeNormal();
		if (WristFromShoulderComp.IsNearlyZero())
		{
			WristFromShoulderComp = PoseUpperComp;
		}

		const float BackDotThreshold = FMath::Clamp(CVarMediaPipeShoulderRollbackBackDotThreshold.GetValueOnAnyThread(), -1.0f, 1.0f);
		const float StepThresholdDegrees = FMath::Max(1.0f, CVarMediaPipeShoulderRollbackStepDegrees.GetValueOnAnyThread());
		const float UpperForwardDot = FVector::DotProduct(PoseUpperComp, ForwardComp);
		const float LowerForwardDot = FVector::DotProduct(PoseLowerComp, ForwardComp);
		const float WristForwardDot = FVector::DotProduct(WristFromShoulderComp, ForwardComp);
		const FVector ElbowPlaneNormalComp = FVector::CrossProduct(PoseUpperComp, PoseLowerComp).GetSafeNormal();
		const FVector OutwardComp = (bIsLeft ? -ShoulderRightComp : ShoulderRightComp).GetSafeNormal();
		const float ElbowPlaneOutwardDot = ElbowPlaneNormalComp.IsNearlyZero() || OutwardComp.IsNearlyZero()
			? 0.0f
			: FVector::DotProduct(ElbowPlaneNormalComp, OutwardComp);

		const float UpperTargetStepDegrees = bHadUpperRotBeforeUpdate
			? FMath::RadiansToDegrees(UpperRotBeforeUpdateCS.AngularDistance(ShoulderRollbackTargetUpperCS))
			: 0.0f;
		const float LowerTargetStepDegrees = bHadLowerRotBeforeUpdate
			? FMath::RadiansToDegrees(LowerRotBeforeUpdateCS.AngularDistance(ShoulderRollbackTargetLowerCS))
			: 0.0f;
		const float UpperAppliedStepDegrees = bHadUpperRotBeforeUpdate
			? FMath::RadiansToDegrees(UpperRotBeforeUpdateCS.AngularDistance(SmoothedUpperRotCS))
			: 0.0f;
		const float LowerAppliedStepDegrees = bHadLowerRotBeforeUpdate
			? FMath::RadiansToDegrees(LowerRotBeforeUpdateCS.AngularDistance(SmoothedLowerRotCS))
			: 0.0f;
		const float UpperTargetFromRefDegrees = FMath::RadiansToDegrees(RefUpperComp.AngularDistance(ShoulderRollbackTargetUpperCS));
		const float LowerTargetFromRefDegrees = FMath::RadiansToDegrees(RefLowerComp.AngularDistance(ShoulderRollbackTargetLowerCS));

		bool& bHasLastUpperForwardDot = bIsLeft ? DiagnosticsState.bHasLastShoulderRollbackUpperForwardDotL : DiagnosticsState.bHasLastShoulderRollbackUpperForwardDotR;
		float& LastUpperForwardDot = bIsLeft ? DiagnosticsState.LastShoulderRollbackUpperForwardDotL : DiagnosticsState.LastShoulderRollbackUpperForwardDotR;
		const bool bHadLastUpperForwardDot = bHasLastUpperForwardDot;
		const float PreviousUpperForwardDot = LastUpperForwardDot;
		const bool bCrossedBehind = bHadLastUpperForwardDot && PreviousUpperForwardDot > -0.05f && UpperForwardDot <= BackDotThreshold;
		bHasLastUpperForwardDot = true;
		LastUpperForwardDot = UpperForwardDot;

		const bool bUpperBehind = UpperForwardDot <= BackDotThreshold;
		const bool bWristBehind = WristForwardDot <= BackDotThreshold;
		const bool bTargetSnap = UpperTargetStepDegrees >= StepThresholdDegrees || LowerTargetStepDegrees >= StepThresholdDegrees;
		const bool bClampHit = ArmMaxStepDegrees > 0.0f &&
			((bHadUpperRotBeforeUpdate && UpperTargetStepDegrees > UpperAppliedStepDegrees + 5.0f && UpperAppliedStepDegrees >= ArmMaxStepDegrees - 0.25f) ||
			 (bHadLowerRotBeforeUpdate && LowerTargetStepDegrees > LowerAppliedStepDegrees + 5.0f && LowerAppliedStepDegrees >= ArmMaxStepDegrees - 0.25f));
		const bool bShouldLog = bCrossedBehind || bUpperBehind || bWristBehind || bTargetSnap || bClampHit;

		if (bShouldLog)
		{
			double& LastShoulderRollbackTraceLogTimeSeconds = bIsLeft ? DiagnosticsState.LastShoulderRollbackTraceLogTimeSecondsL : DiagnosticsState.LastShoulderRollbackTraceLogTimeSecondsR;
			const double NowSeconds = FPlatformTime::Seconds();
			const double RequiredLogIntervalSeconds = static_cast<double>(
				FMath::Clamp(CVarMediaPipeShoulderRollbackTraceLogIntervalSeconds.GetValueOnAnyThread(), 0.02f, 2.0f));
			if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, RequiredLogIntervalSeconds, LastShoulderRollbackTraceLogTimeSeconds))
			{
				FVector LeftHipWorld = FVector::ZeroVector;
				FVector RightHipWorld = FVector::ZeroVector;
				const bool bHasHipMidWorld =
					TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftHip, LeftHipWorld) &&
					TryGetLmWorld((int32)EMediaPipePoseLandmark::RightHip, RightHipWorld);
				const FVector HipMidWorld = bHasHipMidWorld ? (LeftHipWorld + RightHipWorld) * 0.5f : FVector::ZeroVector;
				const float ShoulderForwardCm = bHasHipMidWorld ? FVector::DotProduct(ShoulderWorld - HipMidWorld, ForwardWorld) : 0.0f;
				const float ElbowForwardCm = bHasHipMidWorld ? FVector::DotProduct(ElbowWorld - HipMidWorld, ForwardWorld) : 0.0f;
				const float WristForwardCm = bHasHipMidWorld ? FVector::DotProduct(WristWorld - HipMidWorld, ForwardWorld) : 0.0f;
				const float ShoulderReliability = GetLandmarkReliability(ShoulderLm);
				const float ElbowReliability = GetLandmarkReliability(ElbowLm);
				const float WristReliability = GetLandmarkReliability(WristLm);

				FMediaPipeShoulderRollbackTraceFormatInput TraceInput;
				TraceInput.TargetActorName = TargetActorName;
				TraceInput.bIsLeft = bIsLeft;
				TraceInput.bUpperBehind = bUpperBehind;
				TraceInput.bWristBehind = bWristBehind;
				TraceInput.bCrossedBehind = bCrossedBehind;
				TraceInput.bTargetSnap = bTargetSnap;
				TraceInput.bClampHit = bClampHit;
				TraceInput.bGuardApplied = bShoulderRollbackGuardApplied;
				TraceInput.GuardBlend = ShoulderRollbackGuardBlendUsed;
				TraceInput.UpperForwardDot = UpperForwardDot;
				TraceInput.LowerForwardDot = LowerForwardDot;
				TraceInput.WristForwardDot = WristForwardDot;
				TraceInput.PreviousUpperForwardDot = bHadLastUpperForwardDot ? PreviousUpperForwardDot : 0.0f;
				TraceInput.BackThreshold = BackDotThreshold;
				TraceInput.UpperTargetStepDeg = UpperTargetStepDegrees;
				TraceInput.LowerTargetStepDeg = LowerTargetStepDegrees;
				TraceInput.UpperAppliedStepDeg = UpperAppliedStepDegrees;
				TraceInput.LowerAppliedStepDeg = LowerAppliedStepDegrees;
				TraceInput.ArmMaxStepDeg = ArmMaxStepDegrees;
				TraceInput.UpperTargetFromRefDeg = UpperTargetFromRefDegrees;
				TraceInput.LowerTargetFromRefDeg = LowerTargetFromRefDegrees;
				TraceInput.ElbowPlaneOutwardDot = ElbowPlaneOutwardDot;
				TraceInput.bStablePole = bShoulderRollbackUsesStableRigPole;
				TraceInput.bElbowPlaneRoll = bShoulderRollbackUsesElbowPlaneRoll;
				TraceInput.bArmIK = bUseArmIK;
				TraceInput.bQuestForceArmIK = CVarQuestWristForceArmIK.GetValueOnAnyThread() != 0;
				TraceInput.bLegs = bDriveLegs;
				TraceInput.bLegIK = bUseLegIK;
				TraceInput.bPelvisTranslation = bDrivePelvisTranslation;
				TraceInput.bClavicles = bDriveClavicles;
				TraceInput.bTorsoBasis = bHasTorsoBasis;
				TraceInput.ShoulderReliability = ShoulderReliability;
				TraceInput.ElbowReliability = ElbowReliability;
				TraceInput.WristReliability = WristReliability;
				TraceInput.ShoulderForwardCm = ShoulderForwardCm;
				TraceInput.ElbowForwardCm = ElbowForwardCm;
				TraceInput.WristForwardCm = WristForwardCm;
				TraceInput.ShoulderWorld = ShoulderWorld;
				TraceInput.ElbowWorld = ElbowWorld;
				TraceInput.WristWorld = WristWorld;
				TraceInput.ForwardWorld = ForwardWorld;
				TraceInput.UpWorld = UpWorld;
				TraceInput.ShoulderRightWorld = ShoulderRightWorld;
				TraceInput.HipRightWorld = HipRightWorld;
				UE_LOG(LogMediaPipePose, Warning, TEXT("%s"), *FMediaPipeShoulderRollbackDiagnostics::FormatTraceLog(TraceInput));
			}
		}
	}

	const FVector QuestForearmAxisComp = (bHasSmoothedLowerRotCS ? SmoothedLowerRotCS.RotateVector(RefLowerDir) : PoseLowerComp).GetSafeNormal();
	const FQuat CurrentHandTargetCS = HandBone.IsValidToEvaluate()
		? CSPose.GetComponentSpaceTransform(HandBone.CachedCompactPoseIndex).GetRotation().GetNormalized()
		: RefHandComp;
	const FTransform HandBeforeCS = HandBone.IsValidToEvaluate()
		? CSPose.GetComponentSpaceTransform(HandBone.CachedCompactPoseIndex)
		: FTransform::Identity;
	// OWNERSHIP LATCH (2026-07-03, measured): the previous "latched" ownership was recomputed
	// per frame from (rescue || !tracked), so Quest tracked-flag flicker still alternated hand
	// owners at frame rate (session log: camera owned only ~68%/47% of frames L/R across the
	// measured overhead window; 21 of 30 sampled mp.QuestWristSolve rows showed the Quest path
	// applying mid-takeover). The latch now lives in the KEYED side state: once the camera takes
	// the hand it keeps it until the Quest side stays solidly tracked (and the rescue is off)
	// for mp.MediaPipeHandOwnershipHandbackSeconds of wall-clock time, then hands back once.
	const bool bCameraHandFeaturesEnabled =
		bQuestHandTrackingEnabled &&
		(CVarMediaPipeHandRotationOnQuestLoss.GetValueOnAnyThread() != 0 ||
		 CVarMediaPipeFingersOnQuestLoss.GetValueOnAnyThread() != 0) &&
		!bBodyFusionPoseWriteActive &&
		!bTrackingFusionReplayActive;
	bool bCameraOwnsHandPose = false;
	if (bCameraHandFeaturesEnabled && RuntimeStateKey != 0)
	{
		const bool bWasCameraHandLatched = QuestWristSideState.bCameraHandOwnershipLatched;
		const double OwnershipNowSeconds = FPlatformTime::Seconds();
		const float OwnershipStepSeconds = QuestWristSideState.CameraHandOwnershipLastUpdateTimeSeconds >= 0.0
			? FMath::Clamp(static_cast<float>(OwnershipNowSeconds - QuestWristSideState.CameraHandOwnershipLastUpdateTimeSeconds), 0.0f, 0.1f)
			: 0.0f;
		QuestWristSideState.CameraHandOwnershipLastUpdateTimeSeconds = OwnershipNowSeconds;
		// Flicker grace (2026-07-09): a single dropped tracked frame used to latch camera hand
		// ownership instantly, suppressing DriveQuestHandCS for at least the 0.2s handback -
		// every sub-second Quest flicker became a visible wrist snap to the camera basis and
		// back. Quest keeps the hand through flickers shorter than the tracked-recency grace;
		// DriveQuestHandCS's own held-rotation path bridges those frames.
		// DECOUPLED from the arm rescue (2026-07-10 worn forensics): the old entry clause
		// (bArmRescueActive || !recentlyTracked) handed the WRIST to the camera whenever the
		// rescue owned the ARM, even with Quest tracking healthy - measured L11/R7 cameraLatched
		// entries with rescue=1 questTracked=1 on the mirror Kellan in one 7-minute session,
		// each a visible wrist snap to the camera basis. Hand rotation follows camera ONLY on
		// actual Quest tracking loss (past the grace); a camera-owned arm keeps its Quest hand.
		// Real dropouts still hand the wrist over: fully-gone arms are also !recentlyTracked.
		if (!bQuestSideRecentlyTrackedForArm)
		{
			QuestWristSideState.bCameraHandOwnershipLatched = true;
			QuestWristSideState.CameraHandQuestSolidSeconds = 0.0f;
		}
		else if (QuestWristSideState.bCameraHandOwnershipLatched)
		{
			QuestWristSideState.CameraHandQuestSolidSeconds += OwnershipStepSeconds;
			// FAST HANDBACK when the arm is clearly DOWN (round two, 2026-07-03: after lowering,
			// the Quest flickers at its FOV edge so the full solid dwell never completed and the
			// camera kept owning hands-down with bottom-of-frame landmarks - the session's last
			// ownership rows show both sides still camera-latched after the lowering phase).
			// Down-low the Quest is the trustworthy source and the camera is the flaky one, so a
			// tracked Quest hand takes back after 0.2 s instead of the full overhead dwell.
			const bool bMediaPipeArmClearlyDown =
				bHasMediaPipeArmWorld &&
				(MediaPipeSourceWristWorld.Z - MediaPipeSourceShoulderWorld.Z) <= -10.0f;
			const float RequiredSolidSeconds = bMediaPipeArmClearlyDown
				? 0.2f
				: FMath::Max(0.0f, CVarMediaPipeHandOwnershipHandbackSeconds.GetValueOnAnyThread());
			if (QuestWristSideState.CameraHandQuestSolidSeconds >= RequiredSolidSeconds)
			{
				QuestWristSideState.bCameraHandOwnershipLatched = false;
				QuestWristSideState.CameraHandQuestSolidSeconds = 0.0f;
			}
		}
		bCameraOwnsHandPose = QuestWristSideState.bCameraHandOwnershipLatched;
		if (CVarMediaPipeCameraHandTrace.GetValueOnAnyThread() != 0 &&
			bWasCameraHandLatched != QuestWristSideState.bCameraHandOwnershipLatched)
		{
			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.MediaPipeCameraHandTrace: actor=%s node=%llu key=%u side=%s ownership=%s rescue=%d questTracked=%d questSolidS=%.2f"),
				*TargetActorName.ToString(),
				NodeDiagSerial,
				RuntimeStateKey,
				bIsLeft ? TEXT("L") : TEXT("R"),
				QuestWristSideState.bCameraHandOwnershipLatched ? TEXT("cameraLatched") : TEXT("handbackToQuest"),
				QuestWristSideState.bArmRescueActive ? 1 : 0,
				bQuestSideTrackedForArm ? 1 : 0,
				QuestWristSideState.CameraHandQuestSolidSeconds);
		}
	}
	else if (bCameraHandFeaturesEnabled)
	{
		// Pre-PreUpdate evaluation (RuntimeStateKey==0): no per-component keyed store to latch
		// in; fall back to the per-frame formula for this evaluation only (rescue-decoupled
		// 2026-07-10, same as the latched entry clause above).
		bCameraOwnsHandPose = !bQuestSideTrackedForArm;
	}
	else if (RuntimeStateKey != 0 && QuestWristSideState.bCameraHandOwnershipLatched)
	{
		// Camera-hand features ended (fusion/replay took authority or the trial layer dropped):
		// release the latch so a later re-enable starts clean.
		QuestWristSideState.bCameraHandOwnershipLatched = false;
		QuestWristSideState.CameraHandQuestSolidSeconds = 0.0f;
	}
	FQuestHandRotationTrace QuestHandRotationTrace;
	const bool bQuestHandRotationApplied =
		!bCameraOwnsHandPose &&
		DriveQuestHandCS(CSPose, bIsLeft, QuestForearmAxisComp, CurrentHandTargetCS, DeltaSeconds, &QuestHandRotationTrace, &QuestWristTrace);
	const FTransform HandAfterCS = HandBone.IsValidToEvaluate()
		? CSPose.GetComponentSpaceTransform(HandBone.CachedCompactPoseIndex)
		: FTransform::Identity;
	const float QuestHandRotationDeltaDeg = bQuestHandRotationApplied
		? FMath::RadiansToDegrees(HandBeforeCS.GetRotation().AngularDistance(HandAfterCS.GetRotation()))
		: 0.0f;
	const bool bWouldEnterArmIKIfApplied =
		QuestWristTrace.bMapped != 0 &&
		CVarQuestWristForceArmIK.GetValueOnAnyThread() != 0 &&
		RefUpperLen > KINDA_SMALL_NUMBER &&
		RefLowerLen > KINDA_SMALL_NUMBER &&
		UpperBone.IsValidToEvaluate() &&
		LowerBone.IsValidToEvaluate();
	if (CVarQuestWristDebug.GetValueOnAnyThread() != 0 ||
		CVarQuestWristTrace.GetValueOnAnyThread() != 0 ||
		CVarQuestHandDebug.GetValueOnAnyThread() != 0)
	{
		// Throttle lives in the KEYED side state (2026-07-10): the old pair was a node-member
		// timer (wiped by CacheBones every frame, so it never throttled anything) plus a
		// function-static shared across ALL actors, which rationed one row per interval per
		// side for the whole process - Manny consumed the budget and the acceptance mirror
		// Kellan L starved to 4 rows across the entire 2026-07-10 worn session. Per-actor-side
		// keyed timers give every actor its own cadence. Pre-PreUpdate evaluations
		// (RuntimeStateKey==0) keep a process-wide static so the shared key-0 bucket is never
		// written (keyed-store rule).
		const double NowSeconds = FPlatformTime::Seconds();
		static double LastKeylessQuestWristSolveLogTimes[2] = {-1.0, -1.0};
		double& LastQuestWristSolveLogTimeSeconds = RuntimeStateKey != 0
			? QuestWristSideState.QuestWristSolveLastLogTimeSeconds
			: LastKeylessQuestWristSolveLogTimes[bIsLeft ? 0 : 1];
		const double RequiredLogIntervalSeconds = CVarQuestWristTrace.GetValueOnAnyThread() != 0
			? static_cast<double>(FMath::Clamp(CVarQuestWristTraceLogIntervalSeconds.GetValueOnAnyThread(), 0.05f, 5.0f))
			: 1.0;
		if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(
			NowSeconds,
			RequiredLogIntervalSeconds,
			LastQuestWristSolveLogTimeSeconds))
		{
			FMediaPipeQuestWristSolveLogFormatInput SolveLogInput;
			SolveLogInput.TargetActorName = TargetActorName;
			SolveLogInput.bIsLeft = bIsLeft;
			SolveLogInput.QuestArmMode = QuestArmMode;
			SolveLogInput.ArmOwner =
				bMediaPipeArmRescue ? TEXT("cameraRescue")
				: bUseBodyFusionArm ? TEXT("bodyFusion")
				: bMetaHumanFullArmChainDirectFresh ? TEXT("chainDirect")
				: bQuestArmUsesWristEndpoint ? TEXT("questWrist")
				: TEXT("mediaPipe");
			SolveLogInput.bRequireTrackedForApply = CVarQuestWristRequireTrackedForApply.GetValueOnAnyThread() != 0;
			SolveLogInput.bArmIKBranchEntered = bArmIKBranchEntered;
			SolveLogInput.bForceArmIK = CVarQuestWristForceArmIK.GetValueOnAnyThread() != 0;
			SolveLogInput.bWouldEnterArmIKIfApplied = bWouldEnterArmIKIfApplied;
			SolveLogInput.bUseArmIK = bUseArmIK;
			SolveLogInput.LoggedArmIKTargetComp = LoggedArmIKTargetComp;
			SolveLogInput.LoggedArmIKWristComp = LoggedArmIKWristComp;
			SolveLogInput.HandBoneName = HandBone.BoneName;
			SolveLogInput.QuestHandRotationBlend = QuestHandRotationBlend;
			SolveLogInput.bQuestHandRotationApplied = bQuestHandRotationApplied;
			SolveLogInput.QuestHandRotationDeltaDeg = QuestHandRotationDeltaDeg;
			SolveLogInput.ShoulderWorld = ShoulderWorld;
			SolveLogInput.ElbowWorld = ElbowWorld;
			SolveLogInput.HandBeforeLocationCS = HandBeforeCS.GetTranslation();
			SolveLogInput.HandAfterLocationCS = HandAfterCS.GetTranslation();
			SolveLogInput.WristTrace = &QuestWristTrace;
			SolveLogInput.HandTrace = &QuestHandRotationTrace;
			UE_LOG(LogMediaPipePose, Log, TEXT("%s"), *FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristSolveLog(SolveLogInput));
			FMediaPipeQuestWristPostSolveReportInput PostSolveReportInput;
			PostSolveReportInput.TargetActorName = TargetActorName;
			PostSolveReportInput.bIsLeft = bIsLeft;
			PostSolveReportInput.bEmitTraceLogs = CVarQuestWristTrace.GetValueOnAnyThread() != 0;
			PostSolveReportInput.bEmitHud =
				CVarQuestHandHud.GetValueOnAnyThread() != 0 ||
				CVarQuestWristCalibrationHud.GetValueOnAnyThread() != 0;
			PostSolveReportInput.bQuestHandRotationApplied = bQuestHandRotationApplied;
			PostSolveReportInput.bArmIKBranchEntered = bArmIKBranchEntered;
			PostSolveReportInput.bForceArmIK = CVarQuestWristForceArmIK.GetValueOnAnyThread() != 0;
			PostSolveReportInput.DeltaSeconds = DeltaSeconds;
			PostSolveReportInput.QuestHandRotationDeltaDeg = QuestHandRotationDeltaDeg;
			PostSolveReportInput.RequiredLogIntervalSeconds = RequiredLogIntervalSeconds;
			PostSolveReportInput.HandTrace = &QuestHandRotationTrace;
			FMediaPipeQuestWristDebugReporter::EmitPostSolveReports(PostSolveReportInput);
		}
	}
	if (bQuestHandRotationApplied)
	{
		DriveQuestFingerBonesCS(CSPose, bIsLeft, QuestHands, DeltaSeconds);
		// Diagnostic (round-4 palm-trim leak hunt 2026-07-06): the fitted trim only lands
		// ~4 of 12.5deg in the referee. Measure who rotates the hand AFTER the trimmed
		// write: twist of (post-finger-solve hand rot vs post-DriveQuestHandCS hand rot)
		// about the forearm axis. Gate: mp.MediaPipeCameraHandTrace (runtime-settable).
		if (CVarMediaPipeCameraHandTrace.GetValueOnAnyThread() != 0 && HandBone.IsValidToEvaluate())
		{
			static double LastPalmLeakLogSeconds[2] = {0.0, 0.0};
			double& LastLog = LastPalmLeakLogSeconds[bIsLeft ? 0 : 1];
			const double NowSeconds = FPlatformTime::Seconds();
			if (NowSeconds - LastLog > 1.0)
			{
				LastLog = NowSeconds;
				const FQuat HandAfterFingers =
					CSPose.GetComponentSpaceTransform(HandBone.CachedCompactPoseIndex).GetRotation().GetNormalized();
				const FQuat FingerDelta = (HandAfterFingers * HandAfterCS.GetRotation().GetNormalized().Inverse()).GetNormalized();
				FQuat DeltaSwing = FQuat::Identity;
				float DeltaTwistDeg = 0.0f;
				DecomposeSwingTwistDegAroundAxis(FingerDelta, QuestForearmAxisComp, DeltaSwing, DeltaTwistDeg);
				UE_LOG(LogMediaPipePose, Log,
					TEXT("mp.PalmTrimLeak: side=%s fingerSolveTwistDeltaDeg=%.1f fingerSolveTotalDeltaDeg=%.1f trimL=%.1f trimR=%.1f"),
					bIsLeft ? TEXT("L") : TEXT("R"),
					DeltaTwistDeg,
					FMath::RadiansToDegrees(FingerDelta.GetAngle()),
					CVarQuestWristPalmTrimLeftDeg.GetValueOnAnyThread(),
					CVarQuestWristPalmTrimRightDeg.GetValueOnAnyThread());
			}
		}
	}
	if (bMetaHumanFullArmChainDirectFresh)
	{
		FVector PosedShoulderWorld = FVector::ZeroVector;
		FVector PosedElbowWorld = FVector::ZeroVector;
		FVector PosedHandWorld = WristWorld;
		FMediaPipeQuestWristDebugReporter::TryGetArmWorldAfterSolve(
			CSPose,
			UpperBone,
			LowerBone,
			HandBone,
			TargetCompTransform,
			PosedShoulderWorld,
			PosedElbowWorld,
			PosedHandWorld);
		const float TargetReachCm = FVector::Dist(ShoulderWorld, WristWorld);
		const float ElbowBendDeg = ComputeMediaPipeArmChainElbowBendDegrees(ShoulderWorld, ElbowWorld, WristWorld);
		EmitMetaHumanFullArmChainLog(true, bQuestHandRotationApplied, TargetReachCm, ElbowBendDeg, PosedHandWorld);
	}
	if (CVarMetaHumanArmSanity.GetValueOnAnyThread() != 0 ||
		CVarQuestWristTrace.GetValueOnAnyThread() != 0)
	{
		double& LastMetaHumanArmSanityLogTimeSeconds = bIsLeft ? DiagnosticsState.LastMetaHumanArmSanityLogTimeSecondsL : DiagnosticsState.LastMetaHumanArmSanityLogTimeSecondsR;
		const double NowSeconds = FPlatformTime::Seconds();
		const double SanityLogIntervalSeconds = static_cast<double>(FMath::Clamp(CVarMetaHumanArmSanityLogIntervalSeconds.GetValueOnAnyThread(), 0.05f, 5.0f));
		if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, SanityLogIntervalSeconds, LastMetaHumanArmSanityLogTimeSeconds))
		{
			FVector PosedShoulderWorld = FVector::ZeroVector;
			FVector PosedElbowWorld = FVector::ZeroVector;
			FVector PosedHandWorld = FVector::ZeroVector;
			const bool bHasPosedArm = FMediaPipeQuestWristDebugReporter::TryGetArmWorldAfterSolve(
				CSPose,
				UpperBone,
				LowerBone,
				HandBone,
				TargetCompTransform,
				PosedShoulderWorld,
				PosedElbowWorld,
				PosedHandWorld);

			const float MaxWristErrorCm = FMath::Max(0.0f, CVarMetaHumanArmSanityMaxWristErrorCm.GetValueOnAnyThread());
			const float MaxHandRotErrorDeg = FMath::Max(0.0f, CVarMetaHumanArmSanityMaxHandRotErrorDeg.GetValueOnAnyThread());
			const float MaxBasisErrorDeg = FMath::Max(0.0f, CVarMetaHumanArmSanityMaxBasisErrorDeg.GetValueOnAnyThread());
			const float MaxLengthErrorCm = FMath::Max(0.0f, CVarMetaHumanArmSanityMaxLengthErrorCm.GetValueOnAnyThread());
			const float MinElbowBendDeg = FMath::Max(0.0f, CVarMetaHumanArmSanityMinElbowBendDeg.GetValueOnAnyThread());
			const float MaxSwingDeg = FMath::Max(0.0f, CVarMetaHumanArmSanityMaxSwingDeg.GetValueOnAnyThread());

			const FMediaPipeMetaHumanArmSanityFormatInput SanityInput =
				FMediaPipeQuestWristDiagnosticFormatter::BuildMetaHumanArmSanityInput(
					TargetActorName,
					bIsLeft,
					bHasPosedArm,
					QuestArmMode,
					bQuestHandRotationApplied,
					bArmIKBranchEntered,
					MaxWristErrorCm,
					MaxHandRotErrorDeg,
					MaxBasisErrorDeg,
					MaxLengthErrorCm,
					MinElbowBendDeg,
					MaxSwingDeg,
					RefUpperLen,
					RefLowerLen,
					QuestWristTrace,
					QuestHandRotationTrace,
					PosedShoulderWorld,
					PosedElbowWorld,
					PosedHandWorld,
					ShoulderWorld,
					ElbowWorld);

			FMediaPipeQuestWristDebugReporter::EmitMetaHumanArmSanityReport(
				SanityInput,
				CVarQuestHandCompare.GetValueOnAnyThread() >= 2,
				bIsLeft,
				SanityLogIntervalSeconds);
		}
	}
	if (CVarQuestHandCompare.GetValueOnAnyThread() >= 1)
	{
		double& LastQuestHandCompareLogTimeSeconds = bIsLeft ? DiagnosticsState.LastQuestHandCompareLogTimeSecondsL : DiagnosticsState.LastQuestHandCompareLogTimeSecondsR;
		const double NowSeconds = FPlatformTime::Seconds();
		const double CompareLogIntervalSeconds = static_cast<double>(FMath::Clamp(CVarQuestWristTraceLogIntervalSeconds.GetValueOnAnyThread(), 0.05f, 5.0f));
		if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, CompareLogIntervalSeconds, LastQuestHandCompareLogTimeSeconds))
		{
			const FString TargetActorLabel = TargetActorName.ToString();
			FMediaPipeQuestHandCompareBuildInput CompareInput;
			CompareInput.TargetActorName = TargetActorName;
			CompareInput.bIsLeft = bIsLeft;
			CompareInput.CompareMode = CVarQuestHandCompare.GetValueOnAnyThread();
			CompareInput.bVisibleMetaHuman = TargetMetaHumanProfile.bIsMetaHuman;
			CompareInput.bQuestHandRotationApplied = bQuestHandRotationApplied;
			CompareInput.bArmIKBranchEntered = bArmIKBranchEntered;
			CompareInput.bForceArmIK = CVarQuestWristForceArmIK.GetValueOnAnyThread() != 0;
			CompareInput.QuestHands = &QuestHands;
			CompareInput.QuestWristTrace = QuestWristTrace;
			CompareInput.QuestHandRotationTrace = QuestHandRotationTrace;
			CompareInput.TargetCompTransform = TargetCompTransform;
			CompareInput.PoseToWorldTransform = PoseToWorldTransform;
			CompareInput.AvatarHandComp = HandAfterCS.GetTranslation();
			CompareInput.SolvedWristWorld = WristWorld;
			CompareInput.ShoulderWorld = ShoulderWorld;

			FMediaPipeQuestHandCompareDiagnostics::EmitReport(
				CompareInput,
				CSPose,
				HandBone,
				bIsLeft ? FingerBonesL : FingerBonesR,
				[this](const FVector& QuestDirectionWorld, FVector& OutMediaDirectionWorld)
				{
					return TryMapQuestDirectionToMediaWorld(QuestDirectionWorld, OutMediaDirectionWorld);
				});
		}
	}
	if (bQuestHandRotationApplied)
	{
		return;
	}

	// USER FEEDBACK (2026-07-03): with the overhead rescue keeping the arm on the camera, the
	// HAND froze at its last Quest rotation and snapped on reacquire; the first camera-hand
	// attempt then twisted the wrist because it trusted MediaPipe's left/right HAND LABELS,
	// which do not survive the mirrored self-view. The camera hand for this arm is now chosen
	// by IMAGE-SPACE PROXIMITY: the detected hand whose normalized wrist sits closest to this
	// side's normalized pose wrist (labels ignored; both models share the same image).
	// Ownership is the LATCHED bCameraOwnsHandPose (rescue latch or untracked side), not the
	// per-frame tracked flag: gating on the raw flag let camera and Quest alternate hand
	// ownership at tracked-flag flicker rate (2026-07-03 stability verdict).
	const bool bMediaPipeHandRotationOnQuestLoss =
		CVarMediaPipeHandRotationOnQuestLoss.GetValueOnAnyThread() != 0 &&
		bCameraOwnsHandPose &&
		bHasMediaPipeArmWorld;
	const bool bMediaPipeFingersOnQuestLoss =
		CVarMediaPipeFingersOnQuestLoss.GetValueOnAnyThread() != 0 &&
		bCameraOwnsHandPose &&
		bHasMediaPipeArmWorld;
	// Camera-hand rotation continuity lives in the KEYED side state (2026-07-03, measured:
	// CacheBones_AnyThread -> BuildReferencePoseCache -> ResetRotationSmoothing runs every frame
	// in live VR, wiping the FMediaPipeArmSolverState node members this path previously used, so
	// none of the smoothing/branch-dwell mitigations ever engaged). The legacy camera-only mode
	// (bMediaPipeHandRotationOnQuestLoss off) keeps the node members untouched.
	const bool bUseKeyedCameraHandState = bMediaPipeHandRotationOnQuestLoss && RuntimeStateKey != 0;
	const int32 CameraHandTraceEnabled = CVarMediaPipeCameraHandTrace.GetValueOnAnyThread();
	// "Hold last rotation" must APPLY the held rotation: returning without applying leaves the
	// hand at the pass-through pose for that frame (2026-07-03 log: handSel=none frames occurred
	// about once per second per side during the overhead window - each one was a visible
	// snap-out/snap-in). The hold expires after 0.75 s without a real camera apply.
	auto ApplyHeldCameraHandRotation = [&](FCSPose<FCompactPose>& InOutCSPose) -> bool
	{
		if (!bUseKeyedCameraHandState ||
			!QuestWristSideState.bHasCameraHandLastApplied ||
			!HandBone.IsValidToEvaluate())
		{
			return false;
		}
		const double HeldNowSeconds = FPlatformTime::Seconds();
		if (QuestWristSideState.LastMediaPipeHandRotationApplyTimeSeconds < 0.0 ||
			HeldNowSeconds - QuestWristSideState.LastMediaPipeHandRotationApplyTimeSeconds > 0.75)
		{
			return false;
		}
		ApplyRotationCS(InOutCSPose, HandBone, QuestWristSideState.CameraHandLastAppliedRotCS);
		return true;
	};

	const FMediaPipeRawHandLandmarks* SelectedMediaPipeHandWorld = nullptr;
	const FMediaPipeRawHandLandmarks* SelectedMediaPipeHandNormalized = nullptr;
	bool bSelectedMediaPipeHandIsLeftArray = false;
	float SelectedMediaPipeHandImageDist = -1.0f;
	if ((bMediaPipeHandRotationOnQuestLoss || bMediaPipeFingersOnQuestLoss) &&
		bHasPoseHands &&
		bHasPoseFrame &&
		PoseFrame.Normalized.IsValidIndex(WristLm))
	{
		const FMediaPipePoseLandmark& PoseWristNorm = PoseFrame.Normalized.Points[WristLm];
		auto ImageDistToPoseWrist = [&](const FMediaPipeRawHandLandmarks& HandNormalized) -> float
		{
			const FMediaPipeRawHandLandmark& HandWristNorm = HandNormalized.Landmarks[0];
			const float DX = HandWristNorm.X - PoseWristNorm.X;
			const float DY = HandWristNorm.Y - PoseWristNorm.Y;
			return FMath::Sqrt(DX * DX + DY * DY);
		};
		// Generous acceptance radius in normalized image units: the pose wrist and the hand
		// model's wrist land within a few percent of the frame for the same physical hand,
		// while the OTHER hand is at least a shoulder width away whenever both are detected.
		const float MaxImageDist = 0.12f;
		const float LeftDist = PoseHands.bHasLeft != 0
			? ImageDistToPoseWrist(PoseHands.LeftNormalized)
			: TNumericLimits<float>::Max();
		const float RightDist = PoseHands.bHasRight != 0
			? ImageDistToPoseWrist(PoseHands.RightNormalized)
			: TNumericLimits<float>::Max();
		bool bChooseLeftArray = LeftDist <= RightDist;
		float ChosenDist = bChooseLeftArray ? LeftDist : RightDist;
		// STICKINESS (2026-07-03 worn verdict: twist/snap): overhead both hands hover near each
		// other in the image and the nearest match can alternate at frame rate. Keep the arm on
		// its previous hand unless the other one is decisively closer.
		const uint8 StickySide = QuestWristSideState.MediaPipeHandStickyArraySide;
		if (StickySide != 255)
		{
			const float StickyDist = StickySide == 0 ? LeftDist : RightDist;
			const float OtherDist = StickySide == 0 ? RightDist : LeftDist;
			if (StickyDist <= MaxImageDist && OtherDist >= StickyDist * 0.7f)
			{
				bChooseLeftArray = (StickySide == 0);
				ChosenDist = StickyDist;
			}
		}
		// WRONG-HAND VETO (measured live 2026-07-03 07:13): with this side's own hand undetected
		// for a stretch, the nearest-match adopted the OTHER arm's hand (side L matched mpRight
		// at imageDist 0.087 - a true match measures 0.01-0.02 - then stickiness held it and the
		// wrist snapped 141 degrees into reversed-forward branches). A hand that sits markedly
		// closer to the OTHER arm's pose wrist belongs to that arm: never adopt it, hold instead.
		if (ChosenDist <= MaxImageDist)
		{
			bool bBelongsToOtherArm = false;
			const int32 OtherWristLm = bIsLeft
				? (int32)EMediaPipePoseLandmark::RightWrist
				: (int32)EMediaPipePoseLandmark::LeftWrist;
			if (PoseFrame.Normalized.IsValidIndex(OtherWristLm))
			{
				const FMediaPipePoseLandmark& OtherWristNorm = PoseFrame.Normalized.Points[OtherWristLm];
				if (FMath::IsFinite(OtherWristNorm.X) && FMath::IsFinite(OtherWristNorm.Y))
				{
					const FMediaPipeRawHandLandmarks& ChosenNormalized =
						bChooseLeftArray ? PoseHands.LeftNormalized : PoseHands.RightNormalized;
					const FMediaPipeRawHandLandmark& ChosenWristNorm = ChosenNormalized.Landmarks[HandLm_Wrist];
					const float OtherDX = ChosenWristNorm.X - OtherWristNorm.X;
					const float OtherDY = ChosenWristNorm.Y - OtherWristNorm.Y;
					const float DistToOtherArmWrist = FMath::Sqrt(OtherDX * OtherDX + OtherDY * OtherDY);
					bBelongsToOtherArm = DistToOtherArmWrist < ChosenDist * 0.9f;
				}
			}
			if (!bBelongsToOtherArm)
			{
				SelectedMediaPipeHandWorld = bChooseLeftArray ? &PoseHands.LeftWorld : &PoseHands.RightWorld;
				SelectedMediaPipeHandNormalized = bChooseLeftArray ? &PoseHands.LeftNormalized : &PoseHands.RightNormalized;
				bSelectedMediaPipeHandIsLeftArray = bChooseLeftArray;
				SelectedMediaPipeHandImageDist = ChosenDist;
			}
		}
		if (RuntimeStateKey != 0)
		{
			QuestWristSideState.MediaPipeHandStickyArraySide =
				SelectedMediaPipeHandWorld ? (bSelectedMediaPipeHandIsLeftArray ? 0 : 1) : 255;
		}
	}

	// Fingers while the Quest hand is lost: feed the proximity-matched camera hand through the
	// existing (user-validated) segment-direction finger solver via a synthetic OpenXR-layout
	// snapshot. Without a camera hand this stays the real Quest snapshot, whose untracked side
	// keeps the validated hold-pose behavior.
	FQuestHandTrackingSnapshot SyntheticMediaPipeQuestHands;
	bool bHasSyntheticMediaPipeQuestHands = false;
	bool bQuestFingersForcedHold = false;
	if (bMediaPipeFingersOnQuestLoss && SelectedMediaPipeHandWorld)
	{
		SyntheticMediaPipeQuestHands.Reset();
		FillSyntheticQuestHandSideFromMediaPipe(
			*SelectedMediaPipeHandWorld,
			WristWorld,
			PoseToWorldTransform,
			WorldScale,
			bMirrorLandmarksLR,
			bIsLeft,
			SyntheticMediaPipeQuestHands);
		bHasSyntheticMediaPipeQuestHands = true;
	}
	else if (bCameraOwnsHandPose && CVarMediaPipeFingersOnQuestLoss.GetValueOnAnyThread() != 0)
	{
		// The camera owns the hand but cannot see it this frame: force the side untracked in a
		// copy so the finger pose gate HOLDS. Passing the raw snapshot let single tracked-flag
		// blips drive one frame of synthesized Quest fingers between camera frames - the same
		// alternating-owner snap as the wrist (2026-07-03 stability verdict).
		SyntheticMediaPipeQuestHands = QuestHands;
		if (bIsLeft)
		{
			SyntheticMediaPipeQuestHands.bLeftTracked = 0;
		}
		else
		{
			SyntheticMediaPipeQuestHands.bRightTracked = 0;
		}
		bQuestFingersForcedHold = true;
	}
	const FQuestHandTrackingSnapshot& QuestLossFingerSnapshot =
		(bHasSyntheticMediaPipeQuestHands || bQuestFingersForcedHold) ? SyntheticMediaPipeQuestHands : QuestHands;

	// Throttled takeover diagnostic: names which camera hand (if any) owns this arm's hand pose
	// while the camera owns it (including hold frames where no camera hand is visible), so
	// worn-headset verdicts map to data.
	if (bCameraOwnsHandPose)
	{
		double& LastTakeoverLogTimeSeconds = bIsLeft
			? DiagnosticsState.LastMediaPipeHandTakeoverLogTimeSecondsL
			: DiagnosticsState.LastMediaPipeHandTakeoverLogTimeSecondsR;
		const double TakeoverNowSeconds = FPlatformTime::Seconds();
		if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(TakeoverNowSeconds, 1.0, LastTakeoverLogTimeSeconds))
		{
			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.MediaPipeHandTakeover: actor=%s node=%llu key=%u side=%s ownsHand=%d rescueLatched=%d rotWanted=%d fingersWanted=%d handSel=%s imageDist=%.3f mpHandLeftPresent=%d mpHandRightPresent=%d syntheticFingers=%d fingersHeld=%d"),
				*TargetActorName.ToString(),
				NodeDiagSerial,
				RuntimeStateKey,
				bIsLeft ? TEXT("L") : TEXT("R"),
				bCameraOwnsHandPose ? 1 : 0,
				QuestWristSideState.bArmRescueActive ? 1 : 0,
				bMediaPipeHandRotationOnQuestLoss ? 1 : 0,
				bMediaPipeFingersOnQuestLoss ? 1 : 0,
				SelectedMediaPipeHandWorld ? (bSelectedMediaPipeHandIsLeftArray ? TEXT("mpLeft") : TEXT("mpRight")) : TEXT("none"),
				SelectedMediaPipeHandImageDist,
				bHasPoseHands ? (PoseHands.bHasLeft != 0 ? 1 : 0) : 0,
				bHasPoseHands ? (PoseHands.bHasRight != 0 ? 1 : 0) : 0,
				bHasSyntheticMediaPipeQuestHands ? 1 : 0,
				bQuestFingersForcedHold ? 1 : 0);
		}
	}

	bool bQuestFingersAlreadyDriven = false;
	if (bMetaHumanFullArmChainDirectFresh)
	{
		DriveQuestFingerBonesCS(CSPose, bIsLeft, QuestLossFingerSnapshot, DeltaSeconds);
		if (!bMediaPipeHandRotationOnQuestLoss)
		{
			return;
		}
		bQuestFingersAlreadyDriven = true;
	}
	else if (!bDriveHandRotation)
	{
		DriveQuestFingerBonesCS(CSPose, bIsLeft, QuestLossFingerSnapshot, DeltaSeconds);
		if (!bMediaPipeHandRotationOnQuestLoss)
		{
			return;
		}
		bQuestFingersAlreadyDriven = true;
	}

	FVector HandForwardWorld = FVector::ZeroVector;
	FVector HandUpWorld = FVector::ZeroVector;
	bool bHasHandBasis = false;
	float BasisSin = 1.0f;
	const float RefCameraThumbUpDot = bIsLeft ? RefHandCameraThumbUpDotL : RefHandCameraThumbUpDotR;
	float MeasuredThumbUpDot = 0.0f;
	int32 CameraHandChiralityUsed = 0;
	bool bThumbChiralityConfident = false;
	float CameraHandCurlAvg = -2.0f;

	if (bHasPoseHands)
	{
		auto TryGetRawHandPointWorld = [&](const int32 HandIdx, FVector& OutWorld) -> bool
		{
			// Prefer the image-space proximity match (label-free; survives the mirrored
			// self-view). The label-based lookup remains only for the legacy camera-only mode
			// where no proximity selection ran - in quest-loss mode a failed proximity match
			// must NOT fall back to labels (that is how the wrong hand drove this arm).
			const FMediaPipeRawHandLandmarks* HandWorldPtr = SelectedMediaPipeHandWorld;
			if (!HandWorldPtr)
			{
				if (bMediaPipeHandRotationOnQuestLoss)
				{
					return false;
				}
				const bool bHasSide = bIsLeft ? (PoseHands.bHasLeft != 0) : (PoseHands.bHasRight != 0);
				if (!bHasSide)
				{
					return false;
				}
				HandWorldPtr = bIsLeft ? &PoseHands.LeftWorld : &PoseHands.RightWorld;
			}

			const FMediaPipeRawHandLandmarks& HandWorld = *HandWorldPtr;
			const FMediaPipeRawHandLandmark& WristRaw = HandWorld.Landmarks[HandLm_Wrist];
			const FMediaPipeRawHandLandmark& PointRaw = HandWorld.Landmarks[HandIdx];

			const FVector DeltaMp(
				PointRaw.X - WristRaw.X,
				PointRaw.Y - WristRaw.Y,
				PointRaw.Z - WristRaw.Z);
			FVector DeltaUe = MediaPipePoseCoordinate::MpWorldToUeLocalUnscaled(DeltaMp, bMirrorLandmarksLR) * WorldScale;
			DeltaUe = PoseToWorldTransform.TransformVectorNoScale(DeltaUe);
			OutWorld = WristWorld + DeltaUe;
			return true;
		};

		FVector IndexMcpWorld = FVector::ZeroVector;
		FVector PinkyMcpWorld = FVector::ZeroVector;
		FVector MiddleMcpWorld = FVector::ZeroVector;
		if (TryGetRawHandPointWorld(HandLm_IndexMcp, IndexMcpWorld) &&
			TryGetRawHandPointWorld(HandLm_PinkyMcp, PinkyMcpWorld))
		{
			const bool bHasMiddle = TryGetRawHandPointWorld(HandLm_MiddleMcp, MiddleMcpWorld);

			// IMAGE-PLANE BASIS (rebuilt 2026-07-03 round four): the hand landmarker's DEPTH is
			// the liar - rounds 3/4 measured identical 2D detection quality on both hands while
			// the 3D forward sat near-perpendicular to the forearm on one side all session
			// (fwdDotForearm 0.19 vs 0.68), producing drift the 2D video never showed. The basis
			// is therefore built from the landmark deltas PROJECTED onto the camera image plane
			// (the same proven MpWorldToUe mapping; projection needs no new axis-sign
			// assumptions since a plane is sign-invariant in its normal). What the 2D video
			// shows is now literally what drives the wrist; the unobservable out-of-plane tilt
			// follows the forearm instead of following depth noise. An edge-on hand collapses
			// the 2D across vector -> BasisSin shrinks -> twist freezes (bTwistReliable) or the
			// hold path keeps the last rotation: a monocular physical limit, frozen not spun.
			const FVector ImagePlaneNormalWorld = PoseToWorldTransform.TransformVectorNoScale(
				MediaPipePoseCoordinate::MpWorldToUeLocalUnscaled(FVector(0.0f, 0.0f, 1.0f), bMirrorLandmarksLR)).GetSafeNormal();
			auto ProjectToImagePlane = [&ImagePlaneNormalWorld](const FVector& Direction) -> FVector
			{
				if (ImagePlaneNormalWorld.IsNearlyZero())
				{
					return Direction.GetSafeNormal();
				}
				return (Direction - FVector::DotProduct(Direction, ImagePlaneNormalWorld) * ImagePlaneNormalWorld).GetSafeNormal();
			};

			HandForwardWorld = ProjectToImagePlane((IndexMcpWorld + PinkyMcpWorld) * 0.5f - WristWorld);
			if (HandForwardWorld.IsNearlyZero() && bHasMiddle)
			{
				HandForwardWorld = ProjectToImagePlane(MiddleMcpWorld - WristWorld);
			}

			const FVector AcrossWorld = ProjectToImagePlane(IndexMcpWorld - PinkyMcpWorld);
			if (!HandForwardWorld.IsNearlyZero() && !AcrossWorld.IsNearlyZero())
			{
				BasisSin = FVector::CrossProduct(HandForwardWorld, AcrossWorld).Size();
				HandUpWorld = FVector::CrossProduct(HandForwardWorld, AcrossWorld).GetSafeNormal();
				if (HandUpWorld.IsNearlyZero())
				{
					HandUpWorld = FVector::CrossProduct(HandForwardWorld, PoseUpperWorld).GetSafeNormal();
				}
				bHasHandBasis = !HandUpWorld.IsNearlyZero();
			}

			// PALM CHIRALITY (2026-07-03 trace: the palm-up and palm-flipped branches split
			// 408/326 across one overhead session - the up sign was a continuity coin-flip).
			// The thumb is the one landmark that breaks a hand's mirror symmetry: its side of
			// the measured palm plane, compared with the ref-pose thumb side
			// (RefHandCameraThumbUpDot), resolves the capture chain's mirror parity. The parity
			// is a PHYSICAL CONSTANT (Camo mirror x self-view mirror), so it LOCKS after 0.25 s
			// of consistent confident thumb frames and re-flips only after 1.0 s of sustained
			// contrary evidence - round two measured 34/26 transient flips (L/R) from per-frame
			// re-resolution whenever the thumb crossed the palm plane during palm rotations,
			// each one a visible 180-degree palm spin.
			if (bMediaPipeHandRotationOnQuestLoss && bHasHandBasis)
			{
				int8 FrameChiralitySign = 0;
				FVector ThumbMcpWorld = FVector::ZeroVector;
				if (FMath::Abs(RefCameraThumbUpDot) >= 0.05f &&
					TryGetRawHandPointWorld(HandLm_ThumbMcp, ThumbMcpWorld))
				{
					MeasuredThumbUpDot = FVector::DotProduct(
						(ThumbMcpWorld - WristWorld).GetSafeNormal(), HandUpWorld);
					if (FMath::Abs(MeasuredThumbUpDot) >= 0.2f)
					{
						bThumbChiralityConfident = true;
						FrameChiralitySign =
							((MeasuredThumbUpDot > 0.0f) == (RefCameraThumbUpDot > 0.0f)) ? 1 : -1;
					}
				}
				if (RuntimeStateKey != 0)
				{
					const float ChiralityStepSeconds =
						DeltaSeconds > KINDA_SMALL_NUMBER ? DeltaSeconds : (1.0f / 30.0f);
					int8& LockedSign = QuestWristSideState.CameraHandChiralitySign;
					int8& PendingSign = QuestWristSideState.CameraHandChiralityPendingSign;
					float& PendingSeconds = QuestWristSideState.CameraHandChiralityPendingSeconds;
					if (FrameChiralitySign != 0)
					{
						if (FrameChiralitySign == LockedSign)
						{
							PendingSign = LockedSign;
							PendingSeconds = 0.0f;
						}
						else
						{
							if (PendingSign == FrameChiralitySign)
							{
								PendingSeconds += ChiralityStepSeconds;
							}
							else
							{
								PendingSign = FrameChiralitySign;
								PendingSeconds = ChiralityStepSeconds;
							}
							const float RequiredSeconds = (LockedSign == 0) ? 0.25f : 1.0f;
							if (PendingSeconds >= RequiredSeconds)
							{
								LockedSign = FrameChiralitySign;
								PendingSign = FrameChiralitySign;
								PendingSeconds = 0.0f;
							}
						}
					}
					CameraHandChiralityUsed = LockedSign != 0 ? LockedSign : FrameChiralitySign;
				}
				else
				{
					CameraHandChiralityUsed = FrameChiralitySign;
				}
				if (CameraHandChiralityUsed == -1)
				{
					HandUpWorld = -HandUpWorld;
				}
			}

			// Trace-only fist metric from the raw landmarker deltas: +1 = straight fingers,
			// near 0 or negative = curled. Answers "does MediaPipe see the clenched fist"
			// independently of what the finger solver applies.
			if (CameraHandTraceEnabled != 0 && SelectedMediaPipeHandWorld)
			{
				float CurlSum = 0.0f;
				int32 CurlCount = 0;
				const int32 CurlMcpIndices[4] = {HandLm_IndexMcp, HandLm_MiddleMcp, 13, HandLm_PinkyMcp};
				for (const int32 CurlMcp : CurlMcpIndices)
				{
					const FMediaPipeRawHandLandmark& ProxA = SelectedMediaPipeHandWorld->Landmarks[CurlMcp];
					const FMediaPipeRawHandLandmark& ProxB = SelectedMediaPipeHandWorld->Landmarks[CurlMcp + 1];
					const FMediaPipeRawHandLandmark& DistA = SelectedMediaPipeHandWorld->Landmarks[CurlMcp + 2];
					const FMediaPipeRawHandLandmark& DistB = SelectedMediaPipeHandWorld->Landmarks[CurlMcp + 3];
					const FVector ProximalSeg = FVector(ProxB.X - ProxA.X, ProxB.Y - ProxA.Y, ProxB.Z - ProxA.Z).GetSafeNormal();
					const FVector DistalSeg = FVector(DistB.X - DistA.X, DistB.Y - DistA.Y, DistB.Z - DistA.Z).GetSafeNormal();
					if (!ProximalSeg.IsNearlyZero() && !DistalSeg.IsNearlyZero())
					{
						CurlSum += FVector::DotProduct(ProximalSeg, DistalSeg);
						CurlCount += 1;
					}
				}
				if (CurlCount > 0)
				{
					CameraHandCurlAvg = CurlSum / CurlCount;
				}
			}
		}
	}

	// 2D/3D forward discriminator (round three, 2026-07-03): the left hand's measured 3D forward
	// sat nearly perpendicular to the forearm all session (fwdDotForearm avg 0.19 vs 0.68 right)
	// while arm reliability and image-space hand matching were IDENTICAL on both sides - the 3D
	// geometry is the suspect (ultrawide edge depth distortion), not detection. fwdDot2D checks
	// the same alignment purely in image space: high 2D agreement + low 3D agreement = the depth
	// axis is lying for that hand.
	float FwdDot2D = -2.0f;
	if (SelectedMediaPipeHandNormalized &&
		bHasPoseFrame &&
		PoseFrame.Normalized.IsValidIndex(WristLm) &&
		PoseFrame.Normalized.IsValidIndex(ElbowLm))
	{
		const FMediaPipePoseLandmark& PoseWristN = PoseFrame.Normalized.Points[WristLm];
		const FMediaPipePoseLandmark& PoseElbowN = PoseFrame.Normalized.Points[ElbowLm];
		const FMediaPipeRawHandLandmark& HandWristN = SelectedMediaPipeHandNormalized->Landmarks[HandLm_Wrist];
		const FMediaPipeRawHandLandmark& HandIndexN = SelectedMediaPipeHandNormalized->Landmarks[HandLm_IndexMcp];
		const FMediaPipeRawHandLandmark& HandPinkyN = SelectedMediaPipeHandNormalized->Landmarks[HandLm_PinkyMcp];
		FVector2D Forearm2D(PoseWristN.X - PoseElbowN.X, PoseWristN.Y - PoseElbowN.Y);
		FVector2D HandForward2D(
			(HandIndexN.X + HandPinkyN.X) * 0.5f - HandWristN.X,
			(HandIndexN.Y + HandPinkyN.Y) * 0.5f - HandWristN.Y);
		if (Forearm2D.Normalize(KINDA_SMALL_NUMBER) && HandForward2D.Normalize(KINDA_SMALL_NUMBER))
		{
			FwdDot2D = FVector2D::DotProduct(Forearm2D, HandForward2D);
		}
	}
	// World-space forearm alignment gate: a hand forward near-perpendicular to its forearm makes
	// the swing/twist decomposition ill-conditioned (the round-three left-hand drift: twist
	// target spread 97 deg vs 35 on the healthy right). Such frames HOLD instead of drive.
	const FVector ForearmAxisWorldForGate = TargetCompTransform.TransformVectorNoScale(
		(bHasSmoothedLowerRotCS ? SmoothedLowerRotCS.RotateVector(RefLowerDir) : PoseLowerComp).GetSafeNormal()).GetSafeNormal();
	const float HandForwardDotForearmWorld = (bHasHandBasis && !ForearmAxisWorldForGate.IsNearlyZero())
		? FVector::DotProduct(HandForwardWorld, ForearmAxisWorldForGate)
		: 1.0f;

	// STABILITY (2026-07-03 worn verdict: overhead hands twisted and snapped): while the camera
	// owns the hand, ONLY the proximity-matched 21-landmark basis may drive it. The coarse pose
	// index/pinky palm basis below uses a different geometric convention, and swapping sources
	// at hand-landmarker detection-flicker rate read as snapping; a degenerate edge-on basis
	// (small BasisSin - with the image-plane basis this now means a genuinely edge-on hand)
	// holds the last smoothed rotation - the wrist POSITION keeps tracking and fingers keep
	// their hold-pose. Round four regression lesson: do NOT gate on 3D forearm alignment here -
	// a hard threshold between live and held rotations alternated the output at the boundary
	// and destabilized even the healthy hand.
	if (bMediaPipeHandRotationOnQuestLoss && (!bHasHandBasis || BasisSin < 0.15f))
	{
		const bool bHeldRotationApplied = ApplyHeldCameraHandRotation(CSPose);
		if (CameraHandTraceEnabled != 0)
		{
			const TCHAR* HoldPhase = !bHasHandBasis
				? TEXT("holdNoHand")
				: TEXT("holdDegenerateBasis");
			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.MediaPipeCameraHandTrace: actor=%s node=%llu key=%u side=%s phase=%s held=%d basisSin=%.3f fwdDotForearmW=%.2f fwdDot2D=%.2f handSel=%s imageDist=%.3f cacheBones=%llu dt=%.4f"),
				*TargetActorName.ToString(),
				NodeDiagSerial,
				RuntimeStateKey,
				bIsLeft ? TEXT("L") : TEXT("R"),
				HoldPhase,
				bHeldRotationApplied ? 1 : 0,
				BasisSin,
				HandForwardDotForearmWorld,
				FwdDot2D,
				SelectedMediaPipeHandWorld ? (bSelectedMediaPipeHandIsLeftArray ? TEXT("mpLeft") : TEXT("mpRight")) : TEXT("none"),
				SelectedMediaPipeHandImageDist,
				CacheBonesDiagCount,
				DeltaSeconds);
		}
		if (!bQuestFingersAlreadyDriven)
		{
			DriveQuestFingerBonesCS(CSPose, bIsLeft, QuestLossFingerSnapshot, DeltaSeconds);
		}
		return;
	}

	if (!bHasHandBasis)
	{
		FVector IndexWorld = FVector::ZeroVector;
		FVector PinkyWorld = FVector::ZeroVector;
		if (!TryGetLmWorld(IndexLm, IndexWorld) || !TryGetLmWorld(PinkyLm, PinkyWorld))
		{
			return;
		}

		HandForwardWorld = ((IndexWorld + PinkyWorld) * 0.5f - WristWorld).GetSafeNormal();
		if (HandForwardWorld.IsNearlyZero())
		{
			HandForwardWorld = (IndexWorld - WristWorld).GetSafeNormal();
		}
		if (HandForwardWorld.IsNearlyZero())
		{
			return;
		}

		const FVector AcrossWorld = (IndexWorld - PinkyWorld).GetSafeNormal();
		if (AcrossWorld.IsNearlyZero())
		{
			return;
		}

		BasisSin = FVector::CrossProduct(HandForwardWorld, AcrossWorld).Size();
		HandUpWorld = FVector::CrossProduct(HandForwardWorld, AcrossWorld).GetSafeNormal();
		if (HandUpWorld.IsNearlyZero())
		{
			HandUpWorld = FVector::CrossProduct(HandForwardWorld, PoseUpperWorld).GetSafeNormal();
		}
		if (HandUpWorld.IsNearlyZero())
		{
			return;
		}
		bHasHandBasis = true;
	}

	const bool bTwistReliable = (BasisSin >= HandTwistMinBasisSin);
	const FVector HandForwardComp = TargetCompTransform.InverseTransformVectorNoScale(HandForwardWorld).GetSafeNormal();
	const FVector HandUpComp = TargetCompTransform.InverseTransformVectorNoScale(HandUpWorld).GetSafeNormal();
	if (HandForwardComp.IsNearlyZero() || HandUpComp.IsNearlyZero())
	{
		// Degenerate frame while camera-owned: hold the last applied rotation instead of letting
		// the hand revert to the pass-through pose for one frame (same rule as the basis hold).
		ApplyHeldCameraHandRotation(CSPose);
		if (bMediaPipeHandRotationOnQuestLoss && !bQuestFingersAlreadyDriven)
		{
			DriveQuestFingerBonesCS(CSPose, bIsLeft, QuestLossFingerSnapshot, DeltaSeconds);
		}
		return;
	}

	const FVector ForearmAxisComp = (bHasSmoothedLowerRotCS ? SmoothedLowerRotCS.RotateVector(RefLowerDir) : PoseLowerComp).GetSafeNormal();
	if (ForearmAxisComp.IsNearlyZero())
	{
		ApplyHeldCameraHandRotation(CSPose);
		if (bMediaPipeHandRotationOnQuestLoss && !bQuestFingersAlreadyDriven)
		{
			DriveQuestFingerBonesCS(CSPose, bIsLeft, QuestLossFingerSnapshot, DeltaSeconds);
		}
		return;
	}

	// In quest-loss camera mode the measured knuckle basis maps onto the ref-pose KNUCKLE
	// geometry evaluated with the same formula (RefHandCameraBasisComp), so the palm side is
	// deterministic by construction. Mapping onto the bone-axis basis (legacy path below) left
	// the up sign unmodeled - the 2026-07-03 trace showed it settling on either palm branch.
	const bool bHasRefCameraBasis = bIsLeft ? bHasRefHandCameraBasisL : bHasRefHandCameraBasisR;
	const FQuat& CameraMappingRefBasisComp =
		(bMediaPipeHandRotationOnQuestLoss && bHasRefCameraBasis)
			? (bIsLeft ? RefHandCameraBasisCompL : RefHandCameraBasisCompR)
			: RefHandBasisComp;
	auto MakeTargetHandRot = [&](const FVector& ForwardC, const FVector& UpC) -> FQuat
	{
		const FQuat TargetBasisComp = MakeQuatFromForwardUp(ForwardC, UpC);
		const FQuat DeltaHand = TargetBasisComp * CameraMappingRefBasisComp.Inverse();
		return (DeltaHand * RefHandComp).GetNormalized();
	};

	// In quest-loss camera mode the continuity state binds to the KEYED side store (survives the
	// per-frame CacheBones reset, measured 2026-07-03); legacy camera-only mode keeps the node
	// members so behavior there is unchanged.
	bool& bHasLastGoodTarget = bUseKeyedCameraHandState
		? QuestWristSideState.bHasCameraHandLastGoodTarget
		: (bIsLeft ? LeftArmState.bHasLastGoodHandTarget : RightArmState.bHasLastGoodHandTarget);
	FQuat& LastGoodTarget = bUseKeyedCameraHandState
		? QuestWristSideState.CameraHandLastGoodTargetCS
		: (bIsLeft ? LeftArmState.LastGoodHandTargetCS : RightArmState.LastGoodHandTargetCS);
	int32& ActiveBranch = bUseKeyedCameraHandState
		? QuestWristSideState.CameraHandActiveBranch
		: (bIsLeft ? LeftArmState.ActiveHandTargetBranch : RightArmState.ActiveHandTargetBranch);
	int32& PendingBranch = bUseKeyedCameraHandState
		? QuestWristSideState.CameraHandPendingBranch
		: (bIsLeft ? LeftArmState.PendingHandTargetBranch : RightArmState.PendingHandTargetBranch);
	int32& PendingFrames = bUseKeyedCameraHandState
		? QuestWristSideState.CameraHandPendingBranchFrames
		: (bIsLeft ? LeftArmState.PendingHandTargetBranchFrames : RightArmState.PendingHandTargetBranchFrames);

	bool& bHasSmoothedSwing = bUseKeyedCameraHandState
		? QuestWristSideState.bHasCameraHandSmoothedSwing
		: (bIsLeft ? LeftArmState.bHasSmoothedHandSwingCS : RightArmState.bHasSmoothedHandSwingCS);
	FQuat& SmoothedSwing = bUseKeyedCameraHandState
		? QuestWristSideState.CameraHandSmoothedSwingCS
		: (bIsLeft ? LeftArmState.SmoothedHandSwingCS : RightArmState.SmoothedHandSwingCS);
	bool& bHasSmoothedTwist = bUseKeyedCameraHandState
		? QuestWristSideState.bHasCameraHandSmoothedTwist
		: (bIsLeft ? LeftArmState.bHasSmoothedHandTwist : RightArmState.bHasSmoothedHandTwist);
	float& SmoothedTwistDeg = bUseKeyedCameraHandState
		? QuestWristSideState.CameraHandSmoothedTwistDeg
		: (bIsLeft ? LeftArmState.SmoothedHandTwistDeg : RightArmState.SmoothedHandTwistDeg);

	// Wall-clock smoothing step for the keyed filter: DeltaSeconds can be 0 in Evaluate (the
	// accumulate-in-Update/consume-in-Evaluate CachedDeltaTimeSeconds pattern), and the caps
	// below multiply by it.
	float SmoothStepSeconds = DeltaSeconds;
	bool bSeededFromCurrentPose = false;
	if (bUseKeyedCameraHandState)
	{
		const double SmoothNowSeconds = FPlatformTime::Seconds();
		SmoothStepSeconds = QuestWristSideState.CameraHandSmoothLastStepTimeSeconds >= 0.0
			? FMath::Clamp(static_cast<float>(SmoothNowSeconds - QuestWristSideState.CameraHandSmoothLastStepTimeSeconds), 0.0f, 0.1f)
			: (DeltaSeconds > KINDA_SMALL_NUMBER ? DeltaSeconds : (1.0f / 30.0f));
		QuestWristSideState.CameraHandSmoothLastStepTimeSeconds = SmoothNowSeconds;
	}
	if (bMediaPipeHandRotationOnQuestLoss)
	{
		// Smooth HANDOVER: seed the swing/twist filter from the hand bone's CURRENT pose when the
		// Quest path owned the hand more recently OR the camera has not applied for a while, so
		// the camera target is approached from where the hand visibly is instead of snapping.
		// Runs BEFORE the branch chooser so the first camera frame anchors branch continuity to
		// the current pose, not the ref pose (the ref-pose anchor let the chooser pick a flipped
		// branch on the first frame of every takeover).
		const double SeedNowSeconds = FPlatformTime::Seconds();
		const double LastCameraApplySeconds = QuestWristSideState.LastMediaPipeHandRotationApplyTimeSeconds;
		const bool bQuestOwnedHandMoreRecently =
			QuestWristSideState.LastHandRotationApplyTimeSeconds >= 0.0 &&
			QuestWristSideState.LastHandRotationApplyTimeSeconds > LastCameraApplySeconds;
		const bool bCameraApplyStale =
			LastCameraApplySeconds < 0.0 ||
			(SeedNowSeconds - LastCameraApplySeconds) > 0.75;
		if ((bQuestOwnedHandMoreRecently || bCameraApplyStale) && HandBone.IsValidToEvaluate())
		{
			const FQuat CurrentHandPoseRotCS =
				CSPose.GetComponentSpaceTransform(HandBone.CachedCompactPoseIndex).GetRotation();
			FQuat SeedSwing = FQuat::Identity;
			float SeedTwistDeg = 0.0f;
			if (DecomposeSwingTwistDegAroundAxis(CurrentHandPoseRotCS, ForearmAxisComp, SeedSwing, SeedTwistDeg))
			{
				SmoothedSwing = SeedSwing;
				bHasSmoothedSwing = true;
				SmoothedTwistDeg = SeedTwistDeg;
				bHasSmoothedTwist = true;
				bHasLastGoodTarget = false;
				bSeededFromCurrentPose = true;
			}
		}
	}

	FQuat CurrentHandRotCS = RefHandComp;
	if (bHasSmoothedSwing && bHasSmoothedTwist)
	{
		const FQuat TwistQ(ForearmAxisComp, FMath::DegreesToRadians(SmoothedTwistDeg));
		CurrentHandRotCS = (SmoothedSwing * TwistQ).GetNormalized();
	}
	else if (bHasSmoothedSwing)
	{
		CurrentHandRotCS = SmoothedSwing;
	}

	const FQuat Candidates[4] = {
		MakeTargetHandRot(HandForwardComp, HandUpComp),
		MakeTargetHandRot(-HandForwardComp, HandUpComp),
		MakeTargetHandRot(HandForwardComp, -HandUpComp),
		MakeTargetHandRot(-HandForwardComp, -HandUpComp),
	};

	const FQuat BranchRef = bHasLastGoodTarget ? LastGoodTarget : CurrentHandRotCS;
	// Anatomical hemisphere bound (USER FEEDBACK 2026-07-03: overhead hands folded back against
	// the forearm into a "mantis" pose): a human wrist cannot flex the hand to point BACK along
	// the forearm, so any branch whose forward direction opposes the forearm axis is heavily
	// penalized. Candidates alternate the forward sign (+F,-F,+F,-F); the penalty is soft so a
	// fully-degenerate frame still picks the continuity-best branch instead of none.
	const float HandForwardDotForearm = FVector::DotProduct(HandForwardComp, ForearmAxisComp);
	float CandidateScores[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	float BestScore = -100.0f;
	int32 BestIndex = 0;
	for (int32 i = 0; i < 4; ++i)
	{
		float Score = FMath::Abs(Candidates[i] | BranchRef);
		const float CandidateForwardSign = (i == 1 || i == 3) ? -1.0f : 1.0f;
		if (CandidateForwardSign * HandForwardDotForearm < -0.15f)
		{
			Score -= 10.0f;
		}
		// With the palm parity resolved (session-locked or thumb-confident this frame), the
		// flipped-up candidates are anatomically wrong: penalize them past the continuity margin
		// so the chooser cannot settle on a palm-inverted branch (2026-07-03 trace: it did,
		// roughly half the time before the parity model existed).
		if (CameraHandChiralityUsed != 0 && i >= 2)
		{
			Score -= 0.35f;
		}
		CandidateScores[i] = Score;
		if (Score > BestScore)
		{
			BestScore = Score;
			BestIndex = i;
		}
	}

	// STABILITY (2026-07-03 worn verdict): a branch flip is a visible 180-degree spin, so while
	// the camera owns the hand a switch needs BOTH a decisive score margin over the currently
	// active branch AND twice the usual dwell. Ties at the decision boundary must never oscillate.
	const float BranchSwitchMargin = bMediaPipeHandRotationOnQuestLoss ? 0.10f : 0.0f;
	const int32 EffectiveFlipHysteresisFrames = bMediaPipeHandRotationOnQuestLoss
		? FMath::Max(HandFlipHysteresisFrames, 8)
		: HandFlipHysteresisFrames;
	if (!bHasLastGoodTarget)
	{
		ActiveBranch = BestIndex;
		PendingFrames = 0;
		PendingBranch = BestIndex;
	}
	else if (BestIndex != ActiveBranch &&
		BestScore > CandidateScores[FMath::Clamp(ActiveBranch, 0, 3)] + BranchSwitchMargin)
	{
		if (bTwistReliable)
		{
			if (PendingBranch != BestIndex)
			{
				PendingBranch = BestIndex;
				PendingFrames = 1;
			}
			else
			{
				PendingFrames++;
			}

			if (PendingFrames >= FMath::Max(1, EffectiveFlipHysteresisFrames))
			{
				ActiveBranch = BestIndex;
				PendingFrames = 0;
				PendingBranch = ActiveBranch;
			}
		}
	}
	else
	{
		PendingFrames = 0;
		PendingBranch = ActiveBranch;
	}

	const FQuat MeasuredTarget = Candidates[FMath::Clamp(ActiveBranch, 0, 3)];
	if (bTwistReliable && BestIndex == ActiveBranch)
	{
		LastGoodTarget = MeasuredTarget;
		bHasLastGoodTarget = true;
	}

	FQuat TargetSwing = FQuat::Identity;
	float TargetTwistDeg = 0.0f;
	if (!DecomposeSwingTwistDegAroundAxis(MeasuredTarget, ForearmAxisComp, TargetSwing, TargetTwistDeg))
	{
		ApplyHeldCameraHandRotation(CSPose);
		if (bMediaPipeHandRotationOnQuestLoss && !bQuestFingersAlreadyDriven)
		{
			DriveQuestFingerBonesCS(CSPose, bIsLeft, QuestLossFingerSnapshot, DeltaSeconds);
		}
		return;
	}

	float EffectiveRotationHalfLifeSeconds = ArmIKRotationHalfLifeSeconds;
	if (bMediaPipeHandRotationOnQuestLoss)
	{
		// (The handover seeding moved ABOVE the branch chooser - see bSeededFromCurrentPose.)
		// 0.12 s half-life + hard speed caps below: the camera hand basis is noisy overhead and
		// wrist rotation reads better slightly slow than twitchy (2026-07-03 worn verdict).
		EffectiveRotationHalfLifeSeconds = FMath::Max(EffectiveRotationHalfLifeSeconds, 0.12f);
	}
	const float EffectiveSwingMaxDegreesPerSecond = bMediaPipeHandRotationOnQuestLoss
		? (HandSwingMaxDegreesPerSecond > 0.0f ? FMath::Min(HandSwingMaxDegreesPerSecond, 360.0f) : 360.0f)
		: HandSwingMaxDegreesPerSecond;
	const float EffectiveTwistMaxDegreesPerSecond = bMediaPipeHandRotationOnQuestLoss
		? (HandTwistMaxDegreesPerSecond > 0.0f ? FMath::Min(HandTwistMaxDegreesPerSecond, 270.0f) : 270.0f)
		: HandTwistMaxDegreesPerSecond;
	const float RotAlpha = HalfLifeToAlpha(EffectiveRotationHalfLifeSeconds, SmoothStepSeconds);
	float SwingToTargetDeg = 0.0f;
	if (!bHasSmoothedSwing)
	{
		SmoothedSwing = TargetSwing;
		bHasSmoothedSwing = true;
	}
	else
	{
		float AlphaSwing = RotAlpha;
		const float SwingAngleDeg = FMath::RadiansToDegrees(SmoothedSwing.AngularDistance(TargetSwing));
		SwingToTargetDeg = SwingAngleDeg;
		const float MaxSwingStepDeg = EffectiveSwingMaxDegreesPerSecond * SmoothStepSeconds;
		if (MaxSwingStepDeg > 0.0f && SwingAngleDeg > KINDA_SMALL_NUMBER)
		{
			AlphaSwing = FMath::Min(AlphaSwing, MaxSwingStepDeg / SwingAngleDeg);
		}
		SmoothedSwing = FQuat::Slerp(SmoothedSwing, TargetSwing, AlphaSwing).GetNormalized();
	}

	float DesiredTwistDeg = TargetTwistDeg;
	if (HandTwistClampDegrees > 0.0f)
	{
		DesiredTwistDeg = FMath::Clamp(DesiredTwistDeg, -HandTwistClampDegrees, HandTwistClampDegrees);
	}

	if (!bHasSmoothedTwist)
	{
		SmoothedTwistDeg = DesiredTwistDeg;
		bHasSmoothedTwist = true;
	}
	else if (bTwistReliable)
	{
		float DeltaTwistDeg = FMath::FindDeltaAngleDegrees(SmoothedTwistDeg, DesiredTwistDeg);
		const float MaxTwistStepDeg = EffectiveTwistMaxDegreesPerSecond * SmoothStepSeconds;
		if (MaxTwistStepDeg > 0.0f)
		{
			DeltaTwistDeg = FMath::Clamp(DeltaTwistDeg, -MaxTwistStepDeg, MaxTwistStepDeg);
		}
		SmoothedTwistDeg = SmoothedTwistDeg + (DeltaTwistDeg * RotAlpha);
		if (HandTwistClampDegrees > 0.0f)
		{
			SmoothedTwistDeg = FMath::Clamp(SmoothedTwistDeg, -HandTwistClampDegrees, HandTwistClampDegrees);
		}
	}
	else if (HandTwistClampDegrees > 0.0f)
	{
		SmoothedTwistDeg = FMath::Clamp(SmoothedTwistDeg, -HandTwistClampDegrees, HandTwistClampDegrees);
	}

	const FQuat TwistQ(ForearmAxisComp, FMath::DegreesToRadians(SmoothedTwistDeg));
	const FQuat TargetHandRotCS = ApplyQuestWristPalmTrim(
		(SmoothedSwing * TwistQ).GetNormalized(), ForearmAxisComp, bIsLeft);
	// The user-visible frame-to-frame jump: distance from the previous camera-applied rotation.
	const float AppliedDeltaDeg = (bUseKeyedCameraHandState && QuestWristSideState.bHasCameraHandLastApplied)
		? FMath::RadiansToDegrees(QuestWristSideState.CameraHandLastAppliedRotCS.AngularDistance(TargetHandRotCS))
		: -1.0f;
	ApplyRotationCS(CSPose, HandBone, TargetHandRotCS);
	EmitWristLimitTrace(CSPose, bIsLeft, TargetHandRotCS, ForearmAxisComp, TEXT("camera"));
	if (RuntimeStateKey != 0)
	{
		QuestWristSideState.LastMediaPipeHandRotationApplyTimeSeconds = FPlatformTime::Seconds();
		QuestWristSideState.CameraHandLastAppliedRotCS = TargetHandRotCS;
		QuestWristSideState.bHasCameraHandLastApplied = true;
	}
	if (CameraHandTraceEnabled != 0 && bMediaPipeHandRotationOnQuestLoss)
	{
		UE_LOG(LogMediaPipePose, Log,
			TEXT("mp.MediaPipeCameraHandTrace: actor=%s node=%llu key=%u side=%s phase=apply appliedDeltaDeg=%.1f seeded=%d branch(act=%d best=%d pend=%d frames=%d) scores=[%.3f %.3f %.3f %.3f] twistReliable=%d basisSin=%.3f fwdDotForearm=%.2f fwdDot2D=%.2f chir=%d thumbConf=%d thumbDot=%.2f refThumbDot=%.2f curlAvg=%.2f twistTgt=%.1f twistSm=%.1f swingToTgtDeg=%.1f handSel=%s imageDist=%.3f sticky=%d dt=%.4f stepS=%.4f cacheBones=%llu"),
			*TargetActorName.ToString(),
			NodeDiagSerial,
			RuntimeStateKey,
			bIsLeft ? TEXT("L") : TEXT("R"),
			AppliedDeltaDeg,
			bSeededFromCurrentPose ? 1 : 0,
			ActiveBranch,
			BestIndex,
			PendingBranch,
			PendingFrames,
			CandidateScores[0], CandidateScores[1], CandidateScores[2], CandidateScores[3],
			bTwistReliable ? 1 : 0,
			BasisSin,
			HandForwardDotForearm,
			FwdDot2D,
			CameraHandChiralityUsed,
			bThumbChiralityConfident ? 1 : 0,
			MeasuredThumbUpDot,
			RefCameraThumbUpDot,
			CameraHandCurlAvg,
			TargetTwistDeg,
			SmoothedTwistDeg,
			SwingToTargetDeg,
			SelectedMediaPipeHandWorld ? (bSelectedMediaPipeHandIsLeftArray ? TEXT("mpLeft") : TEXT("mpRight")) : TEXT("none"),
			SelectedMediaPipeHandImageDist,
			QuestWristSideState.MediaPipeHandStickyArraySide,
			DeltaSeconds,
			SmoothStepSeconds,
			CacheBonesDiagCount);
	}
	if (!bQuestFingersAlreadyDriven)
	{
		DriveQuestFingerBonesCS(CSPose, bIsLeft, QuestLossFingerSnapshot, DeltaSeconds);
	}
}
