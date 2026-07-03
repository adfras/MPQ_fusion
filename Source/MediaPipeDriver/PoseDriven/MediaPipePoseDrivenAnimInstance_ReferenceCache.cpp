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

bool FAnimNode_MediaPipePoseDriven::BuildReferencePoseCache(const FBoneContainer& RequiredBones)
{
	bHasRefClavL = false;
	bHasRefClavR = false;
	bHasRefArmL = false;
	bHasRefArmR = false;
	RefLowerArmTwist01CompL = FQuat::Identity;
	RefLowerArmTwist02CompL = FQuat::Identity;
	RefUpperArmTwist01CompL = FQuat::Identity;
	RefUpperArmTwist02CompL = FQuat::Identity;
	RefLowerArmTwist01CompR = FQuat::Identity;
	RefLowerArmTwist02CompR = FQuat::Identity;
	RefUpperArmTwist01CompR = FQuat::Identity;
	RefUpperArmTwist02CompR = FQuat::Identity;
	RefUpperArmTransformCompL = FTransform::Identity;
	RefLowerArmTransformCompL = FTransform::Identity;
	RefHandTransformCompL = FTransform::Identity;
	RefClavTransformCompL = FTransform::Identity;
	RefSpine05TransformComp = FTransform::Identity;
	RefUpperArmTwist01TransformCompL = FTransform::Identity;
	RefUpperArmTwist02TransformCompL = FTransform::Identity;
	RefLowerArmTwist01TransformCompL = FTransform::Identity;
	RefLowerArmTwist02TransformCompL = FTransform::Identity;
	RefUpperArmTransformCompR = FTransform::Identity;
	RefLowerArmTransformCompR = FTransform::Identity;
	RefHandTransformCompR = FTransform::Identity;
	RefClavTransformCompR = FTransform::Identity;
	RefUpperArmTwist01TransformCompR = FTransform::Identity;
	RefUpperArmTwist02TransformCompR = FTransform::Identity;
	RefLowerArmTwist01TransformCompR = FTransform::Identity;
	RefLowerArmTwist02TransformCompR = FTransform::Identity;
	for (int32 Index = 0; Index < MediaPipeMetaHumanClavicleHelperCount; ++Index)
	{
		RefMetaHumanClavicleHelperTransformsCompL[Index] = FTransform::Identity;
		RefMetaHumanClavicleHelperTransformsCompR[Index] = FTransform::Identity;
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanUpperArmHelperCount; ++Index)
	{
		RefMetaHumanUpperArmHelperTransformsCompL[Index] = FTransform::Identity;
		RefMetaHumanUpperArmHelperTransformsCompR[Index] = FTransform::Identity;
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanLowerArmHelperCount; ++Index)
	{
		RefMetaHumanLowerArmHelperTransformsCompL[Index] = FTransform::Identity;
		RefMetaHumanLowerArmHelperTransformsCompR[Index] = FTransform::Identity;
	}
	bHasRefLegL = false;
	bHasRefLegR = false;
	bHasRefFootContactL = false;
	bHasRefFootContactR = false;
	RefBallPosCompL = FVector::ZeroVector;
	RefBallPosCompR = FVector::ZeroVector;
	RefFootFloorZComp = 0.0f;
	bHasRefFootFloorZ = false;
	BodyState.ReferenceRigHipHeightCm = 0.0f;
	bHasReferencePose = false;
	BodyState.bHasReferenceHipHeight = false;
	BodyState.bHasSmoothedPelvisOffset = false;
	BodyState.SmoothedPelvisOffsetComp = FVector::ZeroVector;
	LeftArmState.bHasSmoothedArmIK = false;
	RightArmState.bHasSmoothedArmIK = false;
	LeftLegState.bHasSmoothedLegPlane = false;
	RightLegState.bHasSmoothedLegPlane = false;
	ResetFootPlantState();
	ResetRotationSmoothing();
	for (uint8& B : EverMeasured)
	{
		B = 0;
	}
	bHasLastPoseTimestamp = false;
	LastPoseTimestampUs = 0;
	NumSpineBones = 0;
	for (uint8& Slot : SpineBoneSlots)
	{
		Slot = 0;
	}
	RefHandVisualPalmBasisCompL = FQuat::Identity;
	RefHandVisualPalmBasisCompR = FQuat::Identity;
	bHasRefHandCameraBasisL = false;
	bHasRefHandCameraBasisR = false;
	RefHandCameraBasisCompL = FQuat::Identity;
	RefHandCameraBasisCompR = FQuat::Identity;
	RefHandCameraThumbUpDotL = 0.0f;
	RefHandCameraThumbUpDotR = 0.0f;
	RefNeckPosComp = FVector::ZeroVector;
	RefNeck02PosComp = FVector::ZeroVector;
	RefHeadPosComp = FVector::ZeroVector;
	RefChestPosComp = FVector::ZeroVector;
	bHasRefNeck02PosComp = false;
	bHasRefChestPosComp = false;
	for (FVector& RefSpineTranslation : RefSpineTranslationComp)
	{
		RefSpineTranslation = FVector::ZeroVector;
	}
	for (int32 Index = 0; Index < QuestFingerBoneCount; ++Index)
	{
		bHasRefFingerL[Index] = false;
		bHasRefFingerR[Index] = false;
		RefFingerCompL[Index] = FQuat::Identity;
		RefFingerCompR[Index] = FQuat::Identity;
		RefFingerBasisCompL[Index] = FQuat::Identity;
		RefFingerBasisCompR[Index] = FQuat::Identity;
		RefFingerDirCompL[Index] = FVector::ForwardVector;
		RefFingerDirCompR[Index] = FVector::ForwardVector;
	}
	for (int32 Index = 0; Index < QuestFingerMetacarpalBoneCount; ++Index)
	{
		bHasRefFingerMetacarpalL[Index] = false;
		bHasRefFingerMetacarpalR[Index] = false;
		RefFingerMetacarpalCompL[Index] = FQuat::Identity;
		RefFingerMetacarpalCompR[Index] = FQuat::Identity;
		RefFingerMetacarpalDirCompL[Index] = FVector::ForwardVector;
		RefFingerMetacarpalDirCompR[Index] = FVector::ForwardVector;
	}

	// Cached reference right vectors (component space) for limb twist stabilization.
	FVector RefHipRightComp = FVector::RightVector;
	float RefHipMidZComp = 0.0f;
	bool bHasRefHipMidZComp = false;

	// Build a ref pose CSPose so we can query component-space transforms/positions.
	FCompactPose RefPose;
	RefPose.SetBoneContainer(&RequiredBones);
	RefPose.ResetToRefPose();

	FCSPose<FCompactPose> CSPose;
	CSPose.InitPose(MoveTemp(RefPose));

	auto GetCS = [&](const FBoneReference& Bone, FTransform& OutCS) -> bool
	{
		if (!Bone.IsValidToEvaluate(RequiredBones))
		{
			return false;
		}
		const FCompactPoseBoneIndex Idx = Bone.GetCompactPoseIndex(RequiredBones);
		OutCS = CSPose.GetComponentSpaceTransform(Idx);
		return true;
	};

	// --- Spine chain (pelvis + up to 5 spines + neck + head) ---
	{
		FTransform PelvisCS, Spine1CS, Spine2CS, Spine3CS, Spine4CS, Spine5CS, NeckCS, Neck02CS, HeadCS;
		if (!GetCS(Pelvis, PelvisCS) || !GetCS(Neck, NeckCS) || !GetCS(Head, HeadCS))
		{
			return false;
		}
		const bool bHasNeck02 = GetCS(Neck02, Neck02CS);

		RefPelvisComp = PelvisCS.GetRotation();
		RefPelvisTranslationComp = PelvisCS.GetTranslation();
		RefNeckComp = NeckCS.GetRotation();
		RefNeck02Comp = bHasNeck02 ? Neck02CS.GetRotation() : FQuat::Identity;
		RefHeadComp = HeadCS.GetRotation();

		const bool bHasS1 = GetCS(Spine01, Spine1CS);
		const bool bHasS2 = GetCS(Spine02, Spine2CS);
		const bool bHasS3 = GetCS(Spine03, Spine3CS);
		const bool bHasS4 = GetCS(Spine04, Spine4CS);
		const bool bHasS5 = GetCS(Spine05, Spine5CS);

		NumSpineBones = 0;
		if (bHasS1) { SpineBoneSlots[NumSpineBones] = 1; RefSpineComp[NumSpineBones] = Spine1CS.GetRotation(); RefSpineTranslationComp[NumSpineBones] = Spine1CS.GetTranslation(); NumSpineBones++; }
		if (bHasS2) { SpineBoneSlots[NumSpineBones] = 2; RefSpineComp[NumSpineBones] = Spine2CS.GetRotation(); RefSpineTranslationComp[NumSpineBones] = Spine2CS.GetTranslation(); NumSpineBones++; }
		if (bHasS3) { SpineBoneSlots[NumSpineBones] = 3; RefSpineComp[NumSpineBones] = Spine3CS.GetRotation(); RefSpineTranslationComp[NumSpineBones] = Spine3CS.GetTranslation(); NumSpineBones++; }
		if (bHasS4) { SpineBoneSlots[NumSpineBones] = 4; RefSpineComp[NumSpineBones] = Spine4CS.GetRotation(); RefSpineTranslationComp[NumSpineBones] = Spine4CS.GetTranslation(); NumSpineBones++; }
		if (bHasS5) { SpineBoneSlots[NumSpineBones] = 5; RefSpineComp[NumSpineBones] = Spine5CS.GetRotation(); RefSpineTranslationComp[NumSpineBones] = Spine5CS.GetTranslation(); NumSpineBones++; }

		// Need thighs + clavicles to build a stable reference torso basis.
		FTransform ThighLCS, ThighRCS, ClavLCS, ClavRCS;
		if (!GetCS(ThighL, ThighLCS) || !GetCS(ThighR, ThighRCS) || !GetCS(ClavicleL, ClavLCS) || !GetCS(ClavicleR, ClavRCS))
		{
			return false;
		}

		const FVector PelvisPos = PelvisCS.GetTranslation();
		const FVector NeckPos = NeckCS.GetTranslation();
		const FVector Neck02Pos = bHasNeck02 ? Neck02CS.GetTranslation() : FVector::ZeroVector;
		const FVector HeadPos = HeadCS.GetTranslation();
		RefNeckPosComp = NeckPos;
		RefNeck02PosComp = Neck02Pos;
		bHasRefNeck02PosComp = bHasNeck02 && !RefNeck02PosComp.ContainsNaN();
		RefHeadPosComp = HeadPos;

		const FVector ThighLPos = ThighLCS.GetTranslation();
		const FVector ThighRPos = ThighRCS.GetTranslation();
		const FVector ClavLPos = ClavLCS.GetTranslation();
		const FVector ClavRPos = ClavRCS.GetTranslation();
		RefHipMidZComp = (ThighLPos.Z + ThighRPos.Z) * 0.5f;
		bHasRefHipMidZComp = true;

		const FVector RefHipRight = (ThighRPos - ThighLPos).GetSafeNormal();
		const FVector RefShoulderRight = (ClavRPos - ClavLPos).GetSafeNormal();
		if (!RefHipRight.IsNearlyZero())
		{
			RefHipRightComp = RefHipRight;
		}

		// Build an "up" vector per bone from the chain positions (fallbacks to avoid NaNs).
		auto SafeUp = [](const FVector& From, const FVector& To) -> FVector
		{
			const FVector U = (To - From).GetSafeNormal();
			return U.IsNearlyZero() ? FVector::UpVector : U;
		};

		FVector SpinePos[5] = { PelvisPos, PelvisPos, PelvisPos, PelvisPos, PelvisPos };
		int32 SpineCount = 0;
		if (bHasS1) { SpinePos[SpineCount++] = Spine1CS.GetTranslation(); }
		if (bHasS2) { SpinePos[SpineCount++] = Spine2CS.GetTranslation(); }
		if (bHasS3) { SpinePos[SpineCount++] = Spine3CS.GetTranslation(); }
		if (bHasS4) { SpinePos[SpineCount++] = Spine4CS.GetTranslation(); }
		if (bHasS5) { SpinePos[SpineCount++] = Spine5CS.GetTranslation(); }
		if (SpineCount > 0)
		{
			RefChestPosComp = SpinePos[SpineCount - 1];
			bHasRefChestPosComp = !RefChestPosComp.ContainsNaN();
		}
		else
		{
			RefChestPosComp = (ClavLPos + ClavRPos) * 0.5f;
			bHasRefChestPosComp = !RefChestPosComp.ContainsNaN();
		}

		const FVector PelvisUp = SafeUp(PelvisPos, SpineCount > 0 ? SpinePos[0] : NeckPos);
		RefPelvisBasisComp = MakeQuatFromForwardUp(FVector::CrossProduct(RefHipRight, PelvisUp).GetSafeNormal(), PelvisUp);

		for (int32 i = 0; i < NumSpineBones; ++i)
		{
			const FVector BonePos = SpinePos[i];
			const FVector NextPos = (i + 1 < SpineCount) ? SpinePos[i + 1] : NeckPos;
			const FVector Up = SafeUp(BonePos, NextPos);
			const FVector Fwd = FVector::CrossProduct(RefShoulderRight, Up).GetSafeNormal();
			RefSpineBasisComp[i] = MakeQuatFromForwardUp(Fwd, Up);
		}

		const FVector NeckUp = SafeUp(NeckPos, bHasNeck02 ? Neck02Pos : HeadPos);
		const FVector NeckFwd = FVector::CrossProduct(RefShoulderRight, NeckUp).GetSafeNormal();
		RefNeckBasisComp = MakeQuatFromForwardUp(NeckFwd, NeckUp);

		if (bHasNeck02)
		{
			const FVector Neck02Up = SafeUp(Neck02Pos, HeadPos);
			const FVector Neck02Fwd = FVector::CrossProduct(RefShoulderRight, Neck02Up).GetSafeNormal();
			RefNeck02BasisComp = MakeQuatFromForwardUp(Neck02Fwd, Neck02Up);
			RefHeadBasisComp = RefNeck02BasisComp.IsIdentity() ? RefNeckBasisComp : RefNeck02BasisComp;
		}
		else
		{
			RefNeck02BasisComp = RefNeckBasisComp;
			RefHeadBasisComp = RefNeckBasisComp;
		}
	}

	// --- Arms ---
	auto CacheArmRef = [&](const FBoneReference& Upper, const FBoneReference& Lower, const FBoneReference& Hand,
		FQuat& OutRefUpperComp, FQuat& OutRefLowerComp, FQuat& OutRefHandComp,
		FQuat& OutRefUpperBasisComp, FQuat& OutRefLowerBasisComp, FQuat& OutRefHandBasisComp,
		FQuat& OutRefUpperSurfaceBasisComp, FQuat& OutRefLowerSurfaceBasisComp,
		FVector& OutRefUpperDirComp, FVector& OutRefLowerDirComp,
		FVector& OutRefPoleDirComp, float& OutRefUpperLenComp, float& OutRefLowerLenComp) -> bool
	{
		FTransform UpperCS, LowerCS, HandCS;
		if (!GetCS(Upper, UpperCS) || !GetCS(Lower, LowerCS) || !GetCS(Hand, HandCS))
		{
			return false;
		}

		OutRefUpperComp = UpperCS.GetRotation();
		OutRefLowerComp = LowerCS.GetRotation();
		OutRefHandComp = HandCS.GetRotation();

		const FVector UpperPos = UpperCS.GetTranslation();
		const FVector LowerPos = LowerCS.GetTranslation();
		const FVector HandPos = HandCS.GetTranslation();

		OutRefUpperDirComp = (LowerPos - UpperPos).GetSafeNormal();
		OutRefLowerDirComp = (HandPos - LowerPos).GetSafeNormal();
		OutRefUpperLenComp = (LowerPos - UpperPos).Size();
		OutRefLowerLenComp = (HandPos - LowerPos).Size();

		// A stable pole direction for elbow bend (perpendicular to shoulder->wrist axis, pointing toward the elbow).
		FVector ToWrist = (HandPos - UpperPos);
		FVector DirToWrist = ToWrist.GetSafeNormal();
		if (DirToWrist.IsNearlyZero())
		{
			DirToWrist = OutRefUpperDirComp;
		}
		const FVector ToElbow = (LowerPos - UpperPos);
		FVector ElbowOffset = ToElbow - FVector::DotProduct(ToElbow, DirToWrist) * DirToWrist;
		OutRefPoleDirComp = ElbowOffset.GetSafeNormal();
		if (OutRefPoleDirComp.IsNearlyZero())
		{
			OutRefPoleDirComp = FVector::UpVector;
			OutRefPoleDirComp = (OutRefPoleDirComp - FVector::DotProduct(OutRefPoleDirComp, DirToWrist) * DirToWrist).GetSafeNormal();
			if (OutRefPoleDirComp.IsNearlyZero())
			{
				OutRefPoleDirComp = FVector::RightVector;
			}
		}

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

		const FVector RefUpperUp = OrthoToForward(OutRefPoleDirComp, OutRefUpperDirComp);
		const FVector RefLowerUp = OrthoToForward(OutRefPoleDirComp, OutRefLowerDirComp);
		OutRefUpperBasisComp = MakeQuatFromForwardUp(OutRefUpperDirComp, RefUpperUp);
		OutRefLowerBasisComp = MakeQuatFromForwardUp(OutRefLowerDirComp, RefLowerUp);

		auto ChooseSurfaceUp = [&](const FTransform& BoneCS, const FVector& SegmentDir) -> FVector
		{
			const FVector SegmentDirN = SegmentDir.GetSafeNormal();
			if (SegmentDirN.IsNearlyZero())
			{
				return FVector::UpVector;
			}

			FVector RefBodyForward = FVector::CrossProduct(RefHipRightComp, FVector::UpVector).GetSafeNormal();
			if (RefBodyForward.IsNearlyZero())
			{
				RefBodyForward = FVector::ForwardVector;
			}

			const FVector SurfaceHint = BuildArmSurfaceUpHint(FVector::UpVector, RefBodyForward, SegmentDirN);
			FVector UpHint = SurfaceHint - FVector::DotProduct(SurfaceHint, SegmentDirN) * SegmentDirN;
			UpHint = UpHint.GetSafeNormal();
			if (UpHint.IsNearlyZero())
			{
				UpHint = OrthoToForward(OutRefPoleDirComp, SegmentDirN);
			}
			if (UpHint.IsNearlyZero())
			{
				UpHint = FVector::UpVector;
			}

			const FVector Axes[] = {
				BoneCS.GetUnitAxis(EAxis::X),
				-BoneCS.GetUnitAxis(EAxis::X),
				BoneCS.GetUnitAxis(EAxis::Y),
				-BoneCS.GetUnitAxis(EAxis::Y),
				BoneCS.GetUnitAxis(EAxis::Z),
				-BoneCS.GetUnitAxis(EAxis::Z)
			};

			FVector Best = UpHint;
			float BestScore = -FLT_MAX;
			for (const FVector& Axis : Axes)
			{
				const FVector Candidate = OrthoToForward(Axis, SegmentDirN);
				if (Candidate.IsNearlyZero())
				{
					continue;
				}

				const float Score = FVector::DotProduct(Candidate, UpHint);
				if (Score > BestScore)
				{
					BestScore = Score;
					Best = Candidate;
				}
			}
			return Best.GetSafeNormal();
		};

		const FVector RefUpperSurfaceUp = ChooseSurfaceUp(UpperCS, OutRefUpperDirComp);
		const FVector RefLowerSurfaceUp = ChooseSurfaceUp(LowerCS, OutRefLowerDirComp);
		OutRefUpperSurfaceBasisComp = MakeQuatFromForwardUp(OutRefUpperDirComp, RefUpperSurfaceUp);
		OutRefLowerSurfaceBasisComp = MakeQuatFromForwardUp(OutRefLowerDirComp, RefLowerSurfaceUp);

		const FVector HandForward = HandCS.GetUnitAxis(EAxis::X);
		const FVector HandUp = HandCS.GetUnitAxis(EAxis::Z);
		OutRefHandBasisComp = MakeQuatFromForwardUp(HandForward, HandUp);
		return !OutRefUpperDirComp.IsNearlyZero() &&
			!OutRefLowerDirComp.IsNearlyZero() &&
			!OutRefUpperBasisComp.IsIdentity() &&
			!OutRefLowerBasisComp.IsIdentity() &&
			!OutRefUpperSurfaceBasisComp.IsIdentity() &&
			!OutRefLowerSurfaceBasisComp.IsIdentity();
	};

	bHasRefArmL = CacheArmRef(UpperArmL, LowerArmL, HandL,
		RefUpperArmCompL, RefLowerArmCompL, RefHandCompL,
		RefUpperArmBasisCompL, RefLowerArmBasisCompL, RefHandBasisCompL,
		RefUpperArmSurfaceBasisCompL, RefLowerArmSurfaceBasisCompL,
		RefUpperDirCompL, RefLowerDirCompL,
		RefPoleDirCompL, RefUpperLenCompL, RefLowerLenCompL);
	bHasRefArmR = CacheArmRef(UpperArmR, LowerArmR, HandR,
		RefUpperArmCompR, RefLowerArmCompR, RefHandCompR,
		RefUpperArmBasisCompR, RefLowerArmBasisCompR, RefHandBasisCompR,
		RefUpperArmSurfaceBasisCompR, RefLowerArmSurfaceBasisCompR,
		RefUpperDirCompR, RefLowerDirCompR,
		RefPoleDirCompR, RefUpperLenCompR, RefLowerLenCompR);

	auto CacheRefTransform = [&](const FBoneReference& Bone, FTransform& OutRefTransform)
	{
		FTransform BoneCS;
		OutRefTransform = GetCS(Bone, BoneCS) ? BoneCS : FTransform::Identity;
	};
	CacheRefTransform(UpperArmL, RefUpperArmTransformCompL);
	CacheRefTransform(LowerArmL, RefLowerArmTransformCompL);
	CacheRefTransform(HandL, RefHandTransformCompL);
	CacheRefTransform(ClavicleL, RefClavTransformCompL);
	CacheRefTransform(Spine05, RefSpine05TransformComp);
	CacheRefTransform(UpperArmR, RefUpperArmTransformCompR);
	CacheRefTransform(LowerArmR, RefLowerArmTransformCompR);
	CacheRefTransform(HandR, RefHandTransformCompR);
	CacheRefTransform(ClavicleR, RefClavTransformCompR);

	auto CacheTwistBoneRef = [&](const FBoneReference& TwistBone, FQuat& OutRefTwistComp, FTransform& OutRefTwistTransform)
	{
		CacheRefTransform(TwistBone, OutRefTwistTransform);
		OutRefTwistComp = OutRefTwistTransform.GetRotation().GetNormalized();
	};
	CacheTwistBoneRef(UpperArmTwist01L, RefUpperArmTwist01CompL, RefUpperArmTwist01TransformCompL);
	CacheTwistBoneRef(UpperArmTwist02L, RefUpperArmTwist02CompL, RefUpperArmTwist02TransformCompL);
	CacheTwistBoneRef(LowerArmTwist01L, RefLowerArmTwist01CompL, RefLowerArmTwist01TransformCompL);
	CacheTwistBoneRef(LowerArmTwist02L, RefLowerArmTwist02CompL, RefLowerArmTwist02TransformCompL);
	CacheTwistBoneRef(UpperArmTwist01R, RefUpperArmTwist01CompR, RefUpperArmTwist01TransformCompR);
	CacheTwistBoneRef(UpperArmTwist02R, RefUpperArmTwist02CompR, RefUpperArmTwist02TransformCompR);
	CacheTwistBoneRef(LowerArmTwist01R, RefLowerArmTwist01CompR, RefLowerArmTwist01TransformCompR);
	CacheTwistBoneRef(LowerArmTwist02R, RefLowerArmTwist02CompR, RefLowerArmTwist02TransformCompR);

	for (int32 Index = 0; Index < MediaPipeMetaHumanClavicleHelperCount; ++Index)
	{
		CacheRefTransform(MetaHumanClavicleHelpersL[Index], RefMetaHumanClavicleHelperTransformsCompL[Index]);
		CacheRefTransform(MetaHumanClavicleHelpersR[Index], RefMetaHumanClavicleHelperTransformsCompR[Index]);
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanUpperArmHelperCount; ++Index)
	{
		CacheRefTransform(MetaHumanUpperArmHelpersL[Index], RefMetaHumanUpperArmHelperTransformsCompL[Index]);
		CacheRefTransform(MetaHumanUpperArmHelpersR[Index], RefMetaHumanUpperArmHelperTransformsCompR[Index]);
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanLowerArmHelperCount; ++Index)
	{
		CacheRefTransform(MetaHumanLowerArmHelpersL[Index], RefMetaHumanLowerArmHelperTransformsCompL[Index]);
		CacheRefTransform(MetaHumanLowerArmHelpersR[Index], RefMetaHumanLowerArmHelperTransformsCompR[Index]);
	}

	auto CacheFingerRefs = [&](const FBoneReference& HandBone, const FBoneReference* FingerBones, bool* bOutHasRef, FQuat* OutRefComp, FQuat* OutRefBasisComp, FVector* OutRefDirComp, FVector* OutRefCurlDirComp)
	{
		FTransform HandCS;
		const bool bHasHandCS = GetCS(HandBone, HandCS);

		for (int32 FingerIndex = 0; FingerIndex < QuestFingerCount; ++FingerIndex)
		{
			FVector PreviousDir = FVector::ForwardVector;
			bool bHasPreviousDir = false;
			FVector PreviousCurlDir = FVector::ZeroVector;
			bool bHasPreviousCurlDir = false;

			for (int32 SegmentIndex = 0; SegmentIndex < QuestFingerSegmentsPerFinger; ++SegmentIndex)
			{
				const int32 BoneIndex = QuestFingerBoneIndex(FingerIndex, SegmentIndex);
				bOutHasRef[BoneIndex] = false;
				OutRefComp[BoneIndex] = FQuat::Identity;
				OutRefBasisComp[BoneIndex] = FQuat::Identity;
				OutRefDirComp[BoneIndex] = bHasPreviousDir ? PreviousDir : FVector::ForwardVector;
				OutRefCurlDirComp[BoneIndex] = bHasPreviousCurlDir ? PreviousCurlDir : FVector::ZeroVector;

				FTransform BoneCS;
				if (!GetCS(FingerBones[BoneIndex], BoneCS))
				{
					continue;
				}

				FVector RefDir = FVector::ZeroVector;
				if (SegmentIndex + 1 < QuestFingerSegmentsPerFinger)
				{
					const int32 NextBoneIndex = QuestFingerBoneIndex(FingerIndex, SegmentIndex + 1);
					FTransform NextBoneCS;
					if (GetCS(FingerBones[NextBoneIndex], NextBoneCS))
					{
						RefDir = (NextBoneCS.GetTranslation() - BoneCS.GetTranslation()).GetSafeNormal();
					}
				}
				if (RefDir.IsNearlyZero() && bHasPreviousDir)
				{
					RefDir = PreviousDir;
				}
				if (RefDir.IsNearlyZero())
				{
					RefDir = BoneCS.GetUnitAxis(EAxis::X).GetSafeNormal();
				}
				if (RefDir.IsNearlyZero())
				{
					continue;
				}

				FVector RefUp = BoneCS.GetUnitAxis(EAxis::Z);
				RefUp = (RefUp - FVector::DotProduct(RefUp, RefDir) * RefDir).GetSafeNormal();
				if (RefUp.IsNearlyZero())
				{
					RefUp = BoneCS.GetUnitAxis(EAxis::Y);
					RefUp = (RefUp - FVector::DotProduct(RefUp, RefDir) * RefDir).GetSafeNormal();
				}
				if (RefUp.IsNearlyZero())
				{
					RefUp = FVector::UpVector - FVector::DotProduct(FVector::UpVector, RefDir) * RefDir;
					RefUp = RefUp.GetSafeNormal();
				}
				if (RefUp.IsNearlyZero())
				{
					continue;
				}

				FVector RefCurlDir = FVector::ZeroVector;
				if (bHasHandCS)
				{
					RefCurlDir = HandCS.GetTranslation() - BoneCS.GetTranslation();
					RefCurlDir = (RefCurlDir - FVector::DotProduct(RefCurlDir, RefDir) * RefDir).GetSafeNormal();
				}
				if (RefCurlDir.IsNearlyZero() && bHasPreviousCurlDir)
				{
					RefCurlDir = PreviousCurlDir;
				}
				if (RefCurlDir.IsNearlyZero())
				{
					const FVector Axes[] = {
						BoneCS.GetUnitAxis(EAxis::Y),
						-BoneCS.GetUnitAxis(EAxis::Y),
						BoneCS.GetUnitAxis(EAxis::Z),
						-BoneCS.GetUnitAxis(EAxis::Z)
					};

					float BestScore = -FLT_MAX;
					for (const FVector& Axis : Axes)
					{
						const FVector Candidate = (Axis - FVector::DotProduct(Axis, RefDir) * RefDir).GetSafeNormal();
						if (Candidate.IsNearlyZero())
						{
							continue;
						}

						const float Score = bHasHandCS
							? FVector::DotProduct(Candidate, (HandCS.GetTranslation() - BoneCS.GetTranslation()).GetSafeNormal())
							: FVector::DotProduct(Candidate, -RefUp);
						if (Score > BestScore)
						{
							BestScore = Score;
							RefCurlDir = Candidate;
						}
					}
				}
				if (RefCurlDir.IsNearlyZero())
				{
					RefCurlDir = -RefUp;
				}
				RefCurlDir = (RefCurlDir - FVector::DotProduct(RefCurlDir, RefDir) * RefDir).GetSafeNormal();
				if (RefCurlDir.IsNearlyZero())
				{
					continue;
				}

				const FQuat RefBasis = MakeQuatFromForwardUp(RefDir, RefUp);
				if (RefBasis.IsIdentity())
				{
					continue;
				}

				OutRefComp[BoneIndex] = BoneCS.GetRotation();
				OutRefBasisComp[BoneIndex] = RefBasis;
				OutRefDirComp[BoneIndex] = RefDir;
				OutRefCurlDirComp[BoneIndex] = RefCurlDir;
				bOutHasRef[BoneIndex] = true;
				PreviousDir = RefDir;
				bHasPreviousDir = true;
				PreviousCurlDir = RefCurlDir;
				bHasPreviousCurlDir = true;
			}
		}
	};

	CacheFingerRefs(HandL, FingerBonesL, bHasRefFingerL, RefFingerCompL, RefFingerBasisCompL, RefFingerDirCompL, RefFingerCurlDirCompL);
	CacheFingerRefs(HandR, FingerBonesR, bHasRefFingerR, RefFingerCompR, RefFingerBasisCompR, RefFingerDirCompR, RefFingerCurlDirCompR);

	auto CacheMetacarpalRefs = [&](const FBoneReference* MetacarpalBones, const FBoneReference* FingerBones, bool* bOutHasRef, FQuat* OutRefComp, FVector* OutRefDirComp)
	{
		for (int32 FingerIndex = 1; FingerIndex < QuestFingerCount; ++FingerIndex)
		{
			const int32 MetacarpalIndex = QuestFingerMetacarpalBoneIndex(FingerIndex);
			bOutHasRef[MetacarpalIndex] = false;
			OutRefComp[MetacarpalIndex] = FQuat::Identity;
			OutRefDirComp[MetacarpalIndex] = FVector::ForwardVector;

			FTransform MetacarpalCS;
			if (!GetCS(MetacarpalBones[MetacarpalIndex], MetacarpalCS))
			{
				continue;
			}

			FVector RefDir = FVector::ZeroVector;
			FTransform ProximalCS;
			if (GetCS(FingerBones[QuestFingerBoneIndex(FingerIndex, 0)], ProximalCS))
			{
				RefDir = (ProximalCS.GetTranslation() - MetacarpalCS.GetTranslation()).GetSafeNormal();
			}
			if (RefDir.IsNearlyZero())
			{
				RefDir = MetacarpalCS.GetUnitAxis(EAxis::X).GetSafeNormal();
			}
			if (RefDir.IsNearlyZero())
			{
				continue;
			}

			OutRefComp[MetacarpalIndex] = MetacarpalCS.GetRotation().GetNormalized();
			OutRefDirComp[MetacarpalIndex] = RefDir;
			bOutHasRef[MetacarpalIndex] = true;
		}
	};

	CacheMetacarpalRefs(FingerMetacarpalBonesL, FingerBonesL, bHasRefFingerMetacarpalL, RefFingerMetacarpalCompL, RefFingerMetacarpalDirCompL);
	CacheMetacarpalRefs(FingerMetacarpalBonesR, FingerBonesR, bHasRefFingerMetacarpalR, RefFingerMetacarpalCompR, RefFingerMetacarpalDirCompR);

	auto BuildVisualPalmBasisRef = [&](const FBoneReference& HandBone, const FBoneReference* FingerBones, const bool bIsLeft, const FQuat& FallbackBasis) -> FQuat
	{
		FTransform HandCS;
		FTransform IndexCS;
		FTransform MiddleCS;
		FTransform PinkyCS;
		if (!GetCS(HandBone, HandCS) ||
			!GetCS(FingerBones[QuestFingerBoneIndex(1, 0)], IndexCS) ||
			!GetCS(FingerBones[QuestFingerBoneIndex(2, 0)], MiddleCS) ||
			!GetCS(FingerBones[QuestFingerBoneIndex(4, 0)], PinkyCS))
		{
			return FallbackBasis;
		}

		const FVector HandPos = HandCS.GetTranslation();
		const FVector IndexPos = IndexCS.GetTranslation();
		const FVector MiddlePos = MiddleCS.GetTranslation();
		const FVector PinkyPos = PinkyCS.GetTranslation();

		FVector GeometricForward = ((IndexPos + PinkyPos) * 0.5f - HandPos).GetSafeNormal();
		if (GeometricForward.IsNearlyZero())
		{
			GeometricForward = (MiddlePos - HandPos).GetSafeNormal();
		}
		const FVector Across = (IndexPos - PinkyPos).GetSafeNormal();
		FVector PalmUp = FVector::CrossProduct(GeometricForward, Across).GetSafeNormal();
		if (GeometricForward.IsNearlyZero() || Across.IsNearlyZero() || PalmUp.IsNearlyZero())
		{
			return FallbackBasis;
		}

		const FVector HandYAxis = HandCS.GetUnitAxis(EAxis::Y).GetSafeNormal();
		if (!HandYAxis.IsNearlyZero() && FVector::DotProduct(PalmUp, HandYAxis) < 0.0f)
		{
			PalmUp *= -1.0f;
		}

		FVector BasisForward = GeometricForward;
		if (!bIsLeft)
		{
			// Quest hand basis uses Manny's mirrored right-hand convention: right hand forward is opposite the
			// raw wrist-to-fingers direction, while palm normal remains the visible palm normal.
			BasisForward *= -1.0f;
		}

		const FQuat VisualBasis = MakeQuatFromForwardUp(BasisForward, PalmUp);
		return VisualBasis.IsIdentity() ? FallbackBasis : VisualBasis;
	};

	RefHandVisualPalmBasisCompL = BuildVisualPalmBasisRef(HandL, FingerBonesL, true, RefHandBasisCompL);
	RefHandVisualPalmBasisCompR = BuildVisualPalmBasisRef(HandR, FingerBonesR, false, RefHandBasisCompR);

	// Camera-hand mapping reference: SAME formula as the live 21-landmark basis (raw geometric
	// cross, no bone-axis reference and no side flips - unlike BuildVisualPalmBasisRef above),
	// so the measured->ref delta cancels the formula's chirality convention by construction.
	// The palm side then stops being a continuity coin-flip (2026-07-03 trace: palm-up vs
	// palm-flipped branches split 408/326 across one overhead session). ThumbUpDot records the
	// ref thumb_01 side of the palm plane as the chirality cue for the live thumb landmark.
	auto BuildCameraHandBasisRef = [&](const FBoneReference& HandBone, const FBoneReference* FingerBones,
		bool& bOutHas, FQuat& OutBasis, float& OutThumbUpDot)
	{
		bOutHas = false;
		OutBasis = FQuat::Identity;
		OutThumbUpDot = 0.0f;
		FTransform HandCS;
		FTransform IndexCS;
		FTransform PinkyCS;
		FTransform ThumbCS;
		if (!GetCS(HandBone, HandCS) ||
			!GetCS(FingerBones[QuestFingerBoneIndex(1, 0)], IndexCS) ||
			!GetCS(FingerBones[QuestFingerBoneIndex(4, 0)], PinkyCS) ||
			!GetCS(FingerBones[QuestFingerBoneIndex(0, 0)], ThumbCS))
		{
			return;
		}
		const FVector HandPos = HandCS.GetTranslation();
		const FVector Forward = ((IndexCS.GetTranslation() + PinkyCS.GetTranslation()) * 0.5f - HandPos).GetSafeNormal();
		const FVector Across = (IndexCS.GetTranslation() - PinkyCS.GetTranslation()).GetSafeNormal();
		const FVector Up = FVector::CrossProduct(Forward, Across).GetSafeNormal();
		if (Forward.IsNearlyZero() || Across.IsNearlyZero() || Up.IsNearlyZero())
		{
			return;
		}
		OutBasis = MakeQuatFromForwardUp(Forward, Up);
		OutThumbUpDot = FVector::DotProduct((ThumbCS.GetTranslation() - HandPos).GetSafeNormal(), Up);
		bOutHas = !OutBasis.IsIdentity();
	};
	BuildCameraHandBasisRef(HandL, FingerBonesL, bHasRefHandCameraBasisL, RefHandCameraBasisCompL, RefHandCameraThumbUpDotL);
	BuildCameraHandBasisRef(HandR, FingerBonesR, bHasRefHandCameraBasisR, RefHandCameraBasisCompR, RefHandCameraThumbUpDotR);

	if (CVarQuestHandDebug.GetValueOnAnyThread() != 0)
	{
		FMediaPipeQuestHandDebugReporter::EmitFingerReferenceSummaryLog(
			CountValidQuestFingerRefs(bHasRefFingerL),
			CountValidQuestFingerRefs(bHasRefFingerR),
			!RefHandVisualPalmBasisCompL.IsIdentity(),
			!RefHandVisualPalmBasisCompR.IsIdentity());
	}

	auto CacheClavRef = [&](const FBoneReference& Clav, const FBoneReference& Upper, FQuat& OutRefClavComp, FQuat& OutRefClavBasisComp, FVector& OutRefClavDirComp) -> bool
	{
		FTransform ClavCS, UpperCS;
		if (!GetCS(Clav, ClavCS) || !GetCS(Upper, UpperCS))
		{
			return false;
		}

		OutRefClavComp = ClavCS.GetRotation();
		OutRefClavDirComp = (UpperCS.GetTranslation() - ClavCS.GetTranslation()).GetSafeNormal();
		const FVector RefClavUp = FVector::UpVector - FVector::DotProduct(FVector::UpVector, OutRefClavDirComp) * OutRefClavDirComp;
		OutRefClavBasisComp = MakeQuatFromForwardUp(OutRefClavDirComp, RefClavUp.GetSafeNormal());
		return !OutRefClavDirComp.IsNearlyZero() && !OutRefClavBasisComp.IsIdentity();
	};

	bHasRefClavL = CacheClavRef(ClavicleL, UpperArmL, RefClavCompL, RefClavBasisCompL, RefClavDirCompL);
	bHasRefClavR = CacheClavRef(ClavicleR, UpperArmR, RefClavCompR, RefClavBasisCompR, RefClavDirCompR);

	// --- Legs ---
	auto CacheLegRef = [&](bool bIsLeft, const FBoneReference& Thigh, const FBoneReference& Calf, const FBoneReference& Foot, const FBoneReference& Ball,
		FQuat& OutRefThighComp, FQuat& OutRefCalfComp, FQuat& OutRefFootComp,
		FQuat& OutRefThighBasisComp, FQuat& OutRefCalfBasisComp, bool& bOutHasLegBasis,
		FQuat& OutRefFootBasisComp, bool& bOutHasFootBasis,
		FVector& OutRefThighDirComp, FVector& OutRefCalfDirComp, FVector& OutRefFootDirComp,
		float& OutRefThighLenComp, float& OutRefCalfLenComp, FVector& OutRefAnklePosComp, FVector& OutRefBallPosComp) -> bool
	{
		FTransform ThighCS, CalfCS, FootCS, BallCS;
		if (!GetCS(Thigh, ThighCS) || !GetCS(Calf, CalfCS) || !GetCS(Foot, FootCS) || !GetCS(Ball, BallCS))
		{
			return false;
		}

		OutRefThighComp = ThighCS.GetRotation();
		OutRefCalfComp = CalfCS.GetRotation();
		OutRefFootComp = FootCS.GetRotation();

		const FVector ThighPos = ThighCS.GetTranslation();
		const FVector CalfPos = CalfCS.GetTranslation();
		const FVector FootPos = FootCS.GetTranslation();
		const FVector BallPos = BallCS.GetTranslation();
		OutRefBallPosComp = BallPos;

		OutRefThighDirComp = (CalfPos - ThighPos).GetSafeNormal();
		OutRefCalfDirComp = (FootPos - CalfPos).GetSafeNormal();
		OutRefFootDirComp = (BallPos - FootPos).GetSafeNormal();
		OutRefThighLenComp = (CalfPos - ThighPos).Size();
		OutRefCalfLenComp = (FootPos - CalfPos).Size();
		OutRefAnklePosComp = FootPos;

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

		// Cache reference thigh/calf bases so we can stabilize twist using the knee bend plane normal.
		// IMPORTANT: do not derive the "up" axis from arbitrary bone axes (it can pick the wrong axis and induce 90°/180° twists).
		// Use the character's hip-right axis to define the outward normal of the leg bend plane.
		bOutHasLegBasis = false;
		if (!OutRefThighDirComp.IsNearlyZero() && !OutRefCalfDirComp.IsNearlyZero())
		{
			const FVector Outward = bIsLeft ? -RefHipRightComp : RefHipRightComp;
			const FVector RefThighUp = OrthoToForward(Outward, OutRefThighDirComp);
			const FVector RefCalfUp = OrthoToForward(Outward, OutRefCalfDirComp);
			if (!RefThighUp.IsNearlyZero() && !RefCalfUp.IsNearlyZero() &&
				!FVector::CrossProduct(OutRefThighDirComp, RefThighUp).IsNearlyZero() &&
				!FVector::CrossProduct(OutRefCalfDirComp, RefCalfUp).IsNearlyZero())
			{
				OutRefThighBasisComp = MakeQuatFromForwardUp(OutRefThighDirComp, RefThighUp);
				OutRefCalfBasisComp = MakeQuatFromForwardUp(OutRefCalfDirComp, RefCalfUp);
				bOutHasLegBasis = true;
			}
		}

		// Cache the reference foot basis with the same semantic "up" used by the runtime foot target:
		// a floor/world-up hint projected off the foot aim. Using shin-up here and floor-up at runtime
		// bakes an artificial ankle twist into the retarget delta.
		FVector RefFootUp = FVector::UpVector - FVector::DotProduct(FVector::UpVector, OutRefFootDirComp) * OutRefFootDirComp;
		RefFootUp.Normalize();
		if (RefFootUp.IsNearlyZero())
		{
			RefFootUp = (-OutRefCalfDirComp - FVector::DotProduct(-OutRefCalfDirComp, OutRefFootDirComp) * OutRefFootDirComp).GetSafeNormal();
		}
		bOutHasFootBasis = !OutRefFootDirComp.IsNearlyZero() && !RefFootUp.IsNearlyZero() && !FVector::CrossProduct(OutRefFootDirComp, RefFootUp).IsNearlyZero();
		if (bOutHasFootBasis)
		{
			OutRefFootBasisComp = MakeQuatFromForwardUp(OutRefFootDirComp, RefFootUp);
		}
		else
		{
			OutRefFootBasisComp = FQuat::Identity;
		}

		return !OutRefThighDirComp.IsNearlyZero() && !OutRefCalfDirComp.IsNearlyZero();
	};

	bHasRefLegL = CacheLegRef(true, ThighL, CalfL, FootL, BallL,
		RefThighCompL, RefCalfCompL, RefFootCompL,
		RefThighBasisCompL, RefCalfBasisCompL, bHasRefLegBasisL,
		RefFootBasisCompL, bHasRefFootBasisL,
		RefThighDirCompL, RefCalfDirCompL, RefFootDirCompL,
		RefThighLenCompL, RefCalfLenCompL, RefAnklePosCompL, RefBallPosCompL);
	bHasRefLegR = CacheLegRef(false, ThighR, CalfR, FootR, BallR,
		RefThighCompR, RefCalfCompR, RefFootCompR,
		RefThighBasisCompR, RefCalfBasisCompR, bHasRefLegBasisR,
		RefFootBasisCompR, bHasRefFootBasisR,
		RefThighDirCompR, RefCalfDirCompR, RefFootDirCompR,
		RefThighLenCompR, RefCalfLenCompR, RefAnklePosCompR, RefBallPosCompR);

	bHasRefFootContactL = bHasRefLegL;
	bHasRefFootContactR = bHasRefLegR;

	// Cache a reference "floor" height in component space from foot contact points (ball bones).
	// We use this to ground the character when leg IK is off (prevents "jumping" during squats).
	if (bHasRefFootContactL || bHasRefFootContactR)
	{
		if (bHasRefFootContactL && bHasRefFootContactR)
		{
			RefFootFloorZComp = FMath::Min(RefBallPosCompL.Z, RefBallPosCompR.Z);
		}
		else if (bHasRefFootContactL)
		{
			RefFootFloorZComp = RefBallPosCompL.Z;
		}
		else
		{
			RefFootFloorZComp = RefBallPosCompR.Z;
		}
		bHasRefFootFloorZ = true;
	}
	if (bHasRefHipMidZComp && bHasRefFootFloorZ)
	{
		BodyState.ReferenceRigHipHeightCm = FMath::Max(RefHipMidZComp - RefFootFloorZComp, 1.0f);
	}

	static bool bLoggedMediaPipeTwistBoneAvailability = false;
	if (!bLoggedMediaPipeTwistBoneAvailability)
	{
		const int32 ValidTwistBones =
			(UpperArmTwist01L.IsValidToEvaluate(RequiredBones) ? 1 : 0) +
			(UpperArmTwist02L.IsValidToEvaluate(RequiredBones) ? 1 : 0) +
			(LowerArmTwist01L.IsValidToEvaluate(RequiredBones) ? 1 : 0) +
			(LowerArmTwist02L.IsValidToEvaluate(RequiredBones) ? 1 : 0) +
			(UpperArmTwist01R.IsValidToEvaluate(RequiredBones) ? 1 : 0) +
			(UpperArmTwist02R.IsValidToEvaluate(RequiredBones) ? 1 : 0) +
			(LowerArmTwist01R.IsValidToEvaluate(RequiredBones) ? 1 : 0) +
			(LowerArmTwist02R.IsValidToEvaluate(RequiredBones) ? 1 : 0);
		int32 ValidMetaHumanHelpers = 0;
		for (int32 Index = 0; Index < MediaPipeMetaHumanClavicleHelperCount; ++Index)
		{
			ValidMetaHumanHelpers += MetaHumanClavicleHelpersL[Index].IsValidToEvaluate(RequiredBones) ? 1 : 0;
			ValidMetaHumanHelpers += MetaHumanClavicleHelpersR[Index].IsValidToEvaluate(RequiredBones) ? 1 : 0;
		}
		for (int32 Index = 0; Index < MediaPipeMetaHumanUpperArmHelperCount; ++Index)
		{
			ValidMetaHumanHelpers += MetaHumanUpperArmHelpersL[Index].IsValidToEvaluate(RequiredBones) ? 1 : 0;
			ValidMetaHumanHelpers += MetaHumanUpperArmHelpersR[Index].IsValidToEvaluate(RequiredBones) ? 1 : 0;
		}
		for (int32 Index = 0; Index < MediaPipeMetaHumanLowerArmHelperCount; ++Index)
		{
			ValidMetaHumanHelpers += MetaHumanLowerArmHelpersL[Index].IsValidToEvaluate(RequiredBones) ? 1 : 0;
			ValidMetaHumanHelpers += MetaHumanLowerArmHelpersR[Index].IsValidToEvaluate(RequiredBones) ? 1 : 0;
		}
		UE_LOG(
			LogMediaPipePose,
			Log,
			TEXT("MediaPipe Manny arm twist bones detected: %d valid twist helpers, %d valid MetaHuman sidecar helpers"),
			ValidTwistBones,
			ValidMetaHumanHelpers);
		bLoggedMediaPipeTwistBoneAvailability = true;
	}

	bHasReferencePose = true;
	return true;
}
