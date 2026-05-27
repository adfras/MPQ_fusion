#include "MediaPipeBodyDiagnostics.h"

#include "MediaPipePoseLog.h"

FString FMediaPipeBodyDiagnostics::FormatPoseYawAlignLog(const FMediaPipePoseYawAlignLogInput& Input)
{
	return FString::Printf(
		TEXT("mp.PoseYawAlign: actor=%s enabled=1 applied=%d rejected=%d recentered=%d rawForward=%s desiredActorForward=%s correctedForward=%s rawYaw=%.1f desiredYaw=%.1f targetDeltaYaw=%.1f appliedDeltaYaw=%.1f remainingYawError=%.2f rawYawJump=%.1f desiredYawJump=%.1f targetDeltaJump=%.1f dt=%.3f anchor=%s actorYaw=%.1f sourceYaw=%.1f"),
		*Input.TargetActorName.ToString(),
		Input.bAppliedYawAlignment ? 1 : 0,
		Input.bRejectedYawJump ? 1 : 0,
		Input.bRecenteredYawState ? 1 : 0,
		*Input.RawForwardHorizontal.ToCompactString(),
		*Input.DesiredActorForwardHorizontal.ToCompactString(),
		*Input.CorrectedForwardHorizontal.ToCompactString(),
		Input.RawYawDeg,
		Input.DesiredYawDeg,
		Input.TargetDeltaYawDeg,
		Input.AppliedDeltaYawDeg,
		Input.RemainingYawErrorDeg,
		Input.RawYawJumpDeg,
		Input.DesiredYawJumpDeg,
		Input.TargetDeltaJumpDeg,
		Input.AlignDeltaSeconds,
		*Input.Anchor.ToCompactString(),
		Input.TargetActorYawDeg,
		Input.SourceYawDeg);
}

void FMediaPipeBodyDiagnostics::EmitPoseYawAlignLog(const FMediaPipePoseYawAlignLogInput& Input)
{
	UE_LOG(LogMediaPipePose, Log, TEXT("%s"), *FormatPoseYawAlignLog(Input));
}

FString FMediaPipeBodyDiagnostics::FormatTorsoBasisLog(const FMediaPipeTorsoBasisLogInput& Input)
{
	const float RawTiltDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Input.RawObservedUp.GetSafeNormal(), FVector::UpVector), -1.0f, 1.0f)));
	const float UsedTiltDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Input.ObservedUp.GetSafeNormal(), FVector::UpVector), -1.0f, 1.0f)));
	return FString::Printf(
		TEXT("mp.TorsoDebug: actor=%s rawUp=%s usedUp=%s rawTiltDeg=%.1f usedTiltDeg=%.1f forward=%s actorForward=%d uprightBlend=%.2f maxTiltDeg=%.1f"),
		*Input.TargetActorName.ToString(),
		*Input.RawObservedUp.ToCompactString(),
		*Input.ObservedUp.ToCompactString(),
		RawTiltDegrees,
		UsedTiltDegrees,
		*Input.Forward.ToCompactString(),
		Input.bUseActorForward ? 1 : 0,
		Input.UprightBlend,
		Input.MaxTiltDegrees);
}

void FMediaPipeBodyDiagnostics::EmitTorsoBasisLog(const FMediaPipeTorsoBasisLogInput& Input)
{
	UE_LOG(LogMediaPipePose, Log, TEXT("%s"), *FormatTorsoBasisLog(Input));
}

FString FMediaPipeBodyDiagnostics::FormatMannyLikeArmSolveLog(const FMediaPipeMannyLikeArmSolveLogInput& Input)
{
	return FString::Printf(
		TEXT("[MP MannyLike ArmSolve] side=%s actor=%s shoulderWorld=%s elbowWorld=%s wristWorld=%s poseUpperComp=%s poseLowerComp=%s planeWorld=%s dotBodyForward=%.4f planeComp=%s dotForwardComp=%.4f dotUpComp=%.4f measuredPoleFrac=%.4f upperDown=%.4f wristBelow=%.4f downAlpha=%.4f pole=%s branchPoleRef=%s activePoleRef=%s branchPoleWeight=%.4f scoreA=%.4f scoreB=%.4f useA=%d"),
		Input.bIsLeft ? TEXT("L") : TEXT("R"),
		*Input.TargetActorName.ToString(),
		*Input.ShoulderWorld.ToString(),
		*Input.ElbowWorld.ToString(),
		*Input.WristWorld.ToString(),
		*Input.PoseUpperComp.ToString(),
		*Input.PoseLowerComp.ToString(),
		*Input.PlaneWorld.ToString(),
		FVector::DotProduct(Input.PlaneWorld, Input.ForwardWorld),
		*Input.PlaneComp.ToString(),
		FVector::DotProduct(Input.PlaneComp, Input.ForwardComp),
		FVector::DotProduct(Input.PlaneComp, Input.UpComp),
		Input.MeasuredPoleFraction,
		Input.UpperDownMetric,
		Input.WristBelowWaistMetric,
		Input.DownPosePoleAlpha,
		*Input.Pole.ToString(),
		*Input.BranchPoleReference.ToString(),
		*Input.ActivePoleReference.ToString(),
		Input.BranchPoleWeight,
		Input.ScoreA,
		Input.ScoreB,
		Input.bUseA ? 1 : 0);
}

void FMediaPipeBodyDiagnostics::EmitMannyLikeArmSolveLog(const FMediaPipeMannyLikeArmSolveLogInput& Input)
{
	UE_LOG(LogTemp, Warning, TEXT("%s"), *FormatMannyLikeArmSolveLog(Input));
}
