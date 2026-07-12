#include "MediaPipePoseDrivenAnimInstance.h"

#include "MediaPipeEmbodimentScaleMetrics.h"
#include "MediaPipePoseDiagnosticReporter.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeRuntimeCVars.h"

#include "BonePose.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/PlatformTime.h"
#include "ReferenceSkeleton.h"

#include "MediaPipePoseDrivenAnimInstanceShared.h"

// Avatar metric lock Phase 0 (Docs/AVATAR_METRIC_LOCK_PLAN.md, 2026-07-12).
// Report-only rows from the FINAL solved component-space pose, per actor, ~1Hz:
//   (a) native reference spans from the target skeleton's cached reference pose,
//   (b) driven spans measured from the posed component-space transforms,
//   (c) per-region stretch ratios (driven / native),
//   (d) the dark once-per-session embodiment scale latch S and its raw inputs,
//   (e) per-writer contribution evidence: fused-pose write flag + fused target
//       owners/heights, HMD scaffold state, FK root grounding lift, the believed
//       avatar eye height (the profile constant the census convicts), source hip
//       height and observed source floor.
// Mutates nothing but the keyed trace/latch state; the pose is read-only here.
void FAnimNode_MediaPipePoseDriven::EmitEmbodimentScaleTraceCS(
	FCSPose<FCompactPose>& CSPose,
	const bool bBodyFusionPoseWritten)
{
	if (CVarEmbodimentScaleTrace.GetValueOnAnyThread() == 0 ||
		RuntimeStateKey == 0 ||
		!bHasReferencePose)
	{
		return;
	}

	FMediaPipeEmbodimentScaleRuntimeState& ScaleState = GetEmbodimentScaleRuntimeState(RuntimeStateKey);
	const double NowSeconds = FPlatformTime::Seconds();

	// ---- S latch inputs (dark: latched + reported, consumed by nothing in Phase 0) ----
	const float AvatarRefHeightCm = (bHasRefFootFloorZ && !RefHeadPosComp.IsNearlyZero())
		? RefHeadPosComp.Z - RefFootFloorZComp
		: 0.0f;

	// Frames matter here (measured 2026-07-12, replay rows): the HMD pose lives in
	// WORLD space while the MediaPipe source landmarks (and their observed floor)
	// live in the hip-centered SOURCE frame (source hip Z ~ 0, source floor ~ -90).
	// The HMD pair therefore measures against the world floor under the avatar
	// (the component's world height); the source floor is reported separately as
	// pure source-frame evidence.
	const FMediaPipeFootContactRuntimeState& FootContactState = GetFootContactRuntimeState(RuntimeStateKey);
	const float WorldFloorZ = static_cast<float>(TargetCompTransform.GetLocation().Z);
	float SourceFloorZ = 0.0f;
	bool bHasSourceFloor = false;
	if (FootContactState.Left.bHasObservedSourceFloor && FootContactState.Right.bHasObservedSourceFloor)
	{
		SourceFloorZ = FMath::Min(FootContactState.Left.ObservedSourceFloorZ, FootContactState.Right.ObservedSourceFloorZ);
		bHasSourceFloor = true;
	}
	else if (FootContactState.Left.bHasObservedSourceFloor)
	{
		SourceFloorZ = FootContactState.Left.ObservedSourceFloorZ;
		bHasSourceFloor = true;
	}
	else if (FootContactState.Right.bHasObservedSourceFloor)
	{
		SourceFloorZ = FootContactState.Right.ObservedSourceFloorZ;
		bHasSourceFloor = true;
	}

	const bool bScaffoldHasBaseline = BodyState.HmdHeightScaffold.bHasBaseline;
	const float UserStandingRefCm = bScaffoldHasBaseline
		? BodyState.ScaffoldHmdBaselineZ - WorldFloorZ
		: 0.0f;

	// Two candidate user references for S: the worn HMD scaffold standing baseline
	// (head-height pair, preferred) and the camera's latched standing source hip
	// (hip-height pair, the headset-free fallback). Both dark in Phase 0.
	MediaPipeEmbodimentScale::FMediaPipeEmbodimentScaleLatchInput HmdPair;
	HmdPair.AvatarRefHeightCm = AvatarRefHeightCm;
	HmdPair.UserStandingRefHeightCm = UserStandingRefCm;
	HmdPair.UserRefConfidence01 = bScaffoldHasBaseline ? BodyState.ScaffoldHmdConfidence : 0.0f;
	HmdPair.Source = MediaPipeEmbodimentScale::LatchSourceHmd;
	HmdPair.NowSeconds = NowSeconds;

	MediaPipeEmbodimentScale::FMediaPipeEmbodimentScaleLatchInput CameraPair;
	CameraPair.AvatarRefHeightCm = BodyState.ReferenceRigHipHeightCm;
	CameraPair.UserStandingRefHeightCm = BodyState.ReferenceHipHeightCm;
	CameraPair.UserRefConfidence01 = BodyState.bHasReferenceHipHeight ? 1.0f : 0.0f;
	CameraPair.Source = MediaPipeEmbodimentScale::LatchSourceCamera;
	CameraPair.NowSeconds = NowSeconds;

	const MediaPipeEmbodimentScale::FMediaPipeEmbodimentScaleLatchInput SelectedPair =
		MediaPipeEmbodimentScale::SelectEmbodimentScaleLatchInput(HmdPair, CameraPair);
	MediaPipeEmbodimentScale::UpdateEmbodimentScaleLatch(ScaleState.ScaleLatch, SelectedPair);

	if (!FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(
			NowSeconds, 1.0, ScaleState.TraceLastLogTimeSeconds))
	{
		return;
	}

	auto TryGetBoneCS = [&](const FBoneReference& Bone, FVector& OutPosComp) -> bool
	{
		if (!Bone.IsValidToEvaluate())
		{
			return false;
		}
		OutPosComp = CSPose.GetComponentSpaceTransform(Bone.CachedCompactPoseIndex).GetTranslation();
		return !OutPosComp.ContainsNaN();
	};

	auto GetSpineRefBySlot = [&](const uint8 Slot) -> const FBoneReference&
	{
		switch (Slot)
		{
		case 1: return Spine01;
		case 2: return Spine02;
		case 3: return Spine03;
		case 4: return Spine04;
		case 5: return Spine05;
		default: return Spine03;
		}
	};

	// ---- native reference spans (avatar's own skeleton, component space, cm) ----
	const float NatHipCm = bHasRefFootFloorZ
		? RefPelvisTranslationComp.Z - RefFootFloorZComp
		: 0.0f;
	const float NatHeadCm = AvatarRefHeightCm;
	const bool bHasNatTorso =
		bHasRefChestPosComp && !RefNeckPosComp.IsNearlyZero() && !RefHeadPosComp.IsNearlyZero();
	const float NatTorsoCm = bHasNatTorso
		? MediaPipeEmbodimentScale::ComputeTorsoChainLengthCm(
			RefPelvisTranslationComp, RefChestPosComp, RefNeckPosComp, RefHeadPosComp)
		: 0.0f;
	const float NatLegCmL = bHasRefLegL ? RefThighLenCompL + RefCalfLenCompL : 0.0f;
	const float NatLegCmR = bHasRefLegR ? RefThighLenCompR + RefCalfLenCompR : 0.0f;
	const float NatArmCmL = (RefUpperLenCompL > KINDA_SMALL_NUMBER && RefLowerLenCompL > KINDA_SMALL_NUMBER)
		? RefUpperLenCompL + RefLowerLenCompL
		: 0.0f;
	const float NatArmCmR = (RefUpperLenCompR > KINDA_SMALL_NUMBER && RefLowerLenCompR > KINDA_SMALL_NUMBER)
		? RefUpperLenCompR + RefLowerLenCompR
		: 0.0f;

	// ---- driven spans (final posed component-space transforms, cm) ----
	FVector PelvisPos = FVector::ZeroVector;
	FVector ChestPos = FVector::ZeroVector;
	FVector NeckPos = FVector::ZeroVector;
	FVector HeadPos = FVector::ZeroVector;
	FVector ThighPosL = FVector::ZeroVector;
	FVector CalfPosL = FVector::ZeroVector;
	FVector FootPosL = FVector::ZeroVector;
	FVector BallPosL = FVector::ZeroVector;
	FVector ThighPosR = FVector::ZeroVector;
	FVector CalfPosR = FVector::ZeroVector;
	FVector FootPosR = FVector::ZeroVector;
	FVector BallPosR = FVector::ZeroVector;
	FVector UpperArmPosL = FVector::ZeroVector;
	FVector LowerArmPosL = FVector::ZeroVector;
	FVector HandPosL = FVector::ZeroVector;
	FVector UpperArmPosR = FVector::ZeroVector;
	FVector LowerArmPosR = FVector::ZeroVector;
	FVector HandPosR = FVector::ZeroVector;
	const bool bHasPelvisPos = TryGetBoneCS(Pelvis, PelvisPos);
	const bool bHasChestPos = NumSpineBones > 0 && SpineBoneSlots[NumSpineBones - 1] != 0
		? TryGetBoneCS(GetSpineRefBySlot(SpineBoneSlots[NumSpineBones - 1]), ChestPos)
		: false;
	const bool bHasNeckPos = TryGetBoneCS(Neck, NeckPos);
	const bool bHasHeadPos = TryGetBoneCS(Head, HeadPos);
	const bool bHasLegPosL = TryGetBoneCS(ThighL, ThighPosL) && TryGetBoneCS(CalfL, CalfPosL) && TryGetBoneCS(FootL, FootPosL);
	const bool bHasLegPosR = TryGetBoneCS(ThighR, ThighPosR) && TryGetBoneCS(CalfR, CalfPosR) && TryGetBoneCS(FootR, FootPosR);
	const bool bHasBallPosL = TryGetBoneCS(BallL, BallPosL);
	const bool bHasBallPosR = TryGetBoneCS(BallR, BallPosR);
	const bool bHasArmPosL = TryGetBoneCS(UpperArmL, UpperArmPosL) && TryGetBoneCS(LowerArmL, LowerArmPosL) && TryGetBoneCS(HandL, HandPosL);
	const bool bHasArmPosR = TryGetBoneCS(UpperArmR, UpperArmPosR) && TryGetBoneCS(LowerArmR, LowerArmPosR) && TryGetBoneCS(HandR, HandPosR);

	// Driven floor: the lowest posed ball; the avatar's own reference floor when
	// neither ball resolves.
	float DrivenFloorZ = RefFootFloorZComp;
	if (bHasBallPosL && bHasBallPosR)
	{
		DrivenFloorZ = static_cast<float>(FMath::Min(BallPosL.Z, BallPosR.Z));
	}
	else if (bHasBallPosL)
	{
		DrivenFloorZ = static_cast<float>(BallPosL.Z);
	}
	else if (bHasBallPosR)
	{
		DrivenFloorZ = static_cast<float>(BallPosR.Z);
	}

	const float DrvHipCm = bHasPelvisPos ? static_cast<float>(PelvisPos.Z) - DrivenFloorZ : 0.0f;
	const float DrvHeadCm = bHasHeadPos ? static_cast<float>(HeadPos.Z) - DrivenFloorZ : 0.0f;
	const float DrvTorsoCm = (bHasPelvisPos && bHasChestPos && bHasNeckPos && bHasHeadPos)
		? MediaPipeEmbodimentScale::ComputeTorsoChainLengthCm(PelvisPos, ChestPos, NeckPos, HeadPos)
		: 0.0f;
	const float DrvLegCmL = bHasLegPosL
		? static_cast<float>(FVector::Dist(ThighPosL, CalfPosL) + FVector::Dist(CalfPosL, FootPosL))
		: 0.0f;
	const float DrvLegCmR = bHasLegPosR
		? static_cast<float>(FVector::Dist(ThighPosR, CalfPosR) + FVector::Dist(CalfPosR, FootPosR))
		: 0.0f;
	const float DrvArmCmL = bHasArmPosL
		? static_cast<float>(FVector::Dist(UpperArmPosL, LowerArmPosL) + FVector::Dist(LowerArmPosL, HandPosL))
		: 0.0f;
	const float DrvArmCmR = bHasArmPosR
		? static_cast<float>(FVector::Dist(UpperArmPosR, LowerArmPosR) + FVector::Dist(LowerArmPosR, HandPosR))
		: 0.0f;

	// ---- writer contribution evidence ----
	const int32 FusedPelvisOwner = BodyFusionFrame.Pose.Pelvis.bValid
		? static_cast<int32>(BodyFusionFrame.Pose.Pelvis.Owner)
		: -1;
	const float FusedPelvisWorldZ = BodyFusionFrame.Pose.Pelvis.bValid
		? static_cast<float>(BodyFusionFrame.Pose.Pelvis.LocationWorld.Z)
		: 0.0f;
	const float FusedChestWorldZ = BodyFusionFrame.Pose.Chest.bValid
		? static_cast<float>(BodyFusionFrame.Pose.Chest.LocationWorld.Z)
		: 0.0f;
	const float FusedEyeWorldZ = BodyFusionFrame.Pose.Eye.bValid
		? static_cast<float>(BodyFusionFrame.Pose.Eye.LocationWorld.Z)
		: 0.0f;

	float SourceHipZ = 0.0f;
	bool bHasSourceHip = false;
	{
		const int32 LHipLm = static_cast<int32>(EMediaPipePoseLandmark::LeftHip);
		const int32 RHipLm = static_cast<int32>(EMediaPipePoseLandmark::RightHip);
		FVector LHipWorld = FVector::ZeroVector;
		FVector RHipWorld = FVector::ZeroVector;
		if (IsMeasured(LHipLm) && IsMeasured(RHipLm) &&
			TryGetLmWorld(LHipLm, LHipWorld) && TryGetLmWorld(RHipLm, RHipWorld))
		{
			SourceHipZ = static_cast<float>((LHipWorld.Z + RHipWorld.Z) * 0.5f);
			bHasSourceHip = true;
		}
	}

	const float LiveS = MediaPipeEmbodimentScale::ComputeEmbodimentScale(AvatarRefHeightCm, UserStandingRefCm);
	const float LiveCameraS = MediaPipeEmbodimentScale::ComputeEmbodimentScale(
		BodyState.ReferenceRigHipHeightCm, BodyState.ReferenceHipHeightCm);

	// BIND-pose native spans (root-cause evidence, 2026-07-12): the bind pose is
	// the authored geometry; when the asset's reference skeleton was assembled at
	// different proportions (Emory: short-adult skeleton on a child body), every
	// ref-skeleton-derived "native" span above is inflated and the driven/bind
	// ratios expose the avatar's true stretch. Kellan/Manny: bind == ref -> 1.0.
	{
		const FBoneContainer& BoneContainer = CSPose.GetPose().GetBoneContainer();
		const USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(BoneContainer.GetAsset());
		if (SkelMesh != nullptr)
		{
			const TArray<FMatrix44f>& InvBind = SkelMesh->GetRefBasesInvMatrix();
			const FReferenceSkeleton& RefSkel = SkelMesh->GetRefSkeleton();
			auto TryGetBindPos = [&](const FBoneReference& Bone, FVector& OutPos) -> bool
			{
				if (Bone.BoneName.IsNone())
				{
					return false;
				}
				const int32 BoneIdx = RefSkel.FindBoneIndex(Bone.BoneName);
				if (BoneIdx == INDEX_NONE || !InvBind.IsValidIndex(BoneIdx))
				{
					return false;
				}
				OutPos = MediaPipeEmbodimentScale::BindComponentPositionFromInverseBind(InvBind[BoneIdx]);
				return !OutPos.ContainsNaN();
			};

			FVector BindPelvis = FVector::ZeroVector;
			FVector BindChest = FVector::ZeroVector;
			FVector BindNeck = FVector::ZeroVector;
			FVector BindHead = FVector::ZeroVector;
			FVector BindThighL = FVector::ZeroVector;
			FVector BindCalfL = FVector::ZeroVector;
			FVector BindFootL = FVector::ZeroVector;
			FVector BindBallL = FVector::ZeroVector;
			FVector BindUpperL = FVector::ZeroVector;
			FVector BindLowerL = FVector::ZeroVector;
			FVector BindHandL = FVector::ZeroVector;
			const bool bBindPelvis = TryGetBindPos(Pelvis, BindPelvis);
			const bool bBindChest = NumSpineBones > 0 && SpineBoneSlots[NumSpineBones - 1] != 0
				? TryGetBindPos(GetSpineRefBySlot(SpineBoneSlots[NumSpineBones - 1]), BindChest)
				: false;
			const bool bBindNeck = TryGetBindPos(Neck, BindNeck);
			const bool bBindHead = TryGetBindPos(Head, BindHead);
			const bool bBindLegL = TryGetBindPos(ThighL, BindThighL) && TryGetBindPos(CalfL, BindCalfL) && TryGetBindPos(FootL, BindFootL);
			const bool bBindBallL = TryGetBindPos(BallL, BindBallL);
			const bool bBindArmL = TryGetBindPos(UpperArmL, BindUpperL) && TryGetBindPos(LowerArmL, BindLowerL) && TryGetBindPos(HandL, BindHandL);

			const float BindFloorZ = bBindBallL ? static_cast<float>(BindBallL.Z) : (bBindLegL ? static_cast<float>(BindFootL.Z) : 0.0f);
			const float BindHipCm = bBindPelvis ? static_cast<float>(BindPelvis.Z) - BindFloorZ : 0.0f;
			const float BindHeadCm = bBindHead ? static_cast<float>(BindHead.Z) - BindFloorZ : 0.0f;
			const float BindTorsoCm = (bBindPelvis && bBindChest && bBindNeck && bBindHead)
				? MediaPipeEmbodimentScale::ComputeTorsoChainLengthCm(BindPelvis, BindChest, BindNeck, BindHead)
				: 0.0f;
			const float BindLegCmL = bBindLegL
				? static_cast<float>(FVector::Dist(BindThighL, BindCalfL) + FVector::Dist(BindCalfL, BindFootL))
				: 0.0f;
			const float BindArmCmL = bBindArmL
				? static_cast<float>(FVector::Dist(BindUpperL, BindLowerL) + FVector::Dist(BindLowerL, BindHandL))
				: 0.0f;
			// bindK: how the asset's reference skeleton relates to its authored bind
			// geometry (1.0 = consistent asset; Emory measured ~0.70).
			const float BindK = MediaPipeEmbodimentScale::ComputeSpanRatio(BindHeadCm, NatHeadCm);

			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.EmbodimentScaleTrace.Bind: actor=%s bindFloorZ=%.1f bindHipCm=%.1f bindTorsoCm=%.1f bindHeadCm=%.1f bindLegL=%.1f bindArmL=%.1f bindK=%.3f drvVsBindHipR=%.3f drvVsBindTorsoR=%.3f drvVsBindHeadR=%.3f drvVsBindLegRL=%.3f drvVsBindArmRL=%.3f key=%u"),
				*TargetActorName.ToString(),
				BindFloorZ,
				BindHipCm,
				BindTorsoCm,
				BindHeadCm,
				BindLegCmL,
				BindArmCmL,
				BindK,
				MediaPipeEmbodimentScale::ComputeSpanRatio(DrvHipCm, BindHipCm),
				MediaPipeEmbodimentScale::ComputeSpanRatio(DrvTorsoCm, BindTorsoCm),
				MediaPipeEmbodimentScale::ComputeSpanRatio(DrvHeadCm, BindHeadCm),
				MediaPipeEmbodimentScale::ComputeSpanRatio(DrvLegCmL, BindLegCmL),
				MediaPipeEmbodimentScale::ComputeSpanRatio(DrvArmCmL, BindArmCmL),
				RuntimeStateKey);
		}
	}

	UE_LOG(LogMediaPipePose, Log,
		TEXT("mp.EmbodimentScaleTrace: actor=%s natHipCm=%.1f natTorsoCm=%.1f natHeadCm=%.1f natLegL=%.1f natLegR=%.1f natArmL=%.1f natArmR=%.1f drvFloorZ=%.1f drvHipCm=%.1f drvTorsoCm=%.1f drvHeadCm=%.1f drvLegL=%.1f drvLegR=%.1f drvArmL=%.1f drvArmR=%.1f hipR=%.3f torsoR=%.3f headR=%.3f legRL=%.3f legRR=%.3f armRL=%.3f armRR=%.3f fused=%d pelvOwn=%d pelvWz=%.1f chestWz=%.1f eyeWz=%.1f eyeVal=%d hmdZ=%.1f hmdVal=%d worn=%d scafBaseZ=%.1f scafConf=%.2f scafAlpha=%.2f fusedAlpha=%.2f rootOffZ=%.1f srcHipZ=%.1f hasSrcHip=%d srcFloorZ=%.1f hasSrcFloor=%d compWz=%.1f belEyeZ=%.1f avatarRefCm=%.1f userRefCm=%.1f S=%.3f camAvRefCm=%.1f camUserRefCm=%.1f Scam=%.3f selSrc=%d SL=%.3f latchSrc=%d latched=%d key=%u"),
		*TargetActorName.ToString(),
		NatHipCm, NatTorsoCm, NatHeadCm, NatLegCmL, NatLegCmR, NatArmCmL, NatArmCmR,
		DrivenFloorZ, DrvHipCm, DrvTorsoCm, DrvHeadCm, DrvLegCmL, DrvLegCmR, DrvArmCmL, DrvArmCmR,
		MediaPipeEmbodimentScale::ComputeSpanRatio(DrvHipCm, NatHipCm),
		MediaPipeEmbodimentScale::ComputeSpanRatio(DrvTorsoCm, NatTorsoCm),
		MediaPipeEmbodimentScale::ComputeSpanRatio(DrvHeadCm, NatHeadCm),
		MediaPipeEmbodimentScale::ComputeSpanRatio(DrvLegCmL, NatLegCmL),
		MediaPipeEmbodimentScale::ComputeSpanRatio(DrvLegCmR, NatLegCmR),
		MediaPipeEmbodimentScale::ComputeSpanRatio(DrvArmCmL, NatArmCmL),
		MediaPipeEmbodimentScale::ComputeSpanRatio(DrvArmCmR, NatArmCmR),
		bBodyFusionPoseWritten ? 1 : 0,
		FusedPelvisOwner,
		FusedPelvisWorldZ,
		FusedChestWorldZ,
		FusedEyeWorldZ,
		BodyFusionFrame.Pose.Eye.bValid ? 1 : 0,
		static_cast<float>(CachedQuestHmdWorld.Z),
		bHasCachedQuestHmdPose ? 1 : 0,
		bCachedQuestHmdWorn ? 1 : 0,
		BodyState.ScaffoldHmdBaselineZ,
		BodyState.ScaffoldHmdConfidence,
		BodyState.ScaffoldHmdAlpha01,
		BodyState.ScaffoldFusedAlpha01,
		static_cast<float>(BodyState.SmoothedFkRootGroundOffsetComp.Z),
		SourceHipZ,
		bHasSourceHip ? 1 : 0,
		SourceFloorZ,
		bHasSourceFloor ? 1 : 0,
		WorldFloorZ,
		bHasTargetEyeLocalOffset ? static_cast<float>(TargetEyeLocalOffset.Z) : -1.0f,
		AvatarRefHeightCm,
		UserStandingRefCm,
		LiveS,
		BodyState.ReferenceRigHipHeightCm,
		BodyState.ReferenceHipHeightCm,
		LiveCameraS,
		static_cast<int32>(SelectedPair.Source),
		ScaleState.ScaleLatch.LatchedS,
		static_cast<int32>(ScaleState.ScaleLatch.LatchedSource),
		ScaleState.ScaleLatch.bLatched ? 1 : 0,
		RuntimeStateKey);
}
