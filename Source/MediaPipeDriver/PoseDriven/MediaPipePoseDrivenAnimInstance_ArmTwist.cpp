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

void FAnimNode_MediaPipePoseDriven::DriveArmTwistBonesCS(FCSPose<FCompactPose>& CSPose, float DeltaSeconds)
{
	(void)DeltaSeconds;

	enum EStandardArmTwistHelperIndex : int32
	{
		UpperArmTwist01Index = 0,
		UpperArmTwist02Index,
		LowerArmTwist01Index,
		LowerArmTwist02Index
	};

	enum EMetaHumanArmHelperOffset : int32
	{
		MetaHumanClavicleHelperOffset = 0,
		MetaHumanUpperArmHelperOffset = MetaHumanClavicleHelperOffset + MediaPipeMetaHumanClavicleHelperCount,
		MetaHumanLowerArmHelperOffset = MetaHumanUpperArmHelperOffset + MediaPipeMetaHumanUpperArmHelperCount
	};
	constexpr int32 MetaHumanClavicleChildHelperCount = 2;
	constexpr int32 MetaHumanClaviclePecHelperIndex = 2;
	constexpr int32 MetaHumanUpperArmTwistCor01Index = 0;
	constexpr int32 MetaHumanUpperArmTwist02ChildStartIndex = 1;
	constexpr int32 MetaHumanUpperArmTwist02ChildCount = 3;
	constexpr int32 MetaHumanUpperArmCorrectiveRootIndex = 4;
	constexpr int32 MetaHumanUpperArmCorrectiveChildStartIndex = 5;
	constexpr int32 MetaHumanUpperArmCorrectiveChildCount = 4;
	constexpr int32 MetaHumanLowerArmCorrectiveRootIndex = 0;
	constexpr int32 MetaHumanLowerArmCorrectiveChildStartIndex = 1;
	constexpr int32 MetaHumanLowerArmCorrectiveChildCount = 4;
	constexpr int32 MetaHumanLowerArmWristHelperStartIndex = 5;
	constexpr int32 MetaHumanLowerArmWristHelperCount = 2;

	auto ResetStandardTwistSmoothing = [](FMediaPipeArmSolverState& State, int32 Index)
	{
		if (Index >= 0 && Index < MediaPipeArmStandardTwistHelperCount)
		{
			State.bHasSmoothedArmTwistHelperCS[Index] = false;
			State.SmoothedArmTwistHelperCS[Index] = FTransform::Identity;
		}
	};

	auto ResetAllStandardTwistSmoothing = [&](FMediaPipeArmSolverState& State)
	{
		for (int32 Index = 0; Index < MediaPipeArmStandardTwistHelperCount; ++Index)
		{
			ResetStandardTwistSmoothing(State, Index);
		}
	};

	auto ResetMetaHumanHelperSmoothing = [](FMediaPipeArmSolverState& State, int32 Index)
	{
		if (Index >= 0 && Index < MediaPipeMetaHumanArmDeformationHelperCount)
		{
			State.bHasSmoothedMetaHumanArmHelperCS[Index] = false;
			State.SmoothedMetaHumanArmHelperCS[Index] = FTransform::Identity;
		}
	};

	auto ResetMetaHumanHelperSmoothingRange = [&](FMediaPipeArmSolverState& State, int32 StartIndex, int32 Count)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			ResetMetaHumanHelperSmoothing(State, StartIndex + Index);
		}
	};

	auto ResetAllMetaHumanHelperSmoothing = [&](FMediaPipeArmSolverState& State)
	{
		ResetMetaHumanHelperSmoothingRange(State, 0, MediaPipeMetaHumanArmDeformationHelperCount);
	};

	const bool bBodyFusionMetaHumanHelperWrite =
		ShouldUseBodyFusionPoseForEvaluation() &&
		TargetMetaHumanProfile.IsValidForPoseDriving();
	if (CVarMediaPipeDriveArmTwistBones.GetValueOnAnyThread() == 0 && !bBodyFusionMetaHumanHelperWrite)
	{
		ResetAllStandardTwistSmoothing(LeftArmState);
		ResetAllStandardTwistSmoothing(RightArmState);
		ResetAllMetaHumanHelperSmoothing(LeftArmState);
		ResetAllMetaHumanHelperSmoothing(RightArmState);
		return;
	}

	const bool bDriveMetaHumanArmHelpers =
		CVarMediaPipeDriveMetaHumanArmHelpers.GetValueOnAnyThread() != 0 ||
		bBodyFusionMetaHumanHelperWrite;
	if (!bDriveMetaHumanArmHelpers)
	{
		ResetAllMetaHumanHelperSmoothing(LeftArmState);
		ResetAllMetaHumanHelperSmoothing(RightArmState);
	}

	const bool bQuestRollOwnsUpperArmTwistHelpers =
		(CVarQuestWristUpperArmRollDriveTwistHelpers.GetValueOnAnyThread() != 0 &&
		 CVarQuestWristUpperArmTwistBlend.GetValueOnAnyThread() > KINDA_SMALL_NUMBER &&
		 CVarQuestWristUpperArmMaxTwistDegrees.GetValueOnAnyThread() > KINDA_SMALL_NUMBER) ||
		(CVarQuestWristDriveTwistCorrection.GetValueOnAnyThread() != 0 &&
		 CVarQuestWristTwistCorrectionBlend.GetValueOnAnyThread() > KINDA_SMALL_NUMBER &&
		 CVarQuestWristTwistCorrectionUpperArmShare.GetValueOnAnyThread() > KINDA_SMALL_NUMBER);
	const bool bQuestRollOwnsForearmTwistHelpers =
		(CVarQuestWristForearmRollDriveTwistHelpers.GetValueOnAnyThread() != 0 &&
		 CVarQuestWristForearmTwistBlend.GetValueOnAnyThread() > KINDA_SMALL_NUMBER &&
		 CVarQuestWristForearmMaxTwistDegrees.GetValueOnAnyThread() > KINDA_SMALL_NUMBER) ||
		(CVarQuestWristDriveTwistCorrection.GetValueOnAnyThread() != 0 &&
		 CVarQuestWristTwistCorrectionBlend.GetValueOnAnyThread() > KINDA_SMALL_NUMBER);

	auto ApplyInterpolatedTwistWithSourceParent = [&](
		const FBoneReference& TwistBone,
		const FBoneReference& ParentBone,
		const FBoneReference& SourceParentBone,
		const FBoneReference& SourceBone,
		const FTransform& RefParentComp,
		const FTransform& RefTwistComp,
		const FTransform& RefSourceParentComp,
		const FTransform& RefSourceComp,
		FMediaPipeArmSolverState& State,
		int32 TwistSmoothingIndex)
	{
		(void)State;
		(void)TwistSmoothingIndex;

		if (!TwistBone.IsValidToEvaluate() ||
			!ParentBone.IsValidToEvaluate() ||
			!SourceParentBone.IsValidToEvaluate() ||
			!SourceBone.IsValidToEvaluate())
		{
			return;
		}

		FMediaPipeArmTwistInput TwistInput;
		TwistInput.ParentComponent = CSPose.GetComponentSpaceTransform(ParentBone.CachedCompactPoseIndex);
		TwistInput.SourceParentComponent = CSPose.GetComponentSpaceTransform(SourceParentBone.CachedCompactPoseIndex);
		TwistInput.SourceComponent = CSPose.GetComponentSpaceTransform(SourceBone.CachedCompactPoseIndex);
		TwistInput.ReferenceParentComponent = RefParentComp;
		TwistInput.ReferenceSourceParentComponent = RefSourceParentComp;
		TwistInput.ReferenceTwistComponent = RefTwistComp;
		TwistInput.ReferenceSourceComponent = RefSourceComp;

		FMediaPipeArmTwistResult TwistResult;
		if (!FMediaPipeArmTwistSolver::BuildInterpolatedTwistTransform(TwistInput, TwistResult))
		{
			return;
		}

		// Match OculusXR: twist helpers are derived from the current frame pose only.
		const FBoneTransform BoneTransform(TwistBone.CachedCompactPoseIndex, TwistResult.TwistComponent);
		CSPose.SafeSetCSBoneTransforms(MakeArrayView(&BoneTransform, 1));
	};

	auto ApplyInterpolatedTwist = [&](
		const FBoneReference& TwistBone,
		const FBoneReference& ParentBone,
		const FBoneReference& SourceBone,
		const FTransform& RefParentComp,
		const FTransform& RefTwistComp,
		const FTransform& RefSourceComp,
		FMediaPipeArmSolverState& State,
		int32 TwistSmoothingIndex)
	{
		ApplyInterpolatedTwistWithSourceParent(
			TwistBone,
			ParentBone,
			ParentBone,
			SourceBone,
			RefParentComp,
			RefTwistComp,
			RefParentComp,
			RefSourceComp,
			State,
			TwistSmoothingIndex);
	};

	auto ApplyMetaHumanHelperGroupWithSourceParent = [&](
		const FBoneReference* HelperBones,
		const FTransform* RefHelperTransforms,
		int32 HelperCount,
		const FBoneReference& ParentBone,
		const FBoneReference& SourceParentBone,
		const FBoneReference& SourceBone,
		const FTransform& RefParentComp,
		const FTransform& RefSourceParentComp,
		const FTransform& RefSourceComp,
		FMediaPipeArmSolverState& State,
		int32 MetaHumanHelperOffset)
	{
		if (!bDriveMetaHumanArmHelpers)
		{
			return;
		}

		// OculusXR treats unmapped sidecar bones as interpolated helpers between mapped endpoints.
		for (int32 Index = 0; Index < HelperCount; ++Index)
		{
			ApplyInterpolatedTwistWithSourceParent(
				HelperBones[Index],
				ParentBone,
				SourceParentBone,
				SourceBone,
				RefParentComp,
				RefHelperTransforms[Index],
				RefSourceParentComp,
				RefSourceComp,
				State,
				MediaPipeArmStandardTwistHelperCount + MetaHumanHelperOffset + Index);
		}
	};

	auto ApplyMetaHumanHelperGroup = [&](
		const FBoneReference* HelperBones,
		const FTransform* RefHelperTransforms,
		int32 HelperCount,
		const FBoneReference& ParentBone,
		const FBoneReference& SourceBone,
		const FTransform& RefParentComp,
		const FTransform& RefSourceComp,
		FMediaPipeArmSolverState& State,
		int32 MetaHumanHelperOffset)
	{
		ApplyMetaHumanHelperGroupWithSourceParent(
			HelperBones,
			RefHelperTransforms,
			HelperCount,
			ParentBone,
			ParentBone,
			SourceBone,
			RefParentComp,
			RefParentComp,
			RefSourceComp,
			State,
			MetaHumanHelperOffset);
	};

	auto ApplyMetaHumanUpperArmHelpers = [&](
		const FBoneReference* HelperBones,
		const FTransform* RefHelperTransforms,
		const FBoneReference& UpperArmBone,
		const FBoneReference& UpperArmTwist01Bone,
		const FBoneReference& UpperArmTwist02Bone,
		const FBoneReference& LowerArmBone,
		const FTransform& RefUpperArmComp,
		const FTransform& RefUpperArmTwist01Comp,
		const FTransform& RefUpperArmTwist02Comp,
		const FTransform& RefLowerArmComp,
		FMediaPipeArmSolverState& State)
	{
		if (!bDriveMetaHumanArmHelpers)
		{
			return;
		}

		ApplyMetaHumanHelperGroup(
			&HelperBones[MetaHumanUpperArmTwistCor01Index],
			&RefHelperTransforms[MetaHumanUpperArmTwistCor01Index],
			1,
			UpperArmTwist01Bone,
			LowerArmBone,
			RefUpperArmTwist01Comp,
			RefLowerArmComp,
			State,
			MetaHumanUpperArmHelperOffset + MetaHumanUpperArmTwistCor01Index);
		ApplyMetaHumanHelperGroup(
			&HelperBones[MetaHumanUpperArmTwist02ChildStartIndex],
			&RefHelperTransforms[MetaHumanUpperArmTwist02ChildStartIndex],
			MetaHumanUpperArmTwist02ChildCount,
			UpperArmTwist02Bone,
			LowerArmBone,
			RefUpperArmTwist02Comp,
			RefLowerArmComp,
			State,
			MetaHumanUpperArmHelperOffset + MetaHumanUpperArmTwist02ChildStartIndex);
		ApplyMetaHumanHelperGroup(
			&HelperBones[MetaHumanUpperArmCorrectiveRootIndex],
			&RefHelperTransforms[MetaHumanUpperArmCorrectiveRootIndex],
			1,
			UpperArmBone,
			LowerArmBone,
			RefUpperArmComp,
			RefLowerArmComp,
			State,
			MetaHumanUpperArmHelperOffset + MetaHumanUpperArmCorrectiveRootIndex);
		ApplyMetaHumanHelperGroupWithSourceParent(
			&HelperBones[MetaHumanUpperArmCorrectiveChildStartIndex],
			&RefHelperTransforms[MetaHumanUpperArmCorrectiveChildStartIndex],
			MetaHumanUpperArmCorrectiveChildCount,
			HelperBones[MetaHumanUpperArmCorrectiveRootIndex],
			UpperArmBone,
			LowerArmBone,
			RefHelperTransforms[MetaHumanUpperArmCorrectiveRootIndex],
			RefUpperArmComp,
			RefLowerArmComp,
			State,
			MetaHumanUpperArmHelperOffset + MetaHumanUpperArmCorrectiveChildStartIndex);
	};

	auto ApplyMetaHumanLowerArmHelpers = [&](
		const FBoneReference* HelperBones,
		const FTransform* RefHelperTransforms,
		const FBoneReference& LowerArmBone,
		const FBoneReference& HandBone,
		const FTransform& RefLowerArmComp,
		const FTransform& RefHandComp,
		FMediaPipeArmSolverState& State)
	{
		if (!bDriveMetaHumanArmHelpers)
		{
			return;
		}

		ApplyMetaHumanHelperGroup(
			&HelperBones[MetaHumanLowerArmCorrectiveRootIndex],
			&RefHelperTransforms[MetaHumanLowerArmCorrectiveRootIndex],
			1,
			LowerArmBone,
			HandBone,
			RefLowerArmComp,
			RefHandComp,
			State,
			MetaHumanLowerArmHelperOffset + MetaHumanLowerArmCorrectiveRootIndex);
		ApplyMetaHumanHelperGroupWithSourceParent(
			&HelperBones[MetaHumanLowerArmCorrectiveChildStartIndex],
			&RefHelperTransforms[MetaHumanLowerArmCorrectiveChildStartIndex],
			MetaHumanLowerArmCorrectiveChildCount,
			HelperBones[MetaHumanLowerArmCorrectiveRootIndex],
			LowerArmBone,
			HandBone,
			RefHelperTransforms[MetaHumanLowerArmCorrectiveRootIndex],
			RefLowerArmComp,
			RefHandComp,
			State,
			MetaHumanLowerArmHelperOffset + MetaHumanLowerArmCorrectiveChildStartIndex);
		ApplyMetaHumanHelperGroup(
			&HelperBones[MetaHumanLowerArmWristHelperStartIndex],
			&RefHelperTransforms[MetaHumanLowerArmWristHelperStartIndex],
			MetaHumanLowerArmWristHelperCount,
			LowerArmBone,
			HandBone,
			RefLowerArmComp,
			RefHandComp,
			State,
			MetaHumanLowerArmHelperOffset + MetaHumanLowerArmWristHelperStartIndex);
	};

	ApplyMetaHumanHelperGroup(
		MetaHumanClavicleHelpersL,
		RefMetaHumanClavicleHelperTransformsCompL,
		MetaHumanClavicleChildHelperCount,
		ClavicleL,
		UpperArmL,
		RefClavTransformCompL,
		RefUpperArmTransformCompL,
		LeftArmState,
		MetaHumanClavicleHelperOffset);
	if (bDriveMetaHumanArmHelpers)
	{
		ApplyInterpolatedTwist(
			MetaHumanClavicleHelpersL[MetaHumanClaviclePecHelperIndex],
			Spine05,
			ClavicleL,
			RefSpine05TransformComp,
			RefMetaHumanClavicleHelperTransformsCompL[MetaHumanClaviclePecHelperIndex],
			RefClavTransformCompL,
			LeftArmState,
			MediaPipeArmStandardTwistHelperCount + MetaHumanClavicleHelperOffset + MetaHumanClaviclePecHelperIndex);
	}
	if (!bQuestRollOwnsUpperArmTwistHelpers)
	{
		ApplyInterpolatedTwist(UpperArmTwist01L, UpperArmL, LowerArmL, RefUpperArmTransformCompL, RefUpperArmTwist01TransformCompL, RefLowerArmTransformCompL, LeftArmState, UpperArmTwist01Index);
		ApplyInterpolatedTwist(UpperArmTwist02L, UpperArmL, LowerArmL, RefUpperArmTransformCompL, RefUpperArmTwist02TransformCompL, RefLowerArmTransformCompL, LeftArmState, UpperArmTwist02Index);
		ApplyMetaHumanUpperArmHelpers(
			MetaHumanUpperArmHelpersL,
			RefMetaHumanUpperArmHelperTransformsCompL,
			UpperArmL,
			UpperArmTwist01L,
			UpperArmTwist02L,
			LowerArmL,
			RefUpperArmTransformCompL,
			RefUpperArmTwist01TransformCompL,
			RefUpperArmTwist02TransformCompL,
			RefLowerArmTransformCompL,
			LeftArmState);
	}
	else
	{
		ResetStandardTwistSmoothing(LeftArmState, UpperArmTwist01Index);
		ResetStandardTwistSmoothing(LeftArmState, UpperArmTwist02Index);
		ResetMetaHumanHelperSmoothingRange(LeftArmState, MetaHumanUpperArmHelperOffset, MediaPipeMetaHumanUpperArmHelperCount);
	}
	if (!bQuestRollOwnsForearmTwistHelpers)
	{
		ApplyInterpolatedTwist(LowerArmTwist01L, LowerArmL, HandL, RefLowerArmTransformCompL, RefLowerArmTwist01TransformCompL, RefHandTransformCompL, LeftArmState, LowerArmTwist01Index);
		ApplyInterpolatedTwist(LowerArmTwist02L, LowerArmL, HandL, RefLowerArmTransformCompL, RefLowerArmTwist02TransformCompL, RefHandTransformCompL, LeftArmState, LowerArmTwist02Index);
		ApplyMetaHumanLowerArmHelpers(
			MetaHumanLowerArmHelpersL,
			RefMetaHumanLowerArmHelperTransformsCompL,
			LowerArmL,
			HandL,
			RefLowerArmTransformCompL,
			RefHandTransformCompL,
			LeftArmState);
	}
	else
	{
		ResetStandardTwistSmoothing(LeftArmState, LowerArmTwist01Index);
		ResetStandardTwistSmoothing(LeftArmState, LowerArmTwist02Index);
		ResetMetaHumanHelperSmoothingRange(LeftArmState, MetaHumanLowerArmHelperOffset, MediaPipeMetaHumanLowerArmHelperCount);
	}

	ApplyMetaHumanHelperGroup(
		MetaHumanClavicleHelpersR,
		RefMetaHumanClavicleHelperTransformsCompR,
		MetaHumanClavicleChildHelperCount,
		ClavicleR,
		UpperArmR,
		RefClavTransformCompR,
		RefUpperArmTransformCompR,
		RightArmState,
		MetaHumanClavicleHelperOffset);
	if (bDriveMetaHumanArmHelpers)
	{
		ApplyInterpolatedTwist(
			MetaHumanClavicleHelpersR[MetaHumanClaviclePecHelperIndex],
			Spine05,
			ClavicleR,
			RefSpine05TransformComp,
			RefMetaHumanClavicleHelperTransformsCompR[MetaHumanClaviclePecHelperIndex],
			RefClavTransformCompR,
			RightArmState,
			MediaPipeArmStandardTwistHelperCount + MetaHumanClavicleHelperOffset + MetaHumanClaviclePecHelperIndex);
	}
	if (!bQuestRollOwnsUpperArmTwistHelpers)
	{
		ApplyInterpolatedTwist(UpperArmTwist01R, UpperArmR, LowerArmR, RefUpperArmTransformCompR, RefUpperArmTwist01TransformCompR, RefLowerArmTransformCompR, RightArmState, UpperArmTwist01Index);
		ApplyInterpolatedTwist(UpperArmTwist02R, UpperArmR, LowerArmR, RefUpperArmTransformCompR, RefUpperArmTwist02TransformCompR, RefLowerArmTransformCompR, RightArmState, UpperArmTwist02Index);
		ApplyMetaHumanUpperArmHelpers(
			MetaHumanUpperArmHelpersR,
			RefMetaHumanUpperArmHelperTransformsCompR,
			UpperArmR,
			UpperArmTwist01R,
			UpperArmTwist02R,
			LowerArmR,
			RefUpperArmTransformCompR,
			RefUpperArmTwist01TransformCompR,
			RefUpperArmTwist02TransformCompR,
			RefLowerArmTransformCompR,
			RightArmState);
	}
	else
	{
		ResetStandardTwistSmoothing(RightArmState, UpperArmTwist01Index);
		ResetStandardTwistSmoothing(RightArmState, UpperArmTwist02Index);
		ResetMetaHumanHelperSmoothingRange(RightArmState, MetaHumanUpperArmHelperOffset, MediaPipeMetaHumanUpperArmHelperCount);
	}
	if (!bQuestRollOwnsForearmTwistHelpers)
	{
		ApplyInterpolatedTwist(LowerArmTwist01R, LowerArmR, HandR, RefLowerArmTransformCompR, RefLowerArmTwist01TransformCompR, RefHandTransformCompR, RightArmState, LowerArmTwist01Index);
		ApplyInterpolatedTwist(LowerArmTwist02R, LowerArmR, HandR, RefLowerArmTransformCompR, RefLowerArmTwist02TransformCompR, RefHandTransformCompR, RightArmState, LowerArmTwist02Index);
		ApplyMetaHumanLowerArmHelpers(
			MetaHumanLowerArmHelpersR,
			RefMetaHumanLowerArmHelperTransformsCompR,
			LowerArmR,
			HandR,
			RefLowerArmTransformCompR,
			RefHandTransformCompR,
			RightArmState);
	}
	else
	{
		ResetStandardTwistSmoothing(RightArmState, LowerArmTwist01Index);
		ResetStandardTwistSmoothing(RightArmState, LowerArmTwist02Index);
		ResetMetaHumanHelperSmoothingRange(RightArmState, MetaHumanLowerArmHelperOffset, MediaPipeMetaHumanLowerArmHelperCount);
	}
}
