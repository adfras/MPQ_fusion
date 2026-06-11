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

void FAnimNode_MediaPipePoseDriven::DriveQuestFingerBonesCS(FCSPose<FCompactPose>& CSPose, bool bIsLeft, const FQuestHandTrackingSnapshot& Snapshot, float DeltaSeconds)
{
	const bool bRuntimeQuestHandTracking = CVarQuestHandTracking.GetValueOnAnyThread() != 0;
	const bool bRuntimeDriveQuestFingerBones = CVarQuestHandDriveFingerBones.GetValueOnAnyThread() != 0;
	const bool bSideAvailable = IsQuestHandSideAvailable(Snapshot, bIsLeft);
	const bool bSideTracked = IsQuestHandSideTracked(Snapshot, bIsLeft);
	// Fingers consume per-joint poses when OpenXR publishes them. The tracked flag
	// can be transiently false in VR Preview even while usable joint arrays exist.
	const bool bRequireTracked = false;
	const TCHAR* SkipReason = nullptr;
	if (!bUseQuestHandTracking)
	{
		SkipReason = TEXT("nodeQuestHandTrackingDisabled");
	}
	else if (!bDriveQuestFingerBones)
	{
		SkipReason = TEXT("nodeFingerDriveDisabled");
	}
	else if (!bRuntimeQuestHandTracking)
	{
		SkipReason = TEXT("cvarQuestHandTrackingDisabled");
	}
	else if (!bRuntimeDriveQuestFingerBones)
	{
		SkipReason = TEXT("cvarFingerDriveDisabled");
	}
	else if (!bSideAvailable)
	{
		SkipReason = TEXT("questHandSideUnavailable");
	}
	else if (bRequireTracked && !bSideTracked)
	{
		SkipReason = TEXT("questHandSideNotTracked");
	}

	if (SkipReason)
	{
		if (CVarQuestFingerDebug.GetValueOnAnyThread() != 0 ||
			CVarQuestHandDebug.GetValueOnAnyThread() != 0)
		{
			double& LastQuestFingerSolveLogTimeSeconds = bIsLeft ? DiagnosticsState.LastQuestFingerSolveLogTimeSecondsL : DiagnosticsState.LastQuestFingerSolveLogTimeSecondsR;
			const double NowSeconds = FPlatformTime::Seconds();
			if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, LastQuestFingerSolveLogTimeSeconds))
			{
				UE_LOG(LogMediaPipePose, Log,
					TEXT("mp.QuestFingerSolve: actor=%s side=%s skipped reason=%s useQuestHandTracking=%d driveQuestFingerBones=%d cvarQuestHandTracking=%d cvarDriveFingerBones=%d available=%d tracked=%d requireTracked=%d jointRetarget=%d curlOnly=%d"),
					*TargetActorName.ToString(),
					bIsLeft ? TEXT("L") : TEXT("R"),
					SkipReason,
					bUseQuestHandTracking ? 1 : 0,
					bDriveQuestFingerBones ? 1 : 0,
					bRuntimeQuestHandTracking ? 1 : 0,
					bRuntimeDriveQuestFingerBones ? 1 : 0,
					bSideAvailable ? 1 : 0,
					bSideTracked ? 1 : 0,
					bRequireTracked ? 1 : 0,
					CVarQuestFingerJointRetarget.GetValueOnAnyThread() != 0 ? 1 : 0,
					CVarQuestFingerCurlOnly.GetValueOnAnyThread() != 0 ? 1 : 0);
			}
		}
		return;
	}

	const TStaticArray<FVector, QuestHandKeypointCount>& Positions = GetQuestHandPositions(Snapshot, bIsLeft);
	const TStaticArray<FQuat, QuestHandKeypointCount>& Rotations = bIsLeft ? Snapshot.LeftRotationsWorld : Snapshot.RightRotationsWorld;
	FBoneReference* FingerBones = bIsLeft ? FingerBonesL : FingerBonesR;
	FBoneReference* FingerMetacarpalBones = bIsLeft ? FingerMetacarpalBonesL : FingerMetacarpalBonesR;
	bool* bHasRefFinger = bIsLeft ? bHasRefFingerL : bHasRefFingerR;
	bool* bHasRefFingerMetacarpal = bIsLeft ? bHasRefFingerMetacarpalL : bHasRefFingerMetacarpalR;
	FQuat* RefFingerComp = bIsLeft ? RefFingerCompL : RefFingerCompR;
	FQuat* RefFingerMetacarpalComp = bIsLeft ? RefFingerMetacarpalCompL : RefFingerMetacarpalCompR;
	FVector* RefFingerDirComp = bIsLeft ? RefFingerDirCompL : RefFingerDirCompR;
	FVector* RefFingerMetacarpalDirComp = bIsLeft ? RefFingerMetacarpalDirCompL : RefFingerMetacarpalDirCompR;
	FVector* RefFingerCurlDirComp = bIsLeft ? RefFingerCurlDirCompL : RefFingerCurlDirCompR;
	bool* bHasSmoothedFingerRotCS = bIsLeft ? LeftQuestHandState.bHasSmoothedQuestFingerRotCS : RightQuestHandState.bHasSmoothedQuestFingerRotCS;
	FQuat* SmoothedFingerRotCS = bIsLeft ? LeftQuestHandState.SmoothedQuestFingerRotCS : RightQuestHandState.SmoothedQuestFingerRotCS;
	bool* bHasQuestFingerRetargetSourceRefCS = bIsLeft ? LeftQuestHandState.bHasQuestFingerRetargetSourceRefCS : RightQuestHandState.bHasQuestFingerRetargetSourceRefCS;
	FQuat* QuestFingerRetargetSourceRefCS = bIsLeft ? LeftQuestHandState.QuestFingerRetargetSourceRefCS : RightQuestHandState.QuestFingerRetargetSourceRefCS;
	FQuat* QuestFingerRetargetTargetRefCS = bIsLeft ? LeftQuestHandState.QuestFingerRetargetTargetRefCS : RightQuestHandState.QuestFingerRetargetTargetRefCS;
	const bool bHasQuestFingerAlignmentComp = bIsLeft ? LeftQuestHandState.bHasQuestFingerAlignmentComp : RightQuestHandState.bHasQuestFingerAlignmentComp;
	const FQuat& QuestFingerAlignmentComp = bIsLeft ? LeftQuestHandState.QuestFingerAlignmentComp : RightQuestHandState.QuestFingerAlignmentComp;
	int32 ValidFingerBoneCount = 0;
	for (int32 BoneIndex = 0; BoneIndex < QuestFingerBoneCount; ++BoneIndex)
	{
		if (bHasRefFinger[BoneIndex] && FingerBones[BoneIndex].IsValidToEvaluate())
		{
			++ValidFingerBoneCount;
		}
	}
	int32 ValidMetacarpalBoneCount = 0;
	for (int32 MetacarpalIndex = 0; MetacarpalIndex < QuestFingerMetacarpalBoneCount; ++MetacarpalIndex)
	{
		if (bHasRefFingerMetacarpal[MetacarpalIndex] && FingerMetacarpalBones[MetacarpalIndex].IsValidToEvaluate())
		{
			++ValidMetacarpalBoneCount;
		}
	}
	int32 AppliedFingerBoneCount = 0;
	int32 AppliedThumbBoneCount = 0;
	int32 AppliedMetacarpalBoneCount = 0;
	float LoggedFingerCurl01[QuestFingerCount - 1] = {0.0f, 0.0f, 0.0f, 0.0f};
	float LoggedFingerJointAngleDeg[QuestFingerCount - 1] = {0.0f, 0.0f, 0.0f, 0.0f};
	float LoggedFingerClosedFistAlpha[QuestFingerCount - 1] = {0.0f, 0.0f, 0.0f, 0.0f};
	float LoggedThumbClosedFistAlpha = 0.0f;
	float LoggedThumbCurl01[QuestFingerSegmentsPerFinger] = {0.0f, 0.0f, 0.0f};
	float LoggedThumbJointAngleDeg[QuestFingerSegmentsPerFinger] = {0.0f, 0.0f, 0.0f};
	const TCHAR* ThumbMode = TEXT("none");
	const float FingerSegmentScale[QuestFingerSegmentsPerFinger] = {
		FMath::Clamp(CVarQuestFingerCurlProximalScale.GetValueOnAnyThread(), 0.0f, 2.0f),
		FMath::Clamp(CVarQuestFingerCurlIntermediateScale.GetValueOnAnyThread(), 0.0f, 2.0f),
		FMath::Clamp(CVarQuestFingerCurlDistalScale.GetValueOnAnyThread(), 0.0f, 2.0f)
	};
	const float ThumbSegmentScale[QuestFingerSegmentsPerFinger] = {
		FMath::Clamp(CVarQuestThumbCurlProximalScale.GetValueOnAnyThread(), 0.0f, 2.0f),
		FMath::Clamp(CVarQuestThumbCurlIntermediateScale.GetValueOnAnyThread(), 0.0f, 2.0f),
		FMath::Clamp(CVarQuestThumbCurlDistalScale.GetValueOnAnyThread(), 0.0f, 2.0f)
	};
	auto LogQuestFingerSolve = [&](const TCHAR* Mode, const int32 AppliedCount, const bool bReportQuestFingerAlignment)
	{
		if (CVarQuestFingerDebug.GetValueOnAnyThread() == 0 &&
			CVarQuestHandDebug.GetValueOnAnyThread() == 0)
		{
			return;
		}

		double& LastQuestFingerSolveLogTimeSeconds = bIsLeft ? DiagnosticsState.LastQuestFingerSolveLogTimeSecondsL : DiagnosticsState.LastQuestFingerSolveLogTimeSecondsR;
		const double NowSeconds = FPlatformTime::Seconds();
		const FVector QuestWristWorld = Positions[static_cast<int32>(EHandKeypoint::Wrist)];
		const bool bAvailable = IsQuestHandSideAvailable(Snapshot, bIsLeft);
		const bool bTracked = IsQuestHandSideTracked(Snapshot, bIsLeft);
		FMediaPipeQuestHandDebugReporter::EmitFingerSolveLog(
			TargetActorName,
			bIsLeft,
			bAvailable,
			bTracked,
			bDriveQuestFingerBones,
			AppliedCount,
			AppliedThumbBoneCount,
			AppliedMetacarpalBoneCount,
			ValidFingerBoneCount,
			ValidMetacarpalBoneCount,
			Mode,
			ThumbMode,
			CVarQuestFingerPreserveSpread.GetValueOnAnyThread() != 0,
			bReportQuestFingerAlignment,
			FMath::Clamp(CVarQuestWristPositionBlend.GetValueOnAnyThread(), 0.0f, 1.0f),
			FMath::Clamp(QuestHandRotationBlend, 0.0f, 1.0f),
			FMath::Clamp(CVarQuestFingerMaxCurlDegrees.GetValueOnAnyThread(), 0.0f, 140.0f),
			FMath::Clamp(CVarQuestThumbMaxCurlDegrees.GetValueOnAnyThread(), 0.0f, 120.0f),
			FingerSegmentScale,
			ThumbSegmentScale,
			LoggedFingerCurl01,
			LoggedFingerJointAngleDeg,
			LoggedFingerClosedFistAlpha,
			LoggedThumbClosedFistAlpha,
			LoggedThumbCurl01,
			LoggedThumbJointAngleDeg,
			QuestWristWorld,
			NowSeconds,
			LastQuestFingerSolveLogTimeSeconds);
	};

	const float ConfigHalfLife = FMath::Max(0.0f, QuestFingerRotationHalfLifeSeconds);
	const float CVarHalfLife = FMath::Max(0.0f, CVarQuestFingerRotationHalfLife.GetValueOnAnyThread());
	const float Alpha = HalfLifeToAlpha(FMath::Max(ConfigHalfLife, CVarHalfLife), DeltaSeconds);
	const FMediaPipeQuestFingerCurlSettings CurlSettings{
		CVarQuestFingerCurlOpenAngleDegrees.GetValueOnAnyThread(),
		CVarQuestFingerCurlFullAngleDegrees.GetValueOnAnyThread()
	};
	const FMediaPipeQuestFingerCurlSettings ChainCurlSettings{
		CVarQuestFingerCurlOpenAngleDegrees.GetValueOnAnyThread(),
		CVarQuestFingerChainCurlFullAngleDegrees.GetValueOnAnyThread()
	};

	auto UpdateLoggedCurlMetrics = [&]()
	{
		const float ClosedFistAssist = FMath::Clamp(CVarQuestFingerClosedFistAssist.GetValueOnAnyThread(), 0.0f, 1.0f);
		const float ClosedFistAssistStart01 = FMath::Clamp(CVarQuestFingerClosedFistAssistStart01.GetValueOnAnyThread(), 0.0f, 1.0f);
		const float ClosedFistAssistFull01 = FMath::Max(
			ClosedFistAssistStart01 + 0.01f,
			FMath::Clamp(CVarQuestFingerClosedFistAssistFull01.GetValueOnAnyThread(), 0.0f, 1.0f));
		const float ClosedFistHandAssist = FMath::Clamp(CVarQuestFingerClosedFistHandAssist.GetValueOnAnyThread(), 0.0f, 1.0f);
		const float ClosedFistHandAssistStart01 = FMath::Clamp(CVarQuestFingerClosedFistHandAssistStart01.GetValueOnAnyThread(), 0.0f, 1.0f);
		const float ClosedFistHandAssistFull01 = FMath::Max(
			ClosedFistHandAssistStart01 + 0.01f,
			FMath::Clamp(CVarQuestFingerClosedFistHandAssistFull01.GetValueOnAnyThread(), 0.0f, 1.0f));

		float NonThumbPeakCurl01 = 0.0f;
		float NonThumbPeakCurlSecond01 = 0.0f;
		float NonThumbPeakCurlSum01 = 0.0f;
		int32 NonThumbCurlCount = 0;
		for (int32 FingerIndex = 1; FingerIndex < QuestFingerCount; ++FingerIndex)
		{
			float FingerPeakCurl01 = 0.0f;
			float FingerPeakJointAngleDeg = 0.0f;
			for (int32 SegmentIndex = 0; SegmentIndex < QuestFingerSegmentsPerFinger; ++SegmentIndex)
			{
				float JointAngleDeg = 0.0f;
				const float Curl01 = QuestFingerChainCurl01(Snapshot, bIsLeft, FingerIndex, SegmentIndex, ChainCurlSettings, JointAngleDeg);
				FingerPeakCurl01 = FMath::Max(FingerPeakCurl01, Curl01);
				FingerPeakJointAngleDeg = FMath::Max(FingerPeakJointAngleDeg, JointAngleDeg);
			}

			LoggedFingerCurl01[FingerIndex - 1] = FingerPeakCurl01;
			LoggedFingerJointAngleDeg[FingerIndex - 1] = FingerPeakJointAngleDeg;
			LoggedFingerClosedFistAlpha[FingerIndex - 1] = FMath::Clamp(
				(FingerPeakCurl01 - ClosedFistAssistStart01) / (ClosedFistAssistFull01 - ClosedFistAssistStart01),
				0.0f,
				1.0f) * ClosedFistAssist;

			if (FingerPeakCurl01 > NonThumbPeakCurl01)
			{
				NonThumbPeakCurlSecond01 = NonThumbPeakCurl01;
				NonThumbPeakCurl01 = FingerPeakCurl01;
			}
			else if (FingerPeakCurl01 > NonThumbPeakCurlSecond01)
			{
				NonThumbPeakCurlSecond01 = FingerPeakCurl01;
			}
			NonThumbPeakCurlSum01 += FingerPeakCurl01;
			++NonThumbCurlCount;
		}

		const float NonThumbPeakCurlMean01 = NonThumbCurlCount > 0
			? NonThumbPeakCurlSum01 / static_cast<float>(NonThumbCurlCount)
			: 0.0f;
		const float HandClosedFistCue01 = FMath::Max(
			NonThumbPeakCurlSecond01,
			NonThumbPeakCurl01 * 0.75f + NonThumbPeakCurlMean01 * 0.25f);
		const float HandClosedFistAlpha = FMath::Clamp(
			(HandClosedFistCue01 - ClosedFistHandAssistStart01) / (ClosedFistHandAssistFull01 - ClosedFistHandAssistStart01),
			0.0f,
			1.0f) * ClosedFistHandAssist;
		for (int32 FingerIndex = 1; FingerIndex < QuestFingerCount; ++FingerIndex)
		{
			LoggedFingerClosedFistAlpha[FingerIndex - 1] = FMath::Max(LoggedFingerClosedFistAlpha[FingerIndex - 1], HandClosedFistAlpha);
		}

		LoggedThumbClosedFistAlpha = HandClosedFistAlpha * FMath::Clamp(CVarQuestThumbClosedFistAssist.GetValueOnAnyThread(), 0.0f, 1.0f);
		for (int32 SegmentIndex = 0; SegmentIndex < QuestFingerSegmentsPerFinger; ++SegmentIndex)
		{
			float ThumbJointAngleDeg = 0.0f;
			const float ThumbCurl01 = QuestThumbChainCurl01(Snapshot, bIsLeft, SegmentIndex, ChainCurlSettings, ThumbJointAngleDeg);
			LoggedThumbCurl01[SegmentIndex] = FMath::Max(ThumbCurl01, LoggedThumbClosedFistAlpha);
			LoggedThumbJointAngleDeg[SegmentIndex] = ThumbJointAngleDeg;
		}
	};

	UpdateLoggedCurlMetrics();

	auto TryMapQuestSegmentWorldToComponent = [&](const FVector& QuestSegmentWorld, FVector& OutSegmentComp, const bool bApplyMediaPipeHandAlignment) -> bool
	{
		OutSegmentComp = FVector::ZeroVector;
		if (QuestSegmentWorld.IsNearlyZero())
		{
			return false;
		}

		FVector MappedSegmentWorld = FVector::ZeroVector;
		if (!TryMapQuestDirectionToMediaWorld(QuestSegmentWorld, MappedSegmentWorld))
		{
			return false;
		}

		OutSegmentComp = TargetCompTransform.InverseTransformVectorNoScale(MappedSegmentWorld).GetSafeNormal();
		if (bApplyMediaPipeHandAlignment && bHasQuestFingerAlignmentComp)
		{
			OutSegmentComp = QuestFingerAlignmentComp.RotateVector(OutSegmentComp).GetSafeNormal();
		}
		return !OutSegmentComp.IsNearlyZero();
	};

	auto TryMapQuestJointRotationToComponent = [&](const EHandKeypoint Keypoint, FQuat& OutRotationComp) -> bool
	{
		OutRotationComp = FQuat::Identity;
		const int32 KeyIndex = static_cast<int32>(Keypoint);
		if (KeyIndex < 0 || KeyIndex >= QuestHandKeypointCount)
		{
			return false;
		}

		const FQuat QuestRotWorld = Rotations[KeyIndex].GetNormalized();
		if (QuestRotWorld.IsIdentity())
		{
			return false;
		}

		FQuat MappedQuestRotWorld = QuestRotWorld;
		FQuat QuestToMediaRotationWorld = FQuat::Identity;
		if (TryGetCurrentQuestToMediaSpaceRotation(QuestToMediaRotationWorld))
		{
			MappedQuestRotWorld = (QuestToMediaRotationWorld * QuestRotWorld).GetNormalized();
		}

		OutRotationComp = TargetCompTransform.InverseTransformRotation(MappedQuestRotWorld).GetNormalized();
		return !OutRotationComp.IsIdentity();
	};

	const bool bUsePalmLocalHinge = CVarQuestFingerJointRetarget.GetValueOnAnyThread() != 0;
	const bool bUseCurlOnly = CVarQuestFingerCurlOnly.GetValueOnAnyThread() != 0;
	if (bUsePalmLocalHinge || bUseCurlOnly)
	{
		FVector QuestForwardWorld = FVector::ZeroVector;
		FVector QuestUpWorld = FVector::ZeroVector;
		float QuestBasisSin = 0.0f;
		if (TryBuildQuestHandBasisWorld(Snapshot, bIsLeft, QuestForwardWorld, QuestUpWorld, QuestBasisSin, false) &&
			QuestBasisSin >= 0.08f)
		{
			const FBoneReference& HandBone = bIsLeft ? HandL : HandR;
			const FQuat& RefHandComp = bIsLeft ? RefHandCompL : RefHandCompR;
			const FQuat& RefHandBasisComp = bIsLeft ? RefHandBasisCompL : RefHandBasisCompR;
			if (HandBone.IsValidToEvaluate())
			{
				const FQuat CurrentHandRotCS = CSPose.GetComponentSpaceTransform(HandBone.CachedCompactPoseIndex).GetRotation().GetNormalized();
				const FQuat HandDeltaCS = (CurrentHandRotCS * RefHandComp.Inverse()).GetNormalized();
				const FQuat CurrentHandBasisComp = (HandDeltaCS * RefHandBasisComp).GetNormalized();
				const float Strength = FMath::Clamp(CVarQuestFingerCurlStrength.GetValueOnAnyThread(), 0.0f, 1.5f);
				const float FingerMaxCurlDeg = FMath::Clamp(CVarQuestFingerMaxCurlDegrees.GetValueOnAnyThread(), 0.0f, 140.0f);
				const float ThumbMaxCurlDeg = FMath::Clamp(CVarQuestThumbMaxCurlDegrees.GetValueOnAnyThread(), 0.0f, 120.0f);
				const bool bUseThumbChainCurl = CVarQuestThumbUseChainCurl.GetValueOnAnyThread() != 0;
				const bool bUseFingerChainCurl = CVarQuestFingerUseChainCurl.GetValueOnAnyThread() != 0;
				const float ClosedFistAssist = FMath::Clamp(CVarQuestFingerClosedFistAssist.GetValueOnAnyThread(), 0.0f, 1.0f);
				const float ClosedFistAssistStart01 = FMath::Clamp(CVarQuestFingerClosedFistAssistStart01.GetValueOnAnyThread(), 0.0f, 1.0f);
				const float ClosedFistAssistFull01 = FMath::Max(
					ClosedFistAssistStart01 + 0.01f,
					FMath::Clamp(CVarQuestFingerClosedFistAssistFull01.GetValueOnAnyThread(), 0.0f, 1.0f));
				const float ClosedFistHandAssist = FMath::Clamp(CVarQuestFingerClosedFistHandAssist.GetValueOnAnyThread(), 0.0f, 1.0f);
				const float ClosedFistHandAssistStart01 = FMath::Clamp(CVarQuestFingerClosedFistHandAssistStart01.GetValueOnAnyThread(), 0.0f, 1.0f);
				const float ClosedFistHandAssistFull01 = FMath::Max(
					ClosedFistHandAssistStart01 + 0.01f,
					FMath::Clamp(CVarQuestFingerClosedFistHandAssistFull01.GetValueOnAnyThread(), 0.0f, 1.0f));
				float SolvedCurl01[QuestFingerCount][QuestFingerSegmentsPerFinger] = {};
				float SolvedJointAngleDeg[QuestFingerCount][QuestFingerSegmentsPerFinger] = {};
				bool bHasSolvedCurl[QuestFingerCount][QuestFingerSegmentsPerFinger] = {};
				float FingerPeakCurl01[QuestFingerCount] = {};
				float FingerPeakJointAngleDeg[QuestFingerCount] = {};
				float FingerClosedFistAlpha[QuestFingerCount] = {};
				bool bAppliedAnyCurl = false;

				for (int32 FingerIndex = 0; FingerIndex < QuestFingerCount; ++FingerIndex)
				{
					for (int32 SegmentIndex = 0; SegmentIndex < QuestFingerSegmentsPerFinger; ++SegmentIndex)
					{
						const FVector QuestSegmentWorld = GetQuestFingerSegmentWorld(Snapshot, bIsLeft, FingerIndex, SegmentIndex);
						if (QuestSegmentWorld.IsNearlyZero())
						{
							continue;
						}

						float JointAngleDeg = 0.0f;
						float Curl01 = QuestFingerSegmentCurl01(QuestSegmentWorld, QuestForwardWorld, CurlSettings);
						if (FingerIndex == 0)
						{
							if (bUseThumbChainCurl)
							{
								ThumbMode = TEXT("chain");
								Curl01 = QuestThumbChainCurl01(Snapshot, bIsLeft, SegmentIndex, CurlSettings, JointAngleDeg);
							}
							else
							{
								ThumbMode = TEXT("palmForward");
							}
							LoggedThumbCurl01[SegmentIndex] = Curl01;
							LoggedThumbJointAngleDeg[SegmentIndex] = JointAngleDeg;
						}
						else if (bUseFingerChainCurl)
						{
							float ChainJointAngleDeg = 0.0f;
							const float ChainCurl01 = QuestFingerChainCurl01(Snapshot, bIsLeft, FingerIndex, SegmentIndex, ChainCurlSettings, ChainJointAngleDeg);
							if (ChainCurl01 >= Curl01)
							{
								Curl01 = ChainCurl01;
							}
							JointAngleDeg = ChainJointAngleDeg;
						}

						SolvedCurl01[FingerIndex][SegmentIndex] = Curl01;
						SolvedJointAngleDeg[FingerIndex][SegmentIndex] = JointAngleDeg;
						bHasSolvedCurl[FingerIndex][SegmentIndex] = true;
						if (FingerIndex > 0)
						{
							FingerPeakCurl01[FingerIndex] = FMath::Max(FingerPeakCurl01[FingerIndex], Curl01);
							FingerPeakJointAngleDeg[FingerIndex] = FMath::Max(FingerPeakJointAngleDeg[FingerIndex], JointAngleDeg);
						}
					}
				}

				float NonThumbClosedFistAlphaSum = 0.0f;
				int32 NonThumbClosedFistAlphaCount = 0;
				float NonThumbPeakCurl01 = 0.0f;
				float NonThumbPeakCurlSecond01 = 0.0f;
				float NonThumbPeakCurlSum01 = 0.0f;
				for (int32 FingerIndex = 1; FingerIndex < QuestFingerCount; ++FingerIndex)
				{
					const float ClosedAlpha = FMath::Clamp(
						(FingerPeakCurl01[FingerIndex] - ClosedFistAssistStart01) / (ClosedFistAssistFull01 - ClosedFistAssistStart01),
						0.0f,
						1.0f) * ClosedFistAssist;
					FingerClosedFistAlpha[FingerIndex] = ClosedAlpha;
					LoggedFingerJointAngleDeg[FingerIndex - 1] = FingerPeakJointAngleDeg[FingerIndex];
					LoggedFingerClosedFistAlpha[FingerIndex - 1] = ClosedAlpha;
					NonThumbClosedFistAlphaSum += ClosedAlpha;
					++NonThumbClosedFistAlphaCount;
					NonThumbPeakCurlSum01 += FingerPeakCurl01[FingerIndex];
					if (FingerPeakCurl01[FingerIndex] >= NonThumbPeakCurl01)
					{
						NonThumbPeakCurlSecond01 = NonThumbPeakCurl01;
						NonThumbPeakCurl01 = FingerPeakCurl01[FingerIndex];
					}
					else if (FingerPeakCurl01[FingerIndex] > NonThumbPeakCurlSecond01)
					{
						NonThumbPeakCurlSecond01 = FingerPeakCurl01[FingerIndex];
					}
				}
				const float NonThumbClosedFistAlpha = NonThumbClosedFistAlphaCount > 0
					? NonThumbClosedFistAlphaSum / static_cast<float>(NonThumbClosedFistAlphaCount)
					: 0.0f;
				const float NonThumbPeakCurlMean01 = NonThumbClosedFistAlphaCount > 0
					? NonThumbPeakCurlSum01 / static_cast<float>(NonThumbClosedFistAlphaCount)
					: 0.0f;
				const float HandClosedFistCue01 = FMath::Max(
					NonThumbPeakCurlSecond01,
					NonThumbPeakCurl01 * 0.75f + NonThumbPeakCurlMean01 * 0.25f);
				const float HandClosedFistAlpha = FMath::Clamp(
					(HandClosedFistCue01 - ClosedFistHandAssistStart01) / (ClosedFistHandAssistFull01 - ClosedFistHandAssistStart01),
					0.0f,
					1.0f) * ClosedFistHandAssist;
				LoggedThumbClosedFistAlpha = FMath::Max(
					NonThumbClosedFistAlpha,
					HandClosedFistAlpha) * FMath::Clamp(CVarQuestThumbClosedFistAssist.GetValueOnAnyThread(), 0.0f, 1.0f);
				for (int32 FingerIndex = 1; FingerIndex < QuestFingerCount; ++FingerIndex)
				{
					FingerClosedFistAlpha[FingerIndex] = FMath::Max(FingerClosedFistAlpha[FingerIndex], HandClosedFistAlpha);
					LoggedFingerClosedFistAlpha[FingerIndex - 1] = FingerClosedFistAlpha[FingerIndex];
				}

				auto TryApplyQuestJointOrientationBone = [&](
					const FBoneReference& Bone,
					const int32 StateIndex,
					const EHandKeypoint SourceKeypoint,
					const EHandKeypoint SourceParentKeypoint,
					const FQuat& TargetParentReferenceComp,
					const FQuat& TargetParentLiveComp,
					const FQuat& TargetReferenceComp,
					const bool bCanCaptureSourceReference) -> bool
				{
					if (StateIndex < 0 ||
						StateIndex >= MediaPipeQuestStateFingerBoneCount ||
						!Bone.IsValidToEvaluate())
					{
						return false;
					}

					FQuat SourceLiveComp = FQuat::Identity;
					if (!TryMapQuestJointRotationToComponent(SourceKeypoint, SourceLiveComp))
					{
						return false;
					}

					FQuat SourceParentLiveComp = FQuat::Identity;
					if (!TryMapQuestJointRotationToComponent(SourceParentKeypoint, SourceParentLiveComp))
					{
						return false;
					}

					const FQuat SourceLiveLocal = MakeQuestJointLocalRotation(SourceParentLiveComp, SourceLiveComp);
					if (SourceLiveLocal.IsIdentity())
					{
						return false;
					}

					if (!bHasQuestFingerRetargetSourceRefCS[StateIndex])
					{
						if (!bCanCaptureSourceReference)
						{
							return false;
						}

						QuestFingerRetargetSourceRefCS[StateIndex] = SourceLiveLocal;
						QuestFingerRetargetTargetRefCS[StateIndex] = MakeQuestJointLocalRotation(TargetParentReferenceComp, TargetReferenceComp);
						bHasQuestFingerRetargetSourceRefCS[StateIndex] = true;
					}

					const float SourceDeltaFromRefDeg = FMath::RadiansToDegrees(
						QuestFingerRetargetSourceRefCS[StateIndex].AngularDistance(SourceLiveLocal));
					if (SourceDeltaFromRefDeg > 145.0f)
					{
						return false;
					}

					const FQuat TargetRotCS = RetargetQuestJointLocalToComponent(
						QuestFingerRetargetSourceRefCS[StateIndex],
						QuestFingerRetargetTargetRefCS[StateIndex],
						SourceLiveLocal,
						TargetParentLiveComp);
					UpdateSmoothedRotation(
						bHasSmoothedFingerRotCS[StateIndex],
						SmoothedFingerRotCS[StateIndex],
						TargetRotCS,
						Alpha);
					ApplyRotationCS(CSPose, Bone, SmoothedFingerRotCS[StateIndex]);
					return true;
				};

				// OculusXRMovement drives the full hand hierarchy from joint orientations and
				// avoids using fist-closure clamps as the solve. Do the same here when Quest
				// joint rotations are available, with the palm-local curl path left as fallback.
				if (bUsePalmLocalHinge)
				{
					// VR Preview can start with partially curled hands. Waiting for an open-pose
					// gate leaves the solve stuck in the curl fallback, so initialize from the
					// first valid tracked Quest joint hierarchy.
					const bool bCanCaptureMetacarpalSourceReference = true;
					int32 JointOrientationAppliedFingerBones = 0;
					int32 JointOrientationAppliedThumbBones = 0;
					int32 JointOrientationAppliedMetacarpalBones = 0;

					for (int32 FingerIndex = 1; FingerIndex < QuestFingerCount; ++FingerIndex)
					{
						const int32 MetacarpalIndex = QuestFingerMetacarpalBoneIndex(FingerIndex);
						const int32 StateIndex = MediaPipeQuestStateMetacarpalOffset + MetacarpalIndex;
						if (!bHasRefFingerMetacarpal[MetacarpalIndex])
						{
							continue;
						}

						if (TryApplyQuestJointOrientationBone(
							FingerMetacarpalBones[MetacarpalIndex],
							StateIndex,
							QuestFingerMetacarpalSourceKeypoint(FingerIndex),
							EHandKeypoint::Wrist,
							RefHandComp,
							CurrentHandRotCS,
							RefFingerMetacarpalComp[MetacarpalIndex],
							bCanCaptureMetacarpalSourceReference))
						{
							++JointOrientationAppliedMetacarpalBones;
						}
					}

					float ThumbPeakCurl01 = 0.0f;
					for (int32 SegmentIndex = 0; SegmentIndex < QuestFingerSegmentsPerFinger; ++SegmentIndex)
					{
						ThumbPeakCurl01 = FMath::Max(ThumbPeakCurl01, SolvedCurl01[0][SegmentIndex]);
					}

					for (int32 FingerIndex = 0; FingerIndex < QuestFingerCount; ++FingerIndex)
					{
						for (int32 SegmentIndex = 0; SegmentIndex < QuestFingerSegmentsPerFinger; ++SegmentIndex)
						{
							const int32 BoneIndex = QuestFingerBoneIndex(FingerIndex, SegmentIndex);
							if (!bHasRefFinger[BoneIndex] || !bHasSolvedCurl[FingerIndex][SegmentIndex])
							{
								continue;
							}

							const bool bCanCaptureSourceReference = true;

							EHandKeypoint SourceParentKeypoint = EHandKeypoint::Wrist;
							FQuat TargetParentReferenceComp = RefHandComp;
							FQuat TargetParentLiveComp = CurrentHandRotCS;
							bool bHasValidTargetParent = true;
							if (FingerIndex == 0)
							{
								if (SegmentIndex > 0)
								{
									const int32 ParentBoneIndex = QuestFingerBoneIndex(FingerIndex, SegmentIndex - 1);
									if (!bHasRefFinger[ParentBoneIndex] ||
										!FingerBones[ParentBoneIndex].IsValidToEvaluate())
									{
										bHasValidTargetParent = false;
									}
									else
									{
										SourceParentKeypoint = QuestFingerBoneSourceKeypoint(FingerIndex, SegmentIndex - 1);
										TargetParentReferenceComp = RefFingerComp[ParentBoneIndex];
										TargetParentLiveComp = CSPose.GetComponentSpaceTransform(FingerBones[ParentBoneIndex].CachedCompactPoseIndex).GetRotation().GetNormalized();
									}
								}
							}
							else if (SegmentIndex == 0)
							{
								const int32 MetacarpalIndex = QuestFingerMetacarpalBoneIndex(FingerIndex);
								if (bHasRefFingerMetacarpal[MetacarpalIndex] &&
									FingerMetacarpalBones[MetacarpalIndex].IsValidToEvaluate())
								{
									SourceParentKeypoint = QuestFingerMetacarpalSourceKeypoint(FingerIndex);
									TargetParentReferenceComp = RefFingerMetacarpalComp[MetacarpalIndex];
									TargetParentLiveComp = CSPose.GetComponentSpaceTransform(FingerMetacarpalBones[MetacarpalIndex].CachedCompactPoseIndex).GetRotation().GetNormalized();
								}
							}
							else
							{
								const int32 ParentBoneIndex = QuestFingerBoneIndex(FingerIndex, SegmentIndex - 1);
								if (!bHasRefFinger[ParentBoneIndex] ||
									!FingerBones[ParentBoneIndex].IsValidToEvaluate())
								{
									bHasValidTargetParent = false;
								}
								else
								{
									SourceParentKeypoint = QuestFingerBoneSourceKeypoint(FingerIndex, SegmentIndex - 1);
									TargetParentReferenceComp = RefFingerComp[ParentBoneIndex];
									TargetParentLiveComp = CSPose.GetComponentSpaceTransform(FingerBones[ParentBoneIndex].CachedCompactPoseIndex).GetRotation().GetNormalized();
								}
							}

							if (!bHasValidTargetParent)
							{
								continue;
							}

							if (TryApplyQuestJointOrientationBone(
								FingerBones[BoneIndex],
								BoneIndex,
								QuestFingerBoneSourceKeypoint(FingerIndex, SegmentIndex),
								SourceParentKeypoint,
								TargetParentReferenceComp,
								TargetParentLiveComp,
								RefFingerComp[BoneIndex],
								bCanCaptureSourceReference))
							{
								++JointOrientationAppliedFingerBones;
								if (FingerIndex == 0)
								{
									++JointOrientationAppliedThumbBones;
								}
							}
						}
					}

					if (JointOrientationAppliedFingerBones >= 12)
					{
						for (int32 FingerIndex = 1; FingerIndex < QuestFingerCount; ++FingerIndex)
						{
							LoggedFingerClosedFistAlpha[FingerIndex - 1] = FMath::Clamp(FingerPeakCurl01[FingerIndex], 0.0f, 1.0f);
						}
						LoggedThumbClosedFistAlpha = FMath::Clamp(ThumbPeakCurl01, 0.0f, 1.0f);
						AppliedFingerBoneCount = JointOrientationAppliedFingerBones;
						AppliedThumbBoneCount = JointOrientationAppliedThumbBones;
						AppliedMetacarpalBoneCount = JointOrientationAppliedMetacarpalBones;
						LogQuestFingerSolve(TEXT("jointOrientation"), AppliedFingerBoneCount + AppliedMetacarpalBoneCount, false);
						return;
					}
				}

				if (bUseCurlOnly)
				{
					if (bUsePalmLocalHinge)
					{
						for (int32 FingerIndex = 1; FingerIndex < QuestFingerCount; ++FingerIndex)
						{
							const int32 MetacarpalIndex = QuestFingerMetacarpalBoneIndex(FingerIndex);
							const int32 SmoothIndex = MediaPipeQuestStateMetacarpalOffset + MetacarpalIndex;
							if (!bHasRefFingerMetacarpal[MetacarpalIndex] ||
								!FingerMetacarpalBones[MetacarpalIndex].IsValidToEvaluate() ||
								SmoothIndex < 0 ||
								SmoothIndex >= MediaPipeQuestStateFingerBoneCount)
							{
								continue;
							}

							const FQuat TargetRotCS = (HandDeltaCS * RefFingerMetacarpalComp[MetacarpalIndex]).GetNormalized();
							UpdateSmoothedRotation(
								bHasSmoothedFingerRotCS[SmoothIndex],
								SmoothedFingerRotCS[SmoothIndex],
								TargetRotCS,
								Alpha);
							ApplyRotationCS(CSPose, FingerMetacarpalBones[MetacarpalIndex], SmoothedFingerRotCS[SmoothIndex]);
							++AppliedMetacarpalBoneCount;
						}
					}

					for (int32 FingerIndex = 0; FingerIndex < QuestFingerCount; ++FingerIndex)
					{
						for (int32 SegmentIndex = 0; SegmentIndex < QuestFingerSegmentsPerFinger; ++SegmentIndex)
						{
							const int32 BoneIndex = QuestFingerBoneIndex(FingerIndex, SegmentIndex);
							if (!bHasRefFinger[BoneIndex] ||
								!FingerBones[BoneIndex].IsValidToEvaluate() ||
								RefFingerDirComp[BoneIndex].IsNearlyZero() ||
								RefFingerCurlDirComp[BoneIndex].IsNearlyZero())
							{
								continue;
							}

							if (!bHasSolvedCurl[FingerIndex][SegmentIndex])
							{
								continue;
							}

							const FVector BaseDirComp = HandDeltaCS.RotateVector(RefFingerDirComp[BoneIndex]).GetSafeNormal();
							FVector CurlDirComp = HandDeltaCS.RotateVector(RefFingerCurlDirComp[BoneIndex]).GetSafeNormal();
							if (BaseDirComp.IsNearlyZero() || CurlDirComp.IsNearlyZero())
							{
								continue;
							}

							if (FingerIndex > 0 && CVarQuestFingerPreserveSpread.GetValueOnAnyThread() != 0)
							{
								FVector PalmCurlDirComp = CurrentHandBasisComp.RotateVector(FVector::UpVector).GetSafeNormal();
								PalmCurlDirComp = (PalmCurlDirComp - FVector::DotProduct(PalmCurlDirComp, BaseDirComp) * BaseDirComp).GetSafeNormal();
								if (!PalmCurlDirComp.IsNearlyZero())
								{
									if (FVector::DotProduct(PalmCurlDirComp, CurlDirComp) < 0.0f)
									{
										PalmCurlDirComp *= -1.0f;
									}
									CurlDirComp = PalmCurlDirComp;
								}
							}

							FVector BendAxisComp = FVector::CrossProduct(BaseDirComp, CurlDirComp).GetSafeNormal();
							if (BendAxisComp.IsNearlyZero())
							{
								BendAxisComp = CurrentHandBasisComp.RotateVector(FVector::RightVector).GetSafeNormal();
							}
							if (BendAxisComp.IsNearlyZero())
							{
								continue;
							}

							const float SegmentWeight = FingerIndex == 0
								? ThumbSegmentScale[SegmentIndex]
								: FingerSegmentScale[SegmentIndex];
							const float MaxCurlDeg = FingerIndex == 0 ? ThumbMaxCurlDeg : FingerMaxCurlDeg;
							float Curl01 = SolvedCurl01[FingerIndex][SegmentIndex];
							float SegmentStrength = Strength;
							if (FingerIndex == 0)
							{
								if (bUseThumbChainCurl)
								{
									SegmentStrength *= FMath::Clamp(CVarQuestThumbCurlStrength.GetValueOnAnyThread(), 0.0f, 2.0f);
								}
								Curl01 = FMath::Max(Curl01, LoggedThumbClosedFistAlpha);
								LoggedThumbCurl01[SegmentIndex] = Curl01;
							}
							else
							{
								Curl01 = FMath::Lerp(Curl01, 1.0f, FingerClosedFistAlpha[FingerIndex]);
								LoggedFingerCurl01[FingerIndex - 1] = FMath::Max(LoggedFingerCurl01[FingerIndex - 1], Curl01);
							}

							const float CurlDeg = Curl01 * MaxCurlDeg * SegmentWeight * SegmentStrength;
							const FQuat BaseRotCS = (HandDeltaCS * RefFingerComp[BoneIndex]).GetNormalized();

							if (CurlDeg <= KINDA_SMALL_NUMBER)
							{
								UpdateSmoothedRotation(bHasSmoothedFingerRotCS[BoneIndex], SmoothedFingerRotCS[BoneIndex], BaseRotCS, Alpha);
								ApplyRotationCS(CSPose, FingerBones[BoneIndex], SmoothedFingerRotCS[BoneIndex]);
								++AppliedFingerBoneCount;
								if (FingerIndex == 0)
								{
									++AppliedThumbBoneCount;
								}
								bAppliedAnyCurl = true;
								continue;
							}

							const FVector CandidateA = FQuat(BendAxisComp, FMath::DegreesToRadians(CurlDeg)).RotateVector(BaseDirComp).GetSafeNormal();
							const FVector CandidateB = FQuat(BendAxisComp, FMath::DegreesToRadians(-CurlDeg)).RotateVector(BaseDirComp).GetSafeNormal();
							const FVector TargetDirComp = FVector::DotProduct(CandidateA, CurlDirComp) >= FVector::DotProduct(CandidateB, CurlDirComp)
								? CandidateA
								: CandidateB;
							const FQuat TargetRotCS = (FQuat::FindBetweenNormals(BaseDirComp, TargetDirComp) * BaseRotCS).GetNormalized();

							UpdateSmoothedRotation(bHasSmoothedFingerRotCS[BoneIndex], SmoothedFingerRotCS[BoneIndex], TargetRotCS, Alpha);
							ApplyRotationCS(CSPose, FingerBones[BoneIndex], SmoothedFingerRotCS[BoneIndex]);
							++AppliedFingerBoneCount;
							if (FingerIndex == 0)
							{
								++AppliedThumbBoneCount;
							}
							bAppliedAnyCurl = true;
						}
					}

					if (bAppliedAnyCurl)
					{
						LogQuestFingerSolve(
							bUsePalmLocalHinge ? TEXT("palmLocalHinge") : TEXT("curlOnly"),
							AppliedFingerBoneCount + AppliedMetacarpalBoneCount,
							bHasQuestFingerAlignmentComp);
						return;
					}
				}
			}
		}
	}

	const FBoneReference& SegmentHandBone = bIsLeft ? HandL : HandR;
	const FQuat& SegmentRefHandComp = bIsLeft ? RefHandCompL : RefHandCompR;
	FQuat SegmentCurrentHandRotCS = SegmentRefHandComp;
	if (SegmentHandBone.IsValidToEvaluate())
	{
		SegmentCurrentHandRotCS = CSPose.GetComponentSpaceTransform(SegmentHandBone.CachedCompactPoseIndex).GetRotation().GetNormalized();
	}

	auto TryGetLiveParentDeltaCS = [&](const FQuat& ParentReferenceComp, const FQuat& ParentLiveComp, FQuat& OutParentDeltaCS) -> bool
	{
		const FQuat ParentReference = ParentReferenceComp.GetNormalized();
		const FQuat ParentLive = ParentLiveComp.GetNormalized();
		OutParentDeltaCS = (ParentLive * ParentReference.Inverse()).GetNormalized();
		return true;
	};

	for (int32 FingerIndex = 1; FingerIndex < QuestFingerCount; ++FingerIndex)
	{
		const int32 MetacarpalIndex = QuestFingerMetacarpalBoneIndex(FingerIndex);
		const int32 SmoothIndex = MediaPipeQuestStateMetacarpalOffset + MetacarpalIndex;
		if (!bHasRefFingerMetacarpal[MetacarpalIndex] ||
			!FingerMetacarpalBones[MetacarpalIndex].IsValidToEvaluate() ||
			RefFingerMetacarpalDirComp[MetacarpalIndex].IsNearlyZero() ||
			SmoothIndex < 0 ||
			SmoothIndex >= MediaPipeQuestStateFingerBoneCount)
		{
			continue;
		}

		FVector SegmentComp = FVector::ZeroVector;
		if (!TryMapQuestSegmentWorldToComponent(GetQuestFingerMetacarpalSegmentWorld(Snapshot, bIsLeft, FingerIndex), SegmentComp, false))
		{
			continue;
		}
		if (SegmentComp.IsNearlyZero())
		{
			continue;
		}

		FQuat ParentDeltaCS = FQuat::Identity;
		if (!TryGetLiveParentDeltaCS(SegmentRefHandComp, SegmentCurrentHandRotCS, ParentDeltaCS))
		{
			continue;
		}

		const FQuat TargetRotCS = RetargetQuestSegmentDirectionToBone(
			ParentDeltaCS,
			RefFingerMetacarpalComp[MetacarpalIndex],
			RefFingerMetacarpalDirComp[MetacarpalIndex],
			SegmentComp);
		UpdateSmoothedRotation(bHasSmoothedFingerRotCS[SmoothIndex], SmoothedFingerRotCS[SmoothIndex], TargetRotCS, Alpha);
		ApplyRotationCS(CSPose, FingerMetacarpalBones[MetacarpalIndex], SmoothedFingerRotCS[SmoothIndex]);
		++AppliedMetacarpalBoneCount;
	}

	for (int32 FingerIndex = 0; FingerIndex < QuestFingerCount; ++FingerIndex)
	{
		for (int32 SegmentIndex = 0; SegmentIndex < QuestFingerSegmentsPerFinger; ++SegmentIndex)
		{
			const int32 BoneIndex = QuestFingerBoneIndex(FingerIndex, SegmentIndex);
			if (!bHasRefFinger[BoneIndex] || !FingerBones[BoneIndex].IsValidToEvaluate())
			{
				continue;
			}

			const int32 StartKey = static_cast<int32>(QuestFingerStartKeypoint(FingerIndex, SegmentIndex));
			const int32 EndKey = static_cast<int32>(QuestFingerEndKeypoint(FingerIndex, SegmentIndex));
			if (StartKey < 0 || StartKey >= QuestHandKeypointCount || EndKey < 0 || EndKey >= QuestHandKeypointCount)
			{
				continue;
			}

			FVector SegmentComp = FVector::ZeroVector;
			if (!TryMapQuestSegmentWorldToComponent((Positions[EndKey] - Positions[StartKey]).GetSafeNormal(), SegmentComp, false))
			{
				continue;
			}
			if (SegmentComp.IsNearlyZero() || RefFingerDirComp[BoneIndex].IsNearlyZero())
			{
				continue;
			}

			FQuat ParentReferenceComp = SegmentRefHandComp;
			FQuat ParentLiveComp = SegmentCurrentHandRotCS;
			bool bHasValidParent = true;
			if (FingerIndex == 0)
			{
				if (SegmentIndex > 0)
				{
					const int32 ParentBoneIndex = QuestFingerBoneIndex(FingerIndex, SegmentIndex - 1);
					if (!bHasRefFinger[ParentBoneIndex] || !FingerBones[ParentBoneIndex].IsValidToEvaluate())
					{
						bHasValidParent = false;
					}
					else
					{
						ParentReferenceComp = RefFingerComp[ParentBoneIndex];
						ParentLiveComp = CSPose.GetComponentSpaceTransform(FingerBones[ParentBoneIndex].CachedCompactPoseIndex).GetRotation().GetNormalized();
					}
				}
			}
			else if (SegmentIndex == 0)
			{
				const int32 MetacarpalIndex = QuestFingerMetacarpalBoneIndex(FingerIndex);
				if (bHasRefFingerMetacarpal[MetacarpalIndex] && FingerMetacarpalBones[MetacarpalIndex].IsValidToEvaluate())
				{
					ParentReferenceComp = RefFingerMetacarpalComp[MetacarpalIndex];
					ParentLiveComp = CSPose.GetComponentSpaceTransform(FingerMetacarpalBones[MetacarpalIndex].CachedCompactPoseIndex).GetRotation().GetNormalized();
				}
			}
			else
			{
				const int32 ParentBoneIndex = QuestFingerBoneIndex(FingerIndex, SegmentIndex - 1);
				if (!bHasRefFinger[ParentBoneIndex] || !FingerBones[ParentBoneIndex].IsValidToEvaluate())
				{
					bHasValidParent = false;
				}
				else
				{
					ParentReferenceComp = RefFingerComp[ParentBoneIndex];
					ParentLiveComp = CSPose.GetComponentSpaceTransform(FingerBones[ParentBoneIndex].CachedCompactPoseIndex).GetRotation().GetNormalized();
				}
			}

			FQuat ParentDeltaCS = FQuat::Identity;
			if (!bHasValidParent || !TryGetLiveParentDeltaCS(ParentReferenceComp, ParentLiveComp, ParentDeltaCS))
			{
				continue;
			}

			FVector RetargetSegmentComp = SegmentComp;
			if (SegmentIndex == QuestFingerSegmentsPerFinger - 1)
			{
				const float DistalDirectionWeight = FingerIndex == 0
					? FMath::Clamp(ThumbSegmentScale[SegmentIndex], 0.0f, 1.0f)
					: FMath::Clamp(FingerSegmentScale[SegmentIndex], 0.0f, 1.0f);
				const FVector ParentDrivenRefSegmentComp = ParentDeltaCS.RotateVector(RefFingerDirComp[BoneIndex]).GetSafeNormal();
				if (!ParentDrivenRefSegmentComp.IsNearlyZero())
				{
					RetargetSegmentComp = FMath::Lerp(ParentDrivenRefSegmentComp, SegmentComp, DistalDirectionWeight).GetSafeNormal();
				}
			}

			const FQuat TargetRotCS = RetargetQuestSegmentDirectionToBone(
				ParentDeltaCS,
				RefFingerComp[BoneIndex],
				RefFingerDirComp[BoneIndex],
				RetargetSegmentComp);
			UpdateSmoothedRotation(bHasSmoothedFingerRotCS[BoneIndex], SmoothedFingerRotCS[BoneIndex], TargetRotCS, Alpha);
			ApplyRotationCS(CSPose, FingerBones[BoneIndex], SmoothedFingerRotCS[BoneIndex]);
			++AppliedFingerBoneCount;
			if (FingerIndex == 0)
			{
				++AppliedThumbBoneCount;
			}
		}
	}

	// Segment directions are already mapped into the avatar component frame. Applying
	// the MediaPipe-hand alignment here double-rotates Quest-driven hands.
	LogQuestFingerSolve(TEXT("segmentDirection"), AppliedFingerBoneCount, false);
}
