#include "MediaPipePoseDrivenAnimInstance.h"

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

// Anatomical wrist envelope at the FINAL wrist rotation, whichever path wrote it
// (quest/held/camera - the same three sites ApplyQuestWristPalmTrim covers).
// Phase 0 (2026-07-11): mp.WristLimitTrace report-only rows - twist about the forearm
// axis + swing away from the neutral wrist pose riding the CURRENT forearm, vs the
// anatomical envelope. Phase 2 (2026-07-11): mp.WristAnatomicalClamp clamps the RETURNED
// rotation to mp.WristTwistRangeDeg / mp.WristSwingRangeDeg as the LAST op before the
// bone write; in-range frames return the input bit-exactly, and clamp events carry the
// pre-clamp excess on the trace row. Clamped frames never feed any learner: this
// function writes no pose/solver state (keyed log throttles only, never key 0), and the
// call sites keep their continuity/hold state on the UNCLAMPED value.
FQuat FAnimNode_MediaPipePoseDriven::ApplyWristLimitClampAndTrace(FCSPose<FCompactPose>& CSPose, const bool bIsLeft, const FQuat& FinalHandRotCS, const FVector& ForearmAxisComp, const TCHAR* SourceTag)
{
	const bool bClampEnabled = CVarWristAnatomicalClamp.GetValueOnAnyThread() != 0;
	const bool bTraceEnabled = CVarWristLimitTrace.GetValueOnAnyThread() != 0;
	if ((!bClampEnabled && !bTraceEnabled) || !bHasReferencePose)
	{
		return FinalHandRotCS;
	}
	const FBoneReference& ParentLowerArmBone = bIsLeft ? LowerArmL : LowerArmR;
	if (!ParentLowerArmBone.IsValidToEvaluate())
	{
		return FinalHandRotCS;
	}
	const FQuat LowerArmRotCS = CSPose.GetComponentSpaceTransform(ParentLowerArmBone.CachedCompactPoseIndex).GetRotation().GetNormalized();
	const FQuat& RefLowerComp = bIsLeft ? RefLowerArmCompL : RefLowerArmCompR;
	const FQuat& RefHandComp = bIsLeft ? RefHandCompL : RefHandCompR;
	const float TwistRangeDeg = CVarWristTwistRangeDeg.GetValueOnAnyThread();
	const float SwingRangeDeg = CVarWristSwingRangeDeg.GetValueOnAnyThread();
	MediaPipeTrackingQualityMetrics::FWristLimitSample Sample;
	FQuat ClampedRotCS = FinalHandRotCS;
	if (!MediaPipeTrackingQualityMetrics::ComputeClampedWristRotation(
		FinalHandRotCS,
		LowerArmRotCS,
		RefLowerComp,
		RefHandComp,
		ForearmAxisComp,
		TwistRangeDeg,
		SwingRangeDeg,
		Sample,
		ClampedRotCS))
	{
		return FinalHandRotCS;
	}
	const bool bClampedThisFrame = bClampEnabled && Sample.bOutOfRange;
	if (bTraceEnabled && RuntimeStateKey != 0)
	{
		FQuestWristRuntimeState& RuntimeState = GetQuestWristRuntimeState(RuntimeStateKey);
		FQuestWristSideRuntimeState& SideState = bIsLeft ? RuntimeState.Left : RuntimeState.Right;
		const double NowSeconds = FPlatformTime::Seconds();
		const bool bStatusDue = FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(
			NowSeconds, 1.0, SideState.WristLimitTraceLastLogTimeSeconds);
		const bool bEventDue = Sample.bOutOfRange && FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(
			NowSeconds, 0.25, SideState.WristLimitTraceLastEventLogTimeSeconds);
		if (bStatusDue || bEventDue)
		{
			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.WristLimitTrace: actor=%s side=%s src=%s twistDeg=%.1f swingDeg=%.1f twistExcessDeg=%.1f swingExcessDeg=%.1f out=%d evt=%d clamped=%d rangeT=%.0f rangeS=%.0f key=%u"),
				*TargetActorName.ToString(),
				bIsLeft ? TEXT("L") : TEXT("R"),
				SourceTag,
				Sample.TwistDeg,
				Sample.SwingDeg,
				Sample.TwistExcessDeg,
				Sample.SwingExcessDeg,
				Sample.bOutOfRange ? 1 : 0,
				bEventDue ? 1 : 0,
				bClampedThisFrame ? 1 : 0,
				TwistRangeDeg,
				SwingRangeDeg,
				RuntimeStateKey);
		}
	}
	return bClampedThisFrame ? ClampedRotCS : FinalHandRotCS;
}

bool FAnimNode_MediaPipePoseDriven::DriveQuestHandCS(FCSPose<FCompactPose>& CSPose, bool bIsLeft, const FVector& ForearmAxisComp, const FQuat& MediaPipeHandTargetCS, float DeltaSeconds, FQuestHandRotationTrace* OutTrace, const FQuestWristMappingTrace* WristTrace)
{
	FQuestHandRotationTrace LocalTrace;
	FQuestHandRotationTrace& Trace = OutTrace ? *OutTrace : LocalTrace;
	Trace = FQuestHandRotationTrace{};

	bool& bHasQuestFingerAlignmentComp = bIsLeft ? LeftQuestHandState.bHasQuestFingerAlignmentComp : RightQuestHandState.bHasQuestFingerAlignmentComp;
	FQuat& QuestFingerAlignmentComp = bIsLeft ? LeftQuestHandState.QuestFingerAlignmentComp : RightQuestHandState.QuestFingerAlignmentComp;
	bHasQuestFingerAlignmentComp = false;
	QuestFingerAlignmentComp = FQuat::Identity;

	FQuestWristRuntimeState& QuestWristRuntimeState = GetQuestWristRuntimeState(RuntimeStateKey);
	FQuestWristSideRuntimeState& QuestWristSideState = bIsLeft ? QuestWristRuntimeState.Left : QuestWristRuntimeState.Right;
	auto UpdateCalibrationTrace = [&]()
	{
		FMediaPipeQuestWristCalibrationState::WriteTrace(QuestWristSideState, Trace);
	};
	auto ResetCalibrationMeasurement = [&](const uint8 Reason)
	{
		FMediaPipeQuestWristCalibrationState::ResetMeasurement(QuestWristSideState, Reason, &Trace);
	};
	auto SoftRejectCalibrationMeasurement = [&](const uint8 Reason)
	{
		const FQuestWristCalibrationSoftRejectSettings Settings{
			CVarQuestWristCalibrationSoftGate.GetValueOnAnyThread() != 0,
			CVarQuestWristCalibrationHandLossPauseSeconds.GetValueOnAnyThread(),
			CVarQuestWristCalibrationSoftRejectDecayRate.GetValueOnAnyThread(),
			CVarQuestWristCalibrationHoldSeconds.GetValueOnAnyThread(),
			CVarQuestWristCalibrationStableFrames.GetValueOnAnyThread()
		};
		FMediaPipeQuestWristCalibrationState::SoftRejectMeasurement(
			QuestWristSideState,
			Reason,
			Settings,
			DeltaSeconds,
			FPlatformTime::Seconds(),
			&Trace);
	};
	const int32 ManualResetSerial = FMediaPipeEmbodimentDebugCommands::GetQuestWristManualResetSerial();
	if (QuestWristState.LastQuestWristManualResetSerial != ManualResetSerial)
	{
		QuestWristState.LastQuestWristManualResetSerial = ManualResetSerial;
		QuestWristRuntimeState.ResetCalibration();
		LeftQuestHandState.bHasSmoothedQuestHandRotCS = false;
		RightQuestHandState.bHasSmoothedQuestHandRotCS = false;
		LeftQuestHandState.SmoothedQuestHandRotCS = FQuat::Identity;
		RightQuestHandState.SmoothedQuestHandRotCS = FQuat::Identity;
		LeftQuestHandState.bHasSmoothedQuestHandRotLocal = false;
		RightQuestHandState.bHasSmoothedQuestHandRotLocal = false;
		LeftQuestHandState.SmoothedQuestHandRotLocal = FQuat::Identity;
		RightQuestHandState.SmoothedQuestHandRotLocal = FQuat::Identity;
		LeftQuestHandState.bHasSmoothedQuestForearmTwist = false;
		RightQuestHandState.bHasSmoothedQuestForearmTwist = false;
		LeftQuestHandState.SmoothedQuestForearmTwistDeg = 0.0f;
		RightQuestHandState.SmoothedQuestForearmTwistDeg = 0.0f;
		LeftQuestHandState.bHasSmoothedQuestUpperArmTwist = false;
		RightQuestHandState.bHasSmoothedQuestUpperArmTwist = false;
		LeftQuestHandState.SmoothedQuestUpperArmTwistDeg = 0.0f;
		RightQuestHandState.SmoothedQuestUpperArmTwistDeg = 0.0f;
		for (int32 FingerStateIndex = 0; FingerStateIndex < MediaPipeQuestStateFingerBoneCount; ++FingerStateIndex)
		{
			LeftQuestHandState.bHasQuestFingerRetargetSourceRefCS[FingerStateIndex] = false;
			RightQuestHandState.bHasQuestFingerRetargetSourceRefCS[FingerStateIndex] = false;
			LeftQuestHandState.QuestFingerRetargetSourceRefCS[FingerStateIndex] = FQuat::Identity;
			RightQuestHandState.QuestFingerRetargetSourceRefCS[FingerStateIndex] = FQuat::Identity;
			LeftQuestHandState.QuestFingerRetargetTargetRefCS[FingerStateIndex] = FQuat::Identity;
			RightQuestHandState.QuestFingerRetargetTargetRefCS[FingerStateIndex] = FQuat::Identity;
		}
		DiagnosticsState.LastQuestHandCompareLogTimeSecondsL = -1.0;
		DiagnosticsState.LastQuestHandCompareLogTimeSecondsR = -1.0;
		DiagnosticsState.LastQuestWristSolveLogTimeSecondsL = -1.0;
		DiagnosticsState.LastQuestWristSolveLogTimeSecondsR = -1.0;
		UE_LOG(LogMediaPipePose, Log, TEXT("mp.ResetQuestWristCalibration: applied actor=%s serial=%d state=WaitingForStablePose"), *TargetActorName.ToString(), ManualResetSerial);
		UpdateCalibrationTrace();
	}
	else
	{
		UpdateCalibrationTrace();
	}

	const FBoneReference& HandBone = bIsLeft ? HandL : HandR;
	if (!HandBone.IsValidToEvaluate())
	{
		return false;
	}

	const FBoneReference& ParentLowerArmBone = bIsLeft ? LowerArmL : LowerArmR;
	auto TryApplyHeldQuestHandRotation = [&]() -> bool
	{
		const double NowSeconds = FPlatformTime::Seconds();
		const float GraceSeconds = FMath::Max(0.0f, CVarQuestHandRotationLostTrackingGraceSeconds.GetValueOnAnyThread());
		const float FadeSeconds = FMath::Max(0.0f, CVarQuestHandRotationLostTrackingFadeSeconds.GetValueOnAnyThread());
		if (!QuestWristSideState.bHasRotationCalibration ||
			QuestWristSideState.LastHandRotationApplyTimeSeconds < 0.0 ||
			GraceSeconds + FadeSeconds <= KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const float LostAgeSeconds = static_cast<float>(FMath::Max(0.0, NowSeconds - QuestWristSideState.LastHandRotationApplyTimeSeconds));
		if (LostAgeSeconds > GraceSeconds + FadeSeconds)
		{
			if (ParentLowerArmBone.IsValidToEvaluate())
			{
				const FQuat LowerArmRotCS = CSPose.GetComponentSpaceTransform(ParentLowerArmBone.CachedCompactPoseIndex).GetRotation().GetNormalized();
				bool& bHasSmoothedQuestHandRotLocal = bIsLeft ? LeftQuestHandState.bHasSmoothedQuestHandRotLocal : RightQuestHandState.bHasSmoothedQuestHandRotLocal;
				FQuat& SmoothedQuestHandRotLocal = bIsLeft ? LeftQuestHandState.SmoothedQuestHandRotLocal : RightQuestHandState.SmoothedQuestHandRotLocal;
				SmoothedQuestHandRotLocal = (LowerArmRotCS.Inverse() * MediaPipeHandTargetCS.GetNormalized()).GetNormalized();
				bHasSmoothedQuestHandRotLocal = true;
			}
			else
			{
				bool& bHasSmoothedQuestHandRotCS = bIsLeft ? LeftQuestHandState.bHasSmoothedQuestHandRotCS : RightQuestHandState.bHasSmoothedQuestHandRotCS;
				FQuat& SmoothedQuestHandRotCS = bIsLeft ? LeftQuestHandState.SmoothedQuestHandRotCS : RightQuestHandState.SmoothedQuestHandRotCS;
				SmoothedQuestHandRotCS = MediaPipeHandTargetCS.GetNormalized();
				bHasSmoothedQuestHandRotCS = true;
			}
			return false;
		}

		if (ParentLowerArmBone.IsValidToEvaluate())
		{
			bool& bHasSmoothedQuestHandRotLocal = bIsLeft ? LeftQuestHandState.bHasSmoothedQuestHandRotLocal : RightQuestHandState.bHasSmoothedQuestHandRotLocal;
			FQuat& SmoothedQuestHandRotLocal = bIsLeft ? LeftQuestHandState.SmoothedQuestHandRotLocal : RightQuestHandState.SmoothedQuestHandRotLocal;
			if (!bHasSmoothedQuestHandRotLocal)
			{
				return false;
			}

			const FQuat LowerArmRotCS = CSPose.GetComponentSpaceTransform(ParentLowerArmBone.CachedCompactPoseIndex).GetRotation().GetNormalized();
			const FQuat MediaPipeHandLocal = (LowerArmRotCS.Inverse() * MediaPipeHandTargetCS.GetNormalized()).GetNormalized();
			const float FadeAlpha = FadeSeconds <= KINDA_SMALL_NUMBER
				? 1.0f
				: FMath::Clamp((LostAgeSeconds - GraceSeconds) / FadeSeconds, 0.0f, 1.0f);
			SmoothedQuestHandRotLocal = FQuat::Slerp(SmoothedQuestHandRotLocal, MediaPipeHandLocal, FadeAlpha).GetNormalized();
			const FQuat HeldFinalHandRotCS = ApplyWristLimitClampAndTrace(
				CSPose, bIsLeft,
				ApplyQuestWristPalmTrim(
					(LowerArmRotCS * SmoothedQuestHandRotLocal).GetNormalized(), ForearmAxisComp, bIsLeft),
				ForearmAxisComp, TEXT("held"));
			ApplyRotationCS(CSPose, HandBone, HeldFinalHandRotCS);
			Trace.bHadCalibration = 1;
			Trace.bAppliedHandLocalToLowerArm = 1;
			UpdateCalibrationTrace();
			return true;
		}

		bool& bHasSmoothedQuestHandRotCS = bIsLeft ? LeftQuestHandState.bHasSmoothedQuestHandRotCS : RightQuestHandState.bHasSmoothedQuestHandRotCS;
		FQuat& SmoothedQuestHandRotCS = bIsLeft ? LeftQuestHandState.SmoothedQuestHandRotCS : RightQuestHandState.SmoothedQuestHandRotCS;
		if (!bHasSmoothedQuestHandRotCS)
		{
			return false;
		}

		const float FadeAlpha = FadeSeconds <= KINDA_SMALL_NUMBER
			? 1.0f
			: FMath::Clamp((LostAgeSeconds - GraceSeconds) / FadeSeconds, 0.0f, 1.0f);
		SmoothedQuestHandRotCS = FQuat::Slerp(SmoothedQuestHandRotCS, MediaPipeHandTargetCS.GetNormalized(), FadeAlpha).GetNormalized();
		ApplyRotationCS(CSPose, HandBone, ApplyQuestWristPalmTrim(SmoothedQuestHandRotCS, ForearmAxisComp, bIsLeft));
		Trace.bHadCalibration = 1;
		UpdateCalibrationTrace();
		return true;
	};

	if (!bUseQuestHandTracking || CVarQuestHandTracking.GetValueOnAnyThread() == 0 || !IsQuestHandSideAvailable(QuestHands, bIsLeft))
	{
		if (!QuestWristSideState.bHasRotationCalibration)
		{
			SoftRejectCalibrationMeasurement(bIsLeft ? QuestWristCalibrationReject_LeftHandNotTracked : QuestWristCalibrationReject_RightHandNotTracked);
		}
		return TryApplyHeldQuestHandRotation();
	}

	Trace.bQuestAvailable = 1;
	Trace.bQuestTracked = IsQuestHandSideTracked(QuestHands, bIsLeft) ? 1 : 0;
	const TStaticArray<FVector, QuestHandKeypointCount>& RotationPolicyPositions = GetQuestHandPositions(QuestHands, bIsLeft);
	const FVector RotationPolicyWristWorld = RotationPolicyPositions[static_cast<int32>(EHandKeypoint::Wrist)];
	const double RotationPolicyNowSeconds = FPlatformTime::Seconds();
	const bool bHasRecentAcceptedLiveWristPosition =
		QuestWristSideState.bHasLastAcceptedLiveWristPosition &&
		QuestWristSideState.LastAcceptedLiveWristTimeSeconds >= 0.0;
	const float LastAcceptedLiveWristAgeSeconds = bHasRecentAcceptedLiveWristPosition
		? static_cast<float>(RotationPolicyNowSeconds - QuestWristSideState.LastAcceptedLiveWristTimeSeconds)
		: 0.0f;
	const float UntrackedLiveWristStepFromLastAcceptedCm = bHasRecentAcceptedLiveWristPosition
		? FVector::Dist(RotationPolicyWristWorld, QuestWristSideState.LastAcceptedLiveWristWorld)
		: 0.0f;
	FMediaPipeQuestWristApplyPolicyInput HandRotationPolicyInput;
	HandRotationPolicyInput.bRequireTrackedForApply = CVarQuestHandRotationRequireTracked.GetValueOnAnyThread() != 0;
	HandRotationPolicyInput.bAllowUsableUntrackedForPositionApply =
		FMath::Clamp(CVarQuestArmMode.GetValueOnAnyThread(), 0, 3) >= 2;
	HandRotationPolicyInput.bQuestSideTracked = Trace.bQuestTracked != 0;
	HandRotationPolicyInput.bQuestSideUsable = IsUsableQuestWristPosition(RotationPolicyWristWorld);
	HandRotationPolicyInput.bHasRecentAcceptedLiveWristPosition = bHasRecentAcceptedLiveWristPosition;
	HandRotationPolicyInput.UntrackedLiveWristStepFromLastAcceptedCm = UntrackedLiveWristStepFromLastAcceptedCm;
	HandRotationPolicyInput.LastAcceptedLiveWristAgeSeconds = LastAcceptedLiveWristAgeSeconds;
	HandRotationPolicyInput.MaxUntrackedLiveWristStepCm =
		FMath::Max(0.0f, CVarQuestWristPositionFilterResetDistanceCm.GetValueOnAnyThread());
	HandRotationPolicyInput.MaxUntrackedLiveWristAgeSeconds =
		FMath::Max(0.0f, CVarQuestWristLostTrackingGraceSeconds.GetValueOnAnyThread());
	FMediaPipeQuestHandRotationFramePolicyInput HandRotationFramePolicyInput;
	HandRotationFramePolicyInput.LiveWristPolicy = HandRotationPolicyInput;
	HandRotationFramePolicyInput.bRequireTrackedForHandRotation = CVarQuestHandRotationRequireTracked.GetValueOnAnyThread() != 0;
	HandRotationFramePolicyInput.bQuestSideTracked = Trace.bQuestTracked != 0;
	if (WristTrace)
	{
		HandRotationFramePolicyInput.bCurrentWristPositionApplied = WristTrace->bPositionApplied != 0;
		HandRotationFramePolicyInput.bCurrentWristMapped = WristTrace->bMapped != 0;
		HandRotationFramePolicyInput.bCurrentWristUsedUntrackedJointData = WristTrace->bUsedUntrackedJointData != 0;
		HandRotationFramePolicyInput.bCurrentWristUsedHeldTarget = WristTrace->bUsedHeldQuestWrist != 0;
		HandRotationFramePolicyInput.bCurrentWristRawRejected = WristTrace->bRawQuestRejected != 0;
		HandRotationFramePolicyInput.bCurrentWristBodyFallback = WristTrace->bConstrainedArmBodyFallbackApplied != 0;
	}
	if (!FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame(HandRotationFramePolicyInput))
	{
		if (!QuestWristSideState.bHasRotationCalibration)
		{
			SoftRejectCalibrationMeasurement(bIsLeft ? QuestWristCalibrationReject_LeftHandNotTracked : QuestWristCalibrationReject_RightHandNotTracked);
		}
		return TryApplyHeldQuestHandRotation();
	}

	const FQuat& RefHandComp = bIsLeft ? RefHandCompL : RefHandCompR;
	const int32 PalmMode = FMath::Clamp(CVarQuestPalmMode.GetValueOnAnyThread(), 0, 2);
	const bool bUseQuestAuthoritativeHandOrientation = PalmMode >= 2;
	const FQuat& BoneRefHandBasisComp = bIsLeft ? RefHandBasisCompL : RefHandBasisCompR;
	const FQuat& VisualPalmRefHandBasisComp = bIsLeft ? RefHandVisualPalmBasisCompL : RefHandVisualPalmBasisCompR;
	const FQuat& RefHandBasisComp =
		bUseQuestAuthoritativeHandOrientation && !VisualPalmRefHandBasisComp.IsIdentity()
			? VisualPalmRefHandBasisComp
			: BoneRefHandBasisComp;
	if (RefHandBasisComp.IsIdentity())
	{
		return false;
	}

	FVector HandForwardWorld = FVector::ZeroVector;
	FVector HandUpWorld = FVector::ZeroVector;
	float BasisSin = 0.0f;
	if (!TryBuildQuestHandBasisWorld(QuestHands, bIsLeft, HandForwardWorld, HandUpWorld, BasisSin) ||
		BasisSin < HandTwistMinBasisSin)
	{
		return TryApplyHeldQuestHandRotation();
	}

	bool bQuestHandBasisMapped = false;
	{
		FVector MappedForwardWorld = FVector::ZeroVector;
		FVector MappedUpWorld = FVector::ZeroVector;
		if (TryMapQuestDirectionToMediaWorld(HandForwardWorld, MappedForwardWorld) &&
			TryMapQuestDirectionToMediaWorld(HandUpWorld, MappedUpWorld))
		{
			HandForwardWorld = MappedForwardWorld;
			HandUpWorld = MappedUpWorld;
			bQuestHandBasisMapped = true;
		}
	}
	Trace.bQuestHandBasisMapped = bQuestHandBasisMapped ? 1 : 0;
	if (!bQuestHandBasisMapped)
	{
		return TryApplyHeldQuestHandRotation();
	}

	const FVector HandForwardComp = TargetCompTransform.InverseTransformVectorNoScale(HandForwardWorld).GetSafeNormal();
	FVector HandUpComp = TargetCompTransform.InverseTransformVectorNoScale(HandUpWorld).GetSafeNormal();
	if (HandForwardComp.IsNearlyZero() || HandUpComp.IsNearlyZero())
	{
		return TryApplyHeldQuestHandRotation();
	}

	HandUpComp = (HandUpComp - FVector::DotProduct(HandUpComp, HandForwardComp) * HandForwardComp).GetSafeNormal();
	if (HandUpComp.IsNearlyZero() && !ForearmAxisComp.IsNearlyZero())
	{
		HandUpComp = FVector::CrossProduct(HandForwardComp, ForearmAxisComp).GetSafeNormal();
	}
	if (HandUpComp.IsNearlyZero())
	{
		return TryApplyHeldQuestHandRotation();
	}

	const FVector ForearmAxisN = ForearmAxisComp.GetSafeNormal();
	if (ForearmAxisN.IsNearlyZero())
	{
		return TryApplyHeldQuestHandRotation();
	}

	bool& bHasQuestWristCalibration = QuestWristSideState.bHasRotationCalibration;
	FQuat& QuestWristCalibrationBasisComp = QuestWristSideState.RotationCalibrationBasisComp;

	const FQuat MediaPipeHandTargetNormalized = MediaPipeHandTargetCS.GetNormalized();
	const FQuat MediaPipeHandBasisComp = (MediaPipeHandTargetNormalized * RefHandComp.Inverse() * RefHandBasisComp).GetNormalized();
	auto MakeQuestHandTargetCS = [&](const FQuat& QuestBasisComp) -> FQuat
	{
		return ((QuestBasisComp * RefHandBasisComp.Inverse()) * RefHandComp).GetNormalized();
	};

	const FQuat QuestHandBasisComp = MakeQuatFromForwardUp(HandForwardComp, HandUpComp);
	if (QuestHandBasisComp.IsIdentity())
	{
		return TryApplyHeldQuestHandRotation();
	}

	const FQuat QuestHandTargetCS = MakeQuestHandTargetCS(QuestHandBasisComp);
	if (!MediaPipeHandBasisComp.IsIdentity())
	{
		QuestFingerAlignmentComp = (MediaPipeHandBasisComp * QuestHandBasisComp.Inverse()).GetNormalized();
		bHasQuestFingerAlignmentComp = true;
	}

	constexpr uint8 QuestWristRotationSourcePalm = 1;
	constexpr uint8 QuestWristRotationSourceJoint = 2;
	constexpr uint8 QuestWristRotationSourceProjectedJointAxis = 3;
	constexpr uint8 QuestWristRotationSourceProjectedPalmAxis = 4;
	constexpr uint8 QuestWristRotationSourceSemanticRoll = 5;
	constexpr uint8 QuestWristRotationSourceSemanticBasisRoll = 6;
	FQuat QuestMeasuredWristComp = QuestHandBasisComp;
	uint8 QuestWristRotationSource = QuestWristRotationSourcePalm;
	Trace.bUsedPalmBasis = 1;
	FQuat QuestWristJointComp = FQuat::Identity;
	bool bHasQuestWristJointComp = false;
	bool bMappedQuestWristJointComp = false;
	{
		const TStaticArray<FQuat, QuestHandKeypointCount>& Rotations = bIsLeft ? QuestHands.LeftRotationsWorld : QuestHands.RightRotationsWorld;
		const FQuat QuestWristRotWorld = Rotations[static_cast<int32>(EHandKeypoint::Wrist)].GetNormalized();
		if (!QuestWristRotWorld.IsIdentity())
		{
			FQuat MappedQuestWristRotWorld = QuestWristRotWorld;
			FQuat QuestToMediaRotationWorld = FQuat::Identity;
			if (TryGetCurrentQuestToMediaSpaceRotation(QuestToMediaRotationWorld))
			{
				MappedQuestWristRotWorld = (QuestToMediaRotationWorld * QuestWristRotWorld).GetNormalized();
				bMappedQuestWristJointComp = true;
			}

			QuestWristJointComp = TargetCompTransform.InverseTransformRotation(MappedQuestWristRotWorld).GetNormalized();
			bHasQuestWristJointComp = !QuestWristJointComp.IsIdentity();
		}
	}
	if (bHasQuestWristJointComp)
	{
		const FVector RefX = RefHandBasisComp.RotateVector(FVector::ForwardVector).GetSafeNormal();
		const FVector RefY = RefHandBasisComp.RotateVector(FVector::RightVector).GetSafeNormal();
		const FVector RefZ = RefHandBasisComp.RotateVector(FVector::UpVector).GetSafeNormal();
		const FVector JointX = QuestWristJointComp.RotateVector(FVector::ForwardVector).GetSafeNormal();
		const FVector JointY = QuestWristJointComp.RotateVector(FVector::RightVector).GetSafeNormal();
		const FVector JointZ = QuestWristJointComp.RotateVector(FVector::UpVector).GetSafeNormal();
		const FVector PalmX = QuestHandBasisComp.RotateVector(FVector::ForwardVector).GetSafeNormal();
		const FVector PalmY = QuestHandBasisComp.RotateVector(FVector::RightVector).GetSafeNormal();
		const FVector PalmZ = QuestHandBasisComp.RotateVector(FVector::UpVector).GetSafeNormal();

		auto DotsAgainstReference = [&](const FVector& Axis) -> FVector
		{
			return FVector(
				FVector::DotProduct(Axis, RefX),
				FVector::DotProduct(Axis, RefY),
				FVector::DotProduct(Axis, RefZ));
		};
		auto DotAgainstForearm = [&](const FVector& Axis) -> float
		{
			return ForearmAxisN.IsNearlyZero() ? 0.0f : FVector::DotProduct(Axis, ForearmAxisN);
		};

		Trace.bWristAxisDiagnosticsValid = 1;
		Trace.bWristAxisDiagnosticsMapped = bMappedQuestWristJointComp ? 1 : 0;
		Trace.WristJointXRefDots = DotsAgainstReference(JointX);
		Trace.WristJointYRefDots = DotsAgainstReference(JointY);
		Trace.WristJointZRefDots = DotsAgainstReference(JointZ);
		Trace.PalmXRefDots = DotsAgainstReference(PalmX);
		Trace.PalmYRefDots = DotsAgainstReference(PalmY);
		Trace.PalmZRefDots = DotsAgainstReference(PalmZ);
		Trace.WristJointForearmDots = FVector(DotAgainstForearm(JointX), DotAgainstForearm(JointY), DotAgainstForearm(JointZ));
		Trace.PalmForearmDots = FVector(DotAgainstForearm(PalmX), DotAgainstForearm(PalmY), DotAgainstForearm(PalmZ));
		Trace.ForearmRefDots = ForearmAxisN.IsNearlyZero() ? FVector::ZeroVector : DotsAgainstReference(ForearmAxisN);
	}
	const bool bUseJointRotationForSide =
		CVarQuestWristUseJointRotation.GetValueOnAnyThread() != 0 &&
		PalmMode < 2 &&
		(bIsLeft
			? CVarQuestWristUseJointRotationLeft.GetValueOnAnyThread() != 0
			: CVarQuestWristUseJointRotationRight.GetValueOnAnyThread() != 0);
	if (bUseJointRotationForSide && bHasQuestWristJointComp)
	{
		QuestMeasuredWristComp = QuestWristJointComp;
		QuestWristRotationSource = QuestWristRotationSourceJoint;
		Trace.bUsedJointRotation = 1;
		Trace.bUsedPalmBasis = 0;
	}

	const float ConfigBlend = FMath::Clamp(QuestHandRotationBlend, 0.0f, 1.0f);
	Trace.HandRotationBlend = ConfigBlend;
	const float Blend = ConfigBlend;
	if (Blend <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	auto ProjectToPlane = [](const FVector& Axis, const FVector& PlaneNormal) -> FVector
	{
		const FVector AxisN = Axis.GetSafeNormal();
		const FVector NormalN = PlaneNormal.GetSafeNormal();
		if (AxisN.IsNearlyZero() || NormalN.IsNearlyZero())
		{
			return FVector::ZeroVector;
		}

		return (AxisN - FVector::DotProduct(AxisN, NormalN) * NormalN).GetSafeNormal();
	};
	auto ProjectToForearmPlane = [&](const FVector& Axis) -> FVector
	{
		return ProjectToPlane(Axis, ForearmAxisN);
	};
	auto TryBuildForearmLocalFrameComp = [&](const FQuat& BaselineHandBasisComp, FQuat& OutForearmBasisComp) -> bool
	{
		OutForearmBasisComp = FQuat::Identity;
		if (BaselineHandBasisComp.IsIdentity() || ForearmAxisN.IsNearlyZero())
		{
			return false;
		}

		FVector FrameForwardComp = ForearmAxisN;
		const FVector BaselineForwardComp = QuestBasisAxis(BaselineHandBasisComp, 0);
		if (!BaselineForwardComp.IsNearlyZero() &&
			FVector::DotProduct(BaselineForwardComp, ForearmAxisN) < 0.0f)
		{
			FrameForwardComp *= -1.0f;
		}

		FVector PoleComp = ProjectToPlane(QuestBasisAxis(BaselineHandBasisComp, 2), FrameForwardComp);
		if (PoleComp.IsNearlyZero())
		{
			PoleComp = ProjectToPlane(QuestBasisAxis(BaselineHandBasisComp, 1), FrameForwardComp);
		}
		if (PoleComp.IsNearlyZero())
		{
			PoleComp = ProjectToPlane(QuestBasisAxis(BaselineHandBasisComp, 0), FrameForwardComp);
		}
		if (PoleComp.IsNearlyZero())
		{
			PoleComp = ProjectPoleReferenceToPlane(FVector::UpVector, FrameForwardComp, FVector::RightVector, FVector::ForwardVector);
		}
		if (PoleComp.IsNearlyZero())
		{
			return false;
		}

		OutForearmBasisComp = MakeQuatFromForwardUp(FrameForwardComp, PoleComp);
		return true;
	};
	FQuat CurrentForearmBasisComp = FQuat::Identity;
	const bool bHasCurrentForearmBasisComp = TryBuildForearmLocalFrameComp(MediaPipeHandBasisComp, CurrentForearmBasisComp);
	const FQuat CurrentQuestHandBasisForearmLocal = bHasCurrentForearmBasisComp
		? (CurrentForearmBasisComp.Inverse() * QuestHandBasisComp).GetNormalized()
		: FQuat::Identity;
	const FQuat CurrentQuestHandForearmLocalCS = bHasCurrentForearmBasisComp
		? (CurrentForearmBasisComp.Inverse() * QuestHandTargetCS).GetNormalized()
		: FQuat::Identity;
	constexpr uint8 ProjectedCandidateCount = 6;
	FVector ProjectedCandidateRefs[ProjectedCandidateCount] = {};
	float ProjectedCandidateScores[ProjectedCandidateCount] = {};
	uint8 ProjectedCandidateMask = 0;
	auto AddProjectedCandidate = [&](const uint8 CandidateIndex, const FVector& Axis)
	{
		const FVector AxisN = Axis.GetSafeNormal();
		if (AxisN.IsNearlyZero() || CandidateIndex >= ProjectedCandidateCount)
		{
			return;
		}

		const float Dot = FVector::DotProduct(AxisN, ForearmAxisN);
		const float Score = FMath::Clamp(1.0f - (Dot * Dot), 0.0f, 1.0f);
		if (Score < 0.12f)
		{
			return;
		}

		const FVector Projected = ProjectToForearmPlane(AxisN);
		if (Projected.IsNearlyZero())
		{
			return;
		}

		ProjectedCandidateRefs[CandidateIndex] = Projected;
		ProjectedCandidateScores[CandidateIndex] = Score;
		ProjectedCandidateMask |= (1u << CandidateIndex);
	};
	if (bHasQuestWristJointComp)
	{
		for (uint8 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			AddProjectedCandidate(AxisIndex, QuestBasisAxis(QuestWristJointComp, AxisIndex));
		}
	}
	for (uint8 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		AddProjectedCandidate(AxisIndex + 3, QuestBasisAxis(QuestHandBasisComp, AxisIndex));
	}

	bool bUseProjectedTwistSolve = false;
	float ProjectedQuestTwistDeg = 0.0f;
	uint8 SelectedProjectedCandidate = 0;
	if (QuestWristRotationSource == QuestWristRotationSourcePalm)
	{
		const uint8 ProjectedSource = bHasQuestWristJointComp
			? QuestWristRotationSourceProjectedJointAxis
			: QuestWristRotationSourceProjectedPalmAxis;
		if (ProjectedCandidateMask != 0)
		{
			QuestWristRotationSource = ProjectedSource;
			bUseProjectedTwistSolve = true;
			Trace.bUsedProjectedTwistBasis = 1;
		}
	}
	const bool bUseSemanticRollSolve =
		!QuestHandBasisComp.IsIdentity() &&
		!MediaPipeHandBasisComp.IsIdentity() &&
		bHasCurrentForearmBasisComp;
	const bool bUseSemanticBasisDeltaSolve =
		bUseSemanticRollSolve &&
		CVarQuestWristUseBasisDelta.GetValueOnAnyThread() != 0;
	const bool bUseForearmLocalSemanticRollSolve = bUseSemanticRollSolve && (bUseSemanticBasisDeltaSolve || !bIsLeft);
	if (bUseSemanticRollSolve)
	{
		QuestWristRotationSource = bUseSemanticBasisDeltaSolve
			? QuestWristRotationSourceSemanticBasisRoll
			: QuestWristRotationSourceSemanticRoll;
		bUseProjectedTwistSolve = false;
		Trace.bUsedProjectedTwistBasis = 0;
		Trace.bUsedJointRotation = 0;
		Trace.bUsedPalmBasis = 0;
		Trace.bUsedSemanticRoll = 1;
		Trace.bUsedForearmLocalSemanticRoll = bUseForearmLocalSemanticRollSolve ? 1 : 0;
		Trace.bUsedSemanticBasisDelta = bUseSemanticBasisDeltaSolve ? 1 : 0;
		Trace.SemanticRollAxisIndex = (bUseSemanticBasisDeltaSolve ? 2 : 0) + 1;
		Trace.SemanticRollAxisScore = 1.0f;
	}
	const bool bUseAnatomicalRollAxisSolve =
		!bUseSemanticRollSolve &&
		!bIsLeft &&
		QuestWristRotationSource == QuestWristRotationSourceJoint &&
		bHasQuestWristJointComp;
	if (QuestWristSideState.RotationCalibrationSource != 0 &&
		QuestWristSideState.RotationCalibrationSource != QuestWristRotationSource)
	{
		// TRANSIENT SOURCE-FLIP GUARD (2026-07-10 worn forensics): the solve source above is
		// per-frame data-dependent (quest basis / camera basis / forearm frame), and a hand-
		// tracking loss degrades those for a few frames - the old immediate wipe here threw
		// away an ACCEPTED calibration on every flicker. That also killed the held-rotation
		// bridge (it requires bHasRotationCalibration), so the hand snapped limp with no
		// grace, and each mid-motion re-accept baked a NEW arbitrary neutral basis (measured
		// 2026-07-10: every Tracking->WaitingForStablePose transition coincided with
		// questTracked=0; R hand 11.5s of handRotApplied=0 after one loss). A mismatch must
		// now accumulate a dwell of solve-reachable frames before it may wipe; during the
		// dwell the last rotation holds and the calibration survives. A REAL solve-mode
		// change (console/config) still recalibrates - one second later.
		const double SourceMismatchNowSeconds = FPlatformTime::Seconds();
		const float SourceMismatchStepSeconds = QuestWristSideState.RotationCalibrationSourceMismatchLastTimeSeconds >= 0.0
			? FMath::Clamp(static_cast<float>(SourceMismatchNowSeconds - QuestWristSideState.RotationCalibrationSourceMismatchLastTimeSeconds), 0.0f, 0.1f)
			: 0.0f;
		QuestWristSideState.RotationCalibrationSourceMismatchLastTimeSeconds = SourceMismatchNowSeconds;
		QuestWristSideState.RotationCalibrationSourceMismatchSeconds += SourceMismatchStepSeconds;
		if (QuestWristSideState.RotationCalibrationSourceMismatchSeconds < 1.0f &&
			QuestWristSideState.bHasRotationCalibration)
		{
			return TryApplyHeldQuestHandRotation();
		}
		QuestWristSideState.ResetRotationCalibration();
	}
	else
	{
		QuestWristSideState.RotationCalibrationSourceMismatchSeconds = 0.0f;
		QuestWristSideState.RotationCalibrationSourceMismatchLastTimeSeconds = -1.0;
	}
	QuestWristSideState.RotationCalibrationSource = QuestWristRotationSource;
	if (bUseAnatomicalRollAxisSolve &&
		bHasQuestWristCalibration &&
		!QuestWristSideState.bHasRotationAnatomicalRollAxis)
	{
		QuestWristSideState.ResetRotationCalibration();
		QuestWristSideState.RotationCalibrationSource = QuestWristRotationSource;
	}
	if (bUseSemanticRollSolve &&
		bHasQuestWristCalibration &&
		!QuestWristSideState.bHasRotationSemanticRollAxis)
	{
		QuestWristSideState.ResetRotationCalibration();
		QuestWristSideState.RotationCalibrationSource = QuestWristRotationSource;
	}
	const bool bUseRelativeWristRotationCalibration =
		CVarQuestWristRelativeCalibration.GetValueOnAnyThread() != 0;
	if (bUseRelativeWristRotationCalibration)
	{
		Trace.bHadCalibration = bHasQuestWristCalibration ? 1 : 0;
		if (!bHasQuestWristCalibration)
		{
			Trace.CalibrationBasisErrorDeg = FMath::RadiansToDegrees(
				bUseSemanticBasisDeltaSolve
					? QuestHandBasisComp.AngularDistance(MediaPipeHandBasisComp)
					: QuestHandTargetCS.AngularDistance(MediaPipeHandTargetCS.GetNormalized()));
			const float MaxCalibrationBasisErrorDeg = FMath::Clamp(CVarQuestWristCalibrationMaxBasisErrorDegrees.GetValueOnAnyThread(), 0.0f, 180.0f);

			FQuat NeutralSwingComp = FQuat::Identity;
			float NeutralTwistDeg = 0.0f;
			if (DecomposeSwingTwistDegAroundAxis(
				(QuestHandBasisComp * MediaPipeHandBasisComp.Inverse()).GetNormalized(),
				ForearmAxisN,
				NeutralSwingComp,
				NeutralTwistDeg))
			{
				Trace.CalibrationNeutralTwistDeg = NeutralTwistDeg;
			}
			QuestWristSideState.RotationCalibrationLastBasisErrorDeg = Trace.CalibrationBasisErrorDeg;
			QuestWristSideState.RotationCalibrationLastNeutralTwistDeg = Trace.CalibrationNeutralTwistDeg;

			auto AcceptQuestWristRotationCalibration = [&]() -> bool
			{
				if (bUseSemanticRollSolve)
				{
					QuestWristSideState.bHasRotationProjectedAxis = false;
					QuestWristSideState.RotationProjectedAxisIndex = 0;
					QuestWristSideState.RotationProjectedCalibrationMask = 0;
					for (FVector& Ref : QuestWristSideState.RotationProjectedCalibrationRefsComp)
					{
						Ref = FVector::ZeroVector;
					}
					QuestWristCalibrationBasisComp = bUseForearmLocalSemanticRollSolve
						? (bUseSemanticBasisDeltaSolve ? CurrentQuestHandBasisForearmLocal : CurrentQuestHandForearmLocalCS)
						: QuestHandTargetCS.GetNormalized();

					QuestWristSideState.bHasRotationSemanticRollAxis = true;
					QuestWristSideState.RotationSemanticRollAxisIndex = bUseSemanticBasisDeltaSolve ? 2 : 0;
					QuestWristSideState.RotationSemanticRollCalibrationOffsetDeg = 0.0f;
					QuestWristSideState.RotationSemanticRollCalibrationForwardComp = QuestBasisAxis(QuestWristCalibrationBasisComp, 0);
					QuestWristSideState.RotationSemanticRollCalibrationUpComp = QuestBasisAxis(QuestWristCalibrationBasisComp, 2);
					QuestWristSideState.bHasRotationSemanticRollLastTwist = false;
					QuestWristSideState.RotationSemanticRollLastTwistDeg = 0.0f;
					bHasQuestWristCalibration = true;
					Trace.bUsedSemanticRoll = 1;
					Trace.bUsedForearmLocalSemanticRoll = bUseForearmLocalSemanticRollSolve ? 1 : 0;
					Trace.bUsedSemanticBasisDelta = bUseSemanticBasisDeltaSolve ? 1 : 0;
					Trace.SemanticRollAxisIndex = QuestWristSideState.RotationSemanticRollAxisIndex + 1;
					Trace.SemanticRollAxisScore = 1.0f;
					Trace.SemanticRollCalibrationOffsetDeg = 0.0f;
					Trace.SemanticRollCurrentOffsetDeg = 0.0f;
				}
				else if (bUseProjectedTwistSolve)
				{
					QuestWristSideState.bHasRotationSemanticRollAxis = false;
					QuestWristSideState.RotationSemanticRollAxisIndex = 0;
					QuestWristSideState.RotationSemanticRollCalibrationOffsetDeg = 0.0f;
					QuestWristSideState.RotationSemanticRollCalibrationForwardComp = FVector::ZeroVector;
					QuestWristSideState.RotationSemanticRollCalibrationUpComp = FVector::ZeroVector;
					QuestWristSideState.bHasRotationSemanticRollLastTwist = false;
					QuestWristSideState.RotationSemanticRollLastTwistDeg = 0.0f;
					QuestWristSideState.RotationProjectedCalibrationMask = ProjectedCandidateMask;
					for (uint8 CandidateIndex = 0; CandidateIndex < ProjectedCandidateCount; ++CandidateIndex)
					{
						QuestWristSideState.RotationProjectedCalibrationRefsComp[CandidateIndex] =
							((ProjectedCandidateMask & (1u << CandidateIndex)) != 0)
							? ProjectedCandidateRefs[CandidateIndex]
							: FVector::ZeroVector;
					}
					bHasQuestWristCalibration = QuestWristSideState.RotationProjectedCalibrationMask != 0;
				}
				else
				{
					QuestWristSideState.bHasRotationProjectedAxis = false;
					QuestWristSideState.RotationProjectedAxisIndex = 0;
					QuestWristSideState.RotationProjectedCalibrationMask = 0;
					QuestWristSideState.bHasRotationSemanticRollAxis = false;
					QuestWristSideState.RotationSemanticRollAxisIndex = 0;
					QuestWristSideState.RotationSemanticRollCalibrationOffsetDeg = 0.0f;
					QuestWristSideState.RotationSemanticRollCalibrationForwardComp = FVector::ZeroVector;
					QuestWristSideState.RotationSemanticRollCalibrationUpComp = FVector::ZeroVector;
					QuestWristSideState.bHasRotationSemanticRollLastTwist = false;
					QuestWristSideState.RotationSemanticRollLastTwistDeg = 0.0f;
					QuestWristCalibrationBasisComp = QuestMeasuredWristComp;
					bHasQuestWristCalibration = true;
					if (bUseAnatomicalRollAxisSolve)
					{
						float RollAxisPalmDot = 0.0f;
						float RollAxisForearmDot = 0.0f;
						if (!SelectQuestAnatomicalRollAxis(
							QuestWristJointComp,
							QuestHandBasisComp,
							ForearmAxisN,
							QuestWristSideState.RotationAnatomicalRollAxisIndex,
							QuestWristSideState.RotationAnatomicalRollAxisSign,
							RollAxisPalmDot,
							RollAxisForearmDot))
						{
							bHasQuestWristCalibration = false;
							QuestWristCalibrationBasisComp = FQuat::Identity;
						}
						else
						{
							QuestWristSideState.bHasRotationAnatomicalRollAxis = true;
							Trace.bUsedAnatomicalRollAxis = 1;
							Trace.AnatomicalRollAxisIndex = EncodeQuestSignedAxisIndex(
								QuestWristSideState.RotationAnatomicalRollAxisIndex,
								QuestWristSideState.RotationAnatomicalRollAxisSign);
							Trace.AnatomicalRollAxisPalmDot = RollAxisPalmDot;
							Trace.AnatomicalRollAxisForearmDot = RollAxisForearmDot;
						}
					}
					else
					{
						QuestWristSideState.bHasRotationAnatomicalRollAxis = false;
						QuestWristSideState.RotationAnatomicalRollAxisIndex = 0;
						QuestWristSideState.RotationAnatomicalRollAxisSign = 1.0f;
					}
				}

				if (bHasQuestWristCalibration)
				{
					Trace.bSetCalibration = 1;
					QuestWristSideState.RotationCalibrationState = QuestWristCalibrationState_Accepted;
					QuestWristSideState.RotationCalibrationRejectReason = QuestWristCalibrationReject_None;
				}
				return bHasQuestWristCalibration;
			};

			bool bAcceptedCalibrationThisFrame = false;
			const bool bUseCalibrationGate = CVarQuestWristCalibrationGate.GetValueOnAnyThread() != 0;
			if (bUseCalibrationGate)
			{
				uint8 RejectReason = QuestWristCalibrationReject_None;
				const bool bLeftTracked = IsQuestHandSideTracked(QuestHands, true);
				const bool bRightTracked = IsQuestHandSideTracked(QuestHands, false);
				if (!bRightTracked)
				{
					RejectReason = QuestWristCalibrationReject_RightHandNotTracked;
				}
				else if (!bLeftTracked)
				{
					RejectReason = QuestWristCalibrationReject_LeftHandNotTracked;
				}

				FVector MediaHeadWorld = FVector::ZeroVector;
				FQuat MediaBodyBasisWorld = FQuat::Identity;
				FVector HipRightWorld = FVector::ZeroVector;
				FVector ShoulderRightWorld = FVector::ZeroVector;
				FVector BodyUpWorld = FVector::ZeroVector;
				FVector BodyForwardWorld = FVector::ZeroVector;
				FVector QuestHmdWorld = FVector::ZeroVector;
				FQuat QuestHmdRotWorld = FQuat::Identity;
				const bool bMediaHeadValid = TryGetMediaPipeHeadFrameWorld(MediaHeadWorld, MediaBodyBasisWorld);
				const bool bBodyBasisValid = TryGetTorsoBasisWorld(HipRightWorld, ShoulderRightWorld, BodyUpWorld, BodyForwardWorld);
				const bool bHmdPoseValid = TryGetQuestHmdWorldPose(QuestHmdWorld, QuestHmdRotWorld);
				const float MinArmReliability = FMath::Clamp(CVarMediaPipeArmMinReliability.GetValueOnAnyThread(), 0.0f, 1.0f);
				auto HasUsableLandmark = [&](const EMediaPipePoseLandmark Landmark) -> bool
				{
					const int32 Index = static_cast<int32>(Landmark);
					FVector LmWorld = FVector::ZeroVector;
					return IsMeasured(Index) &&
						TryGetLmWorld(Index, LmWorld) &&
						GetLandmarkReliability(Index) >= MinArmReliability;
				};
				const bool bMediaPipePoseUsable =
					bMediaHeadValid &&
					bBodyBasisValid &&
					bHmdPoseValid &&
					HasUsableLandmark(EMediaPipePoseLandmark::Nose) &&
					HasUsableLandmark(EMediaPipePoseLandmark::LeftShoulder) &&
					HasUsableLandmark(EMediaPipePoseLandmark::RightShoulder) &&
					HasUsableLandmark(EMediaPipePoseLandmark::LeftElbow) &&
					HasUsableLandmark(EMediaPipePoseLandmark::RightElbow) &&
					HasUsableLandmark(EMediaPipePoseLandmark::LeftWrist) &&
					HasUsableLandmark(EMediaPipePoseLandmark::RightWrist);
				if (RejectReason == QuestWristCalibrationReject_None && !bMediaPipePoseUsable)
				{
					RejectReason = QuestWristCalibrationReject_BodyUnstable;
				}
				if (RejectReason == QuestWristCalibrationReject_None &&
					CVarQuestWristCalibrationRequirePoseMatch.GetValueOnAnyThread() != 0)
				{
					FVector LShoulderWorld = FVector::ZeroVector;
					FVector RShoulderWorld = FVector::ZeroVector;
					FVector LElbowWorld = FVector::ZeroVector;
					FVector RElbowWorld = FVector::ZeroVector;
					FVector LWristWorld = FVector::ZeroVector;
					FVector RWristWorld = FVector::ZeroVector;
					FVector LHipWorld = FVector::ZeroVector;
					FVector RHipWorld = FVector::ZeroVector;
					const bool bHasPoseGuideLandmarks =
						TryGetLmWorld(static_cast<int32>(EMediaPipePoseLandmark::LeftShoulder), LShoulderWorld) &&
						TryGetLmWorld(static_cast<int32>(EMediaPipePoseLandmark::RightShoulder), RShoulderWorld) &&
						TryGetLmWorld(static_cast<int32>(EMediaPipePoseLandmark::LeftElbow), LElbowWorld) &&
						TryGetLmWorld(static_cast<int32>(EMediaPipePoseLandmark::RightElbow), RElbowWorld) &&
						TryGetLmWorld(static_cast<int32>(EMediaPipePoseLandmark::LeftWrist), LWristWorld) &&
						TryGetLmWorld(static_cast<int32>(EMediaPipePoseLandmark::RightWrist), RWristWorld) &&
						TryGetLmWorld(static_cast<int32>(EMediaPipePoseLandmark::LeftHip), LHipWorld) &&
						TryGetLmWorld(static_cast<int32>(EMediaPipePoseLandmark::RightHip), RHipWorld);
					bool bPoseGuideMatched = false;
					if (bHasPoseGuideLandmarks)
					{
						FVector ShoulderRightN = (RShoulderWorld - LShoulderWorld).GetSafeNormal();
						if (ShoulderRightN.IsNearlyZero())
						{
							ShoulderRightN = ShoulderRightWorld.GetSafeNormal();
						}
						const float ShoulderWidth = FVector::Dist(LShoulderWorld, RShoulderWorld);
						const FVector HipMidWorld = (LHipWorld + RHipWorld) * 0.5f;
						const FVector ShoulderMidWorld = (LShoulderWorld + RShoulderWorld) * 0.5f;
						const float TorsoHeight = FMath::Abs(ShoulderMidWorld.Z - HipMidWorld.Z);
						const float MinScale = FMath::Max(FMath::Max(ShoulderWidth, TorsoHeight), 20.0f);
						const float MaxHeightError = MinScale * 0.85f;
						const float MinWristZ = HipMidWorld.Z + TorsoHeight * 0.45f;
						const float MaxWristZ = ShoulderMidWorld.Z + MinScale * 0.45f;
						const float MinElbowZ = HipMidWorld.Z + TorsoHeight * 0.35f;
						const float MaxElbowZ = ShoulderMidWorld.Z + MinScale * 0.45f;
						const float LWristSide = FVector::DotProduct(LWristWorld - LShoulderWorld, ShoulderRightN);
						const float RWristSide = FVector::DotProduct(RWristWorld - RShoulderWorld, ShoulderRightN);
						const float LElbowSide = FVector::DotProduct(LElbowWorld - LShoulderWorld, ShoulderRightN);
						const float RElbowSide = FVector::DotProduct(RElbowWorld - RShoulderWorld, ShoulderRightN);
						const bool bArmsOnExpectedSides =
							LWristSide <= MinScale * 0.55f &&
							LElbowSide <= MinScale * 0.55f &&
							RWristSide >= -MinScale * 0.55f &&
							RElbowSide >= -MinScale * 0.55f;
						const bool bArmsAtGuideHeight =
							LWristWorld.Z >= MinWristZ &&
							RWristWorld.Z >= MinWristZ &&
							LWristWorld.Z <= MaxWristZ &&
							RWristWorld.Z <= MaxWristZ &&
							LElbowWorld.Z >= MinElbowZ &&
							RElbowWorld.Z >= MinElbowZ &&
							LElbowWorld.Z <= MaxElbowZ &&
							RElbowWorld.Z <= MaxElbowZ &&
							FMath::Abs(LWristWorld.Z - RWristWorld.Z) <= MaxHeightError;
						const bool bHandsNotCollapsedTogether =
							FVector::Dist2D(LWristWorld, RWristWorld) >= MinScale * 0.35f;
						bool bHandsNotBehindBody = true;
						const FVector BodyForwardN = BodyForwardWorld.GetSafeNormal();
						if (!BodyForwardN.IsNearlyZero())
						{
							const float LWristForward = FVector::DotProduct(LWristWorld - LShoulderWorld, BodyForwardN);
							const float RWristForward = FVector::DotProduct(RWristWorld - RShoulderWorld, BodyForwardN);
							bHandsNotBehindBody =
								LWristForward >= -MinScale * 0.45f &&
								RWristForward >= -MinScale * 0.45f;
						}
						bPoseGuideMatched =
							ShoulderWidth >= 10.0f &&
							bArmsOnExpectedSides &&
							bArmsAtGuideHeight &&
							bHandsNotCollapsedTogether &&
							bHandsNotBehindBody;
					}

					if (!bPoseGuideMatched)
					{
						RejectReason = QuestWristCalibrationReject_PoseNotMatched;
					}
				}

				const double NowSeconds = FPlatformTime::Seconds();
				const TStaticArray<FVector, QuestHandKeypointCount>& QuestPositions = GetQuestHandPositions(QuestHands, bIsLeft);
				const FVector QuestWristWorld = QuestPositions[static_cast<int32>(EHandKeypoint::Wrist)];
				const float BodyYawDeg = QuestPlanarYawDeg(BodyForwardWorld);
				const float MannyYawDeg = TargetCompTransform.Rotator().Yaw;
				if (QuestWristSideState.bHasRotationCalibrationLastSample)
				{
					const double SampleDeltaSeconds = FMath::Max(0.0, NowSeconds - QuestWristSideState.RotationCalibrationLastSampleTimeSeconds);
					if (SampleDeltaSeconds > SMALL_NUMBER)
					{
						Trace.CalibrationHandVelocityCmSec = static_cast<float>(
							FVector::Dist(QuestWristWorld, QuestWristSideState.RotationCalibrationLastWristWorld) / SampleDeltaSeconds);
						Trace.CalibrationHandAngularVelocityDegSec = static_cast<float>(
							FMath::RadiansToDegrees(QuestMeasuredWristComp.AngularDistance(QuestWristSideState.RotationCalibrationLastMeasuredComp)) / SampleDeltaSeconds);
					}
					Trace.CalibrationBodyYawDeltaDeg = FMath::Abs(FMath::FindDeltaAngleDegrees(
						QuestWristSideState.RotationCalibrationLastBodyYawDeg,
						BodyYawDeg));
					Trace.CalibrationMannyYawDeltaDeg = FMath::Abs(FMath::FindDeltaAngleDegrees(
						QuestWristSideState.RotationCalibrationLastMannyYawDeg,
						MannyYawDeg));
				}

				const uint8 CurrentSemanticAxisIndex = bUseSemanticRollSolve
					? static_cast<uint8>(bUseSemanticBasisDeltaSolve ? 3 : 1)
					: 0;
				const bool bSemanticAxisStable =
					CurrentSemanticAxisIndex != 0 &&
					(!QuestWristSideState.bHasRotationCalibrationLastSample ||
						QuestWristSideState.RotationCalibrationLastSemanticAxisIndex == CurrentSemanticAxisIndex);
				const bool bHandsStable =
					Trace.CalibrationHandVelocityCmSec <= FMath::Max(0.0f, CVarQuestWristCalibrationMaxHandVelocityCmSec.GetValueOnAnyThread()) &&
					Trace.CalibrationHandAngularVelocityDegSec <= FMath::Max(0.0f, CVarQuestWristCalibrationMaxHandAngularVelocityDegSec.GetValueOnAnyThread());
				const bool bYawStable =
					Trace.CalibrationBodyYawDeltaDeg <= FMath::Max(0.0f, CVarQuestWristCalibrationMaxYawDeltaDegrees.GetValueOnAnyThread()) &&
					Trace.CalibrationMannyYawDeltaDeg <= FMath::Max(0.0f, CVarQuestWristCalibrationMaxYawDeltaDegrees.GetValueOnAnyThread());
				if (RejectReason == QuestWristCalibrationReject_None && !bSemanticAxisStable)
				{
					RejectReason = QuestWristCalibrationReject_SemanticAxisUnstable;
				}
				if (RejectReason == QuestWristCalibrationReject_None && !bYawStable)
				{
					RejectReason = QuestWristCalibrationReject_BodyUnstable;
				}
				if (RejectReason == QuestWristCalibrationReject_None && !bHandsStable)
				{
					RejectReason = QuestWristCalibrationReject_WristsMoving;
				}
				if (RejectReason == QuestWristCalibrationReject_None &&
					FMath::Abs(Trace.CalibrationNeutralTwistDeg) > FMath::Clamp(CVarQuestWristCalibrationMaxNeutralTwistDegrees.GetValueOnAnyThread(), 0.0f, 180.0f))
				{
					RejectReason = QuestWristCalibrationReject_NeutralTwistTooHigh;
				}
				if (RejectReason == QuestWristCalibrationReject_None &&
					Trace.CalibrationBasisErrorDeg > MaxCalibrationBasisErrorDeg)
				{
					RejectReason = QuestWristCalibrationReject_BasisErrorTooHigh;
				}

				if (RejectReason != QuestWristCalibrationReject_None)
				{
					Trace.bCalibrationRejected = 1;
					SoftRejectCalibrationMeasurement(RejectReason);
				}
				else
				{
					const int32 RequiredStableFrames = FMath::Max(1, CVarQuestWristCalibrationStableFrames.GetValueOnAnyThread());
					const float RequiredStableSeconds = FMath::Max(0.0f, CVarQuestWristCalibrationHoldSeconds.GetValueOnAnyThread());
					if (QuestWristSideState.RotationCalibrationState != QuestWristCalibrationState_MeasuringCalibration ||
						QuestWristSideState.RotationCalibrationMeasureStartTimeSeconds < 0.0)
					{
						QuestWristSideState.RotationCalibrationState = QuestWristCalibrationState_MeasuringCalibration;
						QuestWristSideState.RotationCalibrationRejectReason = QuestWristCalibrationReject_None;
						if (QuestWristSideState.RotationCalibrationStableFrameCount <= 0 &&
							QuestWristSideState.RotationCalibrationStableSeconds <= KINDA_SMALL_NUMBER)
						{
							QuestWristSideState.RotationCalibrationStableFrameCount = 0;
							QuestWristSideState.RotationCalibrationStableSeconds = 0.0f;
							QuestWristSideState.RotationCalibrationMeasureStartTimeSeconds = NowSeconds;
						}
						else
						{
							QuestWristSideState.RotationCalibrationMeasureStartTimeSeconds =
								NowSeconds - QuestWristSideState.RotationCalibrationStableSeconds;
						}
					}

					QuestWristSideState.RotationCalibrationStableFrameCount++;
					const float StableDeltaSeconds = QuestWristSideState.bHasRotationCalibrationLastSample
						? static_cast<float>(FMath::Clamp(
							NowSeconds - QuestWristSideState.RotationCalibrationLastSampleTimeSeconds,
							0.0,
							0.10))
						: FMath::Clamp(DeltaSeconds, 0.0f, 0.10f);
					QuestWristSideState.RotationCalibrationStableSeconds = FMath::Max(
						0.0f,
						QuestWristSideState.RotationCalibrationStableSeconds + StableDeltaSeconds);
					QuestWristSideState.RotationCalibrationFreshStableFrameCount++;
					QuestWristSideState.RotationCalibrationFreshStableSeconds = FMath::Max(
						0.0f,
						QuestWristSideState.RotationCalibrationFreshStableSeconds + StableDeltaSeconds);
					Trace.CalibrationStableFrameCount = QuestWristSideState.RotationCalibrationStableFrameCount;
					Trace.CalibrationStableSeconds = QuestWristSideState.RotationCalibrationStableSeconds;
					QuestWristSideState.RotationCalibrationLastSampleTimeSeconds = NowSeconds;
					QuestWristSideState.RotationCalibrationLastWristWorld = QuestWristWorld;
					QuestWristSideState.RotationCalibrationLastMeasuredComp = QuestMeasuredWristComp;
					QuestWristSideState.RotationCalibrationLastBodyYawDeg = BodyYawDeg;
					QuestWristSideState.RotationCalibrationLastMannyYawDeg = MannyYawDeg;
					QuestWristSideState.RotationCalibrationLastSemanticAxisIndex = CurrentSemanticAxisIndex;
					QuestWristSideState.bHasRotationCalibrationLastSample = true;

					const int32 RequiredFreshFrames = FMath::Min(
						RequiredStableFrames,
						FMath::Max(1, CVarQuestWristCalibrationMinFreshStableFrames.GetValueOnAnyThread()));
					const float RequiredFreshSeconds = FMath::Min(
						RequiredStableSeconds,
						FMath::Max(0.0f, CVarQuestWristCalibrationMinFreshStableSeconds.GetValueOnAnyThread()));
					if (QuestWristSideState.RotationCalibrationStableFrameCount >= RequiredStableFrames &&
						Trace.CalibrationStableSeconds >= RequiredStableSeconds &&
						QuestWristSideState.RotationCalibrationFreshStableFrameCount >= RequiredFreshFrames &&
						QuestWristSideState.RotationCalibrationFreshStableSeconds >= RequiredFreshSeconds)
					{
						bAcceptedCalibrationThisFrame = AcceptQuestWristRotationCalibration();
					}
				}
			}
			else if (Trace.CalibrationBasisErrorDeg > MaxCalibrationBasisErrorDeg)
			{
				Trace.bCalibrationRejected = 1;
				ResetCalibrationMeasurement(QuestWristCalibrationReject_BasisErrorTooHigh);
			}
			else if (FMath::Abs(Trace.CalibrationNeutralTwistDeg) >
				FMath::Clamp(CVarQuestWristCalibrationMaxNeutralTwistDegrees.GetValueOnAnyThread(), 0.0f, 180.0f))
			{
				Trace.bCalibrationRejected = 1;
				ResetCalibrationMeasurement(QuestWristCalibrationReject_NeutralTwistTooHigh);
			}
			else
			{
				AcceptQuestWristRotationCalibration();
			}

			UpdateCalibrationTrace();
			if (bAcceptedCalibrationThisFrame)
			{
				return false;
			}
		}
		else if (QuestWristSideState.RotationCalibrationState == QuestWristCalibrationState_Accepted)
		{
			QuestWristSideState.RotationCalibrationState = QuestWristCalibrationState_Tracking;
			QuestWristSideState.RotationCalibrationRejectReason = QuestWristCalibrationReject_None;
			UpdateCalibrationTrace();
		}
	}
	else
	{
		QuestWristSideState.ResetRotationCalibration();
		UpdateCalibrationTrace();
	}

	if (bUseRelativeWristRotationCalibration && !bHasQuestWristCalibration)
	{
		return false;
	}

	FQuat RelativeQuestHandTargetCS = bUseRelativeWristRotationCalibration && bUseSemanticRollSolve
		? MediaPipeHandTargetCS.GetNormalized()
		: QuestHandTargetCS;
	bool bSuppressQuestAuthoritativeHandOrientationForFrame = false;
	bool bDriveForearmTwistHelpersFromHand = false;
	bool bDriveForearmMainFromQuestRoll = false;
	bool bDriveUpperArmTwistHelpersFromQuestRoll = false;
	float ForearmMainTwistDeg = 0.0f;
	float UpperArmHelperQuestRollDeg = 0.0f;
	if (CVarQuestWristUpperArmRollDriveTwistHelpers.GetValueOnAnyThread() == 0)
	{
		bool& bHasSmoothedQuestUpperArmTwist = bIsLeft ? LeftQuestHandState.bHasSmoothedQuestUpperArmTwist : RightQuestHandState.bHasSmoothedQuestUpperArmTwist;
		float& SmoothedQuestUpperArmTwistDeg = bIsLeft ? LeftQuestHandState.SmoothedQuestUpperArmTwistDeg : RightQuestHandState.SmoothedQuestUpperArmTwistDeg;
		bHasSmoothedQuestUpperArmTwist = false;
		SmoothedQuestUpperArmTwistDeg = 0.0f;
	}
	if (bUseRelativeWristRotationCalibration && bHasQuestWristCalibration)
	{
		FQuat QuestSwingComp = FQuat::Identity;
		float QuestTwistDeg = 0.0f;
		if (bUseSemanticRollSolve)
		{
			if (!QuestWristSideState.bHasRotationSemanticRollAxis)
			{
				return TryApplyHeldQuestHandRotation();
			}

			if (bUseForearmLocalSemanticRollSolve && !bHasCurrentForearmBasisComp)
			{
				return TryApplyHeldQuestHandRotation();
			}

			float WrappedQuestTwistDeg = 0.0f;
			if (bUseForearmLocalSemanticRollSolve)
			{
				const FQuat CurrentQuestLocal = bUseSemanticBasisDeltaSolve
					? CurrentQuestHandBasisForearmLocal
					: CurrentQuestHandForearmLocalCS;
				const FQuat QuestHandDeltaLocal = (CurrentQuestLocal * QuestWristCalibrationBasisComp.Inverse()).GetNormalized();
				FQuat QuestSwingLocal = FQuat::Identity;
				if (bUseSemanticBasisDeltaSolve)
				{
					float ProjectedRollDeg = 0.0f;
					float ProjectedRollScore = 0.0f;
					bool bHasMeasuredPalmRoll = false;
					bool bFallbackProvidedSwing = false;
					const float MinPalmProjectionScore = FMath::Clamp(
						CVarQuestWristSemanticRollMinPalmProjection.GetValueOnAnyThread(),
						0.0f,
						1.0f);
					const bool bHasProjectedPalmRoll = MeasureProjectedBasisRollDeltaDeg(
						QuestWristCalibrationBasisComp,
						CurrentQuestLocal,
						FVector::ForwardVector,
						QuestWristSideState.RotationSemanticRollAxisIndex,
						ProjectedRollDeg,
						ProjectedRollScore);
					Trace.SemanticRollAxisScore = ProjectedRollScore;
					if (PalmMode >= 2)
					{
						// SOURCE HYSTERESIS + CONTINUITY (2026-07-09 worn forensics): the projected
						// roll and the swing-corrected fallback disagree by 20-130deg during side
						// raises while the projection score oscillates around the threshold, so the
						// old first-match-wins selection flapped between the two bases per frame -
						// the right wrist visibly snapped on every switch (38 flap rows, all side=R,
						// the hold never engaged because one source always measured). The source may
						// only change after a dwell; short projection dips HOLD the last roll; every
						// committed switch is rebased through a continuity bias so the output never
						// steps. The bias decays only while the calibrated primary measures - the
						// fallback contributes relative motion, never its absolute offset.
						float SwingCorrectedRollDeg = 0.0f;
						float SwingCorrectedRollScore = 0.0f;
						const bool bPrimaryRollOk = bHasProjectedPalmRoll && ProjectedRollScore >= MinPalmProjectionScore;
						const bool bFallbackRollOk =
							MeasureQuestSwingCorrectedRollDeg(
								CurrentQuestLocal,
								QuestWristSideState.RotationSemanticRollCalibrationForwardComp,
								QuestWristSideState.RotationSemanticRollCalibrationUpComp,
								SwingCorrectedRollDeg,
								SwingCorrectedRollScore) &&
							SwingCorrectedRollScore >= MinPalmProjectionScore;
						// Wall-clock dwell stepping (DeltaSeconds reaches evaluations as 0 without a
						// paired update - same measured failure as the arm-rescue dwell).
						const double PalmRollNowSeconds = FPlatformTime::Seconds();
						const float PalmRollStepSeconds = QuestWristSideState.PalmRollSourceLastUpdateTimeSeconds >= 0.0
							? FMath::Clamp(static_cast<float>(PalmRollNowSeconds - QuestWristSideState.PalmRollSourceLastUpdateTimeSeconds), 0.0f, 0.1f)
							: 0.0f;
						QuestWristSideState.PalmRollSourceLastUpdateTimeSeconds = PalmRollNowSeconds;
						// BOUNDED BIAS LIFETIME (2026-07-10 worn forensics): the bias re-anchors on
						// EVERY primary resume, and during raises the primary drops out faster than
						// the 0.6s decay can run - a wrong roll persisted indefinitely and "only
						// occasionally corrected itself" (user verdict; sticky sideways hand). The
						// bias now carries an age: it re-grounds (age 0) whenever the bias is within
						// a few degrees of zero, and once it lives past the cap without re-grounding
						// it rate-limited-converges to whichever source is measuring - truth wins
						// within ~1s of measured frames, still with no per-frame step. Re-anchoring
						// on a source switch deliberately does NOT reset the age: the re-based bias
						// inherits the old roll's wrongness.
						if (FMath::Abs(QuestWristSideState.PalmRollContinuityBiasDeg) <= 3.0f)
						{
							QuestWristSideState.PalmRollBiasAgeSeconds = 0.0f;
						}
						else
						{
							QuestWristSideState.PalmRollBiasAgeSeconds += PalmRollStepSeconds;
						}
						const bool bPalmRollBiasExpired = QuestWristSideState.PalmRollBiasAgeSeconds >= 0.75f;
						if (bPrimaryRollOk)
						{
							QuestWristSideState.PalmRollPrimaryOkSeconds += PalmRollStepSeconds;
							QuestWristSideState.PalmRollPrimaryBadSeconds = 0.0f;
						}
						else
						{
							QuestWristSideState.PalmRollPrimaryBadSeconds += PalmRollStepSeconds;
							QuestWristSideState.PalmRollPrimaryOkSeconds = 0.0f;
						}
						uint8 PalmRollDesiredSource = 0;
						if (QuestWristSideState.PalmRollActiveSource == 2)
						{
							if (bPrimaryRollOk && (QuestWristSideState.PalmRollPrimaryOkSeconds >= 0.25f || !bFallbackRollOk))
							{
								PalmRollDesiredSource = 1;
							}
							else if (bFallbackRollOk)
							{
								PalmRollDesiredSource = 2;
							}
						}
						else if (bPrimaryRollOk)
						{
							PalmRollDesiredSource = 1;
						}
						else if (bFallbackRollOk && QuestWristSideState.PalmRollPrimaryBadSeconds >= 0.30f)
						{
							PalmRollDesiredSource = 2;
						}
						const bool bHasRollContinuityAnchor = QuestWristSideState.bHasRotationSemanticRollLastTwist;
						const float LastRollWrappedDeg = bHasRollContinuityAnchor
							? FRotator::NormalizeAxis(QuestWristSideState.RotationSemanticRollLastTwistDeg)
							: 0.0f;
						if (PalmRollDesiredSource == 1)
						{
							if (QuestWristSideState.PalmRollActiveSource != 1 && bHasRollContinuityAnchor)
							{
								QuestWristSideState.PalmRollContinuityBiasDeg =
									FRotator::NormalizeAxis(LastRollWrappedDeg - ProjectedRollDeg);
							}
							else
							{
								// Expired biases converge fast (0.2s half-life plus a 60deg/s floor so
								// small residuals cannot linger): the primary IS measuring truth here,
								// and the only reason to keep any bias is step-free smoothness - which
								// an exponential-per-frame decay preserves.
								const float BiasDecayHalfLifeSeconds = bPalmRollBiasExpired ? 0.2f : 0.6f;
								float DecayedBiasDeg = FMath::Lerp(
									QuestWristSideState.PalmRollContinuityBiasDeg,
									0.0f,
									HalfLifeToAlpha(BiasDecayHalfLifeSeconds, PalmRollStepSeconds));
								if (bPalmRollBiasExpired)
								{
									const float LinearBiasDeg = FMath::Sign(QuestWristSideState.PalmRollContinuityBiasDeg) *
										FMath::Max(FMath::Abs(QuestWristSideState.PalmRollContinuityBiasDeg) - 60.0f * PalmRollStepSeconds, 0.0f);
									if (FMath::Abs(LinearBiasDeg) < FMath::Abs(DecayedBiasDeg))
									{
										DecayedBiasDeg = LinearBiasDeg;
									}
								}
								QuestWristSideState.PalmRollContinuityBiasDeg = DecayedBiasDeg;
							}
							WrappedQuestTwistDeg = FRotator::NormalizeAxis(ProjectedRollDeg + QuestWristSideState.PalmRollContinuityBiasDeg);
							bHasMeasuredPalmRoll = true;
							QuestWristSideState.PalmRollActiveSource = 1;
						}
						else if (PalmRollDesiredSource == 2)
						{
							if (QuestWristSideState.PalmRollActiveSource != 2 && bHasRollContinuityAnchor)
							{
								QuestWristSideState.PalmRollContinuityBiasDeg =
									FRotator::NormalizeAxis(LastRollWrappedDeg - SwingCorrectedRollDeg);
							}
							else if (bPalmRollBiasExpired)
							{
								// A young bias stays frozen here (the fallback contributes relative
								// motion, never its untrusted absolute offset) - but an EXPIRED bias
								// means the primary has not grounded the roll for the whole cap, and
								// an indefinite hold is exactly the sticky-wrong wrist. Converge
								// gently to the swing-corrected absolute: the only measurement present.
								QuestWristSideState.PalmRollContinuityBiasDeg = FMath::Lerp(
									QuestWristSideState.PalmRollContinuityBiasDeg,
									0.0f,
									HalfLifeToAlpha(1.0f, PalmRollStepSeconds));
							}
							WrappedQuestTwistDeg = FRotator::NormalizeAxis(SwingCorrectedRollDeg + QuestWristSideState.PalmRollContinuityBiasDeg);
							Trace.SemanticRollAxisScore = FMath::Max(Trace.SemanticRollAxisScore, SwingCorrectedRollScore);
							Trace.bUsedPalmRollFallback = 1;
							bHasMeasuredPalmRoll = true;
							QuestWristSideState.PalmRollActiveSource = 2;
						}
						// PalmRollDesiredSource == 0: neither source is committed this frame - fall
						// through to the hold below. PalmRollActiveSource is kept so the dwell logic
						// resumes from the prior committed source.
					}
					else if (bHasProjectedPalmRoll && ProjectedRollScore >= MinPalmProjectionScore)
					{
						WrappedQuestTwistDeg = ProjectedRollDeg;
						bHasMeasuredPalmRoll = true;
					}
					else if (PalmMode >= 1)
					{
						FQuat FallbackSwingLocal = FQuat::Identity;
						float FallbackTwistDeg = 0.0f;
						if (DecomposeSwingTwistDegAroundAxis(QuestHandDeltaLocal, FVector::ForwardVector, FallbackSwingLocal, FallbackTwistDeg))
						{
							WrappedQuestTwistDeg = FallbackTwistDeg;
							QuestSwingLocal = FallbackSwingLocal;
							Trace.bUsedPalmRollFallback = 1;
							bHasMeasuredPalmRoll = true;
							bFallbackProvidedSwing = true;
						}
					}

					if (!bHasMeasuredPalmRoll)
					{
						WrappedQuestTwistDeg = QuestWristSideState.bHasRotationSemanticRollLastTwist
							? FRotator::NormalizeAxis(QuestWristSideState.RotationSemanticRollLastTwistDeg)
							: 0.0f;
						Trace.bHeldPalmRoll = 1;
					}

					if (!bFallbackProvidedSwing)
					{
						const FQuat TwistLocal(FVector::ForwardVector, FMath::DegreesToRadians(WrappedQuestTwistDeg));
						QuestSwingLocal = (QuestHandDeltaLocal * TwistLocal.Inverse()).GetNormalized();
					}
				}
				else if (!DecomposeSwingTwistDegAroundAxis(QuestHandDeltaLocal, FVector::ForwardVector, QuestSwingLocal, WrappedQuestTwistDeg))
				{
					return TryApplyHeldQuestHandRotation();
				}
				QuestSwingComp = (CurrentForearmBasisComp * QuestSwingLocal * CurrentForearmBasisComp.Inverse()).GetNormalized();
			}
			else
			{
				const FQuat QuestHandDeltaComp = (
					bUseSemanticBasisDeltaSolve
						? QuestHandBasisComp
						: QuestHandTargetCS) * QuestWristCalibrationBasisComp.Inverse();
				const FQuat QuestHandDeltaCompNormalized = QuestHandDeltaComp.GetNormalized();
				if (!DecomposeSwingTwistDegAroundAxis(QuestHandDeltaCompNormalized, ForearmAxisN, QuestSwingComp, WrappedQuestTwistDeg))
				{
					return TryApplyHeldQuestHandRotation();
				}
			}

			if (QuestWristSideState.bHasRotationSemanticRollLastTwist)
			{
				QuestTwistDeg = FMediaPipeQuestWristApplyPolicy::ContinueAngleDegrees(
					QuestWristSideState.RotationSemanticRollLastTwistDeg,
					WrappedQuestTwistDeg);
			}
			else
			{
				QuestTwistDeg = WrappedQuestTwistDeg;
				QuestWristSideState.bHasRotationSemanticRollLastTwist = true;
			}
			// Keep the semantic roll accumulator unwrapped. Normalizing here can flip a
			// steady +170 degree wrist roll to -170 before the max-twist clamp.
			QuestWristSideState.RotationSemanticRollLastTwistDeg = QuestTwistDeg;
			Trace.bUsedSemanticRoll = 1;
			Trace.bUsedForearmLocalSemanticRoll = bUseForearmLocalSemanticRollSolve ? 1 : 0;
			Trace.bUsedSemanticBasisDelta = bUseSemanticBasisDeltaSolve ? 1 : 0;
			Trace.SemanticRollAxisIndex = QuestWristSideState.RotationSemanticRollAxisIndex + 1;
			if (!bUseSemanticBasisDeltaSolve)
			{
				Trace.SemanticRollAxisScore = 1.0f;
			}
			Trace.SemanticRollCalibrationOffsetDeg = QuestWristSideState.RotationSemanticRollCalibrationOffsetDeg;
			Trace.SemanticRollCurrentOffsetDeg = WrappedQuestTwistDeg;
		}
		else if (bUseProjectedTwistSolve)
		{
			const uint8 CalibrationMask = QuestWristSideState.RotationProjectedCalibrationMask;
			float BestScore = -1.0f;
			bool bFoundProjectedTwist = false;
			for (uint8 CandidateIndex = 0; CandidateIndex < ProjectedCandidateCount; ++CandidateIndex)
			{
				const uint8 CandidateBit = (1u << CandidateIndex);
				if ((CalibrationMask & CandidateBit) == 0 || (ProjectedCandidateMask & CandidateBit) == 0)
				{
					continue;
				}

				const FVector CalibrationRef = QuestWristSideState.RotationProjectedCalibrationRefsComp[CandidateIndex].GetSafeNormal();
				const FVector CurrentRef = ProjectedCandidateRefs[CandidateIndex].GetSafeNormal();
				if (CalibrationRef.IsNearlyZero() || CurrentRef.IsNearlyZero())
				{
					continue;
				}

				float Score = ProjectedCandidateScores[CandidateIndex];
				if (QuestWristSideState.bHasRotationProjectedAxis &&
					QuestWristSideState.RotationProjectedAxisIndex == CandidateIndex)
				{
					Score += 0.05f;
				}
				if (Score <= BestScore)
				{
					continue;
				}

				const float SinAngle = FVector::DotProduct(ForearmAxisN, FVector::CrossProduct(CalibrationRef, CurrentRef));
				const float CosAngle = FMath::Clamp(FVector::DotProduct(CalibrationRef, CurrentRef), -1.0f, 1.0f);
				ProjectedQuestTwistDeg = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(SinAngle, CosAngle)));
				SelectedProjectedCandidate = CandidateIndex;
				BestScore = Score;
				bFoundProjectedTwist = true;
			}

			if (!bFoundProjectedTwist)
			{
				return TryApplyHeldQuestHandRotation();
			}

			QuestWristSideState.bHasRotationProjectedAxis = true;
			QuestWristSideState.RotationProjectedAxisIndex = SelectedProjectedCandidate;
			Trace.bProjectedTwistUsesJointAxes = SelectedProjectedCandidate < 3 ? 1 : 0;
			Trace.ProjectedTwistAxisIndex = SelectedProjectedCandidate + 1;
			QuestTwistDeg = ProjectedQuestTwistDeg;
		}
		else
		{
			const FQuat QuestDeltaComp = (QuestMeasuredWristComp * QuestWristCalibrationBasisComp.Inverse()).GetNormalized();
			if (bUseAnatomicalRollAxisSolve)
			{
				if (!QuestWristSideState.bHasRotationAnatomicalRollAxis)
				{
					return TryApplyHeldQuestHandRotation();
				}

				const FVector RollAxisComp = QuestSignedBasisAxis(
					QuestWristCalibrationBasisComp,
					QuestWristSideState.RotationAnatomicalRollAxisIndex,
					QuestWristSideState.RotationAnatomicalRollAxisSign);
				const FVector CurrentRollAxisComp = QuestSignedBasisAxis(
					QuestWristJointComp,
					QuestWristSideState.RotationAnatomicalRollAxisIndex,
					QuestWristSideState.RotationAnatomicalRollAxisSign);
				const FVector CurrentPalmForwardComp = QuestHandBasisComp.RotateVector(FVector::ForwardVector).GetSafeNormal();
				if (RollAxisComp.IsNearlyZero())
				{
					return TryApplyHeldQuestHandRotation();
				}

				Trace.bUsedAnatomicalRollAxis = 1;
				Trace.AnatomicalRollAxisIndex = EncodeQuestSignedAxisIndex(
					QuestWristSideState.RotationAnatomicalRollAxisIndex,
					QuestWristSideState.RotationAnatomicalRollAxisSign);
				Trace.AnatomicalRollAxisPalmDot = CurrentPalmForwardComp.IsNearlyZero()
					? 0.0f
					: FVector::DotProduct(CurrentRollAxisComp, CurrentPalmForwardComp);
				Trace.AnatomicalRollAxisForearmDot = FVector::DotProduct(CurrentRollAxisComp, ForearmAxisN);
				if (!DecomposeSwingTwistDegAroundAxis(QuestDeltaComp, RollAxisComp, QuestSwingComp, QuestTwistDeg))
				{
					return TryApplyHeldQuestHandRotation();
				}
			}
			else if (!DecomposeSwingTwistDegAroundAxis(QuestDeltaComp, ForearmAxisN, QuestSwingComp, QuestTwistDeg))
			{
				return TryApplyHeldQuestHandRotation();
			}
		}

		const FVector TwistAxisComp =
			bUseSemanticRollSolve && bUseForearmLocalSemanticRollSolve && bHasCurrentForearmBasisComp
				? CurrentForearmBasisComp.RotateVector(FVector::ForwardVector).GetSafeNormal()
				: ForearmAxisN;
		if (TwistAxisComp.IsNearlyZero())
		{
			return TryApplyHeldQuestHandRotation();
		}

		const bool bInvertTwist =
			CVarQuestWristInvertTwist.GetValueOnAnyThread() != 0 ||
			(bIsLeft
				? CVarQuestWristInvertTwistLeft.GetValueOnAnyThread() != 0
				: CVarQuestWristInvertTwistRight.GetValueOnAnyThread() != 0);
		if (bInvertTwist)
		{
			QuestTwistDeg *= -1.0f;
		}

		const float TwistBlend = FMath::Clamp(CVarQuestWristTwistBlend.GetValueOnAnyThread(), 0.0f, 1.0f);
		const float SwingBlend = FMath::Clamp(CVarQuestWristSwingBlend.GetValueOnAnyThread(), 0.0f, 1.0f);
		const float MaxTwistDeg = FMath::Clamp(CVarQuestWristMaxTwistDegrees.GetValueOnAnyThread(), 0.0f, 180.0f);
		const float MaxSwingDeg = FMath::Clamp(CVarQuestWristMaxSwingDegrees.GetValueOnAnyThread(), 0.0f, 180.0f);

		const float ClampedTwistDeg = FMath::Clamp(QuestTwistDeg, -MaxTwistDeg, MaxTwistDeg);
		const float LimitedTwistDeg = ClampedTwistDeg * TwistBlend;
		const FQuat TwistQ(TwistAxisComp, FMath::DegreesToRadians(LimitedTwistDeg));
		Trace.bTwistLimitClamped = FMath::Abs(ClampedTwistDeg - QuestTwistDeg) > 0.1f ? 1 : 0;
		Trace.RawTwistDeg = QuestTwistDeg;
		Trace.LimitedTwistDeg = LimitedTwistDeg;
		if (CVarQuestWristUpperArmRollDriveTwistHelpers.GetValueOnAnyThread() != 0)
		{
			const float UpperArmTwistBlend = FMath::Clamp(CVarQuestWristUpperArmTwistBlend.GetValueOnAnyThread(), 0.0f, 1.0f);
			const float UpperArmMaxTwistDeg = FMath::Clamp(CVarQuestWristUpperArmMaxTwistDegrees.GetValueOnAnyThread(), 0.0f, MaxTwistDeg);
			if (UpperArmTwistBlend > KINDA_SMALL_NUMBER && UpperArmMaxTwistDeg > KINDA_SMALL_NUMBER)
			{
				const float UnclampedUpperArmTwistDeg = LimitedTwistDeg * UpperArmTwistBlend;
				const float TargetUpperArmTwistDeg = FMath::Clamp(UnclampedUpperArmTwistDeg, -UpperArmMaxTwistDeg, UpperArmMaxTwistDeg);
				Trace.TargetUpperArmTwistDeg = TargetUpperArmTwistDeg;
				bool& bHasSmoothedQuestUpperArmTwist = bIsLeft ? LeftQuestHandState.bHasSmoothedQuestUpperArmTwist : RightQuestHandState.bHasSmoothedQuestUpperArmTwist;
				float& SmoothedQuestUpperArmTwistDeg = bIsLeft ? LeftQuestHandState.SmoothedQuestUpperArmTwistDeg : RightQuestHandState.SmoothedQuestUpperArmTwistDeg;
				if (!bHasSmoothedQuestUpperArmTwist)
				{
					SmoothedQuestUpperArmTwistDeg = 0.0f;
					bHasSmoothedQuestUpperArmTwist = true;
				}
				const float UpperArmTwistAlpha = HalfLifeToAlpha(ArmIKRotationHalfLifeSeconds, DeltaSeconds);
				float UpperArmTwistStepDeg = FMath::FindDeltaAngleDegrees(SmoothedQuestUpperArmTwistDeg, TargetUpperArmTwistDeg) * UpperArmTwistAlpha;
				if (HandTwistMaxDegreesPerSecond > 0.0f && DeltaSeconds > 0.0f)
				{
					const float MaxTwistStepDeg = HandTwistMaxDegreesPerSecond * DeltaSeconds;
					Trace.UpperArmTwistMaxStepDeg = MaxTwistStepDeg;
					Trace.bUpperArmRateClamped = FMath::Abs(UpperArmTwistStepDeg) > MaxTwistStepDeg + 0.1f ? 1 : 0;
					UpperArmTwistStepDeg = FMath::Clamp(UpperArmTwistStepDeg, -MaxTwistStepDeg, MaxTwistStepDeg);
				}
				Trace.UpperArmTwistStepDeg = UpperArmTwistStepDeg;
				SmoothedQuestUpperArmTwistDeg = FRotator::NormalizeAxis(SmoothedQuestUpperArmTwistDeg + UpperArmTwistStepDeg);
				SmoothedQuestUpperArmTwistDeg = FMath::Clamp(SmoothedQuestUpperArmTwistDeg, -UpperArmMaxTwistDeg, UpperArmMaxTwistDeg);
				Trace.SmoothedUpperArmTwistDeg = SmoothedQuestUpperArmTwistDeg;
				UpperArmHelperQuestRollDeg = SmoothedQuestUpperArmTwistDeg;
				bDriveUpperArmTwistHelpersFromQuestRoll = true;
				Trace.UpperArmTwistHelperScale = UpperArmTwistBlend;
			}
		}

		FQuat LimitedSwingQ = FQuat::Identity;
		float SwingAngleDeg = 0.0f;
		float AppliedSwingDeg = 0.0f;
		if (SwingBlend > KINDA_SMALL_NUMBER && MaxSwingDeg > KINDA_SMALL_NUMBER)
		{
			SwingAngleDeg = FMath::RadiansToDegrees(FQuat::Identity.AngularDistance(QuestSwingComp));
			const float SwingLimitAlpha = SwingAngleDeg > MaxSwingDeg && SwingAngleDeg > KINDA_SMALL_NUMBER
				? MaxSwingDeg / SwingAngleDeg
				: 1.0f;
			const float SwingAlpha = SwingLimitAlpha * SwingBlend;
			LimitedSwingQ = FQuat::Slerp(FQuat::Identity, QuestSwingComp, SwingAlpha).GetNormalized();
			AppliedSwingDeg = SwingAngleDeg * SwingAlpha;
		}
		Trace.RawSwingDeg = SwingAngleDeg;
		Trace.AppliedSwingDeg = AppliedSwingDeg;
		if (CVarQuestWristRejectSwingClamp.GetValueOnAnyThread() != 0 &&
			SwingBlend > KINDA_SMALL_NUMBER &&
			MaxSwingDeg > KINDA_SMALL_NUMBER &&
			SwingAngleDeg > MaxSwingDeg + 0.1f)
		{
			LimitedSwingQ = FQuat::Identity;
			AppliedSwingDeg = 0.0f;
			Trace.AppliedSwingDeg = 0.0f;
			// In authoritative Quest palm mode, suppressing the Quest target here leaves only
			// wrist twist applied, which can visibly flip the hand when the swing projection
			// briefly exceeds the clamp.
			if (!bUseQuestAuthoritativeHandOrientation)
			{
				bSuppressQuestAuthoritativeHandOrientationForFrame = true;
			}
		}

		if (CVarQuestWristTwistDrivesForearm.GetValueOnAnyThread() != 0)
		{
			const float ForearmTwistBlend = FMath::Clamp(CVarQuestWristForearmTwistBlend.GetValueOnAnyThread(), 0.0f, 1.0f);
			const float ForearmMaxTwistDeg = FMath::Clamp(CVarQuestWristForearmMaxTwistDegrees.GetValueOnAnyThread(), 0.0f, MaxTwistDeg);
			const float UnclampedForearmTwistDeg = LimitedTwistDeg * ForearmTwistBlend;
			const float TargetForearmTwistDeg = FMath::Clamp(UnclampedForearmTwistDeg, -ForearmMaxTwistDeg, ForearmMaxTwistDeg);
			Trace.TargetForearmTwistDeg = TargetForearmTwistDeg;
			Trace.bForearmLimitClamped = FMath::Abs(TargetForearmTwistDeg - UnclampedForearmTwistDeg) > 0.1f ? 1 : 0;
			bool& bHasSmoothedQuestForearmTwist = bIsLeft ? LeftQuestHandState.bHasSmoothedQuestForearmTwist : RightQuestHandState.bHasSmoothedQuestForearmTwist;
			float& SmoothedQuestForearmTwistDeg = bIsLeft ? LeftQuestHandState.SmoothedQuestForearmTwistDeg : RightQuestHandState.SmoothedQuestForearmTwistDeg;
			if (!bHasSmoothedQuestForearmTwist)
			{
				SmoothedQuestForearmTwistDeg = 0.0f;
				bHasSmoothedQuestForearmTwist = true;
			}
			const float TwistAlpha = HalfLifeToAlpha(ArmIKRotationHalfLifeSeconds, DeltaSeconds);
			float TwistStepDeg = FMath::FindDeltaAngleDegrees(SmoothedQuestForearmTwistDeg, TargetForearmTwistDeg) * TwistAlpha;
			if (HandTwistMaxDegreesPerSecond > 0.0f && DeltaSeconds > 0.0f)
			{
				const float MaxTwistStepDeg = HandTwistMaxDegreesPerSecond * DeltaSeconds;
				Trace.ForearmTwistMaxStepDeg = MaxTwistStepDeg;
				Trace.bForearmRateClamped = FMath::Abs(TwistStepDeg) > MaxTwistStepDeg + 0.1f ? 1 : 0;
				TwistStepDeg = FMath::Clamp(TwistStepDeg, -MaxTwistStepDeg, MaxTwistStepDeg);
			}
			Trace.ForearmTwistStepDeg = TwistStepDeg;
			SmoothedQuestForearmTwistDeg = FRotator::NormalizeAxis(SmoothedQuestForearmTwistDeg + TwistStepDeg);
			SmoothedQuestForearmTwistDeg = FMath::Clamp(SmoothedQuestForearmTwistDeg, -ForearmMaxTwistDeg, ForearmMaxTwistDeg);
			Trace.SmoothedForearmTwistDeg = SmoothedQuestForearmTwistDeg;
			ForearmMainTwistDeg = SmoothedQuestForearmTwistDeg;
			bDriveForearmMainFromQuestRoll = FMath::Abs(ForearmMainTwistDeg) > KINDA_SMALL_NUMBER;

			const FBoneReference& LowerArmBone = bIsLeft ? LowerArmL : LowerArmR;
			const FBoneReference& LowerArmTwist01Bone = bIsLeft ? LowerArmTwist01L : LowerArmTwist01R;
			const FBoneReference& LowerArmTwist02Bone = bIsLeft ? LowerArmTwist02L : LowerArmTwist02R;

			const FVector LowerArmPosComp = LowerArmBone.IsValidToEvaluate()
				? CSPose.GetComponentSpaceTransform(LowerArmBone.CachedCompactPoseIndex).GetTranslation()
				: FVector::ZeroVector;
			const FVector HandPosComp = HandBone.IsValidToEvaluate()
				? CSPose.GetComponentSpaceTransform(HandBone.CachedCompactPoseIndex).GetTranslation()
				: FVector::ZeroVector;
			const float ForearmLenComp = FVector::DotProduct(HandPosComp - LowerArmPosComp, ForearmAxisN);

			auto GetForearmTwistAlpha = [&](const FBoneReference& Bone) -> float
			{
				if (!Bone.IsValidToEvaluate() || ForearmLenComp <= KINDA_SMALL_NUMBER)
				{
					return 0.0f;
				}

				const FVector BonePosComp = CSPose.GetComponentSpaceTransform(Bone.CachedCompactPoseIndex).GetTranslation();
				return FMath::Clamp(FVector::DotProduct(BonePosComp - LowerArmPosComp, ForearmAxisN) / ForearmLenComp, 0.0f, 1.0f);
			};

			Trace.ForearmTwistAlpha01 = GetForearmTwistAlpha(LowerArmTwist01Bone);
			Trace.ForearmTwistAlpha02 = GetForearmTwistAlpha(LowerArmTwist02Bone);
			if (CVarQuestWristForearmRollDriveTwistHelpers.GetValueOnAnyThread() != 0)
			{
				const float TwistScaleDenom = FMath::Max(FMath::Abs(LimitedTwistDeg), FMath::Abs(SmoothedQuestForearmTwistDeg));
				const float ForearmTwistHelperScale = bDriveForearmMainFromQuestRoll
					? 1.0f
					: (TwistScaleDenom > KINDA_SMALL_NUMBER
						? FMath::Clamp(FMath::Abs(SmoothedQuestForearmTwistDeg) / TwistScaleDenom, 0.0f, 1.0f)
						: 0.0f);
				Trace.ForearmTwistHelperScale = ForearmTwistHelperScale;
				Trace.ForearmTwistApplied01Deg = SmoothedQuestForearmTwistDeg * Trace.ForearmTwistAlpha01 * ForearmTwistHelperScale;
				Trace.ForearmTwistApplied02Deg = SmoothedQuestForearmTwistDeg * Trace.ForearmTwistAlpha02 * ForearmTwistHelperScale;
				bDriveForearmTwistHelpersFromHand =
					ForearmTwistHelperScale > KINDA_SMALL_NUMBER &&
					(Trace.ForearmTwistAlpha01 > KINDA_SMALL_NUMBER || Trace.ForearmTwistAlpha02 > KINDA_SMALL_NUMBER);
			}
		}

		const FQuat ConstrainedQuestDeltaComp = (LimitedSwingQ * TwistQ).GetNormalized();
		if (bUseSemanticRollSolve)
		{
			const FQuat QuestHandDeltaComp = bUseForearmLocalSemanticRollSolve
				? ((bUseSemanticBasisDeltaSolve ? CurrentQuestHandBasisForearmLocal : CurrentQuestHandForearmLocalCS) * QuestWristCalibrationBasisComp.Inverse()).GetNormalized()
				: ((bUseSemanticBasisDeltaSolve ? QuestHandBasisComp : QuestHandTargetCS) * QuestWristCalibrationBasisComp.Inverse()).GetNormalized();
			Trace.bCalibratedHandDeltaValid = 1;
			Trace.CalibratedHandDeltaDeg = FMath::RadiansToDegrees(FQuat::Identity.AngularDistance(QuestHandDeltaComp));
		}
		else
		{
			Trace.bCalibratedHandDeltaValid = 1;
			Trace.CalibratedHandDeltaDeg = FMath::RadiansToDegrees(FQuat::Identity.AngularDistance(ConstrainedQuestDeltaComp));
		}
		if (bUseSemanticBasisDeltaSolve)
		{
			const FQuat RelativeQuestHandBasisComp = (ConstrainedQuestDeltaComp * MediaPipeHandBasisComp).GetNormalized();
			RelativeQuestHandTargetCS = MakeQuestHandTargetCS(RelativeQuestHandBasisComp);
		}
		else
		{
			RelativeQuestHandTargetCS = (ConstrainedQuestDeltaComp * MediaPipeHandTargetCS.GetNormalized()).GetNormalized();
		}
	}
	if (bUseQuestAuthoritativeHandOrientation && !bSuppressQuestAuthoritativeHandOrientationForFrame)
	{
		RelativeQuestHandTargetCS = QuestHandTargetCS;
	}

	const FQuat TargetHandRotCS = Blend >= 1.0f
		? RelativeQuestHandTargetCS
		: FQuat::Slerp(MediaPipeHandTargetCS.GetNormalized(), RelativeQuestHandTargetCS, Blend).GetNormalized();
	const float TargetDeltaFromMediaPipeDeg = FMath::RadiansToDegrees(MediaPipeHandTargetCS.GetNormalized().AngularDistance(TargetHandRotCS));
	const float MaxTargetDeltaFromMediaPipeDeg = FMath::Clamp(CVarQuestHandRotationMaxDeltaFromMediaPipeDegrees.GetValueOnAnyThread(), 0.0f, 180.0f);
	const bool bUseDirectTrackedQuestHandRotation =
		bUseQuestAuthoritativeHandOrientation &&
		Trace.bQuestTracked != 0 &&
		(!WristTrace ||
			(WristTrace->bUsedHeldQuestWrist == 0 &&
			 WristTrace->bRawQuestRejected == 0 &&
			 WristTrace->bConstrainedArmBodyFallbackApplied == 0));
	const float MaxHandRotationStepDeg = bUseDirectTrackedQuestHandRotation
		? 0.0f
		: FMath::Max(0.0f, CVarQuestHandRotationMaxStepDegrees.GetValueOnAnyThread());
	if (MaxTargetDeltaFromMediaPipeDeg > KINDA_SMALL_NUMBER &&
		TargetDeltaFromMediaPipeDeg > MaxTargetDeltaFromMediaPipeDeg)
	{
		Trace.bCalibratedHandDeltaValid = 1;
		Trace.CalibratedHandDeltaDeg = FMath::Max(Trace.CalibratedHandDeltaDeg, TargetDeltaFromMediaPipeDeg);
		return TryApplyHeldQuestHandRotation();
	}
	Trace.QuestExpectedForwardComp = QuestHandTargetCS.RotateVector(FVector::ForwardVector).GetSafeNormal();
	Trace.QuestExpectedUpComp = QuestHandTargetCS.RotateVector(FVector::UpVector).GetSafeNormal();
	Trace.RollTargetForwardComp = TargetHandRotCS.RotateVector(FVector::ForwardVector).GetSafeNormal();
	Trace.RollTargetUpComp = TargetHandRotCS.RotateVector(FVector::UpVector).GetSafeNormal();
	Trace.MediaPipeHandForwardComp = MediaPipeHandTargetCS.GetNormalized().RotateVector(FVector::ForwardVector).GetSafeNormal();
	Trace.MediaPipeHandUpComp = MediaPipeHandTargetCS.GetNormalized().RotateVector(FVector::UpVector).GetSafeNormal();
	Trace.QuestExpectedToRollTargetDeg = FMath::RadiansToDegrees(QuestHandTargetCS.AngularDistance(TargetHandRotCS));
	const FQuat RollTargetBasisComp = (TargetHandRotCS * RefHandComp.Inverse() * RefHandBasisComp).GetNormalized();
	Trace.QuestBasisForwardComp = QuestBasisAxis(QuestHandBasisComp, 0);
	Trace.QuestBasisUpComp = QuestBasisAxis(QuestHandBasisComp, 2);
	Trace.RollTargetBasisForwardComp = QuestBasisAxis(RollTargetBasisComp, 0);
	Trace.RollTargetBasisUpComp = QuestBasisAxis(RollTargetBasisComp, 2);
	Trace.MediaPipeBasisForwardComp = QuestBasisAxis(MediaPipeHandBasisComp, 0);
	Trace.MediaPipeBasisUpComp = QuestBasisAxis(MediaPipeHandBasisComp, 2);
	Trace.QuestBasisToRollBasisForwardErrDeg = QuestAngleBetweenSegmentsDeg(Trace.QuestBasisForwardComp, Trace.RollTargetBasisForwardComp);
	Trace.QuestBasisToRollBasisUpErrDeg = QuestAngleBetweenSegmentsDeg(Trace.QuestBasisUpComp, Trace.RollTargetBasisUpComp);

	const float QuestHandRotationHalfLifeSeconds = bUseDirectTrackedQuestHandRotation
		? 0.0f
		: FMath::Max(0.0f, CVarQuestHandRotationHalfLife.GetValueOnAnyThread());
	const float Alpha = HalfLifeToAlpha(QuestHandRotationHalfLifeSeconds, DeltaSeconds);
	FQuat AppliedQuestHandRotCS = TargetHandRotCS;
	if (ParentLowerArmBone.IsValidToEvaluate())
	{
		FQuat LowerArmRotCS = CSPose.GetComponentSpaceTransform(ParentLowerArmBone.CachedCompactPoseIndex).GetRotation().GetNormalized();
		if (bDriveForearmMainFromQuestRoll)
		{
			const FVector ForearmRollAxisComp = ForearmAxisComp.GetSafeNormal();
			if (!ForearmRollAxisComp.IsNearlyZero())
			{
				const FQuat ForearmRollCS(ForearmRollAxisComp, FMath::DegreesToRadians(ForearmMainTwistDeg));
				ApplyRotationCS(CSPose, ParentLowerArmBone, (ForearmRollCS * LowerArmRotCS).GetNormalized());
				LowerArmRotCS = CSPose.GetComponentSpaceTransform(ParentLowerArmBone.CachedCompactPoseIndex).GetRotation().GetNormalized();
				Trace.bLowerArmMainDriven = 1;
				Trace.bAppliedForearmTwist = 1;
			}
		}
		const FQuat TargetHandRotLocal = (LowerArmRotCS.Inverse() * TargetHandRotCS).GetNormalized();
		bool& bHasSmoothedQuestHandRotLocal = bIsLeft ? LeftQuestHandState.bHasSmoothedQuestHandRotLocal : RightQuestHandState.bHasSmoothedQuestHandRotLocal;
		FQuat& SmoothedQuestHandRotLocal = bIsLeft ? LeftQuestHandState.SmoothedQuestHandRotLocal : RightQuestHandState.SmoothedQuestHandRotLocal;
		if (!bHasSmoothedQuestHandRotLocal)
		{
			SmoothedQuestHandRotLocal = (LowerArmRotCS.Inverse() * MediaPipeHandTargetCS.GetNormalized()).GetNormalized();
			bHasSmoothedQuestHandRotLocal = true;
		}
		if (bUseDirectTrackedQuestHandRotation)
		{
			SmoothedQuestHandRotLocal = TargetHandRotLocal;
		}
		else
		{
			UpdateSmoothedRotation(bHasSmoothedQuestHandRotLocal, SmoothedQuestHandRotLocal, TargetHandRotLocal, Alpha, MaxHandRotationStepDeg);
		}
		AppliedQuestHandRotCS = (LowerArmRotCS * SmoothedQuestHandRotLocal).GetNormalized();
		Trace.bAppliedHandLocalToLowerArm = 1;
	}
	else
	{
		bool& bHasSmoothedQuestHandRotCS = bIsLeft ? LeftQuestHandState.bHasSmoothedQuestHandRotCS : RightQuestHandState.bHasSmoothedQuestHandRotCS;
		FQuat& SmoothedQuestHandRotCS = bIsLeft ? LeftQuestHandState.SmoothedQuestHandRotCS : RightQuestHandState.SmoothedQuestHandRotCS;
		if (!bHasSmoothedQuestHandRotCS)
		{
			SmoothedQuestHandRotCS = MediaPipeHandTargetCS.GetNormalized();
			bHasSmoothedQuestHandRotCS = true;
		}
		if (bUseDirectTrackedQuestHandRotation)
		{
			SmoothedQuestHandRotCS = TargetHandRotCS;
		}
		else
		{
			UpdateSmoothedRotation(bHasSmoothedQuestHandRotCS, SmoothedQuestHandRotCS, TargetHandRotCS, Alpha, MaxHandRotationStepDeg);
		}
		AppliedQuestHandRotCS = SmoothedQuestHandRotCS;
	}
	AppliedQuestHandRotCS = ApplyQuestWristPalmTrim(AppliedQuestHandRotCS, ForearmAxisComp, bIsLeft);
	AppliedQuestHandRotCS = ApplyWristLimitClampAndTrace(CSPose, bIsLeft, AppliedQuestHandRotCS, ForearmAxisComp, TEXT("quest"));
	ApplyRotationCS(CSPose, HandBone, AppliedQuestHandRotCS);
	QuestWristSideState.LastHandRotationApplyTimeSeconds = FPlatformTime::Seconds();
	const FQuat AppliedHandRotCS = HandBone.IsValidToEvaluate()
		? CSPose.GetComponentSpaceTransform(HandBone.CachedCompactPoseIndex).GetRotation().GetNormalized()
		: AppliedQuestHandRotCS;
	if (bDriveUpperArmTwistHelpersFromQuestRoll)
	{
		const FBoneReference& UpperArmBone = bIsLeft ? UpperArmL : UpperArmR;
		const FBoneReference& LowerArmBone = bIsLeft ? LowerArmL : LowerArmR;
		const FBoneReference& UpperArmTwist01Bone = bIsLeft ? UpperArmTwist01L : UpperArmTwist01R;
		const FBoneReference& UpperArmTwist02Bone = bIsLeft ? UpperArmTwist02L : UpperArmTwist02R;
		const FQuat& RefUpperComp = bIsLeft ? RefUpperArmCompL : RefUpperArmCompR;
		const FQuat& RefUpperTwist01Comp = bIsLeft ? RefUpperArmTwist01CompL : RefUpperArmTwist01CompR;
		const FQuat& RefUpperTwist02Comp = bIsLeft ? RefUpperArmTwist02CompL : RefUpperArmTwist02CompR;
		if (UpperArmBone.IsValidToEvaluate() && LowerArmBone.IsValidToEvaluate())
		{
			const FTransform UpperArmTransformCS = CSPose.GetComponentSpaceTransform(UpperArmBone.CachedCompactPoseIndex);
			const FTransform LowerArmTransformCS = CSPose.GetComponentSpaceTransform(LowerArmBone.CachedCompactPoseIndex);
			const FQuat UpperArmRotCS = UpperArmTransformCS.GetRotation().GetNormalized();
			FQuat LowerArmRotCS = LowerArmTransformCS.GetRotation().GetNormalized();
			if ((UpperArmRotCS | LowerArmRotCS) < 0.0f)
			{
				LowerArmRotCS.X *= -1.0f;
				LowerArmRotCS.Y *= -1.0f;
				LowerArmRotCS.Z *= -1.0f;
				LowerArmRotCS.W *= -1.0f;
			}
			const FVector UpperArmPosComp = UpperArmTransformCS.GetTranslation();
			const FVector LowerArmPosComp = LowerArmTransformCS.GetTranslation();
			const FVector UpperTwistAxisComp = (LowerArmPosComp - UpperArmPosComp).GetSafeNormal();
			const float UpperArmLenComp = FVector::DotProduct(LowerArmPosComp - UpperArmPosComp, UpperTwistAxisComp);
			float BaseUpperArmTwistDeg = 0.0f;
			if (!UpperTwistAxisComp.IsNearlyZero())
			{
				const FQuat UpperToLowerDelta = (LowerArmRotCS * UpperArmRotCS.Inverse()).GetNormalized();
				FQuat IgnoredSwing = FQuat::Identity;
				DecomposeSwingTwistDegAroundAxis(UpperToLowerDelta, UpperTwistAxisComp, IgnoredSwing, BaseUpperArmTwistDeg);
			}
			auto GetUpperTwistAlphaFromQuestRoll = [&](const FBoneReference& TwistBone) -> float
			{
				if (!TwistBone.IsValidToEvaluate() ||
					UpperArmLenComp <= KINDA_SMALL_NUMBER ||
					UpperTwistAxisComp.IsNearlyZero())
				{
					return 0.0f;
				}

				const FVector TwistPosComp = CSPose.GetComponentSpaceTransform(TwistBone.CachedCompactPoseIndex).GetTranslation();
				return FMath::Clamp(FVector::DotProduct(TwistPosComp - UpperArmPosComp, UpperTwistAxisComp) / UpperArmLenComp, 0.0f, 1.0f);
			};
			auto ApplyUpperTwistFromQuestRoll = [&](const FBoneReference& TwistBone, const FQuat& RefTwistComp, const float AppliedTwistDeg) -> bool
			{
				if (!TwistBone.IsValidToEvaluate() ||
					UpperTwistAxisComp.IsNearlyZero() ||
					FMath::Abs(AppliedTwistDeg) <= KINDA_SMALL_NUMBER)
				{
					return false;
				}

				const FQuat RefUpperToTwistLocal = (RefUpperComp.Inverse() * RefTwistComp).GetNormalized();
				const FQuat TwistBaseCS = (UpperArmRotCS * RefUpperToTwistLocal).GetNormalized();
				const FQuat TwistCorrectionCS(UpperTwistAxisComp, FMath::DegreesToRadians(AppliedTwistDeg));
				ApplyRotationCS(CSPose, TwistBone, (TwistCorrectionCS * TwistBaseCS).GetNormalized());
				return true;
			};

			Trace.UpperArmTwistAlpha01 = GetUpperTwistAlphaFromQuestRoll(UpperArmTwist01Bone);
			Trace.UpperArmTwistAlpha02 = GetUpperTwistAlphaFromQuestRoll(UpperArmTwist02Bone);
			Trace.UpperArmTwistApplied01Deg = (BaseUpperArmTwistDeg + UpperArmHelperQuestRollDeg) * Trace.UpperArmTwistAlpha01;
			Trace.UpperArmTwistApplied02Deg = (BaseUpperArmTwistDeg + UpperArmHelperQuestRollDeg) * Trace.UpperArmTwistAlpha02;
			const bool bAppliedUpperHelpers =
				ApplyUpperTwistFromQuestRoll(UpperArmTwist02Bone, RefUpperTwist02Comp, Trace.UpperArmTwistApplied02Deg) |
				ApplyUpperTwistFromQuestRoll(UpperArmTwist01Bone, RefUpperTwist01Comp, Trace.UpperArmTwistApplied01Deg);
			Trace.bAppliedForearmTwist = (Trace.bAppliedForearmTwist != 0 || bAppliedUpperHelpers) ? 1 : 0;
		}
	}
	if (CVarQuestWristDriveTwistCorrection.GetValueOnAnyThread() != 0 && ParentLowerArmBone.IsValidToEvaluate())
	{
		const FBoneReference& UpperArmBone = bIsLeft ? UpperArmL : UpperArmR;
		const FBoneReference& UpperArmTwist01Bone = bIsLeft ? UpperArmTwist01L : UpperArmTwist01R;
		const FBoneReference& UpperArmTwist02Bone = bIsLeft ? UpperArmTwist02L : UpperArmTwist02R;
		const FBoneReference& LowerArmTwist01Bone = bIsLeft ? LowerArmTwist01L : LowerArmTwist01R;
		const FBoneReference& LowerArmTwist02Bone = bIsLeft ? LowerArmTwist02L : LowerArmTwist02R;
		const bool bHasRefArm = bIsLeft ? bHasRefArmL : bHasRefArmR;
		const FQuat& RefUpperComp = bIsLeft ? RefUpperArmCompL : RefUpperArmCompR;
		const FQuat& RefLowerComp = bIsLeft ? RefLowerArmCompL : RefLowerArmCompR;
		const FQuat& RefUpperTwist01Comp = bIsLeft ? RefUpperArmTwist01CompL : RefUpperArmTwist01CompR;
		const FQuat& RefUpperTwist02Comp = bIsLeft ? RefUpperArmTwist02CompL : RefUpperArmTwist02CompR;
		const FQuat& RefTwist01Comp = bIsLeft ? RefLowerArmTwist01CompL : RefLowerArmTwist01CompR;
		const FQuat& RefTwist02Comp = bIsLeft ? RefLowerArmTwist02CompL : RefLowerArmTwist02CompR;
		const FTransform UpperArmTransformCS = UpperArmBone.IsValidToEvaluate()
			? CSPose.GetComponentSpaceTransform(UpperArmBone.CachedCompactPoseIndex)
			: FTransform::Identity;
		const FTransform LowerArmTransformCS = CSPose.GetComponentSpaceTransform(ParentLowerArmBone.CachedCompactPoseIndex);
		const FTransform HandTransformCS = CSPose.GetComponentSpaceTransform(HandBone.CachedCompactPoseIndex);
		const FQuat UpperArmRotCS = UpperArmTransformCS.GetRotation().GetNormalized();
		const FQuat LowerArmRotCS = LowerArmTransformCS.GetRotation().GetNormalized();
		const FVector UpperArmPosComp = UpperArmTransformCS.GetTranslation();
		const FVector LowerArmPosComp = LowerArmTransformCS.GetTranslation();
		const FVector HandPosComp = HandTransformCS.GetTranslation();
		const FVector TwistAxisComp = (HandPosComp - LowerArmPosComp).GetSafeNormal();
		const FVector UpperTwistAxisComp = UpperArmBone.IsValidToEvaluate()
			? (LowerArmPosComp - UpperArmPosComp).GetSafeNormal()
			: FVector::ZeroVector;
		const FVector TwistAxisLocal = LowerArmRotCS.Inverse().RotateVector(TwistAxisComp).GetSafeNormal();
		const FQuat RefHandLocal = (RefLowerComp.Inverse() * RefHandComp).GetNormalized();
		const FQuat AppliedHandLocal = (LowerArmRotCS.Inverse() * AppliedHandRotCS).GetNormalized();
		const FQuat HandDeltaLocal = (AppliedHandLocal * RefHandLocal.Inverse()).GetNormalized();
		FQuat IgnoredSwingLocal = FQuat::Identity;
		float SourceHandTwistDeg = 0.0f;
		if (!TwistAxisComp.IsNearlyZero() &&
			!TwistAxisLocal.IsNearlyZero() &&
			bHasRefArm &&
			DecomposeSwingTwistDegAroundAxis(HandDeltaLocal, TwistAxisLocal, IgnoredSwingLocal, SourceHandTwistDeg))
		{
			const float TwistCorrectionBlend = FMath::Clamp(CVarQuestWristTwistCorrectionBlend.GetValueOnAnyThread(), 0.0f, 1.0f);
			const float TwistCorrectionMaxDeg = FMath::Clamp(CVarQuestWristTwistCorrectionMaxDegrees.GetValueOnAnyThread(), 0.0f, 180.0f);
			const float TwistCorrectionStartDeg = FMath::Clamp(CVarQuestWristTwistCorrectionStartDegrees.GetValueOnAnyThread(), 0.0f, 180.0f);
			const float TwistCorrectionFullDeg = FMath::Clamp(
				CVarQuestWristTwistCorrectionFullDegrees.GetValueOnAnyThread(),
				TwistCorrectionStartDeg + 1.0f,
				180.0f);
			const float TwistCorrectionUpperArmShare = FMath::Clamp(CVarQuestWristTwistCorrectionUpperArmShare.GetValueOnAnyThread(), 0.0f, 0.40f);
			const float SourceHandTwistAbsDeg = FMath::Abs(SourceHandTwistDeg);
			const float TwistCorrectionRamp =
				SourceHandTwistAbsDeg > TwistCorrectionStartDeg
					? FMath::Clamp(
						(SourceHandTwistAbsDeg - TwistCorrectionStartDeg) / FMath::Max(TwistCorrectionFullDeg - TwistCorrectionStartDeg, UE_SMALL_NUMBER),
						0.0f,
						1.0f)
					: 0.0f;
			const float CorrectionSourceHandTwistDeg = FMath::Clamp(SourceHandTwistDeg, -TwistCorrectionMaxDeg, TwistCorrectionMaxDeg);
			const float AppliedCorrectionTwistDeg = CorrectionSourceHandTwistDeg * TwistCorrectionBlend * TwistCorrectionRamp;
			const float ForearmLenComp = FVector::DotProduct(HandPosComp - LowerArmPosComp, TwistAxisComp);
			const float UpperArmLenComp = UpperArmBone.IsValidToEvaluate()
				? FVector::DotProduct(LowerArmPosComp - UpperArmPosComp, UpperTwistAxisComp)
				: 0.0f;
			auto GetTwistAlpha = [&](const FBoneReference& TwistBone) -> float
			{
				if (!TwistBone.IsValidToEvaluate() || ForearmLenComp <= KINDA_SMALL_NUMBER)
				{
					return 0.0f;
				}

				const FVector TwistPosComp = CSPose.GetComponentSpaceTransform(TwistBone.CachedCompactPoseIndex).GetTranslation();
				return FMath::Clamp(FVector::DotProduct(TwistPosComp - LowerArmPosComp, TwistAxisComp) / ForearmLenComp, 0.0f, 1.0f);
			};
			auto GetUpperTwistAlpha = [&](const FBoneReference& TwistBone) -> float
			{
				if (!TwistBone.IsValidToEvaluate() ||
					UpperArmLenComp <= KINDA_SMALL_NUMBER ||
					UpperTwistAxisComp.IsNearlyZero())
				{
					return 0.0f;
				}

				const FVector TwistPosComp = CSPose.GetComponentSpaceTransform(TwistBone.CachedCompactPoseIndex).GetTranslation();
				return FMath::Clamp(FVector::DotProduct(TwistPosComp - UpperArmPosComp, UpperTwistAxisComp) / UpperArmLenComp, 0.0f, 1.0f);
			};

			Trace.SourceHandTwistDeg = SourceHandTwistDeg;
			Trace.TargetForearmTwistDeg = AppliedCorrectionTwistDeg;
			Trace.bForearmLimitClamped =
				(FMath::Abs(CorrectionSourceHandTwistDeg - SourceHandTwistDeg) > 0.1f ||
					TwistCorrectionRamp < 1.0f) ? 1 : 0;
			Trace.ForearmTwistHelperScale = TwistCorrectionBlend * TwistCorrectionRamp;
			Trace.ForearmTwistAlpha01 = GetTwistAlpha(LowerArmTwist01Bone);
			Trace.ForearmTwistAlpha02 = GetTwistAlpha(LowerArmTwist02Bone);
			Trace.ForearmTwistApplied01Deg = AppliedCorrectionTwistDeg * Trace.ForearmTwistAlpha01;
			Trace.ForearmTwistApplied02Deg = AppliedCorrectionTwistDeg * Trace.ForearmTwistAlpha02;
			Trace.UpperArmTwistHelperScale = Trace.ForearmTwistHelperScale * TwistCorrectionUpperArmShare;
			Trace.UpperArmTwistAlpha01 = GetUpperTwistAlpha(UpperArmTwist01Bone);
			Trace.UpperArmTwistAlpha02 = GetUpperTwistAlpha(UpperArmTwist02Bone);
			Trace.UpperArmTwistApplied01Deg = AppliedCorrectionTwistDeg * TwistCorrectionUpperArmShare * Trace.UpperArmTwistAlpha01;
			Trace.UpperArmTwistApplied02Deg = AppliedCorrectionTwistDeg * TwistCorrectionUpperArmShare * Trace.UpperArmTwistAlpha02;

			auto ApplyTwistCorrection = [&](const FBoneReference& TwistBone, const FQuat& RefTwistComp, const float AppliedTwistDeg) -> bool
			{
				if (!TwistBone.IsValidToEvaluate() ||
					FMath::Abs(AppliedTwistDeg) <= KINDA_SMALL_NUMBER)
				{
					return false;
				}

				const FQuat RefLowerToTwistLocal = (RefLowerComp.Inverse() * RefTwistComp).GetNormalized();
				const FQuat TwistBaseCS = (LowerArmRotCS * RefLowerToTwistLocal).GetNormalized();
				const FQuat TwistCorrectionCS(TwistAxisComp, FMath::DegreesToRadians(AppliedTwistDeg));
				ApplyRotationCS(CSPose, TwistBone, (TwistCorrectionCS * TwistBaseCS).GetNormalized());
				return true;
			};
			auto ApplyUpperTwistCorrection = [&](const FBoneReference& TwistBone, const FQuat& RefTwistComp, const float AppliedTwistDeg) -> bool
			{
				if (!UpperArmBone.IsValidToEvaluate() ||
					UpperTwistAxisComp.IsNearlyZero() ||
					!TwistBone.IsValidToEvaluate() ||
					FMath::Abs(AppliedTwistDeg) <= KINDA_SMALL_NUMBER)
				{
					return false;
				}

				const FQuat RefUpperToTwistLocal = (RefUpperComp.Inverse() * RefTwistComp).GetNormalized();
				const FQuat TwistBaseCS = (UpperArmRotCS * RefUpperToTwistLocal).GetNormalized();
				const FQuat TwistCorrectionCS(UpperTwistAxisComp, FMath::DegreesToRadians(AppliedTwistDeg));
				ApplyRotationCS(CSPose, TwistBone, (TwistCorrectionCS * TwistBaseCS).GetNormalized());
				return true;
			};

			const bool bHasCorrectionTargets =
				LowerArmTwist01Bone.IsValidToEvaluate() ||
				LowerArmTwist02Bone.IsValidToEvaluate() ||
				UpperArmTwist01Bone.IsValidToEvaluate() ||
				UpperArmTwist02Bone.IsValidToEvaluate();
			const bool bAppliedCorrection =
				ApplyTwistCorrection(LowerArmTwist02Bone, RefTwist02Comp, Trace.ForearmTwistApplied02Deg) |
				ApplyTwistCorrection(LowerArmTwist01Bone, RefTwist01Comp, Trace.ForearmTwistApplied01Deg) |
				ApplyUpperTwistCorrection(UpperArmTwist02Bone, RefUpperTwist02Comp, Trace.UpperArmTwistApplied02Deg) |
				ApplyUpperTwistCorrection(UpperArmTwist01Bone, RefUpperTwist01Comp, Trace.UpperArmTwistApplied01Deg);
			Trace.bAppliedTwistCorrection = bHasCorrectionTargets ? 1 : 0;
			Trace.bAppliedForearmTwist = (Trace.bAppliedForearmTwist != 0 || bAppliedCorrection) ? 1 : 0;

		}
	}
	Trace.MannyAppliedForwardComp = AppliedHandRotCS.RotateVector(FVector::ForwardVector).GetSafeNormal();
	Trace.MannyAppliedUpComp = AppliedHandRotCS.RotateVector(FVector::UpVector).GetSafeNormal();
	const FQuat MannyAppliedBasisComp = (AppliedHandRotCS * RefHandComp.Inverse() * RefHandBasisComp).GetNormalized();
	Trace.MannyAppliedBasisForwardComp = QuestBasisAxis(MannyAppliedBasisComp, 0);
	Trace.MannyAppliedBasisUpComp = QuestBasisAxis(MannyAppliedBasisComp, 2);
	Trace.QuestExpectedToMannyDeg = FMath::RadiansToDegrees(QuestHandTargetCS.AngularDistance(AppliedHandRotCS));
	Trace.RollTargetToMannyDeg = FMath::RadiansToDegrees(TargetHandRotCS.AngularDistance(AppliedHandRotCS));
	Trace.QuestExpectedForwardErrDeg = QuestAngleBetweenSegmentsDeg(Trace.QuestExpectedForwardComp, Trace.MannyAppliedForwardComp);
	Trace.QuestExpectedUpErrDeg = QuestAngleBetweenSegmentsDeg(Trace.QuestExpectedUpComp, Trace.MannyAppliedUpComp);
	Trace.RollTargetForwardErrDeg = QuestAngleBetweenSegmentsDeg(Trace.RollTargetForwardComp, Trace.MannyAppliedForwardComp);
	Trace.RollTargetUpErrDeg = QuestAngleBetweenSegmentsDeg(Trace.RollTargetUpComp, Trace.MannyAppliedUpComp);
	Trace.QuestBasisToMannyBasisForwardErrDeg = QuestAngleBetweenSegmentsDeg(Trace.QuestBasisForwardComp, Trace.MannyAppliedBasisForwardComp);
	Trace.QuestBasisToMannyBasisUpErrDeg = QuestAngleBetweenSegmentsDeg(Trace.QuestBasisUpComp, Trace.MannyAppliedBasisUpComp);
	Trace.bAppliedHandRotation = 1;

	if (bDriveForearmTwistHelpersFromHand)
	{
		const FBoneReference& LowerArmBone = bIsLeft ? LowerArmL : LowerArmR;
		const FBoneReference& LowerArmTwist01Bone = bIsLeft ? LowerArmTwist01L : LowerArmTwist01R;
		const FBoneReference& LowerArmTwist02Bone = bIsLeft ? LowerArmTwist02L : LowerArmTwist02R;
		const FQuat& RefLowerComp = bIsLeft ? RefLowerArmCompL : RefLowerArmCompR;
		const FQuat& RefTwist01Comp = bIsLeft ? RefLowerArmTwist01CompL : RefLowerArmTwist01CompR;
		const FQuat& RefTwist02Comp = bIsLeft ? RefLowerArmTwist02CompL : RefLowerArmTwist02CompR;
		if (LowerArmBone.IsValidToEvaluate())
		{
			const FQuat LowerArmRotCS = CSPose.GetComponentSpaceTransform(LowerArmBone.CachedCompactPoseIndex).GetRotation().GetNormalized();
			auto ApplyForearmHelperRotation = [&](const FBoneReference& Bone, const FQuat& RefTwistComp, const float AppliedTwistDeg) -> bool
			{
				if (!Bone.IsValidToEvaluate() || FMath::Abs(AppliedTwistDeg) <= KINDA_SMALL_NUMBER)
				{
					return false;
				}

				const FQuat RefLowerToTwistLocal = (RefLowerComp.Inverse() * RefTwistComp).GetNormalized();
				const FQuat HelperBaseCS = (LowerArmRotCS * RefLowerToTwistLocal).GetNormalized();
				const FQuat HelperRotCS = (FQuat(ForearmAxisN, FMath::DegreesToRadians(AppliedTwistDeg)) * HelperBaseCS).GetNormalized();
				ApplyRotationCS(CSPose, Bone, HelperRotCS);
				return true;
			};

			const bool bAppliedHelpers =
				ApplyForearmHelperRotation(LowerArmTwist02Bone, RefTwist02Comp, Trace.ForearmTwistApplied02Deg) |
				ApplyForearmHelperRotation(LowerArmTwist01Bone, RefTwist01Comp, Trace.ForearmTwistApplied01Deg);
			Trace.bAppliedForearmTwist = (Trace.bAppliedForearmTwist != 0 || bAppliedHelpers) ? 1 : 0;
		}
	}
	return true;
}
