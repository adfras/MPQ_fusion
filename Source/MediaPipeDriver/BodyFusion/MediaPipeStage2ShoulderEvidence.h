#pragma once

#include "CoreMinimal.h"
#include "MediaPipeEmbodimentCalibrationSolver.h"
#include "MediaPipeTrackingSourceTypes.h"

enum class EMediaPipeStage2ShoulderEvidenceSourceMode : uint8
{
	None = 0,
	RawWorldEars = 1,
	RawWorldEyes = 2,
	RawWorldNose = 3
};

struct MEDIAPIPEDRIVER_API FMediaPipeStage2ShoulderEvidenceSettings
{
	float Blend = 1.0f;
	float ResponseScale = 1.0f;
	float MaxLiftCm = 5.0f;
	float HalfLifeSeconds = 0.04f;
	float ContradictionCm = 20.0f;
	float QuestArmRaiseFadeStartCm = 35.0f;
	float QuestArmRaiseFadeFullCm = 50.0f;
	float ShrugStartCm = 2.0f;
	float ShrugFullCm = 8.0f;
	float NeutralHoldSeconds = 0.25f;
	int32 NeutralHoldFrames = 8;
	float NeutralUpdateToleranceCm = 1.25f;
	float MinShoulderReliability = 0.30f;
	float MinHipReliability = 0.30f;
	float MinHeadReliability = 0.20f;
};

struct MEDIAPIPEDRIVER_API FMediaPipeStage2ShoulderEvidenceSideState
{
	bool bHasSmoothedLift = false;
	float SmoothedLiftCm = 0.0f;
	bool bHasNeutralReference = false;
	float NeutralShoulderLiftFromPelvisCm = 0.0f;
	float NeutralShoulderHeadClearanceCm = 0.0f;
	float NeutralObservationSeconds = 0.0f;
	int32 NeutralObservationFrames = 0;

	void Reset();
	bool HasReadyNeutralReference(const FMediaPipeStage2ShoulderEvidenceSettings& Settings) const;
};

struct MEDIAPIPEDRIVER_API FMediaPipeStage2ShoulderEvidenceResult
{
	bool bValid = false;
	bool bSuppressedByContradiction = false;
	bool bSuppressedByArmOwnership = false;
	bool bNeutralReferenceReady = false;
	bool bNeutralSampleAccepted = false;
	bool bClampHit = false;
	bool bHadContradictionSource = false;
	bool bHadQuestArmRaiseSource = false;
	EMediaPipeStage2ShoulderEvidenceSourceMode SourceMode = EMediaPipeStage2ShoulderEvidenceSourceMode::None;
	float SourceReliability = 0.0f;
	float NeutralObservationSeconds = 0.0f;
	float CandidateShoulderLiftFromPelvisCm = 0.0f;
	float ReferenceShoulderLiftFromPelvisCm = 0.0f;
	float RawLiftDeltaCm = 0.0f;
	float ShoulderHeadClearanceCm = 0.0f;
	float ShoulderHeadClearanceReferenceCm = 0.0f;
	float ShoulderHeadClearanceShrugCm = 0.0f;
	float ClearancePrimaryEvidenceCm = 0.0f;
	float RawLiftConfirmationWeight = 0.0f;
	float SignedLiftEvidenceCm = 0.0f;
	float SignedTargetLiftCm = 0.0f;
	float AppliedResponseScale = 0.0f;
	float PositiveLiftEvidenceCm = 0.0f;
	float UnfadedPositiveTargetLiftCm = 0.0f;
	float QuestWristLiftFromPelvisCm = 0.0f;
	float QuestElbowLiftFromPelvisCm = 0.0f;
	float QuestArmRaiseOwnershipFade = 0.0f;
	float QuestArmRaiseLiftWeight = 1.0f;
	float PositiveTargetLiftCm = 0.0f;
	float ContradictionDeltaCm = 0.0f;
	float SmoothedLiftCm = 0.0f;
};

class MEDIAPIPEDRIVER_API FMediaPipeStage2ShoulderEvidence
{
public:
	static bool BuildSideEvidence(
		const FMediaPipeTrackingSourceFrame& SourceFrame,
		const FMediaPipeEmbodimentCalibration& Calibration,
		bool bIsLeft,
		const FTransform& WorldToComponent,
		const FMediaPipeStage2ShoulderEvidenceSettings& Settings,
		float DeltaSeconds,
		FMediaPipeStage2ShoulderEvidenceSideState& InOutSideState,
		FMediaPipeStage2ShoulderEvidenceResult& OutResult);

	static float HalfLifeToAlpha(float HalfLifeSeconds, float DeltaSeconds);
};
