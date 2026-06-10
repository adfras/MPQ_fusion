#include "MediaPipeStage2ShoulderEvidence.h"

namespace
{
bool IsFiniteVector(const FVector& Value)
{
	return !Value.ContainsNaN();
}

float RemapPositiveUnbounded(const float Value, const float InMin, const float InFull)
{
	if (InFull <= InMin + KINDA_SMALL_NUMBER)
	{
		return Value > InMin ? 1.0f : 0.0f;
	}
	return FMath::Clamp((Value - InMin) / (InFull - InMin), 0.0f, 1.0f);
}

bool TryGetCalibratedLandmarkComp(
	const FMediaPipeTrackingSourceFrame& SourceFrame,
	const FMediaPipeEmbodimentCalibration& Calibration,
	const FTransform& WorldToComponent,
	const EMediaPipePoseLandmark Landmark,
	const float MinReliability,
	FVector& OutComp,
	float& OutReliability)
{
	OutReliability = 0.0f;
	FVector MediaPipeWorld = FVector::ZeroVector;
	if (!SourceFrame.TryGetBodyLandmark(Landmark, MediaPipeWorld, &OutReliability) ||
		OutReliability < MinReliability ||
		!IsFiniteVector(MediaPipeWorld))
	{
		return false;
	}

	const FVector AvatarWorld = Calibration.TransformMediaPipePoint(MediaPipeWorld);
	if (!IsFiniteVector(AvatarWorld))
	{
		return false;
	}

	OutComp = WorldToComponent.TransformPosition(AvatarWorld);
	return IsFiniteVector(OutComp);
}

struct FStage2HeadPair
{
	FVector LeftComp = FVector::ZeroVector;
	FVector RightComp = FVector::ZeroVector;
	float Reliability = 0.0f;
	EMediaPipeStage2ShoulderEvidenceSourceMode SourceMode = EMediaPipeStage2ShoulderEvidenceSourceMode::None;
};

bool TryGetPairedHeadComp(
	const FMediaPipeTrackingSourceFrame& SourceFrame,
	const FMediaPipeEmbodimentCalibration& Calibration,
	const FTransform& WorldToComponent,
	const float MinReliability,
	FStage2HeadPair& OutPair)
{
	auto TryPair = [&](
		const EMediaPipePoseLandmark LeftLandmark,
		const EMediaPipePoseLandmark RightLandmark,
		const EMediaPipeStage2ShoulderEvidenceSourceMode SourceMode) -> bool
	{
		FVector LeftComp = FVector::ZeroVector;
		FVector RightComp = FVector::ZeroVector;
		float LeftReliability = 0.0f;
		float RightReliability = 0.0f;
		if (!TryGetCalibratedLandmarkComp(SourceFrame, Calibration, WorldToComponent, LeftLandmark, MinReliability, LeftComp, LeftReliability) ||
			!TryGetCalibratedLandmarkComp(SourceFrame, Calibration, WorldToComponent, RightLandmark, MinReliability, RightComp, RightReliability))
		{
			return false;
		}

		OutPair.LeftComp = LeftComp;
		OutPair.RightComp = RightComp;
		OutPair.Reliability = FMath::Min(LeftReliability, RightReliability);
		OutPair.SourceMode = SourceMode;
		return true;
	};

	if (TryPair(
		EMediaPipePoseLandmark::LeftEar,
		EMediaPipePoseLandmark::RightEar,
		EMediaPipeStage2ShoulderEvidenceSourceMode::RawWorldEars))
	{
		return true;
	}
	if (TryPair(
		EMediaPipePoseLandmark::LeftEye,
		EMediaPipePoseLandmark::RightEye,
		EMediaPipeStage2ShoulderEvidenceSourceMode::RawWorldEyes))
	{
		return true;
	}

	FVector NoseComp = FVector::ZeroVector;
	float NoseReliability = 0.0f;
	if (TryGetCalibratedLandmarkComp(
		SourceFrame,
		Calibration,
		WorldToComponent,
		EMediaPipePoseLandmark::Nose,
		MinReliability,
		NoseComp,
		NoseReliability))
	{
		OutPair.LeftComp = NoseComp;
		OutPair.RightComp = NoseComp;
		OutPair.Reliability = NoseReliability;
		OutPair.SourceMode = EMediaPipeStage2ShoulderEvidenceSourceMode::RawWorldNose;
		return true;
	}

	return false;
}

bool ShouldAcceptNeutralReferenceSample(
	const FMediaPipeStage2ShoulderEvidenceSideState& SideState,
	const FMediaPipeStage2ShoulderEvidenceSettings& Settings,
	const float CandidateLiftCm,
	const float ClearanceCm,
	const bool bSuppressedByContradiction,
	const bool bHasCompleteQuestArmOwnershipSource,
	const float QuestArmRaiseOwnershipFade)
{
	if (bSuppressedByContradiction ||
		!bHasCompleteQuestArmOwnershipSource ||
		QuestArmRaiseOwnershipFade > KINDA_SMALL_NUMBER)
	{
		return false;
	}

	if (!SideState.bHasNeutralReference)
	{
		return true;
	}

	const float ToleranceCm = FMath::Max(0.1f, Settings.NeutralUpdateToleranceCm);
	const float ShoulderDeltaCm = CandidateLiftCm - SideState.NeutralShoulderLiftFromPelvisCm;
	const float ClearanceDeltaCm = SideState.NeutralShoulderHeadClearanceCm - ClearanceCm;
	if (ShoulderDeltaCm < -ToleranceCm || ClearanceDeltaCm < -ToleranceCm)
	{
		return true;
	}

	return FMath::Abs(ShoulderDeltaCm) <= ToleranceCm &&
		FMath::Abs(ClearanceDeltaCm) <= ToleranceCm;
}

void UpdateNeutralReference(
	const float CandidateLiftCm,
	const float ClearanceCm,
	const float DeltaSeconds,
	const FMediaPipeStage2ShoulderEvidenceSettings& Settings,
	FMediaPipeStage2ShoulderEvidenceSideState& InOutSideState)
{
	if (!InOutSideState.bHasNeutralReference)
	{
		InOutSideState.NeutralShoulderLiftFromPelvisCm = CandidateLiftCm;
		InOutSideState.NeutralShoulderHeadClearanceCm = ClearanceCm;
		InOutSideState.NeutralObservationSeconds = 0.0f;
		InOutSideState.NeutralObservationFrames = 0;
		InOutSideState.bHasNeutralReference = true;
	}
	else if (CandidateLiftCm < InOutSideState.NeutralShoulderLiftFromPelvisCm - FMath::Max(0.1f, Settings.NeutralUpdateToleranceCm) ||
		ClearanceCm > InOutSideState.NeutralShoulderHeadClearanceCm + FMath::Max(0.1f, Settings.NeutralUpdateToleranceCm))
	{
		InOutSideState.NeutralShoulderLiftFromPelvisCm = CandidateLiftCm;
		InOutSideState.NeutralShoulderHeadClearanceCm = ClearanceCm;
		InOutSideState.NeutralObservationSeconds = 0.0f;
		InOutSideState.NeutralObservationFrames = 0;
	}
	else if (InOutSideState.HasReadyNeutralReference(Settings))
	{
		constexpr float ReadyNeutralFollowHalfLifeSeconds = 3.0f;
		const float Alpha = FMediaPipeStage2ShoulderEvidence::HalfLifeToAlpha(
			ReadyNeutralFollowHalfLifeSeconds,
			DeltaSeconds);
		InOutSideState.NeutralShoulderLiftFromPelvisCm = FMath::Lerp(
			InOutSideState.NeutralShoulderLiftFromPelvisCm,
			CandidateLiftCm,
			Alpha);
		InOutSideState.NeutralShoulderHeadClearanceCm = FMath::Lerp(
			InOutSideState.NeutralShoulderHeadClearanceCm,
			ClearanceCm,
			Alpha);
	}

	InOutSideState.NeutralObservationSeconds += FMath::Max(0.0f, DeltaSeconds);
	++InOutSideState.NeutralObservationFrames;
}
}

void FMediaPipeStage2ShoulderEvidenceSideState::Reset()
{
	bHasSmoothedLift = false;
	SmoothedLiftCm = 0.0f;
	bHasNeutralReference = false;
	NeutralShoulderLiftFromPelvisCm = 0.0f;
	NeutralShoulderHeadClearanceCm = 0.0f;
	NeutralObservationSeconds = 0.0f;
	NeutralObservationFrames = 0;
}

bool FMediaPipeStage2ShoulderEvidenceSideState::HasReadyNeutralReference(
	const FMediaPipeStage2ShoulderEvidenceSettings& Settings) const
{
	return bHasNeutralReference &&
		NeutralObservationFrames >= FMath::Max(1, Settings.NeutralHoldFrames) &&
		NeutralObservationSeconds >= FMath::Max(0.0f, Settings.NeutralHoldSeconds);
}

float FMediaPipeStage2ShoulderEvidence::HalfLifeToAlpha(const float HalfLifeSeconds, const float DeltaSeconds)
{
	if (HalfLifeSeconds <= KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}
	return 1.0f - FMath::Exp2(-FMath::Max(0.0f, DeltaSeconds) / HalfLifeSeconds);
}

bool FMediaPipeStage2ShoulderEvidence::BuildSideEvidence(
	const FMediaPipeTrackingSourceFrame& SourceFrame,
	const FMediaPipeEmbodimentCalibration& Calibration,
	const bool bIsLeft,
	const FTransform& WorldToComponent,
	const FMediaPipeStage2ShoulderEvidenceSettings& Settings,
	const float DeltaSeconds,
	FMediaPipeStage2ShoulderEvidenceSideState& InOutSideState,
	FMediaPipeStage2ShoulderEvidenceResult& OutResult)
{
	OutResult = FMediaPipeStage2ShoulderEvidenceResult();
	if (!SourceFrame.bHasBodyPose ||
		!SourceFrame.BodyPoseStatus.IsFresh() ||
		!Calibration.IsUsable() ||
		Settings.MaxLiftCm <= KINDA_SMALL_NUMBER)
	{
		InOutSideState.Reset();
		return false;
	}

	const EMediaPipePoseLandmark SideShoulderLandmark = bIsLeft
		? EMediaPipePoseLandmark::LeftShoulder
		: EMediaPipePoseLandmark::RightShoulder;
	const EMediaPipePoseLandmark OppositeShoulderLandmark = bIsLeft
		? EMediaPipePoseLandmark::RightShoulder
		: EMediaPipePoseLandmark::LeftShoulder;

	FVector SideShoulderComp = FVector::ZeroVector;
	FVector OppositeShoulderComp = FVector::ZeroVector;
	FVector LeftHipComp = FVector::ZeroVector;
	FVector RightHipComp = FVector::ZeroVector;
	float SideShoulderReliability = 0.0f;
	float OppositeShoulderReliability = 0.0f;
	float LeftHipReliability = 0.0f;
	float RightHipReliability = 0.0f;
	if (!TryGetCalibratedLandmarkComp(SourceFrame, Calibration, WorldToComponent, SideShoulderLandmark, Settings.MinShoulderReliability, SideShoulderComp, SideShoulderReliability) ||
		!TryGetCalibratedLandmarkComp(SourceFrame, Calibration, WorldToComponent, OppositeShoulderLandmark, Settings.MinShoulderReliability, OppositeShoulderComp, OppositeShoulderReliability) ||
		!TryGetCalibratedLandmarkComp(SourceFrame, Calibration, WorldToComponent, EMediaPipePoseLandmark::LeftHip, Settings.MinHipReliability, LeftHipComp, LeftHipReliability) ||
		!TryGetCalibratedLandmarkComp(SourceFrame, Calibration, WorldToComponent, EMediaPipePoseLandmark::RightHip, Settings.MinHipReliability, RightHipComp, RightHipReliability))
	{
		InOutSideState.Reset();
		return false;
	}

	FStage2HeadPair HeadPair;
	if (!TryGetPairedHeadComp(SourceFrame, Calibration, WorldToComponent, Settings.MinHeadReliability, HeadPair))
	{
		InOutSideState.Reset();
		return false;
	}

	const FVector SideHeadComp = bIsLeft ? HeadPair.LeftComp : HeadPair.RightComp;
	const FVector CandidatePelvisComp = (LeftHipComp + RightHipComp) * 0.5f;
	const float CandidateShoulderLiftFromPelvisCm = SideShoulderComp.Z - CandidatePelvisComp.Z;

	float ContradictionDeltaCm = 0.0f;
	bool bHadContradictionSource = false;
	bool bSuppressedByContradiction = false;
	const FMediaPipeBodyFusionSourceStatus& ArmChainStatus = bIsLeft
		? SourceFrame.LeftArmChainStatus
		: SourceFrame.RightArmChainStatus;
	FVector QuestShoulderComp = FVector::ZeroVector;
	bool bHadLeftQuestArmRaiseSource = false;
	bool bHadRightQuestArmRaiseSource = false;
	float QuestWristLiftFromPelvisCm = 0.0f;
	float QuestElbowLiftFromPelvisCm = 0.0f;
	float QuestArmRaiseOwnershipFade = 0.0f;
	auto UpdateArmRaiseOwnership = [&](
		const FMediaPipeBodyFusionSourceStatus& Status,
		const FVector& ElbowWorld,
		const FVector& WristWorld,
		bool& bOutHadSideQuestArmSource,
		const bool bRecordSide)
	{
		if (!Status.IsFresh())
		{
			return;
		}

		const FVector ElbowComp = WorldToComponent.TransformPosition(ElbowWorld);
		const FVector WristComp = WorldToComponent.TransformPosition(WristWorld);
		if (!IsFiniteVector(ElbowComp) || !IsFiniteVector(WristComp))
		{
			return;
		}

		bOutHadSideQuestArmSource = true;
		const float WristLiftFromPelvisCm = WristComp.Z - CandidatePelvisComp.Z;
		const float ElbowLiftFromPelvisCm = ElbowComp.Z - CandidatePelvisComp.Z;
		if (bRecordSide)
		{
			QuestWristLiftFromPelvisCm = WristLiftFromPelvisCm;
			QuestElbowLiftFromPelvisCm = ElbowLiftFromPelvisCm;
		}

		const float FadeStartCm = FMath::Max(0.0f, Settings.QuestArmRaiseFadeStartCm);
		const float FadeFullCm = FMath::Max(FadeStartCm + 0.5f, Settings.QuestArmRaiseFadeFullCm);
		const float ArmLiftCm = FMath::Max(WristLiftFromPelvisCm, ElbowLiftFromPelvisCm);
		QuestArmRaiseOwnershipFade = FMath::Max(
			QuestArmRaiseOwnershipFade,
			RemapPositiveUnbounded(ArmLiftCm, FadeStartCm, FadeFullCm));
	};

	if (Settings.ContradictionCm > KINDA_SMALL_NUMBER && ArmChainStatus.IsFresh())
	{
		const FVector QuestShoulderWorld = bIsLeft
			? SourceFrame.LeftArmShoulderWorld
			: SourceFrame.RightArmShoulderWorld;
		QuestShoulderComp = WorldToComponent.TransformPosition(QuestShoulderWorld);
		if (IsFiniteVector(QuestShoulderComp))
		{
			bHadContradictionSource = true;
			ContradictionDeltaCm = FMath::Abs(SideShoulderComp.Z - QuestShoulderComp.Z);
			bSuppressedByContradiction = ContradictionDeltaCm > Settings.ContradictionCm;
		}
	}
	UpdateArmRaiseOwnership(
		SourceFrame.LeftArmChainStatus,
		SourceFrame.LeftArmElbowWorld,
		SourceFrame.LeftArmWristWorld,
		bHadLeftQuestArmRaiseSource,
		bIsLeft);
	UpdateArmRaiseOwnership(
		SourceFrame.RightArmChainStatus,
		SourceFrame.RightArmElbowWorld,
		SourceFrame.RightArmWristWorld,
		bHadRightQuestArmRaiseSource,
		!bIsLeft);
	const bool bHadQuestArmRaiseSource = bHadLeftQuestArmRaiseSource && bHadRightQuestArmRaiseSource;
	const bool bSuppressedByArmOwnership =
		!bHadQuestArmRaiseSource;
	const float QuestArmRaiseLiftWeight = bSuppressedByArmOwnership ? 0.0f : 1.0f;

	const float ShoulderHeadClearanceCm = SideHeadComp.Z - SideShoulderComp.Z;
	if (!FMath::IsFinite(ShoulderHeadClearanceCm) || ShoulderHeadClearanceCm <= KINDA_SMALL_NUMBER)
	{
		InOutSideState.Reset();
		return false;
	}

	const bool bNeutralSampleAccepted = ShouldAcceptNeutralReferenceSample(
		InOutSideState,
		Settings,
		CandidateShoulderLiftFromPelvisCm,
		ShoulderHeadClearanceCm,
		bSuppressedByContradiction,
		bHadQuestArmRaiseSource,
		QuestArmRaiseOwnershipFade);
	if (bNeutralSampleAccepted)
	{
		UpdateNeutralReference(
			CandidateShoulderLiftFromPelvisCm,
			ShoulderHeadClearanceCm,
			DeltaSeconds,
			Settings,
			InOutSideState);
	}

	const bool bNeutralReferenceReady = InOutSideState.HasReadyNeutralReference(Settings);
	const float ReferenceShoulderLiftFromPelvisCm = InOutSideState.bHasNeutralReference
		? InOutSideState.NeutralShoulderLiftFromPelvisCm
		: CandidateShoulderLiftFromPelvisCm;
	const float RefShoulderHeadClearanceCm = InOutSideState.bHasNeutralReference
		? InOutSideState.NeutralShoulderHeadClearanceCm
		: ShoulderHeadClearanceCm;
	const float RawLiftDeltaCm = CandidateShoulderLiftFromPelvisCm - ReferenceShoulderLiftFromPelvisCm;
	const float SignedClearanceLiftCm = RefShoulderHeadClearanceCm - ShoulderHeadClearanceCm;
	const float SignedLiftEvidenceCm = RawLiftDeltaCm;
	const float ShoulderHeadClearanceShrugCm = FMath::Max(0.0f, SignedClearanceLiftCm);
	const float PositiveRawLiftCm = FMath::Max(0.0f, RawLiftDeltaCm);
	const float RawLiftConfirmationWeight = RemapPositiveUnbounded(
		PositiveRawLiftCm,
		FMath::Max(0.0f, Settings.ShrugStartCm),
		FMath::Max(Settings.ShrugStartCm + 0.5f, Settings.ShrugFullCm));
	const float ClearancePrimaryEvidenceCm = SignedClearanceLiftCm;
	const float PositiveLiftEvidenceCm = PositiveRawLiftCm;
	const float AppliedResponseScale = FMath::Clamp(Settings.ResponseScale, 0.0f, 1.0f);
	const bool bEvidenceSafeToApply =
		bNeutralReferenceReady &&
		!bSuppressedByContradiction &&
		!bSuppressedByArmOwnership &&
		PositiveLiftEvidenceCm > FMath::Max(KINDA_SMALL_NUMBER, Settings.ShrugStartCm);
	const float UnfadedTargetLiftCm = FMath::Clamp(
		bEvidenceSafeToApply
			? PositiveLiftEvidenceCm * AppliedResponseScale * FMath::Clamp(Settings.Blend, 0.0f, 1.0f)
			: 0.0f,
		0.0f,
		Settings.MaxLiftCm);
	const float TargetLiftCm = UnfadedTargetLiftCm * QuestArmRaiseLiftWeight;
	const bool bClampHit = UnfadedTargetLiftCm >= Settings.MaxLiftCm - KINDA_SMALL_NUMBER &&
		Settings.MaxLiftCm > KINDA_SMALL_NUMBER;

	if (bSuppressedByContradiction || bSuppressedByArmOwnership || !bNeutralReferenceReady)
	{
		InOutSideState.bHasSmoothedLift = false;
		InOutSideState.SmoothedLiftCm = 0.0f;
	}
	else if (!InOutSideState.bHasSmoothedLift)
	{
		InOutSideState.SmoothedLiftCm = TargetLiftCm;
		InOutSideState.bHasSmoothedLift = true;
	}
	else
	{
		const float Alpha = HalfLifeToAlpha(Settings.HalfLifeSeconds, DeltaSeconds);
		InOutSideState.SmoothedLiftCm = FMath::Lerp(InOutSideState.SmoothedLiftCm, TargetLiftCm, Alpha);
	}

	OutResult.bValid = true;
	OutResult.bSuppressedByContradiction = bSuppressedByContradiction;
	OutResult.bSuppressedByArmOwnership = bSuppressedByArmOwnership;
	OutResult.bNeutralReferenceReady = bNeutralReferenceReady;
	OutResult.bNeutralSampleAccepted = bNeutralSampleAccepted;
	OutResult.bClampHit = bClampHit;
	OutResult.bHadContradictionSource = bHadContradictionSource;
	OutResult.bHadQuestArmRaiseSource = bHadQuestArmRaiseSource;
	OutResult.SourceMode = HeadPair.SourceMode;
	OutResult.SourceReliability = FMath::Min(
		HeadPair.Reliability,
		FMath::Min(
			FMath::Min(SideShoulderReliability, OppositeShoulderReliability),
			FMath::Min(LeftHipReliability, RightHipReliability)));
	OutResult.NeutralObservationSeconds = InOutSideState.NeutralObservationSeconds;
	OutResult.CandidateShoulderLiftFromPelvisCm = CandidateShoulderLiftFromPelvisCm;
	OutResult.ReferenceShoulderLiftFromPelvisCm = ReferenceShoulderLiftFromPelvisCm;
	OutResult.RawLiftDeltaCm = RawLiftDeltaCm;
	OutResult.ShoulderHeadClearanceCm = ShoulderHeadClearanceCm;
	OutResult.ShoulderHeadClearanceReferenceCm = RefShoulderHeadClearanceCm;
	OutResult.ShoulderHeadClearanceShrugCm = ShoulderHeadClearanceShrugCm;
	OutResult.ClearancePrimaryEvidenceCm = ClearancePrimaryEvidenceCm;
	OutResult.RawLiftConfirmationWeight = RawLiftConfirmationWeight;
	OutResult.SignedLiftEvidenceCm = SignedLiftEvidenceCm;
	OutResult.SignedTargetLiftCm = TargetLiftCm;
	OutResult.AppliedResponseScale = AppliedResponseScale;
	OutResult.PositiveLiftEvidenceCm = PositiveLiftEvidenceCm;
	OutResult.UnfadedPositiveTargetLiftCm = UnfadedTargetLiftCm;
	OutResult.QuestWristLiftFromPelvisCm = QuestWristLiftFromPelvisCm;
	OutResult.QuestElbowLiftFromPelvisCm = QuestElbowLiftFromPelvisCm;
	OutResult.QuestArmRaiseOwnershipFade = QuestArmRaiseOwnershipFade;
	OutResult.QuestArmRaiseLiftWeight = QuestArmRaiseLiftWeight;
	OutResult.PositiveTargetLiftCm = TargetLiftCm;
	OutResult.ContradictionDeltaCm = ContradictionDeltaCm;
	OutResult.SmoothedLiftCm =
		(bSuppressedByContradiction || bSuppressedByArmOwnership || !bNeutralReferenceReady)
			? 0.0f
			: InOutSideState.SmoothedLiftCm;
	return true;
}
