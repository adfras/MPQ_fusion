#include "MediaPipeQuestHandDebugReporter.h"

#include "DrawDebugHelpers.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Features/IModularFeatures.h"
#include "HAL/IConsoleManager.h"
#include "HeadMountedDisplayTypes.h"
#include "IHandTracker.h"
#include "InputCoreTypes.h"
#include "MediaPipePoseDiagnosticReporter.h"
#include "MediaPipePoseLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr int32 QuestFingerCount = 5;
	constexpr int32 QuestFingerSegmentsPerFinger = 3;

	bool IsFiniteQuestVector(const FVector& Vector)
	{
		return FMath::IsFinite(Vector.X) && FMath::IsFinite(Vector.Y) && FMath::IsFinite(Vector.Z);
	}

	bool IsUsableQuestWristPosition(const FVector& WristWorld)
	{
		return IsFiniteQuestVector(WristWorld) && !WristWorld.IsNearlyZero();
	}

	bool IsQuestHandSideAvailable(const FQuestHandTrackingSnapshot& Snapshot, const bool bIsLeft)
	{
		return bIsLeft ? (Snapshot.bHasLeft != 0) : (Snapshot.bHasRight != 0);
	}

	const TStaticArray<FVector, QuestHandKeypointCount>& GetQuestHandPositions(
		const FQuestHandTrackingSnapshot& Snapshot,
		const bool bIsLeft)
	{
		return bIsLeft ? Snapshot.LeftPositionsWorld : Snapshot.RightPositionsWorld;
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

	TSharedRef<FJsonObject> QuestVectorToJson(const FVector& Value)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("x"), Value.X);
		Object->SetNumberField(TEXT("y"), Value.Y);
		Object->SetNumberField(TEXT("z"), Value.Z);
		return Object;
	}

	TSharedRef<FJsonObject> QuestQuatToJson(const FQuat& Value)
	{
		const FQuat Normalized = Value.GetNormalized();
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("x"), Normalized.X);
		Object->SetNumberField(TEXT("y"), Normalized.Y);
		Object->SetNumberField(TEXT("z"), Normalized.Z);
		Object->SetNumberField(TEXT("w"), Normalized.W);
		return Object;
	}

	void SetQuestVectorArrayJson(
		TSharedRef<FJsonObject> Root,
		const TCHAR* FieldName,
		const TStaticArray<FVector, QuestHandKeypointCount>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Reserve(QuestHandKeypointCount);
		for (const FVector& Value : Values)
		{
			Array.Add(MakeShared<FJsonValueObject>(QuestVectorToJson(Value)));
		}
		Root->SetArrayField(FieldName, MoveTemp(Array));
	}

	void SetQuestQuatArrayJson(
		TSharedRef<FJsonObject> Root,
		const TCHAR* FieldName,
		const TStaticArray<FQuat, QuestHandKeypointCount>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Reserve(QuestHandKeypointCount);
		for (const FQuat& Value : Values)
		{
			Array.Add(MakeShared<FJsonValueObject>(QuestQuatToJson(Value)));
		}
		Root->SetArrayField(FieldName, MoveTemp(Array));
	}

	void SetQuestFloatArrayJson(
		TSharedRef<FJsonObject> Root,
		const TCHAR* FieldName,
		const TStaticArray<float, QuestHandKeypointCount>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Reserve(QuestHandKeypointCount);
		for (const float Value : Values)
		{
			Array.Add(MakeShared<FJsonValueNumber>(Value));
		}
		Root->SetArrayField(FieldName, MoveTemp(Array));
	}

	bool TryReadQuestVectorJson(const TSharedPtr<FJsonObject>& Object, FVector& OutValue)
	{
		if (!Object.IsValid())
		{
			return false;
		}

		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		if (!Object->TryGetNumberField(TEXT("x"), X) ||
			!Object->TryGetNumberField(TEXT("y"), Y) ||
			!Object->TryGetNumberField(TEXT("z"), Z))
		{
			return false;
		}

		OutValue = FVector(X, Y, Z);
		return true;
	}

	bool TryReadQuestQuatJson(const TSharedPtr<FJsonObject>& Object, FQuat& OutValue)
	{
		if (!Object.IsValid())
		{
			return false;
		}

		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		double W = 1.0;
		if (!Object->TryGetNumberField(TEXT("x"), X) ||
			!Object->TryGetNumberField(TEXT("y"), Y) ||
			!Object->TryGetNumberField(TEXT("z"), Z) ||
			!Object->TryGetNumberField(TEXT("w"), W))
		{
			return false;
		}

		OutValue = FQuat(X, Y, Z, W).GetNormalized();
		return true;
	}

	bool TryReadQuestVectorArrayJson(
		const TSharedPtr<FJsonObject>& Root,
		const TCHAR* FieldName,
		TStaticArray<FVector, QuestHandKeypointCount>& OutValues)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Root.IsValid() || !Root->TryGetArrayField(FieldName, Array) || !Array || Array->Num() != QuestHandKeypointCount)
		{
			return false;
		}

		for (int32 Index = 0; Index < QuestHandKeypointCount; ++Index)
		{
			if (!TryReadQuestVectorJson((*Array)[Index]->AsObject(), OutValues[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool TryReadQuestQuatArrayJson(
		const TSharedPtr<FJsonObject>& Root,
		const TCHAR* FieldName,
		TStaticArray<FQuat, QuestHandKeypointCount>& OutValues)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Root.IsValid() || !Root->TryGetArrayField(FieldName, Array) || !Array || Array->Num() != QuestHandKeypointCount)
		{
			return false;
		}

		for (int32 Index = 0; Index < QuestHandKeypointCount; ++Index)
		{
			if (!TryReadQuestQuatJson((*Array)[Index]->AsObject(), OutValues[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool TryReadQuestFloatArrayJson(
		const TSharedPtr<FJsonObject>& Root,
		const TCHAR* FieldName,
		TStaticArray<float, QuestHandKeypointCount>& OutValues)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Root.IsValid() || !Root->TryGetArrayField(FieldName, Array) || !Array || Array->Num() != QuestHandKeypointCount)
		{
			return false;
		}

		for (int32 Index = 0; Index < QuestHandKeypointCount; ++Index)
		{
			OutValues[Index] = static_cast<float>((*Array)[Index]->AsNumber());
		}
		return true;
	}

	void QueryQuestHandSideForLog(
		const IHandTracker* HandTracker,
		const EControllerHand Hand,
		bool& bOutSuccess,
		bool& bOutTracked,
		int32& OutPositionCount,
		int32& OutRotationCount,
		int32& OutRadiusCount,
		FVector& OutWristWorld)
	{
		bOutSuccess = false;
		bOutTracked = false;
		OutPositionCount = 0;
		OutRotationCount = 0;
		OutRadiusCount = 0;
		OutWristWorld = FVector::ZeroVector;

		if (!HandTracker)
		{
			return;
		}

		if (!HandTracker->IsHandTrackingStateValid())
		{
			return;
		}

		TArray<FVector> Positions;
		TArray<FQuat> Rotations;
		TArray<float> Radii;
		bool bTracked = false;
		bOutSuccess = HandTracker->GetAllKeypointStates(Hand, Positions, Rotations, Radii, bTracked);
		bOutTracked = bTracked;
		OutPositionCount = Positions.Num();
		OutRotationCount = Rotations.Num();
		OutRadiusCount = Radii.Num();

		const int32 WristIndex = static_cast<int32>(EHandKeypoint::Wrist);
		if (Positions.IsValidIndex(WristIndex))
		{
			OutWristWorld = Positions[WristIndex];
		}
	}
}

void FMediaPipeQuestHandDebugReporter::DrawSkeletonWorld(
	UWorld* World,
	const FQuestHandTrackingSnapshot& Snapshot,
	const bool bIsLeft)
{
	if (!World || !IsQuestHandSideAvailable(Snapshot, bIsLeft))
	{
		return;
	}

	const TStaticArray<FVector, QuestHandKeypointCount>& Positions = GetQuestHandPositions(Snapshot, bIsLeft);
	const FColor LineColor = bIsLeft ? FColor::Cyan : FColor::Green;
	const FColor PointColor = bIsLeft ? FColor::Blue : FColor::Green;
	constexpr float LifeTimeSeconds = 0.04f;
	constexpr float Thickness = 1.25f;
	auto DrawSegment = [&](const EHandKeypoint A, const EHandKeypoint B)
	{
		const FVector Start = Positions[static_cast<int32>(A)];
		const FVector End = Positions[static_cast<int32>(B)];
		if (!IsUsableQuestWristPosition(Start) || !IsUsableQuestWristPosition(End))
		{
			return;
		}
		DrawDebugLine(World, Start, End, LineColor, false, LifeTimeSeconds, 0, Thickness);
		DrawDebugPoint(World, End, 4.0f, PointColor, false, LifeTimeSeconds);
	};

	const FVector Wrist = Positions[static_cast<int32>(EHandKeypoint::Wrist)];
	if (IsUsableQuestWristPosition(Wrist))
	{
		DrawDebugPoint(World, Wrist, 6.0f, FColor::White, false, LifeTimeSeconds);
		DrawDebugString(
			World,
			Wrist + FVector(0.0, 0.0, 6.0),
			bIsLeft ? TEXT("Quest L raw, not avatar-space") : TEXT("Quest R raw, not avatar-space"),
			nullptr,
			LineColor,
			LifeTimeSeconds,
			true,
			0.75f);
	}

	DrawSegment(EHandKeypoint::Wrist, EHandKeypoint::ThumbMetacarpal);
	DrawSegment(EHandKeypoint::Wrist, EHandKeypoint::IndexProximal);
	DrawSegment(EHandKeypoint::Wrist, EHandKeypoint::MiddleProximal);
	DrawSegment(EHandKeypoint::Wrist, EHandKeypoint::RingProximal);
	DrawSegment(EHandKeypoint::Wrist, EHandKeypoint::LittleProximal);
	for (int32 FingerIndex = 0; FingerIndex < QuestFingerCount; ++FingerIndex)
	{
		for (int32 SegmentIndex = 0; SegmentIndex < QuestFingerSegmentsPerFinger; ++SegmentIndex)
		{
			DrawSegment(
				QuestFingerStartKeypoint(FingerIndex, SegmentIndex),
				QuestFingerEndKeypoint(FingerIndex, SegmentIndex));
		}
	}
}

void FMediaPipeQuestHandDebugReporter::DrawSkeletonHmdRelativeAvatarWorld(
	UWorld* World,
	const FQuestHandTrackingSnapshot& Snapshot,
	const bool bIsLeft,
	const FVector& QuestHmdWorld,
	const FQuat& QuestHmdRotWorld,
	const FVector& TrackingUpWorldInput,
	const FTransform& TargetCompTransform,
	const FMediaPipeAvatarEmbodimentProfile& TargetProfile,
	const float PositionScale,
	const float MaxOffsetCm)
{
	if (!World || !IsQuestHandSideAvailable(Snapshot, bIsLeft) || !TargetProfile.IsValid())
	{
		return;
	}

	const TStaticArray<FVector, QuestHandKeypointCount>& Positions = GetQuestHandPositions(Snapshot, bIsLeft);
	TStaticArray<FVector, QuestHandKeypointCount> MappedPositions;
	FMediaPipeAvatarHmdWristMapInput MapInput;
	MapInput.QuestAnchorWorld = QuestHmdWorld;
	MapInput.QuestAnchorYawWorld = QuestHmdRotWorld;
	MapInput.QuestTrackingUpWorld = TrackingUpWorldInput;
	MapInput.TargetCompTransform = TargetCompTransform;
	MapInput.Profile = TargetProfile;
	MapInput.bHasProfileEyeLocalOffset = true;
	MapInput.PositionScale = PositionScale;
	MapInput.MaxOffsetCm = MaxOffsetCm;
	for (int32 Index = 0; Index < QuestHandKeypointCount; ++Index)
	{
		const FVector RawPoint = Positions[Index];
		if (!IsUsableQuestWristPosition(RawPoint))
		{
			MappedPositions[Index] = FVector::ZeroVector;
			continue;
		}

		MapInput.QuestWristWorld = RawPoint;
		FMediaPipeAvatarHmdWristMapResult MapResult;
		if (!FMediaPipeAvatarEmbodimentSolver::MapQuestHmdRelativeWristToAvatarWorld(MapInput, MapResult))
		{
			MappedPositions[Index] = FVector::ZeroVector;
			continue;
		}
		MappedPositions[Index] = MapResult.MappedWristWorld;
	}

	const FColor LineColor = bIsLeft ? FColor::Cyan : FColor::Green;
	const FColor PointColor = bIsLeft ? FColor::Blue : FColor::Green;
	constexpr float LifeTimeSeconds = 0.04f;
	constexpr float Thickness = 1.75f;
	auto DrawSegment = [&](const EHandKeypoint A, const EHandKeypoint B)
	{
		const FVector Start = MappedPositions[static_cast<int32>(A)];
		const FVector End = MappedPositions[static_cast<int32>(B)];
		if (!IsUsableQuestWristPosition(Start) || !IsUsableQuestWristPosition(End))
		{
			return;
		}
		DrawDebugLine(World, Start, End, LineColor, false, LifeTimeSeconds, 0, Thickness);
		DrawDebugPoint(World, End, 4.5f, PointColor, false, LifeTimeSeconds);
	};

	const FVector Wrist = MappedPositions[static_cast<int32>(EHandKeypoint::Wrist)];
	if (IsUsableQuestWristPosition(Wrist))
	{
		DrawDebugPoint(World, Wrist, 7.0f, FColor::White, false, LifeTimeSeconds);
		DrawDebugString(
			World,
			Wrist + FVector(0.0, 0.0, 7.0),
			bIsLeft ? TEXT("Quest L mapped avatar-space") : TEXT("Quest R mapped avatar-space"),
			nullptr,
			LineColor,
			LifeTimeSeconds,
			true,
			0.75f);
	}

	DrawSegment(EHandKeypoint::Wrist, EHandKeypoint::ThumbMetacarpal);
	DrawSegment(EHandKeypoint::Wrist, EHandKeypoint::IndexProximal);
	DrawSegment(EHandKeypoint::Wrist, EHandKeypoint::MiddleProximal);
	DrawSegment(EHandKeypoint::Wrist, EHandKeypoint::RingProximal);
	DrawSegment(EHandKeypoint::Wrist, EHandKeypoint::LittleProximal);
	for (int32 FingerIndex = 0; FingerIndex < QuestFingerCount; ++FingerIndex)
	{
		for (int32 SegmentIndex = 0; SegmentIndex < QuestFingerSegmentsPerFinger; ++SegmentIndex)
		{
			DrawSegment(
				QuestFingerStartKeypoint(FingerIndex, SegmentIndex),
				QuestFingerEndKeypoint(FingerIndex, SegmentIndex));
		}
	}
}

void FMediaPipeQuestHandDebugReporter::DisplayHud(
	const double NowSeconds,
	double& LastHudTimeSeconds,
	const FQuestHandTrackingSnapshot& Snapshot)
{
	if (!GEngine ||
		!FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 0.5, LastHudTimeSeconds))
	{
		return;
	}

	const bool bAnyTracked = Snapshot.bLeftTracked != 0 || Snapshot.bRightTracked != 0;
	GEngine->AddOnScreenDebugMessage(
		909103,
		1.0f,
		bAnyTracked ? FColor::Green : FColor::Yellow,
		BuildHudMessage(Snapshot));
}

void FMediaPipeQuestHandDebugReporter::EmitFingerSolveLog(
	const FMediaPipeQuestFingerSolveLogInput& Input,
	const double NowSeconds,
	double& LastLogTimeSeconds)
{
	if (!FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, LastLogTimeSeconds))
	{
		return;
	}

	UE_LOG(LogMediaPipePose, Log, TEXT("%s"), *FormatFingerSolveLog(Input));
}

FString FMediaPipeQuestHandDebugReporter::FormatFingerSolveLog(
	const FMediaPipeQuestFingerSolveLogInput& Input)
{
	return FString::Printf(
		TEXT("mp.QuestFingerSolve: actor=%s side=%s available=%d tracked=%d drive=%d appliedBones=%d thumbApplied=%d metaApplied=%d validRefBones=%d metaValid=%d mode=%s thumbMode=%s preserveSpread=%d alignToMediaHand=%d wristPositionBlend=%.2f handRotationBlend=%.2f fingerMaxCurl=%.1f thumbMaxCurl=%.1f fingerScale=[%.2f %.2f %.2f] thumbScale=[%.2f %.2f %.2f] fingerCurl01=[%.2f %.2f %.2f %.2f] fingerJointDeg=[%.1f %.1f %.1f %.1f] fingerFistAlpha=[%.2f %.2f %.2f %.2f] thumbFistAlpha=%.2f thumbCurl01=[%.2f %.2f %.2f] thumbJointDeg=[%.1f %.1f %.1f] questWristWorld=%s"),
		*Input.TargetActorName.ToString(),
		Input.bIsLeft ? TEXT("L") : TEXT("R"),
		Input.bAvailable ? 1 : 0,
		Input.bTracked ? 1 : 0,
		Input.bDriveQuestFingerBones ? 1 : 0,
		Input.AppliedCount,
		Input.AppliedThumbBoneCount,
		Input.AppliedMetacarpalBoneCount,
		Input.ValidFingerBoneCount,
		Input.ValidMetacarpalBoneCount,
		*Input.Mode,
		*Input.ThumbMode,
		Input.bPreserveSpread ? 1 : 0,
		Input.bHasQuestFingerAlignmentComp ? 1 : 0,
		Input.WristPositionBlend,
		Input.HandRotationBlend,
		Input.FingerMaxCurlDeg,
		Input.ThumbMaxCurlDeg,
		Input.FingerSegmentScale[0],
		Input.FingerSegmentScale[1],
		Input.FingerSegmentScale[2],
		Input.ThumbSegmentScale[0],
		Input.ThumbSegmentScale[1],
		Input.ThumbSegmentScale[2],
		Input.FingerCurl01[0],
		Input.FingerCurl01[1],
		Input.FingerCurl01[2],
		Input.FingerCurl01[3],
		Input.FingerJointAngleDeg[0],
		Input.FingerJointAngleDeg[1],
		Input.FingerJointAngleDeg[2],
		Input.FingerJointAngleDeg[3],
		Input.FingerClosedFistAlpha[0],
		Input.FingerClosedFistAlpha[1],
		Input.FingerClosedFistAlpha[2],
		Input.FingerClosedFistAlpha[3],
		Input.ThumbClosedFistAlpha,
		Input.ThumbCurl01[0],
		Input.ThumbCurl01[1],
		Input.ThumbCurl01[2],
		Input.ThumbJointAngleDeg[0],
		Input.ThumbJointAngleDeg[1],
		Input.ThumbJointAngleDeg[2],
		*Input.QuestWristWorld.ToCompactString());
}

void FMediaPipeQuestHandDebugReporter::EmitFingerSolveLog(
	const FName TargetActorName,
	const bool bIsLeft,
	const bool bAvailable,
	const bool bTracked,
	const bool bDriveQuestFingerBones,
	const int32 AppliedCount,
	const int32 AppliedThumbBoneCount,
	const int32 AppliedMetacarpalBoneCount,
	const int32 ValidFingerBoneCount,
	const int32 ValidMetacarpalBoneCount,
	const TCHAR* Mode,
	const TCHAR* ThumbMode,
	const bool bPreserveSpread,
	const bool bHasQuestFingerAlignmentComp,
	const float WristPositionBlend,
	const float HandRotationBlend,
	const float FingerMaxCurlDeg,
	const float ThumbMaxCurlDeg,
	const float (&FingerSegmentScale)[3],
	const float (&ThumbSegmentScale)[3],
	const float (&FingerCurl01)[4],
	const float (&FingerJointAngleDeg)[4],
	const float (&FingerClosedFistAlpha)[4],
	const float ThumbClosedFistAlpha,
	const float (&ThumbCurl01)[3],
	const float (&ThumbJointAngleDeg)[3],
	const FVector& QuestWristWorld,
	const double NowSeconds,
	double& LastLogTimeSeconds)
{
	FMediaPipeQuestFingerSolveLogInput Input;
	Input.TargetActorName = TargetActorName;
	Input.bIsLeft = bIsLeft;
	Input.bAvailable = bAvailable;
	Input.bTracked = bTracked;
	Input.bDriveQuestFingerBones = bDriveQuestFingerBones;
	Input.AppliedCount = AppliedCount;
	Input.AppliedThumbBoneCount = AppliedThumbBoneCount;
	Input.AppliedMetacarpalBoneCount = AppliedMetacarpalBoneCount;
	Input.ValidFingerBoneCount = ValidFingerBoneCount;
	Input.ValidMetacarpalBoneCount = ValidMetacarpalBoneCount;
	Input.Mode = Mode;
	Input.ThumbMode = ThumbMode;
	Input.bPreserveSpread = bPreserveSpread;
	Input.bHasQuestFingerAlignmentComp = bHasQuestFingerAlignmentComp;
	Input.WristPositionBlend = WristPositionBlend;
	Input.HandRotationBlend = HandRotationBlend;
	Input.FingerMaxCurlDeg = FingerMaxCurlDeg;
	Input.ThumbMaxCurlDeg = ThumbMaxCurlDeg;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Input.FingerSegmentScale[Index] = FingerSegmentScale[Index];
		Input.ThumbSegmentScale[Index] = ThumbSegmentScale[Index];
		Input.ThumbCurl01[Index] = ThumbCurl01[Index];
		Input.ThumbJointAngleDeg[Index] = ThumbJointAngleDeg[Index];
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Input.FingerCurl01[Index] = FingerCurl01[Index];
		Input.FingerJointAngleDeg[Index] = FingerJointAngleDeg[Index];
		Input.FingerClosedFistAlpha[Index] = FingerClosedFistAlpha[Index];
	}
	Input.ThumbClosedFistAlpha = ThumbClosedFistAlpha;
	Input.QuestWristWorld = QuestWristWorld;
	EmitFingerSolveLog(Input, NowSeconds, LastLogTimeSeconds);
}

void FMediaPipeQuestHandDebugReporter::EmitFingerReferenceSummaryLog(
	const int32 ValidLeftFingerRefs,
	const int32 ValidRightFingerRefs,
	const bool bHasLeftVisualPalmBasis,
	const bool bHasRightVisualPalmBasis)
{
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.QuestHandDebug: fingerRefBones left=%d/%d right=%d/%d visualPalmBasisL=%d visualPalmBasisR=%d"),
		ValidLeftFingerRefs,
		QuestFingerBoneCount,
		ValidRightFingerRefs,
		QuestFingerBoneCount,
		bHasLeftVisualPalmBasis ? 1 : 0,
		bHasRightVisualPalmBasis ? 1 : 0);
}

void FMediaPipeQuestHandDebugReporter::EmitReplayLoadedLog(
	const FString& ResolvedPath,
	const FQuestHandTrackingSnapshot& Snapshot)
{
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.QuestHandReplayFile: loaded '%s' left(has=%d tracked=%d) right(has=%d tracked=%d). Enable with mp.QuestHandReplay 1."),
		*ResolvedPath,
		Snapshot.bHasLeft ? 1 : 0,
		Snapshot.bLeftTracked ? 1 : 0,
		Snapshot.bHasRight ? 1 : 0,
		Snapshot.bRightTracked ? 1 : 0);
}

void FMediaPipeQuestHandDebugReporter::EmitCaptureWriteLog(
	const TCHAR* CommandName,
	const FString& OutputPath,
	const FQuestHandTrackingSnapshot& Snapshot)
{
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("%s: wrote '%s' left(has=%d tracked=%d) right(has=%d tracked=%d)."),
		CommandName,
		*OutputPath,
		Snapshot.bHasLeft ? 1 : 0,
		Snapshot.bLeftTracked ? 1 : 0,
		Snapshot.bHasRight ? 1 : 0,
		Snapshot.bRightTracked ? 1 : 0);
}

void FMediaPipeQuestHandDebugReporter::EmitCaptureGuideStartedLog(const FString& RunId)
{
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.StartQuestHandCaptureGuide: started run '%s'. VR text sequence: 10s prepare, 6s open, 6s half_fist, 6s closed_fist, 4s done."),
		*RunId);
}

FString FMediaPipeQuestHandDebugReporter::BuildCaptureGuideText(
	const FString& DisplayName,
	const double RemainingSeconds,
	const bool bAnyHandTracked,
	const bool bBothHandsTracked)
{
	const FString TrackingLine = bBothHandsTracked ? TEXT("TRACKING: BOTH HANDS") : (bAnyHandTracked ? TEXT("TRACKING: ONE HAND") : TEXT("NO HANDS - SHOW HANDS"));
	return FString::Printf(
		TEXT("%s\n%.0fs\n%s"),
		*DisplayName,
		FMath::CeilToDouble(RemainingSeconds),
		*TrackingLine);
}

void FMediaPipeQuestHandDebugReporter::DumpQuestHandTracking()
{
	const TArray<IHandTracker*> HandTrackers = IModularFeatures::Get().GetModularFeatureImplementations<IHandTracker>(IHandTracker::GetModularFeatureName());
	UE_LOG(LogMediaPipePose, Log, TEXT("mp.DumpQuestHands: handTrackerCount=%d"), HandTrackers.Num());

	if (HandTrackers.Num() == 0)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.DumpQuestHands: no IHandTracker providers are registered. Enable OpenXRHandTracking and run in an active XR session."));
		return;
	}

	for (int32 TrackerIndex = 0; TrackerIndex < HandTrackers.Num(); ++TrackerIndex)
	{
		const IHandTracker* HandTracker = HandTrackers[TrackerIndex];
		if (!HandTracker)
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("mp.DumpQuestHands: tracker[%d] is null."), TrackerIndex);
			continue;
		}

		bool bLeftSuccess = false;
		bool bLeftTracked = false;
		int32 LeftPositions = 0;
		int32 LeftRotations = 0;
		int32 LeftRadii = 0;
		FVector LeftWrist = FVector::ZeroVector;
		QueryQuestHandSideForLog(HandTracker, EControllerHand::Left, bLeftSuccess, bLeftTracked, LeftPositions, LeftRotations, LeftRadii, LeftWrist);

		bool bRightSuccess = false;
		bool bRightTracked = false;
		int32 RightPositions = 0;
		int32 RightRotations = 0;
		int32 RightRadii = 0;
		FVector RightWrist = FVector::ZeroVector;
		QueryQuestHandSideForLog(HandTracker, EControllerHand::Right, bRightSuccess, bRightTracked, RightPositions, RightRotations, RightRadii, RightWrist);

		UE_LOG(
			LogMediaPipePose,
			Log,
			TEXT("mp.DumpQuestHands: tracker[%d] device=%s stateValid=%d left(success=%d tracked=%d pos=%d rot=%d radii=%d wrist=%s) right(success=%d tracked=%d pos=%d rot=%d radii=%d wrist=%s)"),
			TrackerIndex,
			*HandTracker->GetHandTrackerDeviceTypeName().ToString(),
			HandTracker->IsHandTrackingStateValid() ? 1 : 0,
			bLeftSuccess ? 1 : 0,
			bLeftTracked ? 1 : 0,
			LeftPositions,
			LeftRotations,
			LeftRadii,
			*LeftWrist.ToCompactString(),
			bRightSuccess ? 1 : 0,
			bRightTracked ? 1 : 0,
			RightPositions,
			RightRotations,
			RightRadii,
			*RightWrist.ToCompactString());
	}

	UE_LOG(LogMediaPipePose, Log, TEXT("mp.DumpQuestHands: if success=0, the OpenXR runtime has not delivered hand joint poses yet. Enter VR Preview, put controllers down, and keep hands visible to the Quest cameras."));
}

namespace
{
	FAutoConsoleCommand CmdDumpQuestHands(
		TEXT("mp.DumpQuestHands"),
		TEXT("Logs Quest/OpenXR hand tracker providers and current keypoint availability for left and right hands."),
		FConsoleCommandDelegate::CreateStatic(&FMediaPipeQuestHandDebugReporter::DumpQuestHandTracking));
}

FString FMediaPipeQuestHandDebugReporter::SanitizeReplayName(const FString& RawName)
{
	FString Name = RawName;
	Name.TrimStartAndEndInline();
	Name.ReplaceInline(TEXT("\""), TEXT(""));
	Name.ReplaceInline(TEXT("'"), TEXT(""));
	if (Name.IsEmpty())
	{
		Name = TEXT("quest_hand_pose");
	}

	for (int32 Index = 0; Index < Name.Len(); ++Index)
	{
		const TCHAR Ch = Name[Index];
		const bool bSafe =
			FChar::IsAlnum(Ch) ||
			Ch == TEXT('_') ||
			Ch == TEXT('-') ||
			Ch == TEXT('.');
		if (!bSafe)
		{
			Name[Index] = TEXT('_');
		}
	}
	return Name;
}

FString FMediaPipeQuestHandDebugReporter::GetReplayDirectory()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("QuestHandReplays"));
}

FString FMediaPipeQuestHandDebugReporter::ResolveReplayPath(const FString& RawPathOrName)
{
	FString PathOrName = RawPathOrName;
	PathOrName.TrimStartAndEndInline();
	PathOrName.ReplaceInline(TEXT("\""), TEXT(""));
	PathOrName.ReplaceInline(TEXT("'"), TEXT(""));
	if (PathOrName.IsEmpty())
	{
		return FString();
	}

	const bool bLooksLikePath =
		PathOrName.Contains(TEXT("/")) ||
		PathOrName.Contains(TEXT("\\")) ||
		FPaths::IsRelative(PathOrName) == false;
	if (!bLooksLikePath)
	{
		FString FileName = SanitizeReplayName(PathOrName);
		if (!FileName.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase))
		{
			FileName += TEXT(".json");
		}
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(GetReplayDirectory(), FileName));
	}

	FString ResolvedPath = PathOrName;
	if (FPaths::IsRelative(ResolvedPath))
	{
		ResolvedPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), ResolvedPath);
	}
	return ResolvedPath;
}

bool FMediaPipeQuestHandDebugReporter::SaveSnapshotToFile(
	const FQuestHandTrackingSnapshot& Snapshot,
	const FString& OutputPath)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetNumberField(TEXT("keypointCount"), QuestHandKeypointCount);
	Root->SetNumberField(TEXT("handTrackerCount"), Snapshot.HandTrackerCount);
	Root->SetNumberField(TEXT("validHandTrackerCount"), Snapshot.ValidHandTrackerCount);
	Root->SetNumberField(TEXT("hasLeft"), Snapshot.bHasLeft);
	Root->SetNumberField(TEXT("hasRight"), Snapshot.bHasRight);
	Root->SetNumberField(TEXT("leftTracked"), Snapshot.bLeftTracked);
	Root->SetNumberField(TEXT("rightTracked"), Snapshot.bRightTracked);
	SetQuestVectorArrayJson(Root, TEXT("leftPositionsWorld"), Snapshot.LeftPositionsWorld);
	SetQuestQuatArrayJson(Root, TEXT("leftRotationsWorld"), Snapshot.LeftRotationsWorld);
	SetQuestFloatArrayJson(Root, TEXT("leftRadii"), Snapshot.LeftRadii);
	SetQuestVectorArrayJson(Root, TEXT("rightPositionsWorld"), Snapshot.RightPositionsWorld);
	SetQuestQuatArrayJson(Root, TEXT("rightRotationsWorld"), Snapshot.RightRotationsWorld);
	SetQuestFloatArrayJson(Root, TEXT("rightRadii"), Snapshot.RightRadii);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		return false;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	return FFileHelper::SaveStringToFile(JsonText, *OutputPath);
}

bool FMediaPipeQuestHandDebugReporter::LoadSnapshotFromFile(
	const FString& InputPath,
	FQuestHandTrackingSnapshot& OutSnapshot)
{
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *InputPath))
	{
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	double KeypointCount = 0.0;
	if (!Root->TryGetNumberField(TEXT("keypointCount"), KeypointCount) ||
		static_cast<int32>(KeypointCount) != QuestHandKeypointCount)
	{
		return false;
	}

	OutSnapshot.Reset();
	double NumberValue = 0.0;
	if (Root->TryGetNumberField(TEXT("handTrackerCount"), NumberValue))
	{
		OutSnapshot.HandTrackerCount = static_cast<int32>(NumberValue);
	}
	if (Root->TryGetNumberField(TEXT("validHandTrackerCount"), NumberValue))
	{
		OutSnapshot.ValidHandTrackerCount = static_cast<int32>(NumberValue);
	}
	if (Root->TryGetNumberField(TEXT("hasLeft"), NumberValue))
	{
		OutSnapshot.bHasLeft = static_cast<uint8>(NumberValue != 0.0);
	}
	if (Root->TryGetNumberField(TEXT("hasRight"), NumberValue))
	{
		OutSnapshot.bHasRight = static_cast<uint8>(NumberValue != 0.0);
	}
	if (Root->TryGetNumberField(TEXT("leftTracked"), NumberValue))
	{
		OutSnapshot.bLeftTracked = static_cast<uint8>(NumberValue != 0.0);
	}
	if (Root->TryGetNumberField(TEXT("rightTracked"), NumberValue))
	{
		OutSnapshot.bRightTracked = static_cast<uint8>(NumberValue != 0.0);
	}

	if (!TryReadQuestVectorArrayJson(Root, TEXT("leftPositionsWorld"), OutSnapshot.LeftPositionsWorld) ||
		!TryReadQuestQuatArrayJson(Root, TEXT("leftRotationsWorld"), OutSnapshot.LeftRotationsWorld) ||
		!TryReadQuestVectorArrayJson(Root, TEXT("rightPositionsWorld"), OutSnapshot.RightPositionsWorld) ||
		!TryReadQuestQuatArrayJson(Root, TEXT("rightRotationsWorld"), OutSnapshot.RightRotationsWorld))
	{
		return false;
	}

	TryReadQuestFloatArrayJson(Root, TEXT("leftRadii"), OutSnapshot.LeftRadii);
	TryReadQuestFloatArrayJson(Root, TEXT("rightRadii"), OutSnapshot.RightRadii);
	return true;
}

bool FMediaPipeQuestHandDebugReporter::GetCaptureGuidePhase(
	const double ElapsedSeconds,
	FString& OutPoseName,
	FString& OutDisplayName,
	double& OutPhaseStartSeconds,
	double& OutPhaseEndSeconds,
	bool& bOutCapturePhase,
	FColor& OutColor)
{
	constexpr double PrepSeconds = 10.0;
	constexpr double PoseSeconds = 6.0;
	constexpr double DoneSeconds = 4.0;
	const double OpenStart = PrepSeconds;
	const double HalfStart = OpenStart + PoseSeconds;
	const double ClosedStart = HalfStart + PoseSeconds;
	const double DoneStart = ClosedStart + PoseSeconds;
	const double EndSeconds = DoneStart + DoneSeconds;

	if (ElapsedSeconds < PrepSeconds)
	{
		OutPoseName = TEXT("prepare");
		OutDisplayName = TEXT("GET READY\nHands visible");
		OutPhaseStartSeconds = 0.0;
		OutPhaseEndSeconds = PrepSeconds;
		bOutCapturePhase = false;
		OutColor = FColor::Cyan;
		return true;
	}
	if (ElapsedSeconds < HalfStart)
	{
		OutPoseName = TEXT("open");
		OutDisplayName = TEXT("OPEN HANDS");
		OutPhaseStartSeconds = OpenStart;
		OutPhaseEndSeconds = HalfStart;
		bOutCapturePhase = true;
		OutColor = FColor::Green;
		return true;
	}
	if (ElapsedSeconds < ClosedStart)
	{
		OutPoseName = TEXT("half_fist");
		OutDisplayName = TEXT("HALF FIST");
		OutPhaseStartSeconds = HalfStart;
		OutPhaseEndSeconds = ClosedStart;
		bOutCapturePhase = true;
		OutColor = FColor::Yellow;
		return true;
	}
	if (ElapsedSeconds < DoneStart)
	{
		OutPoseName = TEXT("closed_fist");
		OutDisplayName = TEXT("CLOSED FIST");
		OutPhaseStartSeconds = ClosedStart;
		OutPhaseEndSeconds = DoneStart;
		bOutCapturePhase = true;
		OutColor = FColor(255, 128, 0);
		return true;
	}
	if (ElapsedSeconds < EndSeconds)
	{
		OutPoseName = TEXT("done");
		OutDisplayName = TEXT("DONE\nYou can remove headset");
		OutPhaseStartSeconds = DoneStart;
		OutPhaseEndSeconds = EndSeconds;
		bOutCapturePhase = false;
		OutColor = FColor::Green;
		return true;
	}

	return false;
}

FString FMediaPipeQuestHandDebugReporter::BuildHudMessage(
	const FQuestHandTrackingSnapshot& Snapshot)
{
	if (Snapshot.HandTrackerCount <= 0)
	{
		return TEXT("Quest hands: no OpenXR hand tracker. Use VR Preview / active OpenXR runtime.");
	}

	if (Snapshot.ValidHandTrackerCount <= 0)
	{
		return TEXT("Quest hands: OpenXR hand tracker present, but state is not valid yet.");
	}

	if (Snapshot.bHasLeft == 0 && Snapshot.bHasRight == 0)
	{
		return TEXT("Quest hands: no joint poses yet. Put controllers down and keep hands visible.");
	}

	if (Snapshot.bLeftTracked == 0 && Snapshot.bRightTracked == 0)
	{
		return TEXT("Quest hands: joint data exists, but neither hand is currently tracked.");
	}

	if (Snapshot.bLeftTracked != 0 && Snapshot.bRightTracked != 0)
	{
		return TEXT("Quest hands: left + right tracked.");
	}

	return Snapshot.bLeftTracked != 0 ? TEXT("Quest hands: left tracked only.") : TEXT("Quest hands: right tracked only.");
}
