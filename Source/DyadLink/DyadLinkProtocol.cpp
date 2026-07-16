#include "DyadLinkProtocol.h"

#include "EmbodiedFusionComponent.h"
#include "MediaPipeTrackingFusionDatasetReplay.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace DyadLinkProtocol
{
const FString TypeHello(TEXT("HELLO"));
const FString TypeChoices(TEXT("CHOICES"));
const FString TypeReady(TEXT("READY"));
const FString TypeGo(TEXT("GO"));
const FString TypeGoAck(TEXT("GO_ACK"));
const FString TypeHeartbeat(TEXT("HEARTBEAT"));
const FString TypeBye(TEXT("BYE"));
const FString TypeRow(TEXT("ROW"));

namespace
{
TArray<TSharedPtr<FJsonValue>> VectorToJsonArray(const FVector& Vector)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Add(MakeShared<FJsonValueNumber>(Vector.X));
	Values.Add(MakeShared<FJsonValueNumber>(Vector.Y));
	Values.Add(MakeShared<FJsonValueNumber>(Vector.Z));
	return Values;
}

TArray<TSharedPtr<FJsonValue>> QuatToJsonArray(const FQuat& Quat)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Add(MakeShared<FJsonValueNumber>(Quat.X));
	Values.Add(MakeShared<FJsonValueNumber>(Quat.Y));
	Values.Add(MakeShared<FJsonValueNumber>(Quat.Z));
	Values.Add(MakeShared<FJsonValueNumber>(Quat.W));
	return Values;
}

TSharedPtr<FJsonObject> BuildHandObject(
	const FMediaPipeTrackingHandSourceSnapshot& Hands, const bool bLeft, const double TimeSeconds)
{
	const bool bHas = bLeft ? Hands.bHasLeft != 0 : Hands.bHasRight != 0;
	TSharedPtr<FJsonObject> HandObject = MakeShared<FJsonObject>();
	HandObject->SetBoolField(TEXT("has_hand"), bHas);
	if (!bHas)
	{
		return HandObject;
	}
	const TStaticArray<FVector, MediaPipeTrackingHandKeypointCount>& Positions =
		bLeft ? Hands.LeftPositionsWorld : Hands.RightPositionsWorld;
	const TStaticArray<FQuat, MediaPipeTrackingHandKeypointCount>& Rotations =
		bLeft ? Hands.LeftRotationsWorld : Hands.RightRotationsWorld;
	// Keypoint index 1 is the wrist in the Quest hand layout; the v1 wrist_world field
	// mirrors what the recorder wrote so v1-only consumers stay compatible.
	HandObject->SetArrayField(TEXT("wrist_world"), VectorToJsonArray(Positions[1]));
	HandObject->SetNumberField(TEXT("timestamp_seconds"), TimeSeconds);
	HandObject->SetNumberField(TEXT("confidence"), 1.0);
	const bool bFullKeypoints = bLeft ? Hands.bLeftHasFullKeypoints != 0 : Hands.bRightHasFullKeypoints != 0;
	if (bFullKeypoints)
	{
		HandObject->SetBoolField(TEXT("keypoints_tracked"), bLeft ? Hands.bLeftTracked != 0 : Hands.bRightTracked != 0);
		TArray<TSharedPtr<FJsonValue>> PositionValues;
		TArray<TSharedPtr<FJsonValue>> RotationValues;
		for (int32 Index = 0; Index < MediaPipeTrackingHandKeypointCount; ++Index)
		{
			PositionValues.Add(MakeShared<FJsonValueArray>(VectorToJsonArray(Positions[Index])));
			RotationValues.Add(MakeShared<FJsonValueArray>(QuatToJsonArray(Rotations[Index])));
		}
		HandObject->SetArrayField(TEXT("keypoints_world"), PositionValues);
		HandObject->SetArrayField(TEXT("keypoint_quats"), RotationValues);
	}
	return HandObject;
}

TSharedPtr<FJsonObject> BuildArmChainObject(
	const FMediaPipeTrackingArmChainSideSnapshot& Side, const double TimeSeconds)
{
	TSharedPtr<FJsonObject> ChainObject = MakeShared<FJsonObject>();
	ChainObject->SetBoolField(TEXT("has_chain"), Side.bHasChain);
	if (Side.bHasChain)
	{
		ChainObject->SetArrayField(TEXT("shoulder_world"), VectorToJsonArray(Side.ShoulderWorld));
		ChainObject->SetArrayField(TEXT("elbow_world"), VectorToJsonArray(Side.ElbowWorld));
		ChainObject->SetArrayField(TEXT("wrist_world"), VectorToJsonArray(Side.WristWorld));
		ChainObject->SetNumberField(TEXT("timestamp_seconds"), TimeSeconds);
		ChainObject->SetNumberField(TEXT("confidence"), Side.Confidence);
	}
	return ChainObject;
}
} // namespace

FString MakeMessageLine(const TSharedRef<FJsonObject>& Message)
{
	FString Serialized;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Serialized);
	FJsonSerializer::Serialize(Message, Writer);
	Serialized += TEXT("\n");
	return Serialized;
}

TSharedRef<FJsonObject> MakeHello(
	const FString& Seat, const FString& SessionId, const double WallClockMs, const double MonotonicMs)
{
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("type"), TypeHello);
	Message->SetStringField(TEXT("seat"), Seat);
	Message->SetNumberField(TEXT("protocolVersion"), ProtocolVersion);
	Message->SetStringField(TEXT("sessionId"), SessionId);
	Message->SetNumberField(TEXT("wallClockMs"), WallClockMs);
	Message->SetNumberField(TEXT("monotonicMs"), MonotonicMs);
	return Message;
}

TSharedRef<FJsonObject> MakeChoices(
	const FString& SelfAvatar, const FString& PartnerAvatar, const FString& ChoiceMode)
{
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("type"), TypeChoices);
	Message->SetStringField(TEXT("selfAvatar"), SelfAvatar);
	Message->SetStringField(TEXT("partnerAvatar"), PartnerAvatar);
	Message->SetStringField(TEXT("choiceMode"), ChoiceMode);
	return Message;
}

TSharedRef<FJsonObject> MakeReady()
{
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("type"), TypeReady);
	return Message;
}

TSharedRef<FJsonObject> MakeGo(const FString& Level)
{
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("type"), TypeGo);
	Message->SetStringField(TEXT("level"), Level);
	return Message;
}

TSharedRef<FJsonObject> MakeGoAck(const FString& Level)
{
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("type"), TypeGoAck);
	Message->SetStringField(TEXT("level"), Level);
	return Message;
}

TSharedRef<FJsonObject> MakeHeartbeat(const double MonotonicMs)
{
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("type"), TypeHeartbeat);
	Message->SetNumberField(TEXT("monotonicMs"), MonotonicMs);
	return Message;
}

TSharedRef<FJsonObject> MakeBye(const FString& Reason)
{
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("type"), TypeBye);
	Message->SetStringField(TEXT("reason"), Reason);
	return Message;
}

TSharedPtr<FJsonObject> ParseMessageLine(const FString& Line)
{
	TSharedPtr<FJsonObject> Message;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
	if (!FJsonSerializer::Deserialize(Reader, Message) || !Message.IsValid())
	{
		return nullptr;
	}
	return Message;
}

TSharedRef<FJsonObject> BuildSourceRowPayload(
	const FEmbodiedFusionSourceObservations& Observations,
	const double TimeSeconds,
	const FString& PhaseName)
{
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetNumberField(TEXT("t"), TimeSeconds);
	if (!PhaseName.IsEmpty())
	{
		TSharedPtr<FJsonObject> PhaseObject = MakeShared<FJsonObject>();
		PhaseObject->SetStringField(TEXT("phase_name"), PhaseName);
		Payload->SetObjectField(TEXT("phase"), PhaseObject);
	}

	TSharedPtr<FJsonObject> SourceObject = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> HmdObject = MakeShared<FJsonObject>();
	HmdObject->SetBoolField(TEXT("has_pose"), Observations.HmdPose.bHasPose);
	if (Observations.HmdPose.bHasPose)
	{
		HmdObject->SetArrayField(TEXT("loc"), VectorToJsonArray(Observations.HmdPose.LocationWorld));
		HmdObject->SetArrayField(TEXT("quat"), QuatToJsonArray(Observations.HmdPose.RotationWorld));
		HmdObject->SetArrayField(TEXT("tracking_up"), VectorToJsonArray(Observations.HmdPose.TrackingUpWorld));
		HmdObject->SetNumberField(TEXT("timestamp_seconds"), TimeSeconds);
		HmdObject->SetNumberField(TEXT("confidence"), 1.0);
	}
	SourceObject->SetObjectField(TEXT("hmd"), HmdObject);

	SourceObject->SetObjectField(TEXT("left_hand"), BuildHandObject(Observations.Hands, true, TimeSeconds));
	SourceObject->SetObjectField(TEXT("right_hand"), BuildHandObject(Observations.Hands, false, TimeSeconds));
	SourceObject->SetObjectField(TEXT("left_arm_chain"), BuildArmChainObject(Observations.ArmChain.Left, TimeSeconds));
	SourceObject->SetObjectField(TEXT("right_arm_chain"), BuildArmChainObject(Observations.ArmChain.Right, TimeSeconds));

	TSharedPtr<FJsonObject> HipsObject = MakeShared<FJsonObject>();
	HipsObject->SetBoolField(TEXT("has_hips"), Observations.ArmChain.bHasHips);
	if (Observations.ArmChain.bHasHips)
	{
		HipsObject->SetArrayField(TEXT("loc"), VectorToJsonArray(Observations.ArmChain.HipsLocationWorld));
		HipsObject->SetArrayField(TEXT("quat"), QuatToJsonArray(Observations.ArmChain.HipsRotationWorld));
		HipsObject->SetBoolField(TEXT("orientation_valid"), Observations.ArmChain.bHipsOrientationValid != 0);
		HipsObject->SetNumberField(TEXT("timestamp_seconds"), TimeSeconds);
		HipsObject->SetNumberField(TEXT("confidence"), Observations.ArmChain.HipsConfidence);
	}
	SourceObject->SetObjectField(TEXT("body_hips"), HipsObject);

	// Body pose: emit only valid landmarks, named from the parser's own table so the
	// round trip is exact by construction.
	bool bHasAnyLandmark = false;
	TSharedPtr<FJsonObject> LandmarksObject = MakeShared<FJsonObject>();
	for (const TPair<FString, EMediaPipePoseLandmark>& Entry :
		FMediaPipeTrackingFusionDatasetReplayRuntime::GetSourceRowLandmarkNames())
	{
		const int32 LandmarkIndex = static_cast<int32>(Entry.Value);
		if (Observations.BodyPose.LandmarkValid[LandmarkIndex] == 0)
		{
			continue;
		}
		bHasAnyLandmark = true;
		TSharedPtr<FJsonObject> LandmarkObject = MakeShared<FJsonObject>();
		LandmarkObject->SetBoolField(TEXT("valid"), true);
		LandmarkObject->SetNumberField(TEXT("reliability"), Observations.BodyPose.LandmarkReliability[LandmarkIndex]);
		LandmarkObject->SetArrayField(TEXT("pos"), VectorToJsonArray(Observations.BodyPose.LandmarksWorld[LandmarkIndex]));
		LandmarksObject->SetObjectField(Entry.Key, LandmarkObject);
	}
	TSharedPtr<FJsonObject> BodyPoseObject = MakeShared<FJsonObject>();
	BodyPoseObject->SetBoolField(TEXT("has_body_pose"), bHasAnyLandmark);
	BodyPoseObject->SetNumberField(TEXT("timestamp_seconds"), TimeSeconds);
	BodyPoseObject->SetNumberField(TEXT("confidence"), 1.0);
	if (bHasAnyLandmark)
	{
		BodyPoseObject->SetObjectField(TEXT("landmarks"), LandmarksObject);
	}
	SourceObject->SetObjectField(TEXT("body_pose"), BodyPoseObject);

	TSharedPtr<FJsonObject> FusionObject = MakeShared<FJsonObject>();
	FusionObject->SetObjectField(TEXT("source"), SourceObject);
	Payload->SetObjectField(TEXT("fusion"), FusionObject);
	return Payload;
}

TSharedRef<FJsonObject> MakeRow(
	const int64 Sequence,
	const double TMonoMs,
	const TSharedRef<FJsonObject>& Payload)
{
	TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
	Message->SetStringField(TEXT("type"), TypeRow);
	Message->SetNumberField(TEXT("seq"), static_cast<double>(Sequence));
	Message->SetNumberField(TEXT("tMonoMs"), TMonoMs);
	Message->SetObjectField(TEXT("payload"), Payload);
	return Message;
}

void RestampObservations(FEmbodiedFusionSourceObservations& Observations, const double StampNowSeconds)
{
	Observations.NowSeconds = StampNowSeconds;
	if (Observations.HmdPose.bHasPose)
	{
		Observations.HmdPose.TimestampSeconds = StampNowSeconds;
	}
	if (Observations.Hands.bHasLeft != 0)
	{
		Observations.Hands.LeftTimestampSeconds = StampNowSeconds;
	}
	if (Observations.Hands.bHasRight != 0)
	{
		Observations.Hands.RightTimestampSeconds = StampNowSeconds;
	}
	if (Observations.ArmChain.Left.bHasChain)
	{
		Observations.ArmChain.Left.TimestampSeconds = StampNowSeconds;
	}
	if (Observations.ArmChain.Right.bHasChain)
	{
		Observations.ArmChain.Right.TimestampSeconds = StampNowSeconds;
	}
	Observations.BodyPose.TimestampSeconds = StampNowSeconds;
}
} // namespace DyadLinkProtocol

bool FDyadLinkFraming::AppendBytes(const uint8* Bytes, const int32 Count, TArray<FString>& OutLines)
{
	if (bPoisoned)
	{
		return false;
	}
	if (Bytes && Count > 0)
	{
		Buffer.Append(Bytes, Count);
	}

	int32 LineStart = 0;
	for (int32 Index = 0; Index < Buffer.Num(); ++Index)
	{
		if (Buffer[Index] != '\n')
		{
			continue;
		}
		int32 LineEnd = Index;
		if (LineEnd > LineStart && Buffer[LineEnd - 1] == '\r')
		{
			--LineEnd;
		}
		FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Buffer.GetData() + LineStart), LineEnd - LineStart);
		OutLines.Emplace(Converted.Length(), Converted.Get());
		LineStart = Index + 1;
	}
	if (LineStart > 0)
	{
		Buffer.RemoveAt(0, LineStart, EAllowShrinking::No);
	}
	if (Buffer.Num() > MaxLineBytes)
	{
		bPoisoned = true;
		Buffer.Reset();
		return false;
	}
	return true;
}

void FDyadLinkClock::ProcessHelloExchange(
	const double LocalSendMonoMs, const double PeerMonoMs, const double LocalRecvMonoMs)
{
	RoundTripMs = FMath::Max(0.0, LocalRecvMonoMs - LocalSendMonoMs);
	// The peer stamped its HELLO roughly midway through our round trip.
	OffsetMs = PeerMonoMs - (LocalSendMonoMs + RoundTripMs * 0.5);
	bHasOffset = true;
}
