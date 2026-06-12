#include "MediaPipeQuestFingerSolver.h"

namespace MediaPipeQuestFingerSolver
{
	static_assert(QuestHandKeypointCount == EHandKeypointCount, "Quest hand snapshot must match Unreal's OpenXR hand keypoint count.");

	const TCHAR* const QuestFingerBoneNamesL[QuestFingerBoneCount] =
	{
		TEXT("thumb_01_l"), TEXT("thumb_02_l"), TEXT("thumb_03_l"),
		TEXT("index_01_l"), TEXT("index_02_l"), TEXT("index_03_l"),
		TEXT("middle_01_l"), TEXT("middle_02_l"), TEXT("middle_03_l"),
		TEXT("ring_01_l"), TEXT("ring_02_l"), TEXT("ring_03_l"),
		TEXT("pinky_01_l"), TEXT("pinky_02_l"), TEXT("pinky_03_l")
	};

	const TCHAR* const QuestFingerBoneNamesR[QuestFingerBoneCount] =
	{
		TEXT("thumb_01_r"), TEXT("thumb_02_r"), TEXT("thumb_03_r"),
		TEXT("index_01_r"), TEXT("index_02_r"), TEXT("index_03_r"),
		TEXT("middle_01_r"), TEXT("middle_02_r"), TEXT("middle_03_r"),
		TEXT("ring_01_r"), TEXT("ring_02_r"), TEXT("ring_03_r"),
		TEXT("pinky_01_r"), TEXT("pinky_02_r"), TEXT("pinky_03_r")
	};

	const TCHAR* const QuestFingerMetacarpalBoneNamesL[QuestMetacarpalBoneCount] =
	{
		TEXT("index_metacarpal_l"),
		TEXT("middle_metacarpal_l"),
		TEXT("ring_metacarpal_l"),
		TEXT("pinky_metacarpal_l")
	};

	const TCHAR* const QuestFingerMetacarpalBoneNamesR[QuestMetacarpalBoneCount] =
	{
		TEXT("index_metacarpal_r"),
		TEXT("middle_metacarpal_r"),
		TEXT("ring_metacarpal_r"),
		TEXT("pinky_metacarpal_r")
	};

	int32 QuestFingerBoneIndex(const int32 FingerIndex, const int32 SegmentIndex)
	{
		return FingerIndex * QuestFingerSegmentsPerFinger + SegmentIndex;
	}

	int32 QuestFingerMetacarpalBoneIndex(const int32 FingerIndex)
	{
		return FMath::Clamp(FingerIndex - 1, 0, QuestMetacarpalBoneCount - 1);
	}

	EHandKeypoint QuestFingerStartKeypoint(const int32 FingerIndex, const int32 SegmentIndex)
	{
		if (FingerIndex == 0)
		{
			switch (SegmentIndex)
			{
			case 0: return EHandKeypoint::ThumbMetacarpal;
			case 1: return EHandKeypoint::ThumbProximal;
			default: return EHandKeypoint::ThumbDistal;
			}
		}

		const int32 Base = static_cast<int32>(EHandKeypoint::IndexProximal) + (FingerIndex - 1) * 5;
		return static_cast<EHandKeypoint>(Base + SegmentIndex);
	}

	EHandKeypoint QuestFingerEndKeypoint(const int32 FingerIndex, const int32 SegmentIndex)
	{
		if (FingerIndex == 0)
		{
			switch (SegmentIndex)
			{
			case 0: return EHandKeypoint::ThumbProximal;
			case 1: return EHandKeypoint::ThumbDistal;
			default: return EHandKeypoint::ThumbTip;
			}
		}

		const int32 Base = static_cast<int32>(EHandKeypoint::IndexIntermediate) + (FingerIndex - 1) * 5;
		return static_cast<EHandKeypoint>(Base + SegmentIndex);
	}

	EHandKeypoint QuestFingerMetacarpalStartKeypoint(const int32 FingerIndex)
	{
		if (FingerIndex <= 0 || FingerIndex >= QuestFingerCount)
		{
			return EHandKeypoint::Palm;
		}

		const int32 Base = static_cast<int32>(EHandKeypoint::IndexMetacarpal) + (FingerIndex - 1) * 5;
		return static_cast<EHandKeypoint>(Base);
	}

	EHandKeypoint QuestFingerMetacarpalEndKeypoint(const int32 FingerIndex)
	{
		if (FingerIndex <= 0 || FingerIndex >= QuestFingerCount)
		{
			return EHandKeypoint::Palm;
		}

		const int32 Base = static_cast<int32>(EHandKeypoint::IndexProximal) + (FingerIndex - 1) * 5;
		return static_cast<EHandKeypoint>(Base);
	}

	EHandKeypoint QuestFingerBoneSourceKeypoint(const int32 FingerIndex, const int32 SegmentIndex)
	{
		return QuestFingerStartKeypoint(FingerIndex, SegmentIndex);
	}

	EHandKeypoint QuestFingerMetacarpalSourceKeypoint(const int32 FingerIndex)
	{
		return QuestFingerMetacarpalStartKeypoint(FingerIndex);
	}

	FQuat ApplyQuestJointRestOffset(const FQuat& SourceReferenceComp, const FQuat& TargetReferenceComp, const FQuat& SourceLiveComp)
	{
		const FQuat SourceReference = SourceReferenceComp.GetNormalized();
		const FQuat TargetReference = TargetReferenceComp.GetNormalized();
		const FQuat SourceLive = SourceLiveComp.GetNormalized();
		return ((SourceLive * SourceReference.Inverse()) * TargetReference).GetNormalized();
	}

	FQuat MakeQuestJointLocalRotation(const FQuat& ParentComp, const FQuat& ChildComp)
	{
		return (ParentComp.GetNormalized().Inverse() * ChildComp.GetNormalized()).GetNormalized();
	}

	FQuat RetargetQuestJointLocalToComponent(
		const FQuat& SourceReferenceLocal,
		const FQuat& TargetReferenceLocal,
		const FQuat& SourceLiveLocal,
		const FQuat& TargetParentLiveComp)
	{
		const FQuat TargetLiveLocal = ApplyQuestJointRestOffset(
			SourceReferenceLocal,
			TargetReferenceLocal,
			SourceLiveLocal);
		return (TargetParentLiveComp.GetNormalized() * TargetLiveLocal).GetNormalized();
	}

	FQuat RetargetQuestSegmentDirectionToBone(
		const FQuat& CurrentHandDeltaCS,
		const FQuat& TargetReferenceComp,
		const FVector& TargetReferenceSegmentComp,
		const FVector& QuestSegmentComp)
	{
		const FQuat HandDelta = CurrentHandDeltaCS.GetNormalized();
		const FQuat BaseRotCS = (HandDelta * TargetReferenceComp.GetNormalized()).GetNormalized();
		const FVector BaseSegmentComp = HandDelta.RotateVector(TargetReferenceSegmentComp).GetSafeNormal();
		const FVector TargetSegmentComp = QuestSegmentComp.GetSafeNormal();
		if (BaseSegmentComp.IsNearlyZero() || TargetSegmentComp.IsNearlyZero())
		{
			return BaseRotCS;
		}

		return (FQuat::FindBetweenNormals(BaseSegmentComp, TargetSegmentComp) * BaseRotCS).GetNormalized();
	}

	FVector ClampQuestFingerSegmentSplay(
		const FVector& DesiredSegmentDir,
		const FVector& CurlPlaneNormal,
		const float MaxSplayDeg)
	{
		const FVector Dir = DesiredSegmentDir.GetSafeNormal();
		const FVector Normal = CurlPlaneNormal.GetSafeNormal();
		if (Dir.IsNearlyZero() || Normal.IsNearlyZero())
		{
			return DesiredSegmentDir;
		}

		const float OutOfPlaneSin = FMath::Clamp(FVector::DotProduct(Dir, Normal), -1.0f, 1.0f);
		const float SplayDeg = FMath::RadiansToDegrees(FMath::Asin(OutOfPlaneSin));
		const float MaxDeg = FMath::Max(MaxSplayDeg, 0.0f);
		if (FMath::Abs(SplayDeg) <= MaxDeg)
		{
			return Dir;
		}

		const FVector InPlane = (Dir - OutOfPlaneSin * Normal).GetSafeNormal();
		if (InPlane.IsNearlyZero())
		{
			// Degenerate: the direction is parallel to the hinge axis - no in-plane component
			// to preserve, so leave it alone rather than inventing one.
			return Dir;
		}

		const float AllowedRad = FMath::DegreesToRadians(MaxDeg) * FMath::Sign(SplayDeg);
		return (InPlane * FMath::Cos(AllowedRad) + Normal * FMath::Sin(AllowedRad)).GetSafeNormal();
	}

	float MeasureQuestFingerSegmentSplayDeg(const FVector& SegmentDir, const FVector& CurlPlaneNormal)
	{
		const FVector Dir = SegmentDir.GetSafeNormal();
		const FVector Normal = CurlPlaneNormal.GetSafeNormal();
		if (Dir.IsNearlyZero() || Normal.IsNearlyZero())
		{
			return 0.0f;
		}
		return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(Dir, Normal), -1.0f, 1.0f)));
	}

	FVector ApplyQuestFingerSegmentSplayDeg(
		const FVector& SegmentDir,
		const FVector& CurlPlaneNormal,
		const float SplayDeg)
	{
		const FVector Dir = SegmentDir.GetSafeNormal();
		const FVector Normal = CurlPlaneNormal.GetSafeNormal();
		if (Dir.IsNearlyZero() || Normal.IsNearlyZero())
		{
			return SegmentDir;
		}

		const float OutOfPlaneSin = FMath::Clamp(FVector::DotProduct(Dir, Normal), -1.0f, 1.0f);
		const FVector InPlane = (Dir - OutOfPlaneSin * Normal).GetSafeNormal();
		if (InPlane.IsNearlyZero())
		{
			// Degenerate: parallel to the hinge axis - no in-plane component to preserve.
			return Dir;
		}

		const float SplayRad = FMath::DegreesToRadians(FMath::Clamp(SplayDeg, -80.0f, 80.0f));
		return (InPlane * FMath::Cos(SplayRad) + Normal * FMath::Sin(SplayRad)).GetSafeNormal();
	}

	bool UpdateQuestHandPoseGate(
		FMediaPipeQuestHandPoseGateState& State,
		const float MeanCurl01,
		const bool bTracked,
		const float DeltaSeconds,
		const FMediaPipeQuestHandPoseGateSettings& Settings)
	{
		float RatePerSec = 0.0f;
		if (State.bHasLastSample && DeltaSeconds > 0.0f)
		{
			RatePerSec = FMath::Abs(MeanCurl01 - State.LastSampleMeanCurl01) / DeltaSeconds;
		}
		State.LastSampleMeanCurl01 = MeanCurl01;
		const bool bFirstSample = !State.bHasLastSample;
		State.bHasLastSample = true;

		if (!bTracked)
		{
			// Untracked frames carry stale or garbage joints: hold, and re-accept through the
			// stability window once tracking returns.
			State.bRecovering = true;
			State.StableSeconds = 0.0f;
			return true;
		}
		if (bFirstSample)
		{
			return false;
		}

		if (RatePerSec > FMath::Max(Settings.MaxCurlRatePerSec, 0.1f))
		{
			// Faster than fingers can physically move: tracking collapse, not motion.
			State.bRecovering = true;
			State.StableSeconds = 0.0f;
			return true;
		}

		if (State.bRecovering)
		{
			if (RatePerSec <= FMath::Max(Settings.StableRatePerSec, 0.05f))
			{
				State.StableSeconds += DeltaSeconds;
				if (State.StableSeconds >= FMath::Max(Settings.RecoverSeconds, 0.0f))
				{
					State.bRecovering = false;
					State.StableSeconds = 0.0f;
					return false;
				}
			}
			else
			{
				State.StableSeconds = 0.0f;
			}
			return true;
		}

		return false;
	}

	void EnforceQuestFingerPairSeparation(
		FVector& InOutDirA,
		FVector& InOutDirB,
		const FVector& SeparationAxis,
		const float MinSeparationDeg)
	{
		const FVector Axis = SeparationAxis.GetSafeNormal();
		const FVector DirA = InOutDirA.GetSafeNormal();
		const FVector DirB = InOutDirB.GetSafeNormal();
		if (Axis.IsNearlyZero() || DirA.IsNearlyZero() || DirB.IsNearlyZero() || MinSeparationDeg <= 0.0f)
		{
			return;
		}

		const FVector RawCross = FVector::CrossProduct(DirA, DirB);
		const float UnsignedDeg = FMath::RadiansToDegrees(
			FMath::Atan2(RawCross.Size(), FVector::DotProduct(DirA, DirB)));
		const float SignedDeg = FVector::DotProduct(RawCross, Axis) >= 0.0f ? UnsignedDeg : -UnsignedDeg;
		if (SignedDeg >= MinSeparationDeg)
		{
			return;
		}

		const float HalfDeficitDeg = (MinSeparationDeg - SignedDeg) * 0.5f;
		InOutDirA = DirA.RotateAngleAxis(-HalfDeficitDeg, Axis).GetSafeNormal();
		InOutDirB = DirB.RotateAngleAxis(HalfDeficitDeg, Axis).GetSafeNormal();
	}

	FVector GetQuestFingerSegmentWorld(const FQuestHandTrackingSnapshot& Snapshot, const bool bIsLeft, const int32 FingerIndex, const int32 SegmentIndex)
	{
		const int32 StartKey = static_cast<int32>(QuestFingerStartKeypoint(FingerIndex, SegmentIndex));
		const int32 EndKey = static_cast<int32>(QuestFingerEndKeypoint(FingerIndex, SegmentIndex));
		if (StartKey < 0 || StartKey >= QuestHandKeypointCount || EndKey < 0 || EndKey >= QuestHandKeypointCount)
		{
			return FVector::ZeroVector;
		}

		const TStaticArray<FVector, QuestHandKeypointCount>& Positions = bIsLeft ? Snapshot.LeftPositionsWorld : Snapshot.RightPositionsWorld;
		return (Positions[EndKey] - Positions[StartKey]).GetSafeNormal();
	}

	FVector GetQuestFingerMetacarpalSegmentWorld(const FQuestHandTrackingSnapshot& Snapshot, const bool bIsLeft, const int32 FingerIndex)
	{
		const int32 StartKey = static_cast<int32>(QuestFingerMetacarpalStartKeypoint(FingerIndex));
		const int32 EndKey = static_cast<int32>(QuestFingerMetacarpalEndKeypoint(FingerIndex));
		if (StartKey < 0 || StartKey >= QuestHandKeypointCount || EndKey < 0 || EndKey >= QuestHandKeypointCount)
		{
			return FVector::ZeroVector;
		}

		const TStaticArray<FVector, QuestHandKeypointCount>& Positions = bIsLeft ? Snapshot.LeftPositionsWorld : Snapshot.RightPositionsWorld;
		return (Positions[EndKey] - Positions[StartKey]).GetSafeNormal();
	}

	float RemapQuestFingerCurlAngle01(const float AngleDeg, const FMediaPipeQuestFingerCurlSettings& Settings)
	{
		const float OpenDeg = FMath::Max(0.0f, Settings.OpenAngleDeg);
		const float FullDeg = FMath::Max(OpenDeg + 1.0f, Settings.FullAngleDeg);
		return FMath::Clamp((AngleDeg - OpenDeg) / (FullDeg - OpenDeg), 0.0f, 1.0f);
	}

	float QuestFingerSegmentCurl01(const FVector& SegmentWorld, const FVector& HandForwardWorld, const FMediaPipeQuestFingerCurlSettings& Settings)
	{
		const FVector SegmentN = SegmentWorld.GetSafeNormal();
		const FVector ForwardN = HandForwardWorld.GetSafeNormal();
		if (SegmentN.IsNearlyZero() || ForwardN.IsNearlyZero())
		{
			return 0.0f;
		}

		const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(SegmentN, ForwardN), -1.0f, 1.0f)));
		return RemapQuestFingerCurlAngle01(AngleDeg, Settings);
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

	float QuestFingerChainCurl01(
		const FQuestHandTrackingSnapshot& Snapshot,
		const bool bIsLeft,
		const int32 FingerIndex,
		const int32 SegmentIndex,
		const FMediaPipeQuestFingerCurlSettings& Settings,
		float& OutJointAngleDeg)
	{
		OutJointAngleDeg = 0.0f;
		if (FingerIndex <= 0 || FingerIndex >= QuestFingerCount)
		{
			return 0.0f;
		}

		const int32 Base = static_cast<int32>(EHandKeypoint::IndexMetacarpal) + (FingerIndex - 1) * 5;
		const int32 MetacarpalKey = Base;
		const int32 ProximalKey = Base + 1;
		const int32 IntermediateKey = Base + 2;
		const int32 DistalKey = Base + 3;
		const int32 TipKey = Base + 4;
		if (TipKey >= QuestHandKeypointCount)
		{
			return 0.0f;
		}

		const TStaticArray<FVector, QuestHandKeypointCount>& Positions = bIsLeft ? Snapshot.LeftPositionsWorld : Snapshot.RightPositionsWorld;
		const FVector Metacarpal = Positions[MetacarpalKey];
		const FVector Proximal = Positions[ProximalKey];
		const FVector Intermediate = Positions[IntermediateKey];
		const FVector Distal = Positions[DistalKey];
		const FVector Tip = Positions[TipKey];

		const FVector MetaToProx = Proximal - Metacarpal;
		const FVector ProxToInter = Intermediate - Proximal;
		const FVector InterToDist = Distal - Intermediate;
		const FVector DistToTip = Tip - Distal;
		if (MetaToProx.IsNearlyZero() || ProxToInter.IsNearlyZero() || InterToDist.IsNearlyZero() || DistToTip.IsNearlyZero())
		{
			return 0.0f;
		}

		switch (SegmentIndex)
		{
		case 0:
			OutJointAngleDeg = QuestAngleBetweenSegmentsDeg(MetaToProx, ProxToInter);
			break;
		case 1:
			OutJointAngleDeg = QuestAngleBetweenSegmentsDeg(ProxToInter, InterToDist);
			break;
		default:
			OutJointAngleDeg = QuestAngleBetweenSegmentsDeg(InterToDist, DistToTip);
			break;
		}

		return RemapQuestFingerCurlAngle01(OutJointAngleDeg, Settings);
	}

	float QuestThumbChainCurl01(
		const FQuestHandTrackingSnapshot& Snapshot,
		const bool bIsLeft,
		const int32 SegmentIndex,
		const FMediaPipeQuestFingerCurlSettings& Settings,
		float& OutJointAngleDeg)
	{
		OutJointAngleDeg = 0.0f;

		const TStaticArray<FVector, QuestHandKeypointCount>& Positions = bIsLeft ? Snapshot.LeftPositionsWorld : Snapshot.RightPositionsWorld;
		const FVector Metacarpal = Positions[static_cast<int32>(EHandKeypoint::ThumbMetacarpal)];
		const FVector Proximal = Positions[static_cast<int32>(EHandKeypoint::ThumbProximal)];
		const FVector Distal = Positions[static_cast<int32>(EHandKeypoint::ThumbDistal)];
		const FVector Tip = Positions[static_cast<int32>(EHandKeypoint::ThumbTip)];

		const FVector MetaToProx = Proximal - Metacarpal;
		const FVector ProxToDist = Distal - Proximal;
		const FVector DistToTip = Tip - Distal;
		if (MetaToProx.IsNearlyZero() || ProxToDist.IsNearlyZero() || DistToTip.IsNearlyZero())
		{
			return 0.0f;
		}

		switch (SegmentIndex)
		{
		case 0:
			// Thumb base curl is inferred conservatively from the first thumb joint; sideways thumb spread is not curl.
			OutJointAngleDeg = 0.5f * QuestAngleBetweenSegmentsDeg(MetaToProx, ProxToDist);
			break;
		case 1:
			OutJointAngleDeg = QuestAngleBetweenSegmentsDeg(MetaToProx, ProxToDist);
			break;
		default:
			OutJointAngleDeg = QuestAngleBetweenSegmentsDeg(ProxToDist, DistToTip);
			break;
		}

		return RemapQuestFingerCurlAngle01(OutJointAngleDeg, Settings);
	}

	int32 CountValidQuestFingerRefs(const bool* bHasRefFinger)
	{
		int32 Count = 0;
		for (int32 Index = 0; Index < QuestFingerBoneCount; ++Index)
		{
			Count += bHasRefFinger[Index] ? 1 : 0;
		}
		return Count;
	}

	int32 CountValidQuestMetacarpalRefs(const bool* bHasRefFingerMetacarpal)
	{
		int32 Count = 0;
		for (int32 Index = 0; Index < QuestMetacarpalBoneCount; ++Index)
		{
			Count += bHasRefFingerMetacarpal[Index] ? 1 : 0;
		}
		return Count;
	}
}
