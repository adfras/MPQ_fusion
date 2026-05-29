#include "MediaPipeQuestHandCompareDiagnostics.h"

#include "Engine/Engine.h"
#include "HeadMountedDisplayTypes.h"
#include "IHandTracker.h"
#include "MediaPipePoseLog.h"

namespace
{
	constexpr int32 QuestFingerSegmentsPerFinger = 3;

	int32 QuestFingerBoneIndex(const int32 FingerIndex, const int32 SegmentIndex)
	{
		return FingerIndex * QuestFingerSegmentsPerFinger + SegmentIndex;
	}

	bool IsFiniteVector(const FVector& Vector)
	{
		return FMath::IsFinite(Vector.X) && FMath::IsFinite(Vector.Y) && FMath::IsFinite(Vector.Z);
	}

	bool IsUsableQuestWristPosition(const FVector& WristWorld)
	{
		return IsFiniteVector(WristWorld) && !WristWorld.IsNearlyZero();
	}

	float QuestAngleBetweenSegmentsDeg(const FVector& A, const FVector& B)
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

	float SignedAngleDegAroundAxis(const FVector& From, const FVector& To, const FVector& Axis)
	{
		const FVector FromN = From.GetSafeNormal();
		const FVector ToN = To.GetSafeNormal();
		const FVector AxisN = Axis.GetSafeNormal();
		if (FromN.IsNearlyZero() || ToN.IsNearlyZero() || AxisN.IsNearlyZero())
		{
			return 0.0f;
		}

		const float SinAngle = FVector::DotProduct(AxisN, FVector::CrossProduct(FromN, ToN));
		const float CosAngle = FMath::Clamp(FVector::DotProduct(FromN, ToN), -1.0f, 1.0f);
		return FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(SinAngle, CosAngle)));
	}

	bool BuildPalmBasisFromPoints(
		const FVector& Wrist,
		const FVector& Index,
		const FVector& Middle,
		const FVector& Pinky,
		FVector& OutForward,
		FVector& OutAcross,
		FVector& OutUp,
		float& OutBasisSin)
	{
		OutForward = ((Index + Pinky) * 0.5f - Wrist).GetSafeNormal();
		if (OutForward.IsNearlyZero())
		{
			OutForward = (Middle - Wrist).GetSafeNormal();
		}
		OutAcross = (Index - Pinky).GetSafeNormal();
		if (OutForward.IsNearlyZero() || OutAcross.IsNearlyZero())
		{
			return false;
		}

		const FVector Cross = FVector::CrossProduct(OutForward, OutAcross);
		OutBasisSin = Cross.Size();
		OutUp = Cross.GetSafeNormal();
		return !OutUp.IsNearlyZero();
	}

	bool TryGetBoneLocationComp(
		FCSPose<FCompactPose>& CSPose,
		const FBoneReference& Bone,
		FVector& OutLocationComp)
	{
		if (!Bone.IsValidToEvaluate())
		{
			return false;
		}

		OutLocationComp = CSPose.GetComponentSpaceTransform(Bone.CachedCompactPoseIndex).GetTranslation();
		return true;
	}
}

FMediaPipeQuestHandCompareSnapshot FMediaPipeQuestHandCompareDiagnostics::BuildSnapshot(
	const FMediaPipeQuestHandCompareBuildInput& Input,
	FCSPose<FCompactPose>& CSPose,
	const FBoneReference& HandBone,
	const FBoneReference* FingerBones,
	TFunctionRef<bool(const FVector& QuestDirectionWorld, FVector& OutMediaDirectionWorld)> MapQuestDirectionToMediaWorld)
{
	static_assert(QuestHandKeypointCount == EHandKeypointCount, "Quest hand snapshot must match Unreal's OpenXR hand keypoint count.");

	FMediaPipeQuestHandCompareSnapshot Snapshot;
	Snapshot.TargetActorLabel = Input.TargetActorName.ToString();
	Snapshot.bIsLeft = Input.bIsLeft;
	Snapshot.CompareMode = Input.CompareMode;
	Snapshot.bVisibleMetaHuman = Input.bVisibleMetaHuman;
	Snapshot.bQuestHandRotationApplied = Input.bQuestHandRotationApplied;
	Snapshot.bArmIKBranchEntered = Input.bArmIKBranchEntered;
	Snapshot.bForceArmIK = Input.bForceArmIK;
	Snapshot.QuestWristTrace = Input.QuestWristTrace;
	Snapshot.QuestHandRotationTrace = Input.QuestHandRotationTrace;
	Snapshot.TargetCompLocation = Input.TargetCompTransform.GetLocation();
	Snapshot.SourceActorLocation = Input.PoseToWorldTransform.GetLocation();
	Snapshot.AvatarHandComp = Input.AvatarHandComp;
	Snapshot.AvatarHandWorld = Input.TargetCompTransform.TransformPosition(Input.AvatarHandComp);
	Snapshot.SolvedWristWorld = Input.SolvedWristWorld;
	Snapshot.ShoulderWorld = Input.ShoulderWorld;
	Snapshot.TargetToSourceLocCm = FVector::Dist(Snapshot.TargetCompLocation, Snapshot.SourceActorLocation);

	const TStaticArray<FVector, QuestHandKeypointCount>* QuestPositions = nullptr;
	if (Input.QuestHands)
	{
		QuestPositions = Input.bIsLeft ? &Input.QuestHands->LeftPositionsWorld : &Input.QuestHands->RightPositionsWorld;
	}
	auto GetQuestPosition = [QuestPositions](const EHandKeypoint Keypoint) -> FVector
	{
		return QuestPositions ? (*QuestPositions)[static_cast<int32>(Keypoint)] : FVector::ZeroVector;
	};

	Snapshot.RawQuestWristWorld = GetQuestPosition(EHandKeypoint::Wrist);
	Snapshot.RawQuestWristInTargetComp = IsUsableQuestWristPosition(Snapshot.RawQuestWristWorld)
		? Input.TargetCompTransform.InverseTransformPosition(Snapshot.RawQuestWristWorld)
		: FVector::ZeroVector;
	Snapshot.MediaPipeWristInTargetComp = IsUsableQuestWristPosition(Input.QuestWristTrace.MediaPipeWristWorld)
		? Input.TargetCompTransform.InverseTransformPosition(Input.QuestWristTrace.MediaPipeWristWorld)
		: FVector::ZeroVector;
	Snapshot.MappedQuestWristInTargetComp =
		Input.QuestWristTrace.bMapped != 0 && IsUsableQuestWristPosition(Input.QuestWristTrace.MappedQuestWristWorld)
			? Input.TargetCompTransform.InverseTransformPosition(Input.QuestWristTrace.MappedQuestWristWorld)
			: FVector::ZeroVector;
	Snapshot.FinalWristInTargetComp = IsUsableQuestWristPosition(Input.QuestWristTrace.FinalWristWorld)
		? Input.TargetCompTransform.InverseTransformPosition(Input.QuestWristTrace.FinalWristWorld)
		: FVector::ZeroVector;

	Snapshot.RawQuestToAvatarCm = IsUsableQuestWristPosition(Snapshot.RawQuestWristWorld)
		? FVector::Dist(Snapshot.RawQuestWristWorld, Snapshot.AvatarHandWorld)
		: 0.0f;
	Snapshot.MappedQuestToAvatarCm =
		Input.QuestWristTrace.bMapped != 0 && IsUsableQuestWristPosition(Input.QuestWristTrace.MappedQuestWristWorld)
			? FVector::Dist(Input.QuestWristTrace.MappedQuestWristWorld, Snapshot.AvatarHandWorld)
			: 0.0f;
	Snapshot.FinalWristToAvatarCm = IsUsableQuestWristPosition(Input.QuestWristTrace.FinalWristWorld)
		? FVector::Dist(Input.QuestWristTrace.FinalWristWorld, Snapshot.AvatarHandWorld)
		: 0.0f;
	Snapshot.RawQuestToAvatarTargetCompCm = IsUsableQuestWristPosition(Snapshot.RawQuestWristWorld)
		? FVector::Dist(Snapshot.RawQuestWristInTargetComp, Snapshot.AvatarHandComp)
		: 0.0f;
	Snapshot.MappedQuestToAvatarTargetCompCm =
		Input.QuestWristTrace.bMapped != 0 && IsUsableQuestWristPosition(Input.QuestWristTrace.MappedQuestWristWorld)
			? FVector::Dist(Snapshot.MappedQuestWristInTargetComp, Snapshot.AvatarHandComp)
			: 0.0f;
	Snapshot.FinalWristToAvatarTargetCompCm = IsUsableQuestWristPosition(Input.QuestWristTrace.FinalWristWorld)
		? FVector::Dist(Snapshot.FinalWristInTargetComp, Snapshot.AvatarHandComp)
		: 0.0f;
	Snapshot.MediaPipeWristToAvatarTargetCompCm = IsUsableQuestWristPosition(Input.QuestWristTrace.MediaPipeWristWorld)
		? FVector::Dist(Snapshot.MediaPipeWristInTargetComp, Snapshot.AvatarHandComp)
		: 0.0f;
	Snapshot.MappedOffsetFromMediaPipeCm =
		Input.QuestWristTrace.bMapped != 0 && IsUsableQuestWristPosition(Input.QuestWristTrace.MappedQuestWristWorld)
			? FVector::Dist(Input.QuestWristTrace.MappedQuestWristWorld, Input.QuestWristTrace.MediaPipeWristWorld)
			: 0.0f;
	Snapshot.FinalOffsetFromMediaPipeCm = IsUsableQuestWristPosition(Input.QuestWristTrace.FinalWristWorld)
		? FVector::Dist(Input.QuestWristTrace.FinalWristWorld, Input.QuestWristTrace.MediaPipeWristWorld)
		: 0.0f;
	Snapshot.FinalToSolvedWristCm = IsUsableQuestWristPosition(Input.QuestWristTrace.FinalWristWorld)
		? FVector::Dist(Input.QuestWristTrace.FinalWristWorld, Input.SolvedWristWorld)
		: 0.0f;

	Snapshot.QuestPalmWristWorld = GetQuestPosition(EHandKeypoint::Wrist);
	Snapshot.QuestPalmIndexWorld = GetQuestPosition(EHandKeypoint::IndexProximal);
	Snapshot.QuestPalmMiddleWorld = GetQuestPosition(EHandKeypoint::MiddleProximal);
	Snapshot.QuestPalmPinkyWorld = GetQuestPosition(EHandKeypoint::LittleProximal);
	FVector QuestPalmForwardWorld = FVector::ZeroVector;
	FVector QuestPalmAcrossWorld = FVector::ZeroVector;
	FVector QuestPalmUpWorld = FVector::ZeroVector;
	if (IsUsableQuestWristPosition(Snapshot.QuestPalmWristWorld) &&
		IsUsableQuestWristPosition(Snapshot.QuestPalmIndexWorld) &&
		IsUsableQuestWristPosition(Snapshot.QuestPalmMiddleWorld) &&
		IsUsableQuestWristPosition(Snapshot.QuestPalmPinkyWorld) &&
		BuildPalmBasisFromPoints(
			Snapshot.QuestPalmWristWorld,
			Snapshot.QuestPalmIndexWorld,
			Snapshot.QuestPalmMiddleWorld,
			Snapshot.QuestPalmPinkyWorld,
			QuestPalmForwardWorld,
			QuestPalmAcrossWorld,
			QuestPalmUpWorld,
			Snapshot.QuestPalmBasisSin))
	{
		Snapshot.RawQuestPalmForwardComp = Input.TargetCompTransform.InverseTransformVectorNoScale(QuestPalmForwardWorld).GetSafeNormal();
		Snapshot.RawQuestPalmAcrossComp = Input.TargetCompTransform.InverseTransformVectorNoScale(QuestPalmAcrossWorld).GetSafeNormal();
		Snapshot.RawQuestPalmUpComp = Input.TargetCompTransform.InverseTransformVectorNoScale(QuestPalmUpWorld).GetSafeNormal();
		Snapshot.bHasRawQuestPalmPlane =
			!Snapshot.RawQuestPalmForwardComp.IsNearlyZero() &&
			!Snapshot.RawQuestPalmAcrossComp.IsNearlyZero() &&
			!Snapshot.RawQuestPalmUpComp.IsNearlyZero();

		FVector MappedQuestPalmForwardWorld = FVector::ZeroVector;
		FVector MappedQuestPalmAcrossWorld = FVector::ZeroVector;
		FVector MappedQuestPalmUpWorld = FVector::ZeroVector;
		if (MapQuestDirectionToMediaWorld(QuestPalmForwardWorld, MappedQuestPalmForwardWorld) &&
			MapQuestDirectionToMediaWorld(QuestPalmAcrossWorld, MappedQuestPalmAcrossWorld) &&
			MapQuestDirectionToMediaWorld(QuestPalmUpWorld, MappedQuestPalmUpWorld))
		{
			Snapshot.QuestPalmForwardComp = Input.TargetCompTransform.InverseTransformVectorNoScale(MappedQuestPalmForwardWorld).GetSafeNormal();
			Snapshot.QuestPalmAcrossComp = Input.TargetCompTransform.InverseTransformVectorNoScale(MappedQuestPalmAcrossWorld).GetSafeNormal();
			Snapshot.QuestPalmUpComp = Input.TargetCompTransform.InverseTransformVectorNoScale(MappedQuestPalmUpWorld).GetSafeNormal();
			Snapshot.bHasQuestPalmPlane =
				!Snapshot.QuestPalmForwardComp.IsNearlyZero() &&
				!Snapshot.QuestPalmAcrossComp.IsNearlyZero() &&
				!Snapshot.QuestPalmUpComp.IsNearlyZero();
			Snapshot.bMappedQuestPalmPlane = Snapshot.bHasQuestPalmPlane;
		}

		if (!Snapshot.bHasQuestPalmPlane && Snapshot.bHasRawQuestPalmPlane)
		{
			Snapshot.QuestPalmForwardComp = Snapshot.RawQuestPalmForwardComp;
			Snapshot.QuestPalmAcrossComp = Snapshot.RawQuestPalmAcrossComp;
			Snapshot.QuestPalmUpComp = Snapshot.RawQuestPalmUpComp;
			Snapshot.bHasQuestPalmPlane = true;
		}
	}

	const int32 IndexBaseBoneIndex = QuestFingerBoneIndex(1, 0);
	const int32 MiddleBaseBoneIndex = QuestFingerBoneIndex(2, 0);
	const int32 PinkyBaseBoneIndex = QuestFingerBoneIndex(4, 0);
	Snapshot.HandBoneName = HandBone.BoneName;
	if (FingerBones)
	{
		Snapshot.IndexBoneName = FingerBones[IndexBaseBoneIndex].BoneName;
		Snapshot.MiddleBoneName = FingerBones[MiddleBaseBoneIndex].BoneName;
		Snapshot.PinkyBoneName = FingerBones[PinkyBaseBoneIndex].BoneName;
		if (TryGetBoneLocationComp(CSPose, HandBone, Snapshot.AvatarPalmHandComp) &&
			TryGetBoneLocationComp(CSPose, FingerBones[IndexBaseBoneIndex], Snapshot.AvatarPalmIndexComp) &&
			TryGetBoneLocationComp(CSPose, FingerBones[MiddleBaseBoneIndex], Snapshot.AvatarPalmMiddleComp) &&
			TryGetBoneLocationComp(CSPose, FingerBones[PinkyBaseBoneIndex], Snapshot.AvatarPalmPinkyComp))
		{
			Snapshot.bHasAvatarPalmPlane = BuildPalmBasisFromPoints(
				Snapshot.AvatarPalmHandComp,
				Snapshot.AvatarPalmIndexComp,
				Snapshot.AvatarPalmMiddleComp,
				Snapshot.AvatarPalmPinkyComp,
				Snapshot.AvatarPalmForwardComp,
				Snapshot.AvatarPalmAcrossComp,
				Snapshot.AvatarPalmUpComp,
				Snapshot.AvatarPalmBasisSin);
		}
	}

	if (Snapshot.bHasQuestPalmPlane && Snapshot.bHasAvatarPalmPlane)
	{
		Snapshot.PalmPlaneForwardErrDeg = QuestAngleBetweenSegmentsDeg(Snapshot.QuestPalmForwardComp, Snapshot.AvatarPalmForwardComp);
		Snapshot.PalmPlaneAcrossErrDeg = QuestAngleBetweenSegmentsDeg(Snapshot.QuestPalmAcrossComp, Snapshot.AvatarPalmAcrossComp);
		Snapshot.PalmPlaneUpErrDeg = QuestAngleBetweenSegmentsDeg(Snapshot.QuestPalmUpComp, Snapshot.AvatarPalmUpComp);
		const FVector AvatarUpProjectedOnQuestForward =
			(Snapshot.AvatarPalmUpComp - FVector::DotProduct(Snapshot.AvatarPalmUpComp, Snapshot.QuestPalmForwardComp) * Snapshot.QuestPalmForwardComp).GetSafeNormal();
		const FVector QuestUpProjectedOnQuestForward =
			(Snapshot.QuestPalmUpComp - FVector::DotProduct(Snapshot.QuestPalmUpComp, Snapshot.QuestPalmForwardComp) * Snapshot.QuestPalmForwardComp).GetSafeNormal();
		if (!AvatarUpProjectedOnQuestForward.IsNearlyZero() && !QuestUpProjectedOnQuestForward.IsNearlyZero())
		{
			Snapshot.PalmPlaneSignedRollErrDeg = SignedAngleDegAroundAxis(
				QuestUpProjectedOnQuestForward,
				AvatarUpProjectedOnQuestForward,
				Snapshot.QuestPalmForwardComp);
		}
	}

	if (Snapshot.bHasRawQuestPalmPlane && Snapshot.bHasAvatarPalmPlane)
	{
		Snapshot.RawPalmPlaneForwardErrDeg = QuestAngleBetweenSegmentsDeg(Snapshot.RawQuestPalmForwardComp, Snapshot.AvatarPalmForwardComp);
		Snapshot.RawPalmPlaneAcrossErrDeg = QuestAngleBetweenSegmentsDeg(Snapshot.RawQuestPalmAcrossComp, Snapshot.AvatarPalmAcrossComp);
		Snapshot.RawPalmPlaneUpErrDeg = QuestAngleBetweenSegmentsDeg(Snapshot.RawQuestPalmUpComp, Snapshot.AvatarPalmUpComp);
		const FVector AvatarUpProjectedOnRawQuestForward =
			(Snapshot.AvatarPalmUpComp - FVector::DotProduct(Snapshot.AvatarPalmUpComp, Snapshot.RawQuestPalmForwardComp) * Snapshot.RawQuestPalmForwardComp).GetSafeNormal();
		const FVector RawQuestUpProjectedOnRawQuestForward =
			(Snapshot.RawQuestPalmUpComp - FVector::DotProduct(Snapshot.RawQuestPalmUpComp, Snapshot.RawQuestPalmForwardComp) * Snapshot.RawQuestPalmForwardComp).GetSafeNormal();
		if (!AvatarUpProjectedOnRawQuestForward.IsNearlyZero() && !RawQuestUpProjectedOnRawQuestForward.IsNearlyZero())
		{
			Snapshot.RawPalmPlaneSignedRollErrDeg = SignedAngleDegAroundAxis(
				RawQuestUpProjectedOnRawQuestForward,
				AvatarUpProjectedOnRawQuestForward,
				Snapshot.RawQuestPalmForwardComp);
		}
	}

	return Snapshot;
}

void FMediaPipeQuestHandCompareDiagnostics::EmitReport(
	const FMediaPipeQuestHandCompareBuildInput& Input,
	FCSPose<FCompactPose>& CSPose,
	const FBoneReference& HandBone,
	const FBoneReference* FingerBones,
	TFunctionRef<bool(const FVector& QuestDirectionWorld, FVector& OutMediaDirectionWorld)> MapQuestDirectionToMediaWorld)
{
	const FMediaPipeQuestHandCompareSnapshot CompareSnapshot =
		BuildSnapshot(Input, CSPose, HandBone, FingerBones, MapQuestDirectionToMediaWorld);

	UE_LOG(LogMediaPipePose, Log, TEXT("%s"), *FormatQuestPalmPlaneCompareLog(CompareSnapshot));
	UE_LOG(LogMediaPipePose, Log, TEXT("%s"), *FormatQuestHandCompareLog(CompareSnapshot));

	const FString TargetActorLabel = Input.TargetActorName.ToString();
	const bool bIsMannyActor = TargetActorLabel.Contains(TEXT("Manny"), ESearchCase::IgnoreCase);
	if (Input.CompareMode >= 2 && GEngine && (Input.bVisibleMetaHuman || !bIsMannyActor))
	{
		const FMediaPipeQuestHandCompareHudFormatResult CompareHud = FormatQuestHandCompareHud(CompareSnapshot);
		GEngine->AddOnScreenDebugMessage(
			Input.bIsLeft ? 909141 : 909142,
			0.30f,
			CompareHud.Color,
			CompareHud.Text);
	}
}

FString FMediaPipeQuestHandCompareDiagnostics::FormatQuestPalmPlaneCompareLog(
	const FMediaPipeQuestHandCompareSnapshot& Snapshot)
{
	return FString::Printf(
		TEXT("mp.QuestPalmPlaneCompare: actor=%s side=%s questTracked=%d validQuestPalm=%d questPalmMapped=%d validAvatarPalm=%d palmForwardErrDeg=%.1f palmAcrossErrDeg=%.1f palmUpErrDeg=%.1f signedPalmRollErrDeg=%.1f rawPalmForwardErrDeg=%.1f rawPalmAcrossErrDeg=%.1f rawPalmUpErrDeg=%.1f rawSignedPalmRollErrDeg=%.1f questBasisSin=%.2f avatarBasisSin=%.2f questPalmFwd=%s questPalmAcross=%s questPalmUp=%s rawQuestPalmFwd=%s rawQuestPalmAcross=%s rawQuestPalmUp=%s avatarPalmFwd=%s avatarPalmAcross=%s avatarPalmUp=%s avatarPalmHand=%s avatarPalmIndex=%s avatarPalmMiddle=%s avatarPalmPinky=%s rawQuestWrist=%s rawQuestIndex=%s rawQuestMiddle=%s rawQuestPinky=%s handBone=%s indexBone=%s middleBone=%s pinkyBone=%s"),
		*Snapshot.TargetActorLabel,
		Snapshot.bIsLeft ? TEXT("L") : TEXT("R"),
		Snapshot.QuestHandRotationTrace.bQuestTracked ? 1 : 0,
		Snapshot.bHasQuestPalmPlane ? 1 : 0,
		Snapshot.bMappedQuestPalmPlane ? 1 : 0,
		Snapshot.bHasAvatarPalmPlane ? 1 : 0,
		Snapshot.PalmPlaneForwardErrDeg,
		Snapshot.PalmPlaneAcrossErrDeg,
		Snapshot.PalmPlaneUpErrDeg,
		Snapshot.PalmPlaneSignedRollErrDeg,
		Snapshot.RawPalmPlaneForwardErrDeg,
		Snapshot.RawPalmPlaneAcrossErrDeg,
		Snapshot.RawPalmPlaneUpErrDeg,
		Snapshot.RawPalmPlaneSignedRollErrDeg,
		Snapshot.QuestPalmBasisSin,
		Snapshot.AvatarPalmBasisSin,
		*Snapshot.QuestPalmForwardComp.ToCompactString(),
		*Snapshot.QuestPalmAcrossComp.ToCompactString(),
		*Snapshot.QuestPalmUpComp.ToCompactString(),
		*Snapshot.RawQuestPalmForwardComp.ToCompactString(),
		*Snapshot.RawQuestPalmAcrossComp.ToCompactString(),
		*Snapshot.RawQuestPalmUpComp.ToCompactString(),
		*Snapshot.AvatarPalmForwardComp.ToCompactString(),
		*Snapshot.AvatarPalmAcrossComp.ToCompactString(),
		*Snapshot.AvatarPalmUpComp.ToCompactString(),
		*Snapshot.AvatarPalmHandComp.ToCompactString(),
		*Snapshot.AvatarPalmIndexComp.ToCompactString(),
		*Snapshot.AvatarPalmMiddleComp.ToCompactString(),
		*Snapshot.AvatarPalmPinkyComp.ToCompactString(),
		*Snapshot.QuestPalmWristWorld.ToCompactString(),
		*Snapshot.QuestPalmIndexWorld.ToCompactString(),
		*Snapshot.QuestPalmMiddleWorld.ToCompactString(),
		*Snapshot.QuestPalmPinkyWorld.ToCompactString(),
		*Snapshot.HandBoneName.ToString(),
		*Snapshot.IndexBoneName.ToString(),
		*Snapshot.MiddleBoneName.ToString(),
		*Snapshot.PinkyBoneName.ToString());
}

FString FMediaPipeQuestHandCompareDiagnostics::FormatQuestHandCompareLog(
	const FMediaPipeQuestHandCompareSnapshot& Snapshot)
{
	return FString::Printf(
		TEXT("mp.QuestHandCompare: actor=%s side=%s mode=%d tracked=%d handApplied=%d handLocal=%d visibleMetaHuman=%d rawQuestWrist=%s mappedQuestWrist=%s finalWrist=%s mediaPipeWrist=%s solvedWrist=%s shoulderWorld=%s avatarHand=%s targetCompLoc=%s sourceLoc=%s targetToSourceLocCm=%.1f rawQuestWristTargetComp=%s mappedQuestWristTargetComp=%s finalWristTargetComp=%s mediaPipeWristTargetComp=%s avatarHandTargetComp=%s rawQuestToAvatarCm=%.1f mappedQuestToAvatarCm=%.1f finalWristToAvatarCm=%.1f rawQuestToAvatarTargetCompCm=%.1f mappedQuestToAvatarTargetCompCm=%.1f finalWristToAvatarTargetCompCm=%.1f mediaPipeWristToAvatarTargetCompCm=%.1f mappedOffsetFromMediaPipeCm=%.1f finalOffsetFromMediaPipeCm=%.1f finalToSolvedWristCm=%.1f handOnlyToAvatarDeg=%.1f handOnlyToRetargetDeg=%.1f retargetToAvatarDeg=%.1f questBasisFwdErrDeg=%.1f questBasisUpErrDeg=%.1f questFwd=%s questUp=%s avatarFwd=%s avatarUp=%s rollTargetFwd=%s rollTargetUp=%s rawTwistDeg=%.1f limitedTwistDeg=%.1f swingAppliedDeg=%.1f semanticAxis=%u semanticScore=%.2f palmHeld=%d palmFallback=%d armIK=%d forceIK=%d twistCorrection=%d"),
		*Snapshot.TargetActorLabel,
		Snapshot.bIsLeft ? TEXT("L") : TEXT("R"),
		Snapshot.CompareMode,
		Snapshot.QuestHandRotationTrace.bQuestTracked ? 1 : 0,
		Snapshot.bQuestHandRotationApplied ? 1 : 0,
		Snapshot.QuestHandRotationTrace.bAppliedHandLocalToLowerArm ? 1 : 0,
		Snapshot.bVisibleMetaHuman ? 1 : 0,
		*Snapshot.RawQuestWristWorld.ToCompactString(),
		*Snapshot.QuestWristTrace.MappedQuestWristWorld.ToCompactString(),
		*Snapshot.QuestWristTrace.FinalWristWorld.ToCompactString(),
		*Snapshot.QuestWristTrace.MediaPipeWristWorld.ToCompactString(),
		*Snapshot.SolvedWristWorld.ToCompactString(),
		*Snapshot.ShoulderWorld.ToCompactString(),
		*Snapshot.AvatarHandWorld.ToCompactString(),
		*Snapshot.TargetCompLocation.ToCompactString(),
		*Snapshot.SourceActorLocation.ToCompactString(),
		Snapshot.TargetToSourceLocCm,
		*Snapshot.RawQuestWristInTargetComp.ToCompactString(),
		*Snapshot.MappedQuestWristInTargetComp.ToCompactString(),
		*Snapshot.FinalWristInTargetComp.ToCompactString(),
		*Snapshot.MediaPipeWristInTargetComp.ToCompactString(),
		*Snapshot.AvatarHandComp.ToCompactString(),
		Snapshot.RawQuestToAvatarCm,
		Snapshot.MappedQuestToAvatarCm,
		Snapshot.FinalWristToAvatarCm,
		Snapshot.RawQuestToAvatarTargetCompCm,
		Snapshot.MappedQuestToAvatarTargetCompCm,
		Snapshot.FinalWristToAvatarTargetCompCm,
		Snapshot.MediaPipeWristToAvatarTargetCompCm,
		Snapshot.MappedOffsetFromMediaPipeCm,
		Snapshot.FinalOffsetFromMediaPipeCm,
		Snapshot.FinalToSolvedWristCm,
		Snapshot.QuestHandRotationTrace.QuestExpectedToMannyDeg,
		Snapshot.QuestHandRotationTrace.QuestExpectedToRollTargetDeg,
		Snapshot.QuestHandRotationTrace.RollTargetToMannyDeg,
		Snapshot.QuestHandRotationTrace.QuestBasisToMannyBasisForwardErrDeg,
		Snapshot.QuestHandRotationTrace.QuestBasisToMannyBasisUpErrDeg,
		*Snapshot.QuestHandRotationTrace.QuestBasisForwardComp.ToCompactString(),
		*Snapshot.QuestHandRotationTrace.QuestBasisUpComp.ToCompactString(),
		*Snapshot.QuestHandRotationTrace.MannyAppliedBasisForwardComp.ToCompactString(),
		*Snapshot.QuestHandRotationTrace.MannyAppliedBasisUpComp.ToCompactString(),
		*Snapshot.QuestHandRotationTrace.RollTargetBasisForwardComp.ToCompactString(),
		*Snapshot.QuestHandRotationTrace.RollTargetBasisUpComp.ToCompactString(),
		Snapshot.QuestHandRotationTrace.RawTwistDeg,
		Snapshot.QuestHandRotationTrace.LimitedTwistDeg,
		Snapshot.QuestHandRotationTrace.AppliedSwingDeg,
		Snapshot.QuestHandRotationTrace.SemanticRollAxisIndex,
		Snapshot.QuestHandRotationTrace.SemanticRollAxisScore,
		Snapshot.QuestHandRotationTrace.bHeldPalmRoll ? 1 : 0,
		Snapshot.QuestHandRotationTrace.bUsedPalmRollFallback ? 1 : 0,
		Snapshot.bArmIKBranchEntered ? 1 : 0,
		Snapshot.bForceArmIK ? 1 : 0,
		Snapshot.QuestHandRotationTrace.bAppliedTwistCorrection ? 1 : 0);
}

FMediaPipeQuestHandCompareHudFormatResult FMediaPipeQuestHandCompareDiagnostics::FormatQuestHandCompareHud(
	const FMediaPipeQuestHandCompareSnapshot& Snapshot)
{
	FMediaPipeQuestHandCompareHudFormatResult Result;
	Result.Color = Snapshot.PalmPlaneUpErrDeg > 35.0f ? FColor::Yellow : FColor::Green;
	Result.Text = FString::Printf(
		TEXT("Quest vs MetaHuman hand %s %s\nraw->avatar %.1fcm mp-offset %.1fcm\nboneRot %.1fdeg basis %.1f/%.1f mapped palm F/N %.1f/%.1f roll %.1f"),
		*Snapshot.TargetActorLabel,
		Snapshot.bIsLeft ? TEXT("L") : TEXT("R"),
		Snapshot.RawQuestToAvatarCm,
		Snapshot.MappedOffsetFromMediaPipeCm,
		Snapshot.QuestHandRotationTrace.QuestExpectedToMannyDeg,
		Snapshot.QuestHandRotationTrace.QuestBasisToMannyBasisForwardErrDeg,
		Snapshot.QuestHandRotationTrace.QuestBasisToMannyBasisUpErrDeg,
		Snapshot.PalmPlaneForwardErrDeg,
		Snapshot.PalmPlaneUpErrDeg,
		Snapshot.PalmPlaneSignedRollErrDeg);
	return Result;
}
