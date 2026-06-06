#include "MediaPipeQuestWristDiagnosticFormatter.h"

#include "HeadMountedDisplayTypes.h"
#include "MediaPipePoseDiagnostics.h"
#include "MediaPipeQuestHandTypes.h"
#include "MediaPipeQuestWristTraceTypes.h"

namespace
{
	bool IsQuestWristCalibrationMatched(const uint8 CalibrationState)
	{
		return CalibrationState == QuestWristCalibrationState_MeasuringCalibration ||
			CalibrationState == QuestWristCalibrationState_Accepted ||
			CalibrationState == QuestWristCalibrationState_Tracking;
	}

	bool IsFiniteQuestVector(const FVector& Vector)
	{
		return FMath::IsFinite(Vector.X) && FMath::IsFinite(Vector.Y) && FMath::IsFinite(Vector.Z);
	}

	bool IsUsableQuestWristPosition(const FVector& WristWorld)
	{
		return IsFiniteQuestVector(WristWorld) && !WristWorld.IsNearlyZero();
	}

	float AngleBetweenSegmentsDeg(const FVector& A, const FVector& B)
	{
		const FVector ANorm = A.GetSafeNormal();
		const FVector BNorm = B.GetSafeNormal();
		if (ANorm.IsNearlyZero() || BNorm.IsNearlyZero())
		{
			return 0.0f;
		}

		const float Dot = FMath::Clamp(FVector::DotProduct(ANorm, BNorm), -1.0f, 1.0f);
		return FMath::RadiansToDegrees(FMath::Acos(Dot));
	}

	void AppendReason(FString& Reasons, const TCHAR* Reason)
	{
		if (!Reasons.IsEmpty())
		{
			Reasons += TEXT("|");
		}
		Reasons += Reason;
	}
}

FMediaPipeQuestWristCalibrationSideFormatInput::FMediaPipeQuestWristCalibrationSideFormatInput(
	const bool bInTracked,
	const uint8 InCalibrationState,
	const uint8 InCalibrationRejectReason,
	const int32 InStableFrameCount,
	const float InBasisErrorDeg,
	const float InNeutralTwistDeg)
	: bTracked(bInTracked)
	, CalibrationState(InCalibrationState)
	, CalibrationRejectReason(InCalibrationRejectReason)
	, StableFrameCount(InStableFrameCount)
	, BasisErrorDeg(InBasisErrorDeg)
	, NeutralTwistDeg(InNeutralTwistDeg)
{
}

FMediaPipeQuestHandDivergenceFormatInput FMediaPipeQuestHandDivergenceFormatInput::FromTrace(
	const FName TargetActorName,
	const bool bIsLeft,
	const FQuestHandRotationTrace& Trace)
{
	FMediaPipeQuestHandDivergenceFormatInput Input;
	Input.TargetActorName = TargetActorName;
	Input.bIsLeft = bIsLeft;
	Input.QuestExpectedForwardComp = Trace.QuestExpectedForwardComp;
	Input.QuestExpectedUpComp = Trace.QuestExpectedUpComp;
	Input.RollTargetForwardComp = Trace.RollTargetForwardComp;
	Input.RollTargetUpComp = Trace.RollTargetUpComp;
	Input.MannyAppliedForwardComp = Trace.MannyAppliedForwardComp;
	Input.MannyAppliedUpComp = Trace.MannyAppliedUpComp;
	Input.MediaPipeHandForwardComp = Trace.MediaPipeHandForwardComp;
	Input.MediaPipeHandUpComp = Trace.MediaPipeHandUpComp;
	Input.QuestBasisForwardComp = Trace.QuestBasisForwardComp;
	Input.QuestBasisUpComp = Trace.QuestBasisUpComp;
	Input.RollTargetBasisForwardComp = Trace.RollTargetBasisForwardComp;
	Input.RollTargetBasisUpComp = Trace.RollTargetBasisUpComp;
	Input.MannyAppliedBasisForwardComp = Trace.MannyAppliedBasisForwardComp;
	Input.MannyAppliedBasisUpComp = Trace.MannyAppliedBasisUpComp;
	Input.MediaPipeBasisForwardComp = Trace.MediaPipeBasisForwardComp;
	Input.MediaPipeBasisUpComp = Trace.MediaPipeBasisUpComp;
	Input.QuestExpectedToMannyDeg = Trace.QuestExpectedToMannyDeg;
	Input.QuestExpectedToRollTargetDeg = Trace.QuestExpectedToRollTargetDeg;
	Input.RollTargetToMannyDeg = Trace.RollTargetToMannyDeg;
	Input.QuestExpectedForwardErrDeg = Trace.QuestExpectedForwardErrDeg;
	Input.QuestExpectedUpErrDeg = Trace.QuestExpectedUpErrDeg;
	Input.RollTargetForwardErrDeg = Trace.RollTargetForwardErrDeg;
	Input.RollTargetUpErrDeg = Trace.RollTargetUpErrDeg;
	Input.QuestBasisToMannyBasisForwardErrDeg = Trace.QuestBasisToMannyBasisForwardErrDeg;
	Input.QuestBasisToMannyBasisUpErrDeg = Trace.QuestBasisToMannyBasisUpErrDeg;
	Input.QuestBasisToRollBasisForwardErrDeg = Trace.QuestBasisToRollBasisForwardErrDeg;
	Input.QuestBasisToRollBasisUpErrDeg = Trace.QuestBasisToRollBasisUpErrDeg;
	Input.RawTwistDeg = Trace.RawTwistDeg;
	Input.SemanticRollAxisIndex = Trace.SemanticRollAxisIndex;
	Input.SemanticRollAxisScore = Trace.SemanticRollAxisScore;
	return Input;
}

FMediaPipeQuestWristRollCompactFormatInput FMediaPipeQuestWristRollCompactFormatInput::FromTrace(
	const FName TargetActorName,
	const bool bIsLeft,
	const bool bQuestHandRotationApplied,
	const bool bArmIKBranchEntered,
	const bool bForceArmIK,
	const float ForearmTwistVelocityDegPerSec,
	const float QuestHandRotationDeltaDeg,
	const FQuestHandRotationTrace& Trace)
{
	FMediaPipeQuestWristRollCompactFormatInput Input;
	Input.TargetActorName = TargetActorName;
	Input.bIsLeft = bIsLeft;
	Input.bQuestHandRotationApplied = bQuestHandRotationApplied;
	Input.bArmIKBranchEntered = bArmIKBranchEntered;
	Input.bForceArmIK = bForceArmIK;
	Input.bQuestTracked = Trace.bQuestTracked != 0;
	Input.bQuestHandBasisMapped = Trace.bQuestHandBasisMapped != 0;
	Input.bUsedSemanticRoll = Trace.bUsedSemanticRoll != 0;
	Input.bUsedForearmLocalSemanticRoll = Trace.bUsedForearmLocalSemanticRoll != 0;
	Input.bUsedSemanticBasisDelta = Trace.bUsedSemanticBasisDelta != 0;
	Input.bUsedPalmRollFallback = Trace.bUsedPalmRollFallback != 0;
	Input.bHeldPalmRoll = Trace.bHeldPalmRoll != 0;
	Input.bAppliedHandLocalToLowerArm = Trace.bAppliedHandLocalToLowerArm != 0;
	Input.bAppliedTwistCorrection = Trace.bAppliedTwistCorrection != 0;
	Input.bLowerArmMainDriven = Trace.bLowerArmMainDriven != 0;
	Input.bHadCalibration = Trace.bHadCalibration != 0;
	Input.bSetCalibration = Trace.bSetCalibration != 0;
	Input.bCalibrationRejected = Trace.bCalibrationRejected != 0;
	Input.bTwistLimitClamped = Trace.bTwistLimitClamped != 0;
	Input.bForearmLimitClamped = Trace.bForearmLimitClamped != 0;
	Input.bForearmRateClamped = Trace.bForearmRateClamped != 0;
	Input.bUpperArmRateClamped = Trace.bUpperArmRateClamped != 0;
	Input.bCalibratedHandDeltaValid = Trace.bCalibratedHandDeltaValid != 0;
	Input.CalibrationState = Trace.CalibrationState;
	Input.CalibrationRejectReason = Trace.CalibrationRejectReason;
	Input.SemanticRollAxisIndex = Trace.SemanticRollAxisIndex;
	Input.AnatomicalRollAxisIndex = Trace.AnatomicalRollAxisIndex;
	Input.CalibrationStableFrameCount = Trace.CalibrationStableFrameCount;
	Input.CalibrationBasisErrorDeg = Trace.CalibrationBasisErrorDeg;
	Input.CalibrationNeutralTwistDeg = Trace.CalibrationNeutralTwistDeg;
	Input.SemanticRollAxisScore = Trace.SemanticRollAxisScore;
	Input.RawTwistDeg = Trace.RawTwistDeg;
	Input.LimitedTwistDeg = Trace.LimitedTwistDeg;
	Input.SourceHandTwistDeg = Trace.SourceHandTwistDeg;
	Input.RawSwingDeg = Trace.RawSwingDeg;
	Input.AppliedSwingDeg = Trace.AppliedSwingDeg;
	Input.TargetForearmTwistDeg = Trace.TargetForearmTwistDeg;
	Input.SmoothedForearmTwistDeg = Trace.SmoothedForearmTwistDeg;
	Input.ForearmTwistStepDeg = Trace.ForearmTwistStepDeg;
	Input.ForearmTwistMaxStepDeg = Trace.ForearmTwistMaxStepDeg;
	Input.ForearmTwistVelocityDegPerSec = ForearmTwistVelocityDegPerSec;
	Input.ForearmTwistHelperScale = Trace.ForearmTwistHelperScale;
	Input.ForearmTwistApplied01Deg = Trace.ForearmTwistApplied01Deg;
	Input.ForearmTwistApplied02Deg = Trace.ForearmTwistApplied02Deg;
	Input.TargetUpperArmTwistDeg = Trace.TargetUpperArmTwistDeg;
	Input.SmoothedUpperArmTwistDeg = Trace.SmoothedUpperArmTwistDeg;
	Input.UpperArmTwistStepDeg = Trace.UpperArmTwistStepDeg;
	Input.UpperArmTwistMaxStepDeg = Trace.UpperArmTwistMaxStepDeg;
	Input.UpperArmTwistHelperScale = Trace.UpperArmTwistHelperScale;
	Input.UpperArmTwistApplied01Deg = Trace.UpperArmTwistApplied01Deg;
	Input.UpperArmTwistApplied02Deg = Trace.UpperArmTwistApplied02Deg;
	Input.CalibratedHandDeltaDeg = Trace.CalibratedHandDeltaDeg;
	Input.QuestHandRotationDeltaDeg = QuestHandRotationDeltaDeg;
	Input.QuestExpectedToMannyDeg = Trace.QuestExpectedToMannyDeg;
	Input.QuestExpectedToRollTargetDeg = Trace.QuestExpectedToRollTargetDeg;
	Input.RollTargetToMannyDeg = Trace.RollTargetToMannyDeg;
	Input.QuestExpectedForwardErrDeg = Trace.QuestExpectedForwardErrDeg;
	Input.QuestExpectedUpErrDeg = Trace.QuestExpectedUpErrDeg;
	Input.RollTargetForwardErrDeg = Trace.RollTargetForwardErrDeg;
	Input.RollTargetUpErrDeg = Trace.RollTargetUpErrDeg;
	Input.QuestBasisToMannyBasisForwardErrDeg = Trace.QuestBasisToMannyBasisForwardErrDeg;
	Input.QuestBasisToMannyBasisUpErrDeg = Trace.QuestBasisToMannyBasisUpErrDeg;
	Input.QuestBasisToRollBasisForwardErrDeg = Trace.QuestBasisToRollBasisForwardErrDeg;
	Input.QuestBasisToRollBasisUpErrDeg = Trace.QuestBasisToRollBasisUpErrDeg;
	return Input;
}

FMediaPipeQuestWristSideHudFormatInput FMediaPipeQuestWristSideHudFormatInput::FromTrace(
	const bool bIsLeft,
	const bool bQuestHandRotationApplied,
	const bool bArmIKBranchEntered,
	const bool bForceArmIK,
	const FQuestHandRotationTrace& Trace)
{
	FMediaPipeQuestWristSideHudFormatInput Input;
	Input.bIsLeft = bIsLeft;
	Input.bQuestHandRotationApplied = bQuestHandRotationApplied;
	Input.bArmIKBranchEntered = bArmIKBranchEntered;
	Input.bForceArmIK = bForceArmIK;
	Input.bQuestAvailable = Trace.bQuestAvailable != 0;
	Input.bQuestTracked = Trace.bQuestTracked != 0;
	Input.bQuestHandBasisMapped = Trace.bQuestHandBasisMapped != 0;
	Input.bUsedSemanticRoll = Trace.bUsedSemanticRoll != 0;
	Input.bUsedForearmLocalSemanticRoll = Trace.bUsedForearmLocalSemanticRoll != 0;
	Input.bUsedAnatomicalRollAxis = Trace.bUsedAnatomicalRollAxis != 0;
	Input.CalibrationState = Trace.CalibrationState;
	Input.CalibrationRejectReason = Trace.CalibrationRejectReason;
	Input.SemanticRollAxisIndex = Trace.SemanticRollAxisIndex;
	Input.AnatomicalRollAxisIndex = Trace.AnatomicalRollAxisIndex;
	Input.CalibrationStableFrameCount = Trace.CalibrationStableFrameCount;
	Input.CalibrationBasisErrorDeg = Trace.CalibrationBasisErrorDeg;
	Input.CalibrationNeutralTwistDeg = Trace.CalibrationNeutralTwistDeg;
	Input.SemanticRollAxisScore = Trace.SemanticRollAxisScore;
	Input.RawTwistDeg = Trace.RawTwistDeg;
	Input.AppliedSwingDeg = Trace.AppliedSwingDeg;
	return Input;
}

FMediaPipeMetaHumanArmSanityFormatInput FMediaPipeMetaHumanArmSanityFormatInput::FromTraces(
	const FName TargetActorName,
	const bool bIsLeft,
	const bool bBroken,
	const FString& Reasons,
	const bool bHasPosedArm,
	const int32 QuestArmMode,
	const bool bQuestHandRotationApplied,
	const bool bArmIKBranchEntered,
	const float WristTargetErrorCm,
	const float MappedWristErrorCm,
	const float MaxWristErrorCm,
	const float UpperLenCm,
	const float RefUpperLenCm,
	const float UpperLenErrCm,
	const float LowerLenCm,
	const float RefLowerLenCm,
	const float LowerLenErrCm,
	const float ElbowBendDeg,
	const float MinElbowBendDeg,
	const float TargetReachCm,
	const float PosedReachCm,
	const float MaxHandRotErrorDeg,
	const float MaxBasisErrorDeg,
	const float MaxSwingDeg,
	const FQuestWristMappingTrace& WristTrace,
	const FQuestHandRotationTrace& HandTrace,
	const FVector& PosedShoulderWorld,
	const FVector& PosedElbowWorld,
	const FVector& PosedHandWorld,
	const FVector& SolveShoulderWorld,
	const FVector& SolveElbowWorld)
{
	FMediaPipeMetaHumanArmSanityFormatInput Input;
	Input.TargetActorName = TargetActorName;
	Input.bIsLeft = bIsLeft;
	Input.bBroken = bBroken;
	Input.Reasons = Reasons;
	Input.bHasPosedArm = bHasPosedArm;
	Input.QuestArmMode = QuestArmMode;
	Input.bTargetMapped = WristTrace.bMapped != 0;
	Input.bPositionApplied = WristTrace.bPositionApplied != 0;
	Input.bQuestTracked = WristTrace.bQuestTracked != 0;
	Input.bQuestHandRotationApplied = bQuestHandRotationApplied;
	Input.bHandLocal = HandTrace.bAppliedHandLocalToLowerArm != 0;
	Input.bReachClamped = WristTrace.bReachClamped != 0;
	Input.bConstrainedArmSolveApplied = WristTrace.bConstrainedArmSolveApplied != 0;
	Input.bArmIKBranchEntered = bArmIKBranchEntered;
	Input.WristTargetErrorCm = WristTargetErrorCm;
	Input.MappedWristErrorCm = MappedWristErrorCm;
	Input.MaxWristErrorCm = MaxWristErrorCm;
	Input.UpperLenCm = UpperLenCm;
	Input.RefUpperLenCm = RefUpperLenCm;
	Input.UpperLenErrCm = UpperLenErrCm;
	Input.LowerLenCm = LowerLenCm;
	Input.RefLowerLenCm = RefLowerLenCm;
	Input.LowerLenErrCm = LowerLenErrCm;
	Input.ElbowBendDeg = ElbowBendDeg;
	Input.MinElbowBendDeg = MinElbowBendDeg;
	Input.TargetReachCm = TargetReachCm;
	Input.PosedReachCm = PosedReachCm;
	Input.HandRotErrorDeg = HandTrace.QuestExpectedToMannyDeg;
	Input.MaxHandRotErrorDeg = MaxHandRotErrorDeg;
	Input.BasisForwardErrorDeg = HandTrace.QuestBasisToMannyBasisForwardErrDeg;
	Input.BasisUpErrorDeg = HandTrace.QuestBasisToMannyBasisUpErrDeg;
	Input.MaxBasisErrorDeg = MaxBasisErrorDeg;
	Input.SwingAppliedDeg = HandTrace.AppliedSwingDeg;
	Input.MaxSwingDeg = MaxSwingDeg;
	Input.TwistLimitedDeg = HandTrace.LimitedTwistDeg;
	Input.PosedShoulderWorld = PosedShoulderWorld;
	Input.PosedElbowWorld = PosedElbowWorld;
	Input.PosedHandWorld = PosedHandWorld;
	Input.FinalWristWorld = WristTrace.FinalWristWorld;
	Input.MappedWristWorld = WristTrace.MappedQuestWristWorld;
	Input.SolveShoulderWorld = SolveShoulderWorld;
	Input.SolveElbowWorld = SolveElbowWorld;
	return Input;
}

FString FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristSnapshotLog(
	const FName TargetActorName,
	const FQuestHandTrackingSnapshot& Snapshot,
	const bool bHasCachedQuestHmdPose,
	const FVector& CachedQuestHmdWorld)
{
	const FVector LeftWristWorld = Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::Wrist)];
	const FVector RightWristWorld = Snapshot.RightPositionsWorld[static_cast<int32>(EHandKeypoint::Wrist)];
	return FString::Printf(
		TEXT("mp.QuestWristSnapshot: actor=%s left(has=%d tracked=%d wrist=%s) right(has=%d tracked=%d wrist=%s) hmdPose=%d hmdWorld=%s"),
		*TargetActorName.ToString(),
		Snapshot.bHasLeft ? 1 : 0,
		Snapshot.bLeftTracked ? 1 : 0,
		*LeftWristWorld.ToCompactString(),
		Snapshot.bHasRight ? 1 : 0,
		Snapshot.bRightTracked ? 1 : 0,
		*RightWristWorld.ToCompactString(),
		bHasCachedQuestHmdPose ? 1 : 0,
		*CachedQuestHmdWorld.ToCompactString());
}

FString FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristReplaySnapshotLog(const FString& ReplayPath)
{
	return FString::Printf(TEXT("mp.QuestWristSnapshot: using replay '%s'."), *ReplayPath);
}

FString FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristSolveLog(
	const FMediaPipeQuestWristSolveLogFormatInput& Input)
{
	const FQuestWristMappingTrace DefaultWristTrace;
	const FQuestHandRotationTrace DefaultHandTrace;
	const FQuestWristMappingTrace& QuestWristTrace = Input.WristTrace ? *Input.WristTrace : DefaultWristTrace;
	const FQuestHandRotationTrace& QuestHandRotationTrace = Input.HandTrace ? *Input.HandTrace : DefaultHandTrace;
	return FString::Printf(
		TEXT("mp.QuestWristSolve: actor=%s side=%s questArmMode=%d traceOnly=%d positionApplied=%d requireTrackedApply=%d questTracked=%d untrackedData=%d rawRejected=%d requestedBlend=%.2f effectiveBlend=%.2f runtimeKey=%u applyCalibHad=%d applyCalibSet=%d applyCalibAge=%.1f traceCalibHad=%d traceCalibSet=%d traceCalibAge=%.1f rawToHmdCm=%.1f rawCalibDeltaCm=%.1f mappedOffsetCm=%.1f rawQuestWrist=%s hmdPose=%d mediaHead=%d mediaHeadWorld=%s calib=%s usedLiveHmd=%d hmdTransAnchor=%d hmdTransAlpha=%.2f hmdTransSpeedCmSec=%.1f hmdTransLagCm=%.1f relative=%d mapped=%d held=%d posFilter=%d posFilterAlpha=%.2f posFilterSpeedCmSec=%.1f posFilterTargetDeltaCm=%.1f posFilterFilteredDeltaCm=%.1f reachClamped=%d reachAssist=%d reachAssistBlend=%.2f reachAssistElbowMoveCm=%.1f reachAssistElbow=%s driftGuard=%d driftAlpha=%.2f driftOffsetCm=%.1f driftReachBlend=%.2f driftPoleBlend=%.2f questArmSolve=%d questArmSolveBlend=%.2f questArmWristAuthority=%.2f questArmMpElbowHint=%.2f questArmReachScale=%d questArmReachScaleValue=%.3f questArmReachScaleAlpha=%.2f questArmReachScaleObservedMaxCm=%.1f questArmReachScaleTargetCm=%.1f questArmLenCalibStage=%u questArmLenCalibStable=%.2f questArmLenForwardCm=%.1f questArmLenDownCm=%.1f questArmLenTargetCm=%.1f questArmDownFrame=%d questArmDownFrameScale=%.3f questArmDownFrameAlpha=%.2f questArmDownFrameObservedDropCm=%.1f questArmDownFrameTargetDropCm=%.1f questArmSourceElbowHint=%d questArmSourceElbow=%s questArmCloseReach=%.2f questArmStablePoleDown=%.2f questArmElbowMoveCm=%.1f questArmPoleContinuity=%d questArmNearFullPoleAlpha=%.2f questArmReachContinuity=%d questArmReachRawCm=%.1f questArmReachPrevCm=%.1f questArmReachMaxStepCm=%.1f questArmDownStraighten=%d questArmDownStraightenAlpha=%.2f questArmBodyFallback=%d questArmBodyFallbackReach=%.2f questArmBodyFallbackTargetReachCm=%.1f questArmBodyFallbackTargetReachFrac=%.3f questArmBodyFallbackDown=%d questArmBodyFallbackDownAlpha=%.2f questArmDropoutDown=%d questArmDropoutAlpha=%.2f questArmDropoutReachCm=%.1f questArmDropoutTargetReachCm=%.1f questArmDropoutDownDom=%.2f questArmDropoutLastTrackedAge=%.2f questArmDropoutMpHint=%d questArmWristStepCm=%.1f questArmCandidateElbowStepCm=%.1f questArmAllowedElbowStepCm=%.1f questArmElbow=%s mappedWrist=%s finalWristWorld=%s mediaPipeWrist=%s shoulderWorld=%s elbowWorld=%s armIKEntered=%d questForceArmIK=%d wouldArmIKIfApplied=%d useArmIKSetting=%d ikTargetComp=%s ikSolvedWristComp=%s questHandCall=1 handBone=%s handRotationBlend=%.2f handRotApplied=%d handLocal=%d handRotDeltaDeg=%.2f questHandAvailable=%d questHandTracked=%d wristJointRot=%d wristPalmBasis=%d wristProjectedTwist=%d wristProjectedJointAxes=%d wristProjectedAxis=%d wristAnatomicalRoll=%d wristAnatomicalAxis=%d wristAnatomicalPalmDot=%.2f wristAnatomicalForearmDot=%.2f questHandBasisMapped=%d wristSemanticRoll=%d wristSemanticLocal=%d wristSemanticBasis=%d wristPalmFallback=%d wristPalmHeld=%d wristSemanticAxis=%d wristSemanticScore=%.2f wristSemanticCalibDeg=%.1f wristSemanticOffsetDeg=%.1f wristAxisDiag=%d wristAxisMap=%d jointX_refXYZ=%s jointY_refXYZ=%s jointZ_refXYZ=%s palmX_refXYZ=%s palmY_refXYZ=%s palmZ_refXYZ=%s jointXYZ_forearm=%s palmXYZ_forearm=%s forearm_refXYZ=%s wristRotCalibHad=%d wristRotCalibSet=%d wristCalibErrDeg=%.1f wristCalibReject=%d calibrationState=%s calibrationRejectReason=\"%s\" stableFrameCount=%d calibErrDeg=%.1f neutralTwistDeg=%.1f handVelCmSec=%.1f handAngVelDegSec=%.1f bodyYawDeltaDeg=%.1f mannyYawDeltaDeg=%.1f wristTwistRawDeg=%.1f wristTwistLimitedDeg=%.1f sourceHandTwistDeg=%.1f wristForearmTwistDeg=%.1f forearmAlpha01=%.2f forearmAlpha02=%.2f forearmHelperMode=handLocalTwist forearmHelperScale=%.2f forearmTwist01Deg=%.1f forearmTwist02Deg=%.1f upperArmHelperScale=%.2f upperArmTargetDeg=%.1f upperArmAppliedDeg=%.1f upperArmStepDeg=%.1f upperArmMaxStepDeg=%.1f upperArmRateClamp=%d upperArmTwist01Deg=%.1f upperArmTwist02Deg=%.1f wristSwingRawDeg=%.1f wristSwingAppliedDeg=%.1f forearmTwistApplied=%d twistCorrection=%d lowerarmMainDriven=%d handBeforeLocCS=%s handAfterLocCS=%s"),
		*Input.TargetActorName.ToString(),
		Input.bIsLeft ? TEXT("L") : TEXT("R"),
		Input.QuestArmMode,
		QuestWristTrace.bTraceOnly ? 1 : 0,
		QuestWristTrace.bPositionApplied ? 1 : 0,
		Input.bRequireTrackedForApply ? 1 : 0,
		QuestWristTrace.bQuestTracked ? 1 : 0,
		QuestWristTrace.bUsedUntrackedJointData ? 1 : 0,
		QuestWristTrace.bRawQuestRejected ? 1 : 0,
		QuestWristTrace.RequestedBlend,
		QuestWristTrace.EffectiveBlend,
		QuestWristTrace.RuntimeStateKey,
		QuestWristTrace.bHadApplyCalibration ? 1 : 0,
		QuestWristTrace.bSetApplyCalibration ? 1 : 0,
		QuestWristTrace.ApplyCalibrationAgeSeconds,
		QuestWristTrace.bHadTraceCalibration ? 1 : 0,
		QuestWristTrace.bSetTraceCalibration ? 1 : 0,
		QuestWristTrace.TraceCalibrationAgeSeconds,
		QuestWristTrace.RawWristToHmdCm,
		QuestWristTrace.RawCalibrationDeltaCm,
		QuestWristTrace.MappedOffsetFromMediaPipeCm,
		*QuestWristTrace.RawQuestWristWorld.ToCompactString(),
		QuestWristTrace.bHmdPoseValid ? 1 : 0,
		QuestWristTrace.bMediaHeadValid ? 1 : 0,
		*QuestWristTrace.MediaPipeHeadWorld.ToCompactString(),
		FMediaPipePoseDiagnostics::QuestMediaCalibrationModeName(QuestWristTrace.CalibrationMode),
		QuestWristTrace.bUsedLiveHmdAnchor ? 1 : 0,
		QuestWristTrace.bUsedLiveHmdTranslationAnchor ? 1 : 0,
		QuestWristTrace.HmdAvatarTranslationFilterAlpha,
		QuestWristTrace.HmdAvatarTranslationSpeedCmSec,
		QuestWristTrace.HmdAvatarTranslationLagCm,
		QuestWristTrace.bUsedRelativeWristCalibration ? 1 : 0,
		QuestWristTrace.bMapped ? 1 : 0,
		QuestWristTrace.bUsedHeldQuestWrist ? 1 : 0,
		QuestWristTrace.bPositionFilterApplied ? 1 : 0,
		QuestWristTrace.PositionFilterAlpha,
		QuestWristTrace.PositionFilterSpeedCmSec,
		QuestWristTrace.PositionFilterTargetDeltaCm,
		QuestWristTrace.PositionFilterFilteredDeltaCm,
		QuestWristTrace.bReachClamped ? 1 : 0,
		QuestWristTrace.bReachAssistApplied ? 1 : 0,
		QuestWristTrace.ReachAssistBlend,
		QuestWristTrace.ReachAssistElbowMoveCm,
		*QuestWristTrace.ReachAssistElbowWorld.ToCompactString(),
		QuestWristTrace.bDriftGuardApplied ? 1 : 0,
		QuestWristTrace.DriftGuardAlpha,
		QuestWristTrace.DriftGuardOffsetCm,
		QuestWristTrace.DriftGuardReachAssistBlend,
		QuestWristTrace.DriftGuardPoleBlend,
		QuestWristTrace.bConstrainedArmSolveApplied ? 1 : 0,
		QuestWristTrace.ConstrainedArmSolveBlend,
		QuestWristTrace.ConstrainedArmWristAuthority,
		QuestWristTrace.ConstrainedArmMediaPipeElbowHint,
		QuestWristTrace.bConstrainedArmReachScaleApplied ? 1 : 0,
		QuestWristTrace.ConstrainedArmReachScale,
		QuestWristTrace.ConstrainedArmReachScaleAlpha,
		QuestWristTrace.ConstrainedArmReachScaleObservedMaxCm,
		QuestWristTrace.ConstrainedArmReachScaleTargetReachCm,
		QuestWristTrace.ArmLengthCalibrationStage,
		QuestWristTrace.ArmLengthCalibrationStableSeconds,
		QuestWristTrace.ArmLengthCalibrationForwardReachCm,
		QuestWristTrace.ArmLengthCalibrationDownDropCm,
		QuestWristTrace.ArmLengthCalibrationTargetReachCm,
		QuestWristTrace.bConstrainedArmDownFrameCorrectionApplied ? 1 : 0,
		QuestWristTrace.ConstrainedArmDownFrameScale,
		QuestWristTrace.ConstrainedArmDownFrameAlpha,
		QuestWristTrace.ConstrainedArmDownFrameObservedDropCm,
		QuestWristTrace.ConstrainedArmDownFrameTargetDropCm,
		QuestWristTrace.bConstrainedArmSourceElbowHintApplied ? 1 : 0,
		*QuestWristTrace.ConstrainedArmSourceElbowHintWorld.ToCompactString(),
		QuestWristTrace.ConstrainedArmCloseReachAlpha,
		QuestWristTrace.ConstrainedArmStablePoleDown,
		QuestWristTrace.ConstrainedArmElbowMoveCm,
		QuestWristTrace.bConstrainedArmPoleContinuityApplied ? 1 : 0,
		QuestWristTrace.ConstrainedArmNearFullPoleAlpha,
		QuestWristTrace.bConstrainedArmReachContinuityApplied ? 1 : 0,
		QuestWristTrace.ConstrainedArmReachContinuityRawReachCm,
		QuestWristTrace.ConstrainedArmReachContinuityPreviousReachCm,
		QuestWristTrace.ConstrainedArmReachContinuityMaxStepCm,
		QuestWristTrace.bConstrainedArmDownStraightened ? 1 : 0,
		QuestWristTrace.ConstrainedArmDownStraightenAlpha,
		QuestWristTrace.bConstrainedArmBodyFallbackApplied ? 1 : 0,
		QuestWristTrace.ConstrainedArmBodyFallbackReachFraction,
		QuestWristTrace.ConstrainedArmBodyFallbackTargetReachCm,
		QuestWristTrace.ConstrainedArmBodyFallbackTargetReachFraction,
		QuestWristTrace.bConstrainedArmBodyFallbackDownStraightened ? 1 : 0,
		QuestWristTrace.ConstrainedArmBodyFallbackDownStraightenAlpha,
		QuestWristTrace.bConstrainedArmDropoutDownFallbackApplied ? 1 : 0,
		QuestWristTrace.ConstrainedArmDropoutDownFallbackAlpha,
		QuestWristTrace.ConstrainedArmDropoutDirectReachCm,
		QuestWristTrace.ConstrainedArmDropoutTargetReachCm,
		QuestWristTrace.ConstrainedArmDropoutDownDominance,
		QuestWristTrace.ConstrainedArmDropoutLastTrackedAgeSeconds,
		QuestWristTrace.bConstrainedArmDropoutMediaPipeHintUsed ? 1 : 0,
		QuestWristTrace.ConstrainedArmWristStepCm,
		QuestWristTrace.ConstrainedArmCandidateElbowStepCm,
		QuestWristTrace.ConstrainedArmAllowedElbowStepCm,
		*QuestWristTrace.ConstrainedArmElbowWorld.ToCompactString(),
		*QuestWristTrace.MappedQuestWristWorld.ToCompactString(),
		*QuestWristTrace.FinalWristWorld.ToCompactString(),
		*QuestWristTrace.MediaPipeWristWorld.ToCompactString(),
		*Input.ShoulderWorld.ToCompactString(),
		*Input.ElbowWorld.ToCompactString(),
		Input.bArmIKBranchEntered ? 1 : 0,
		Input.bForceArmIK ? 1 : 0,
		Input.bWouldEnterArmIKIfApplied ? 1 : 0,
		Input.bUseArmIK ? 1 : 0,
		*Input.LoggedArmIKTargetComp.ToCompactString(),
		*Input.LoggedArmIKWristComp.ToCompactString(),
		*Input.HandBoneName.ToString(),
		FMath::Clamp(Input.QuestHandRotationBlend, 0.0f, 1.0f),
		Input.bQuestHandRotationApplied ? 1 : 0,
		QuestHandRotationTrace.bAppliedHandLocalToLowerArm ? 1 : 0,
		Input.QuestHandRotationDeltaDeg,
		QuestHandRotationTrace.bQuestAvailable ? 1 : 0,
		QuestHandRotationTrace.bQuestTracked ? 1 : 0,
		QuestHandRotationTrace.bUsedJointRotation ? 1 : 0,
		QuestHandRotationTrace.bUsedPalmBasis ? 1 : 0,
		QuestHandRotationTrace.bUsedProjectedTwistBasis ? 1 : 0,
		QuestHandRotationTrace.bProjectedTwistUsesJointAxes ? 1 : 0,
		QuestHandRotationTrace.ProjectedTwistAxisIndex,
		QuestHandRotationTrace.bUsedAnatomicalRollAxis ? 1 : 0,
		QuestHandRotationTrace.AnatomicalRollAxisIndex,
		QuestHandRotationTrace.AnatomicalRollAxisPalmDot,
		QuestHandRotationTrace.AnatomicalRollAxisForearmDot,
		QuestHandRotationTrace.bQuestHandBasisMapped ? 1 : 0,
		QuestHandRotationTrace.bUsedSemanticRoll ? 1 : 0,
		QuestHandRotationTrace.bUsedForearmLocalSemanticRoll ? 1 : 0,
		QuestHandRotationTrace.bUsedSemanticBasisDelta ? 1 : 0,
		QuestHandRotationTrace.bUsedPalmRollFallback ? 1 : 0,
		QuestHandRotationTrace.bHeldPalmRoll ? 1 : 0,
		QuestHandRotationTrace.SemanticRollAxisIndex,
		QuestHandRotationTrace.SemanticRollAxisScore,
		QuestHandRotationTrace.SemanticRollCalibrationOffsetDeg,
		QuestHandRotationTrace.SemanticRollCurrentOffsetDeg,
		QuestHandRotationTrace.bWristAxisDiagnosticsValid ? 1 : 0,
		QuestHandRotationTrace.bWristAxisDiagnosticsMapped ? 1 : 0,
		*QuestHandRotationTrace.WristJointXRefDots.ToCompactString(),
		*QuestHandRotationTrace.WristJointYRefDots.ToCompactString(),
		*QuestHandRotationTrace.WristJointZRefDots.ToCompactString(),
		*QuestHandRotationTrace.PalmXRefDots.ToCompactString(),
		*QuestHandRotationTrace.PalmYRefDots.ToCompactString(),
		*QuestHandRotationTrace.PalmZRefDots.ToCompactString(),
		*QuestHandRotationTrace.WristJointForearmDots.ToCompactString(),
		*QuestHandRotationTrace.PalmForearmDots.ToCompactString(),
		*QuestHandRotationTrace.ForearmRefDots.ToCompactString(),
		QuestHandRotationTrace.bHadCalibration ? 1 : 0,
		QuestHandRotationTrace.bSetCalibration ? 1 : 0,
		QuestHandRotationTrace.CalibrationBasisErrorDeg,
		QuestHandRotationTrace.bCalibrationRejected ? 1 : 0,
		FMediaPipePoseDiagnostics::QuestWristCalibrationStateName(QuestHandRotationTrace.CalibrationState),
		FMediaPipePoseDiagnostics::QuestWristCalibrationRejectReasonName(QuestHandRotationTrace.CalibrationRejectReason),
		QuestHandRotationTrace.CalibrationStableFrameCount,
		QuestHandRotationTrace.CalibrationBasisErrorDeg,
		QuestHandRotationTrace.CalibrationNeutralTwistDeg,
		QuestHandRotationTrace.CalibrationHandVelocityCmSec,
		QuestHandRotationTrace.CalibrationHandAngularVelocityDegSec,
		QuestHandRotationTrace.CalibrationBodyYawDeltaDeg,
		QuestHandRotationTrace.CalibrationMannyYawDeltaDeg,
		QuestHandRotationTrace.RawTwistDeg,
		QuestHandRotationTrace.LimitedTwistDeg,
		QuestHandRotationTrace.SourceHandTwistDeg,
		QuestHandRotationTrace.SmoothedForearmTwistDeg,
		QuestHandRotationTrace.ForearmTwistAlpha01,
		QuestHandRotationTrace.ForearmTwistAlpha02,
		QuestHandRotationTrace.ForearmTwistHelperScale,
		QuestHandRotationTrace.ForearmTwistApplied01Deg,
		QuestHandRotationTrace.ForearmTwistApplied02Deg,
		QuestHandRotationTrace.UpperArmTwistHelperScale,
		QuestHandRotationTrace.TargetUpperArmTwistDeg,
		QuestHandRotationTrace.SmoothedUpperArmTwistDeg,
		QuestHandRotationTrace.UpperArmTwistStepDeg,
		QuestHandRotationTrace.UpperArmTwistMaxStepDeg,
		QuestHandRotationTrace.bUpperArmRateClamped ? 1 : 0,
		QuestHandRotationTrace.UpperArmTwistApplied01Deg,
		QuestHandRotationTrace.UpperArmTwistApplied02Deg,
		QuestHandRotationTrace.RawSwingDeg,
		QuestHandRotationTrace.AppliedSwingDeg,
		QuestHandRotationTrace.bAppliedForearmTwist ? 1 : 0,
		QuestHandRotationTrace.bAppliedTwistCorrection ? 1 : 0,
		QuestHandRotationTrace.bLowerArmMainDriven ? 1 : 0,
		*Input.HandBeforeLocationCS.ToCompactString(),
		*Input.HandAfterLocationCS.ToCompactString());
}

FString FMediaPipeQuestWristDiagnosticFormatter::FormatQuestHandDivergence(
	const FMediaPipeQuestHandDivergenceFormatInput& Input)
{
	return FString::Printf(
		TEXT("mp.QuestHandDivergence: actor=%s side=%s questExpectedFwd=%s questExpectedUp=%s rollTargetFwd=%s rollTargetUp=%s mannyAppliedFwd=%s mannyAppliedUp=%s mediaPipeFwd=%s mediaPipeUp=%s questBasisFwd=%s questBasisUp=%s rollBasisFwd=%s rollBasisUp=%s mannyBasisFwd=%s mannyBasisUp=%s mediaPipeBasisFwd=%s mediaPipeBasisUp=%s questToMannyDeg=%.1f questToRollTargetDeg=%.1f rollTargetToMannyDeg=%.1f questFwdErrDeg=%.1f questUpErrDeg=%.1f rollFwdErrDeg=%.1f rollUpErrDeg=%.1f questBasisFwdErrDeg=%.1f questBasisUpErrDeg=%.1f questBasisRollFwdErrDeg=%.1f questBasisRollUpErrDeg=%.1f wristTwistRawDeg=%.1f wristSemanticAxis=%d wristSemanticScore=%.2f"),
		*Input.TargetActorName.ToString(),
		Input.bIsLeft ? TEXT("L") : TEXT("R"),
		*Input.QuestExpectedForwardComp.ToCompactString(),
		*Input.QuestExpectedUpComp.ToCompactString(),
		*Input.RollTargetForwardComp.ToCompactString(),
		*Input.RollTargetUpComp.ToCompactString(),
		*Input.MannyAppliedForwardComp.ToCompactString(),
		*Input.MannyAppliedUpComp.ToCompactString(),
		*Input.MediaPipeHandForwardComp.ToCompactString(),
		*Input.MediaPipeHandUpComp.ToCompactString(),
		*Input.QuestBasisForwardComp.ToCompactString(),
		*Input.QuestBasisUpComp.ToCompactString(),
		*Input.RollTargetBasisForwardComp.ToCompactString(),
		*Input.RollTargetBasisUpComp.ToCompactString(),
		*Input.MannyAppliedBasisForwardComp.ToCompactString(),
		*Input.MannyAppliedBasisUpComp.ToCompactString(),
		*Input.MediaPipeBasisForwardComp.ToCompactString(),
		*Input.MediaPipeBasisUpComp.ToCompactString(),
		Input.QuestExpectedToMannyDeg,
		Input.QuestExpectedToRollTargetDeg,
		Input.RollTargetToMannyDeg,
		Input.QuestExpectedForwardErrDeg,
		Input.QuestExpectedUpErrDeg,
		Input.RollTargetForwardErrDeg,
		Input.RollTargetUpErrDeg,
		Input.QuestBasisToMannyBasisForwardErrDeg,
		Input.QuestBasisToMannyBasisUpErrDeg,
		Input.QuestBasisToRollBasisForwardErrDeg,
		Input.QuestBasisToRollBasisUpErrDeg,
		Input.RawTwistDeg,
		static_cast<int32>(Input.SemanticRollAxisIndex),
		Input.SemanticRollAxisScore);
}

FString FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristRollCompact(
	const FMediaPipeQuestWristRollCompactFormatInput& Input)
{
	return FString::Printf(
		TEXT("mp.QuestWristRollCompact: actor=%s side=%s applied=%d tracked=%d mapped=%d calibrationState=%s calibrationRejectReason=\"%s\" stableFrameCount=%d calibErrDeg=%.1f neutralTwistDeg=%.1f semantic=%d semanticLocal=%d semanticBasis=%d palmFallback=%d palmHeld=%d handLocal=%d twistCorrection=%d lowerarmMainDriven=%d axis=%u score=%.2f calibHad=%d calibSet=%d calibReject=%d rawTwistDeg=%.1f limitedTwistDeg=%.1f sourceHandTwistDeg=%.1f twistClamp=%d swingRawDeg=%.1f swingAppliedDeg=%.1f forearmTargetDeg=%.1f forearmAppliedDeg=%.1f forearmClamp=%d forearmStepDeg=%.1f forearmMaxStepDeg=%.1f forearmVelDegSec=%.1f forearmRateClamp=%d forearmHelperScale=%.2f forearmTwist01Deg=%.1f forearmTwist02Deg=%.1f upperArmHelperScale=%.2f upperArmTargetDeg=%.1f upperArmAppliedDeg=%.1f upperArmStepDeg=%.1f upperArmMaxStepDeg=%.1f upperArmRateClamp=%d upperArmTwist01Deg=%.1f upperArmTwist02Deg=%.1f handDeltaValid=%d handDeltaDeg=%.1f handAppliedDeltaDeg=%.1f questToMannyDeg=%.1f questToRollTargetDeg=%.1f rollTargetToMannyDeg=%.1f questFwdErrDeg=%.1f questUpErrDeg=%.1f rollFwdErrDeg=%.1f rollUpErrDeg=%.1f questBasisFwdErrDeg=%.1f questBasisUpErrDeg=%.1f questBasisRollFwdErrDeg=%.1f questBasisRollUpErrDeg=%.1f armIK=%d forceIK=%d"),
		*Input.TargetActorName.ToString(),
		Input.bIsLeft ? TEXT("L") : TEXT("R"),
		Input.bQuestHandRotationApplied ? 1 : 0,
		Input.bQuestTracked ? 1 : 0,
		Input.bQuestHandBasisMapped ? 1 : 0,
		FMediaPipePoseDiagnostics::QuestWristCalibrationStateName(Input.CalibrationState),
		FMediaPipePoseDiagnostics::QuestWristCalibrationRejectReasonName(Input.CalibrationRejectReason),
		Input.CalibrationStableFrameCount,
		Input.CalibrationBasisErrorDeg,
		Input.CalibrationNeutralTwistDeg,
		Input.bUsedSemanticRoll ? 1 : 0,
		Input.bUsedForearmLocalSemanticRoll ? 1 : 0,
		Input.bUsedSemanticBasisDelta ? 1 : 0,
		Input.bUsedPalmRollFallback ? 1 : 0,
		Input.bHeldPalmRoll ? 1 : 0,
		Input.bAppliedHandLocalToLowerArm ? 1 : 0,
		Input.bAppliedTwistCorrection ? 1 : 0,
		Input.bLowerArmMainDriven ? 1 : 0,
		Input.bUsedSemanticRoll ? Input.SemanticRollAxisIndex : Input.AnatomicalRollAxisIndex,
		Input.SemanticRollAxisScore,
		Input.bHadCalibration ? 1 : 0,
		Input.bSetCalibration ? 1 : 0,
		Input.bCalibrationRejected ? 1 : 0,
		Input.RawTwistDeg,
		Input.LimitedTwistDeg,
		Input.SourceHandTwistDeg,
		Input.bTwistLimitClamped ? 1 : 0,
		Input.RawSwingDeg,
		Input.AppliedSwingDeg,
		Input.TargetForearmTwistDeg,
		Input.SmoothedForearmTwistDeg,
		Input.bForearmLimitClamped ? 1 : 0,
		Input.ForearmTwistStepDeg,
		Input.ForearmTwistMaxStepDeg,
		Input.ForearmTwistVelocityDegPerSec,
		Input.bForearmRateClamped ? 1 : 0,
		Input.ForearmTwistHelperScale,
		Input.ForearmTwistApplied01Deg,
		Input.ForearmTwistApplied02Deg,
		Input.UpperArmTwistHelperScale,
		Input.TargetUpperArmTwistDeg,
		Input.SmoothedUpperArmTwistDeg,
		Input.UpperArmTwistStepDeg,
		Input.UpperArmTwistMaxStepDeg,
		Input.bUpperArmRateClamped ? 1 : 0,
		Input.UpperArmTwistApplied01Deg,
		Input.UpperArmTwistApplied02Deg,
		Input.bCalibratedHandDeltaValid ? 1 : 0,
		Input.CalibratedHandDeltaDeg,
		Input.QuestHandRotationDeltaDeg,
		Input.QuestExpectedToMannyDeg,
		Input.QuestExpectedToRollTargetDeg,
		Input.RollTargetToMannyDeg,
		Input.QuestExpectedForwardErrDeg,
		Input.QuestExpectedUpErrDeg,
		Input.RollTargetForwardErrDeg,
		Input.RollTargetUpErrDeg,
		Input.QuestBasisToMannyBasisForwardErrDeg,
		Input.QuestBasisToMannyBasisUpErrDeg,
		Input.QuestBasisToRollBasisForwardErrDeg,
		Input.QuestBasisToRollBasisUpErrDeg,
		Input.bArmIKBranchEntered ? 1 : 0,
		Input.bForceArmIK ? 1 : 0);
}

FMediaPipeQuestWristHudFormatResult FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristCalibrationHud(
	const FMediaPipeQuestWristCalibrationSideFormatInput& Left,
	const FMediaPipeQuestWristCalibrationSideFormatInput& Right)
{
	FMediaPipeQuestWristHudFormatResult Result;
	Result.Color = (IsQuestWristCalibrationMatched(Left.CalibrationState) &&
		IsQuestWristCalibrationMatched(Right.CalibrationState)) ? FColor::Green : FColor::Yellow;
	Result.Text = FString::Printf(
		TEXT("QUEST WRIST CALIBRATION\nL: %s tracked=%d stable=%d err=%.1f twist0=%.1f reason=%s\nR: %s tracked=%d stable=%d err=%.1f twist0=%.1f reason=%s\nPose: upright, forearms forward, palms face each other, thumbs up"),
		FMediaPipePoseDiagnostics::QuestWristCalibrationStateName(Left.CalibrationState),
		Left.bTracked ? 1 : 0,
		Left.StableFrameCount,
		Left.BasisErrorDeg,
		Left.NeutralTwistDeg,
		FMediaPipePoseDiagnostics::QuestWristCalibrationRejectReasonName(Left.CalibrationRejectReason),
		FMediaPipePoseDiagnostics::QuestWristCalibrationStateName(Right.CalibrationState),
		Right.bTracked ? 1 : 0,
		Right.StableFrameCount,
		Right.BasisErrorDeg,
		Right.NeutralTwistDeg,
		FMediaPipePoseDiagnostics::QuestWristCalibrationRejectReasonName(Right.CalibrationRejectReason));
	return Result;
}

FMediaPipeQuestWristHudFormatResult FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristSideCalibrationHud(
	const FMediaPipeQuestWristSideHudFormatInput& Input)
{
	const bool bWristPathLive =
		Input.bQuestHandRotationApplied &&
		Input.bQuestAvailable &&
		Input.bQuestTracked &&
		(Input.bUsedSemanticRoll || Input.bUsedAnatomicalRollAxis);
	const bool bCalibrationPoseMatched = IsQuestWristCalibrationMatched(Input.CalibrationState);

	FMediaPipeQuestWristHudFormatResult Result;
	Result.Color = (bWristPathLive || bCalibrationPoseMatched) ? FColor::Green : FColor::Yellow;
	Result.Text = FString::Printf(
		TEXT("%s wrist calibration=%s reason=%s stable=%d err=%.1f twist0=%.1f\nPose: upright, head forward, elbows in 90deg, forearms forward, palms face, thumbs up\nsem=%d local=%d mapped=%d axis=%u score=%.2f twist=%.1f swing=%.1f hand=%d tracked=%d IK=%d forceIK=%d"),
		Input.bIsLeft ? TEXT("L") : TEXT("R"),
		FMediaPipePoseDiagnostics::QuestWristCalibrationStateName(Input.CalibrationState),
		FMediaPipePoseDiagnostics::QuestWristCalibrationRejectReasonName(Input.CalibrationRejectReason),
		Input.CalibrationStableFrameCount,
		Input.CalibrationBasisErrorDeg,
		Input.CalibrationNeutralTwistDeg,
		Input.bUsedSemanticRoll ? 1 : 0,
		Input.bUsedForearmLocalSemanticRoll ? 1 : 0,
		Input.bQuestHandBasisMapped ? 1 : 0,
		Input.bUsedSemanticRoll ? Input.SemanticRollAxisIndex : Input.AnatomicalRollAxisIndex,
		Input.SemanticRollAxisScore,
		Input.RawTwistDeg,
		Input.AppliedSwingDeg,
		Input.bQuestHandRotationApplied ? 1 : 0,
		Input.bQuestTracked ? 1 : 0,
		Input.bArmIKBranchEntered ? 1 : 0,
		Input.bForceArmIK ? 1 : 0);
	return Result;
}

FString FMediaPipeQuestWristDiagnosticFormatter::FormatMetaHumanArmSanityLog(
	const FMediaPipeMetaHumanArmSanityFormatInput& Input)
{
	return FString::Printf(
		TEXT("mp.MetaHumanArmSanity: actor=%s side=%s broken=%d reasons=\"%s\" hasPosedArm=%d questArmMode=%d targetMapped=%d positionApplied=%d questTracked=%d handApplied=%d handLocal=%d wristTargetErrCm=%.1f mappedWristErrCm=%.1f maxWristErrCm=%.1f upperLenCm=%.1f refUpperLenCm=%.1f upperLenErrCm=%.1f lowerLenCm=%.1f refLowerLenCm=%.1f lowerLenErrCm=%.1f elbowBendDeg=%.1f minElbowBendDeg=%.1f targetReachCm=%.1f posedReachCm=%.1f targetReachClamped=%d constrainedArmSolve=%d armIKEntered=%d handRotErrDeg=%.1f maxHandRotErrDeg=%.1f basisFwdErrDeg=%.1f basisUpErrDeg=%.1f maxBasisErrDeg=%.1f swingAppliedDeg=%.1f maxSwingDeg=%.1f twistLimitedDeg=%.1f posedShoulder=%s posedElbow=%s posedHand=%s finalWrist=%s mappedWrist=%s solveShoulder=%s solveElbow=%s"),
		*Input.TargetActorName.ToString(),
		Input.bIsLeft ? TEXT("L") : TEXT("R"),
		Input.bBroken ? 1 : 0,
		*Input.Reasons,
		Input.bHasPosedArm ? 1 : 0,
		Input.QuestArmMode,
		Input.bTargetMapped ? 1 : 0,
		Input.bPositionApplied ? 1 : 0,
		Input.bQuestTracked ? 1 : 0,
		Input.bQuestHandRotationApplied ? 1 : 0,
		Input.bHandLocal ? 1 : 0,
		Input.WristTargetErrorCm,
		Input.MappedWristErrorCm,
		Input.MaxWristErrorCm,
		Input.UpperLenCm,
		Input.RefUpperLenCm,
		Input.UpperLenErrCm,
		Input.LowerLenCm,
		Input.RefLowerLenCm,
		Input.LowerLenErrCm,
		Input.ElbowBendDeg,
		Input.MinElbowBendDeg,
		Input.TargetReachCm,
		Input.PosedReachCm,
		Input.bReachClamped ? 1 : 0,
		Input.bConstrainedArmSolveApplied ? 1 : 0,
		Input.bArmIKBranchEntered ? 1 : 0,
		Input.HandRotErrorDeg,
		Input.MaxHandRotErrorDeg,
		Input.BasisForwardErrorDeg,
		Input.BasisUpErrorDeg,
		Input.MaxBasisErrorDeg,
		Input.SwingAppliedDeg,
		Input.MaxSwingDeg,
		Input.TwistLimitedDeg,
		*Input.PosedShoulderWorld.ToCompactString(),
		*Input.PosedElbowWorld.ToCompactString(),
		*Input.PosedHandWorld.ToCompactString(),
		*Input.FinalWristWorld.ToCompactString(),
		*Input.MappedWristWorld.ToCompactString(),
		*Input.SolveShoulderWorld.ToCompactString(),
		*Input.SolveElbowWorld.ToCompactString());
}

FMediaPipeQuestWristHudFormatResult FMediaPipeQuestWristDiagnosticFormatter::FormatMetaHumanArmSanityHud(
	const FMediaPipeMetaHumanArmSanityFormatInput& Input)
{
	FMediaPipeQuestWristHudFormatResult Result;
	Result.Color = Input.bBroken ? FColor::Red : FColor::Green;
	Result.Text = FString::Printf(
		TEXT("MetaHuman arm %s %s\n%s\nwrist %.1fcm handRot %.1fdeg basis %.1f/%.1f elbow %.1fdeg"),
		*Input.TargetActorName.ToString(),
		Input.bIsLeft ? TEXT("L") : TEXT("R"),
		*Input.Reasons,
		Input.WristTargetErrorCm,
		Input.HandRotErrorDeg,
		Input.BasisForwardErrorDeg,
		Input.BasisUpErrorDeg,
		Input.ElbowBendDeg);
	return Result;
}

FMediaPipeMetaHumanArmSanityFormatInput FMediaPipeQuestWristDiagnosticFormatter::BuildMetaHumanArmSanityInput(
	const FName TargetActorName,
	const bool bIsLeft,
	const bool bHasPosedArm,
	const int32 QuestArmMode,
	const bool bQuestHandRotationApplied,
	const bool bArmIKBranchEntered,
	const float MaxWristErrorCm,
	const float MaxHandRotErrorDeg,
	const float MaxBasisErrorDeg,
	const float MaxLengthErrorCm,
	const float MinElbowBendDeg,
	const float MaxSwingDeg,
	const float RefUpperLenCm,
	const float RefLowerLenCm,
	const FQuestWristMappingTrace& WristTrace,
	const FQuestHandRotationTrace& HandTrace,
	const FVector& PosedShoulderWorld,
	const FVector& PosedElbowWorld,
	const FVector& PosedHandWorld,
	const FVector& SolveShoulderWorld,
	const FVector& SolveElbowWorld)
{
	const bool bHasFinalTarget =
		WristTrace.bMapped != 0 &&
		IsUsableQuestWristPosition(WristTrace.FinalWristWorld);
	const float WristTargetErrorCm = (bHasPosedArm && bHasFinalTarget)
		? FVector::Dist(PosedHandWorld, WristTrace.FinalWristWorld)
		: 0.0f;
	const float MappedWristErrorCm =
		bHasPosedArm && WristTrace.bMapped != 0 && IsUsableQuestWristPosition(WristTrace.MappedQuestWristWorld)
			? FVector::Dist(PosedHandWorld, WristTrace.MappedQuestWristWorld)
			: 0.0f;
	const float UpperLenCm = bHasPosedArm ? FVector::Dist(PosedShoulderWorld, PosedElbowWorld) : 0.0f;
	const float LowerLenCm = bHasPosedArm ? FVector::Dist(PosedElbowWorld, PosedHandWorld) : 0.0f;
	const float UpperLenErrCm = bHasPosedArm ? FMath::Abs(UpperLenCm - RefUpperLenCm) : 0.0f;
	const float LowerLenErrCm = bHasPosedArm ? FMath::Abs(LowerLenCm - RefLowerLenCm) : 0.0f;
	const float TargetReachCm = bHasFinalTarget ? FVector::Dist(SolveShoulderWorld, WristTrace.FinalWristWorld) : 0.0f;
	const float PosedReachCm = bHasPosedArm ? FVector::Dist(PosedShoulderWorld, PosedHandWorld) : 0.0f;
	const FVector ElbowToShoulder = bHasPosedArm ? (PosedShoulderWorld - PosedElbowWorld).GetSafeNormal() : FVector::ZeroVector;
	const FVector ElbowToHand = bHasPosedArm ? (PosedHandWorld - PosedElbowWorld).GetSafeNormal() : FVector::ZeroVector;
	const float ElbowBendDeg = (!ElbowToShoulder.IsNearlyZero() && !ElbowToHand.IsNearlyZero())
		? AngleBetweenSegmentsDeg(ElbowToShoulder, ElbowToHand)
		: 0.0f;

	const bool bMissingArm = !bHasPosedArm;
	const bool bMissingTarget = WristTrace.bPositionApplied != 0 && !bHasFinalTarget;
	const bool bWristTargetFailure =
		bHasPosedArm &&
		bHasFinalTarget &&
		MaxWristErrorCm > KINDA_SMALL_NUMBER &&
		WristTargetErrorCm > MaxWristErrorCm;
	const bool bLengthFailure =
		bHasPosedArm &&
		MaxLengthErrorCm > KINDA_SMALL_NUMBER &&
		(UpperLenErrCm > MaxLengthErrorCm || LowerLenErrCm > MaxLengthErrorCm);
	const bool bElbowCollapseFailure =
		bHasPosedArm &&
		MinElbowBendDeg > KINDA_SMALL_NUMBER &&
		TargetReachCm > 25.0f &&
		ElbowBendDeg > 0.0f &&
		ElbowBendDeg < MinElbowBendDeg;
	const bool bHandRotFailure =
		bQuestHandRotationApplied &&
		MaxHandRotErrorDeg > KINDA_SMALL_NUMBER &&
		HandTrace.QuestExpectedToMannyDeg > MaxHandRotErrorDeg;
	const bool bBasisFailure =
		bQuestHandRotationApplied &&
		MaxBasisErrorDeg > KINDA_SMALL_NUMBER &&
		(HandTrace.QuestBasisToMannyBasisForwardErrDeg > MaxBasisErrorDeg ||
		 HandTrace.QuestBasisToMannyBasisUpErrDeg > MaxBasisErrorDeg);
	const bool bSwingFailure =
		bQuestHandRotationApplied &&
		MaxSwingDeg > KINDA_SMALL_NUMBER &&
		HandTrace.AppliedSwingDeg >= MaxSwingDeg;
	const bool bBroken =
		bMissingArm ||
		bMissingTarget ||
		bWristTargetFailure ||
		bLengthFailure ||
		bElbowCollapseFailure ||
		bHandRotFailure ||
		bBasisFailure ||
		bSwingFailure;

	FString Reasons;
	if (bMissingArm) { AppendReason(Reasons, TEXT("missingArmBones")); }
	if (bMissingTarget) { AppendReason(Reasons, TEXT("missingQuestTarget")); }
	if (bWristTargetFailure) { AppendReason(Reasons, TEXT("wristTargetError")); }
	if (bLengthFailure) { AppendReason(Reasons, TEXT("segmentLengthError")); }
	if (bElbowCollapseFailure) { AppendReason(Reasons, TEXT("elbowCollapsed")); }
	if (bHandRotFailure) { AppendReason(Reasons, TEXT("handRotError")); }
	if (bBasisFailure) { AppendReason(Reasons, TEXT("basisError")); }
	if (bSwingFailure) { AppendReason(Reasons, TEXT("swingClamp")); }
	if (Reasons.IsEmpty()) { Reasons = TEXT("ok"); }

	return FMediaPipeMetaHumanArmSanityFormatInput::FromTraces(
		TargetActorName,
		bIsLeft,
		bBroken,
		Reasons,
		bHasPosedArm,
		QuestArmMode,
		bQuestHandRotationApplied,
		bArmIKBranchEntered,
		WristTargetErrorCm,
		MappedWristErrorCm,
		MaxWristErrorCm,
		UpperLenCm,
		RefUpperLenCm,
		UpperLenErrCm,
		LowerLenCm,
		RefLowerLenCm,
		LowerLenErrCm,
		ElbowBendDeg,
		MinElbowBendDeg,
		TargetReachCm,
		PosedReachCm,
		MaxHandRotErrorDeg,
		MaxBasisErrorDeg,
		MaxSwingDeg,
		WristTrace,
		HandTrace,
		PosedShoulderWorld,
		PosedElbowWorld,
		PosedHandWorld,
		SolveShoulderWorld,
		SolveElbowWorld);
}
