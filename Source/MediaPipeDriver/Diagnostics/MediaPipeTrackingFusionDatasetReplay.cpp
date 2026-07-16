#include "MediaPipeTrackingFusionDatasetReplay.h"

#include "Containers/StringConv.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "MediaPipeCVarPolicy.h"
#include "MediaPipeDriverRuntime.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeTrackingFusionDataset.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
TAutoConsoleVariable<FString> CVarTrackingFusionDatasetReplayFile(
	TEXT("mp.TrackingFusionDatasetReplayFile"),
	TEXT(""),
	TEXT("Tracking-fusion dataset manifest or JSONL sample file used by mp.LoadTrackingFusionDatasetReplay."));

TAutoConsoleVariable<float> CVarTrackingFusionDatasetReplayTimeScale(
	TEXT("mp.TrackingFusionDatasetReplayTimeScale"),
	1.0f,
	TEXT("Playback speed for tracking-fusion dataset replay. 1.0 replays at recorded speed."));

TAutoConsoleVariable<float> CVarTrackingFusionDatasetReplayStartOffsetSeconds(
	TEXT("mp.TrackingFusionDatasetReplayStartOffsetSeconds"),
	0.0f,
	TEXT("Replay time in seconds to start from when a tracking-fusion dataset replay starts or restarts."));

TAutoConsoleVariable<int32> CVarTrackingFusionDatasetReplayLoop(
	TEXT("mp.TrackingFusionDatasetReplayLoop"),
	1,
	TEXT("When non-zero, tracking-fusion dataset replay loops after the last sample."));

TAutoConsoleVariable<int32> CVarTrackingFusionDatasetReplayAllowTrackingFusionCapture(
	TEXT("mp.TrackingFusionDatasetReplayAllowTrackingFusionCapture"),
	0,
	TEXT("When non-zero, the replay startup actor will not clear mp.RecordTrackingFusionDatasetOnPlay. Used for deterministic replay-output avatar captures without live hardware."));

TAutoConsoleVariable<int32> CVarTrackingFusionDatasetReplayLiveParity(
	TEXT("mp.TrackingFusionDatasetReplayLiveParity"),
	0,
	TEXT("Replay policy live-parity mode for accuracy scoring against external references (MHA offline solves). 0 (default) = the verified byte-stable replay evaluation policy. 1 = replace the fusion-authority entries with the live defaults (MediaPipeAuthority=0, FullBodyMediaPipeAuthority=0, DriveSpine=0) and fold the live lower-body trial settings into the ReplayEvaluation layer so live profile re-applies cannot stomp them. NOT byte-stable; never enable for the replay regression gate."));

bool TryGetObjectField(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* FieldName,
	TSharedPtr<FJsonObject>& OutObject)
{
	OutObject.Reset();
	if (!Object.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* Child = nullptr;
	if (!Object->TryGetObjectField(FieldName, Child) || !Child)
	{
		return false;
	}

	OutObject = *Child;
	return OutObject.IsValid();
}

bool TryGetObjectPath(
	const TSharedPtr<FJsonObject>& Object,
	std::initializer_list<const TCHAR*> Path,
	TSharedPtr<FJsonObject>& OutObject)
{
	TSharedPtr<FJsonObject> CurrentObject = Object;
	for (const TCHAR* Segment : Path)
	{
		TSharedPtr<FJsonObject> ChildObject;
		if (!TryGetObjectField(CurrentObject, Segment, ChildObject))
		{
			OutObject.Reset();
			return false;
		}
		CurrentObject = ChildObject;
	}
	OutObject = CurrentObject;
	return OutObject.IsValid();
}

bool TryReadBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, const bool DefaultValue = false)
{
	if (!Object.IsValid())
	{
		return DefaultValue;
	}

	bool BoolValue = false;
	if (Object->TryGetBoolField(FieldName, BoolValue))
	{
		return BoolValue;
	}

	double NumberValue = 0.0;
	if (Object->TryGetNumberField(FieldName, NumberValue))
	{
		return NumberValue != 0.0;
	}
	return DefaultValue;
}

double ReadNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, const double DefaultValue = 0.0)
{
	if (!Object.IsValid())
	{
		return DefaultValue;
	}

	double NumberValue = 0.0;
	return Object->TryGetNumberField(FieldName, NumberValue) ? NumberValue : DefaultValue;
}

void SkipJsonWhitespace(const FString& Text, int32& Index)
{
	while (Index < Text.Len() && FChar::IsWhitespace(Text[Index]))
	{
		++Index;
	}
}

bool TryExtractNumberFieldAfter(
	const FString& Text,
	const TCHAR* FieldName,
	const int32 StartIndex,
	double& OutNumber)
{
	OutNumber = 0.0;
	const FString Token = FString::Printf(TEXT("\"%s\""), FieldName);
	const int32 FieldIndex = Text.Find(Token, ESearchCase::CaseSensitive, ESearchDir::FromStart, StartIndex);
	if (FieldIndex == INDEX_NONE)
	{
		return false;
	}

	int32 ValueIndex = FieldIndex + Token.Len();
	SkipJsonWhitespace(Text, ValueIndex);
	if (ValueIndex >= Text.Len() || Text[ValueIndex] != TEXT(':'))
	{
		return false;
	}
	++ValueIndex;
	SkipJsonWhitespace(Text, ValueIndex);

	int32 EndIndex = ValueIndex;
	while (EndIndex < Text.Len())
	{
		const TCHAR Char = Text[EndIndex];
		if (!(FChar::IsDigit(Char) ||
			Char == TEXT('-') ||
			Char == TEXT('+') ||
			Char == TEXT('.') ||
			Char == TEXT('e') ||
			Char == TEXT('E')))
		{
			break;
		}
		++EndIndex;
	}

	if (EndIndex <= ValueIndex)
	{
		return false;
	}

	OutNumber = FCString::Atod(*Text.Mid(ValueIndex, EndIndex - ValueIndex));
	return true;
}

bool TryExtractStringFieldAfter(
	const FString& Text,
	const TCHAR* FieldName,
	const int32 StartIndex,
	FString& OutValue)
{
	OutValue.Reset();
	const FString Token = FString::Printf(TEXT("\"%s\""), FieldName);
	const int32 FieldIndex = Text.Find(Token, ESearchCase::CaseSensitive, ESearchDir::FromStart, StartIndex);
	if (FieldIndex == INDEX_NONE)
	{
		return false;
	}

	int32 ValueIndex = FieldIndex + Token.Len();
	SkipJsonWhitespace(Text, ValueIndex);
	if (ValueIndex >= Text.Len() || Text[ValueIndex] != TEXT(':'))
	{
		return false;
	}
	++ValueIndex;
	SkipJsonWhitespace(Text, ValueIndex);
	if (ValueIndex >= Text.Len() || Text[ValueIndex] != TEXT('"'))
	{
		return false;
	}
	++ValueIndex;

	FString Value;
	bool bEscaped = false;
	for (int32 Index = ValueIndex; Index < Text.Len(); ++Index)
	{
		const TCHAR Char = Text[Index];
		if (bEscaped)
		{
			Value.AppendChar(Char);
			bEscaped = false;
			continue;
		}
		if (Char == TEXT('\\'))
		{
			bEscaped = true;
			continue;
		}
		if (Char == TEXT('"'))
		{
			OutValue = MoveTemp(Value);
			return true;
		}
		Value.AppendChar(Char);
	}
	return false;
}

int32 FindMatchingJsonObjectEnd(const FString& Text, const int32 ObjectStartIndex)
{
	if (!Text.IsValidIndex(ObjectStartIndex) || Text[ObjectStartIndex] != TEXT('{'))
	{
		return INDEX_NONE;
	}

	int32 Depth = 0;
	bool bInString = false;
	bool bEscaped = false;
	for (int32 Index = ObjectStartIndex; Index < Text.Len(); ++Index)
	{
		const TCHAR Char = Text[Index];
		if (bInString)
		{
			if (bEscaped)
			{
				bEscaped = false;
			}
			else if (Char == TEXT('\\'))
			{
				bEscaped = true;
			}
			else if (Char == TEXT('"'))
			{
				bInString = false;
			}
			continue;
		}

		if (Char == TEXT('"'))
		{
			bInString = true;
		}
		else if (Char == TEXT('{'))
		{
			++Depth;
		}
		else if (Char == TEXT('}'))
		{
			--Depth;
			if (Depth == 0)
			{
				return Index;
			}
			if (Depth < 0)
			{
				return INDEX_NONE;
			}
		}
	}
	return INDEX_NONE;
}

bool TryExtractObjectFieldFragmentAfter(
	const FString& Text,
	const TCHAR* FieldName,
	const int32 StartIndex,
	FString& OutFragment)
{
	OutFragment.Reset();
	const FString Token = FString::Printf(TEXT("\"%s\""), FieldName);
	const int32 FieldIndex = Text.Find(Token, ESearchCase::CaseSensitive, ESearchDir::FromStart, StartIndex);
	if (FieldIndex == INDEX_NONE)
	{
		return false;
	}

	int32 ValueIndex = FieldIndex + Token.Len();
	SkipJsonWhitespace(Text, ValueIndex);
	if (ValueIndex >= Text.Len() || Text[ValueIndex] != TEXT(':'))
	{
		return false;
	}
	++ValueIndex;
	SkipJsonWhitespace(Text, ValueIndex);
	if (ValueIndex >= Text.Len() || Text[ValueIndex] != TEXT('{'))
	{
		return false;
	}

	const int32 EndIndex = FindMatchingJsonObjectEnd(Text, ValueIndex);
	if (EndIndex == INDEX_NONE)
	{
		return false;
	}

	OutFragment = Text.Mid(ValueIndex, EndIndex - ValueIndex + 1);
	return true;
}

TSharedPtr<FJsonObject> BuildCompactReplaySampleObjectFromLine(const FString& TrimmedLine)
{
	if (TrimmedLine.IsEmpty())
	{
		return nullptr;
	}

	const int32 FusionIndex =
		TrimmedLine.Find(TEXT("\"fusion\""), ESearchCase::CaseSensitive, ESearchDir::FromStart);
	if (FusionIndex == INDEX_NONE)
	{
		return nullptr;
	}

	FString SourceFragment;
	if (!TryExtractObjectFieldFragmentAfter(TrimmedLine, TEXT("source"), FusionIndex, SourceFragment))
	{
		return nullptr;
	}

	const FString SourceWrapperText = FString::Printf(TEXT("{\"source\":%s}"), *SourceFragment);
	TSharedPtr<FJsonObject> SourceWrapperObject;
	const TSharedRef<TJsonReader<>> SourceReader = TJsonReaderFactory<>::Create(SourceWrapperText);
	if (!FJsonSerializer::Deserialize(SourceReader, SourceWrapperObject) || !SourceWrapperObject.IsValid())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> SourceObject;
	if (!TryGetObjectField(SourceWrapperObject, TEXT("source"), SourceObject))
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> SampleObject = MakeShared<FJsonObject>();
	double TimeSeconds = 0.0;
	TryExtractNumberFieldAfter(TrimmedLine, TEXT("t"), 0, TimeSeconds);
	SampleObject->SetNumberField(TEXT("t"), TimeSeconds);

	FString PhaseName;
	const int32 PhaseIndex =
		TrimmedLine.Find(TEXT("\"phase\""), ESearchCase::CaseSensitive, ESearchDir::FromStart);
	if (PhaseIndex != INDEX_NONE &&
		TryExtractStringFieldAfter(TrimmedLine, TEXT("phase_name"), PhaseIndex, PhaseName))
	{
		TSharedPtr<FJsonObject> PhaseObject = MakeShared<FJsonObject>();
		PhaseObject->SetStringField(TEXT("phase_name"), PhaseName);
		SampleObject->SetObjectField(TEXT("phase"), PhaseObject);
	}

	TSharedPtr<FJsonObject> FusionObject = MakeShared<FJsonObject>();
	FusionObject->SetObjectField(TEXT("source"), SourceObject);
	SampleObject->SetObjectField(TEXT("fusion"), FusionObject);
	return SampleObject;
}

bool TryReadVectorArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, FVector& OutVector)
{
	OutVector = FVector::ZeroVector;
	if (!Object.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (!Object->TryGetArrayField(FieldName, Array) || !Array || Array->Num() < 3)
	{
		return false;
	}

	double X = 0.0;
	double Y = 0.0;
	double Z = 0.0;
	if (!(*Array)[0]->TryGetNumber(X) ||
		!(*Array)[1]->TryGetNumber(Y) ||
		!(*Array)[2]->TryGetNumber(Z))
	{
		return false;
	}

	OutVector = FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
	return !OutVector.ContainsNaN();
}

bool TryReadQuatArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, FQuat& OutQuat)
{
	OutQuat = FQuat::Identity;
	if (!Object.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (!Object->TryGetArrayField(FieldName, Array) || !Array || Array->Num() < 4)
	{
		return false;
	}

	double X = 0.0;
	double Y = 0.0;
	double Z = 0.0;
	double W = 1.0;
	if (!(*Array)[0]->TryGetNumber(X) ||
		!(*Array)[1]->TryGetNumber(Y) ||
		!(*Array)[2]->TryGetNumber(Z) ||
		!(*Array)[3]->TryGetNumber(W))
	{
		return false;
	}

	OutQuat = FQuat(
		static_cast<float>(X),
		static_cast<float>(Y),
		static_cast<float>(Z),
		static_cast<float>(W));
	if (OutQuat.ContainsNaN() || OutQuat.SizeSquared() <= UE_SMALL_NUMBER)
	{
		OutQuat = FQuat::Identity;
		return false;
	}
	OutQuat.Normalize();
	return true;
}

const TMap<FString, EMediaPipePoseLandmark>& LandmarkNameMap()
{
	static const TMap<FString, EMediaPipePoseLandmark> Map = {
		{ TEXT("nose"), EMediaPipePoseLandmark::Nose },
		{ TEXT("left_eye_inner"), EMediaPipePoseLandmark::LeftEyeInner },
		{ TEXT("left_eye"), EMediaPipePoseLandmark::LeftEye },
		{ TEXT("left_eye_outer"), EMediaPipePoseLandmark::LeftEyeOuter },
		{ TEXT("right_eye_inner"), EMediaPipePoseLandmark::RightEyeInner },
		{ TEXT("right_eye"), EMediaPipePoseLandmark::RightEye },
		{ TEXT("right_eye_outer"), EMediaPipePoseLandmark::RightEyeOuter },
		{ TEXT("left_ear"), EMediaPipePoseLandmark::LeftEar },
		{ TEXT("right_ear"), EMediaPipePoseLandmark::RightEar },
		{ TEXT("mouth_left"), EMediaPipePoseLandmark::MouthLeft },
		{ TEXT("mouth_right"), EMediaPipePoseLandmark::MouthRight },
		{ TEXT("left_shoulder"), EMediaPipePoseLandmark::LeftShoulder },
		{ TEXT("right_shoulder"), EMediaPipePoseLandmark::RightShoulder },
		{ TEXT("left_elbow"), EMediaPipePoseLandmark::LeftElbow },
		{ TEXT("right_elbow"), EMediaPipePoseLandmark::RightElbow },
		{ TEXT("left_wrist"), EMediaPipePoseLandmark::LeftWrist },
		{ TEXT("right_wrist"), EMediaPipePoseLandmark::RightWrist },
		{ TEXT("left_pinky"), EMediaPipePoseLandmark::LeftPinky },
		{ TEXT("right_pinky"), EMediaPipePoseLandmark::RightPinky },
		{ TEXT("left_index"), EMediaPipePoseLandmark::LeftIndex },
		{ TEXT("right_index"), EMediaPipePoseLandmark::RightIndex },
		{ TEXT("left_thumb"), EMediaPipePoseLandmark::LeftThumb },
		{ TEXT("right_thumb"), EMediaPipePoseLandmark::RightThumb },
		{ TEXT("left_hip"), EMediaPipePoseLandmark::LeftHip },
		{ TEXT("right_hip"), EMediaPipePoseLandmark::RightHip },
		{ TEXT("left_knee"), EMediaPipePoseLandmark::LeftKnee },
		{ TEXT("right_knee"), EMediaPipePoseLandmark::RightKnee },
		{ TEXT("left_ankle"), EMediaPipePoseLandmark::LeftAnkle },
		{ TEXT("right_ankle"), EMediaPipePoseLandmark::RightAnkle },
		{ TEXT("left_heel"), EMediaPipePoseLandmark::LeftHeel },
		{ TEXT("right_heel"), EMediaPipePoseLandmark::RightHeel },
		{ TEXT("left_foot_index"), EMediaPipePoseLandmark::LeftFootIndex },
		{ TEXT("right_foot_index"), EMediaPipePoseLandmark::RightFootIndex },
	};
	return Map;
}

FString ResolveReplayPath(const FString& RawPath)
{
	const FString Trimmed = RawPath.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return FString();
	}

	return FPaths::ConvertRelativePathToFull(
		FPaths::IsRelative(Trimmed)
			? FPaths::Combine(FPaths::ProjectDir(), Trimmed)
			: Trimmed);
}

void SetConsoleInt(const TCHAR* Name, const int32 Value)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		Variable->Set(Value, ECVF_SetByConsole);
	}
}

void SetConsoleFloat(const TCHAR* Name, const float Value)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		Variable->Set(Value, ECVF_SetByConsole);
	}
}

void SetConsoleString(const TCHAR* Name, const FString& Value)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		Variable->Set(*Value, ECVF_SetByConsole);
	}
}

void ApplyReplayPoseCVars()
{
	FMediaPipeTrackingFusionDatasetReplayRuntime::ApplyReplayPoseCVars_GameThread();
}

double ResolveReplayStartOffsetSeconds(const double DurationSeconds)
{
	const double RawOffsetSeconds = FMath::Max(
		0.0,
		static_cast<double>(CVarTrackingFusionDatasetReplayStartOffsetSeconds.GetValueOnGameThread()));
	if (DurationSeconds <= UE_SMALL_NUMBER)
	{
		return RawOffsetSeconds;
	}
	if (CVarTrackingFusionDatasetReplayLoop.GetValueOnGameThread() != 0)
	{
		return FMath::Fmod(RawOffsetSeconds, DurationSeconds);
	}
	return FMath::Min(RawOffsetSeconds, DurationSeconds);
}

FString CleanReplayPathArgument(FString RawPath)
{
	RawPath.TrimStartAndEndInline();
	while (!RawPath.IsEmpty())
	{
		const TCHAR LastChar = RawPath[RawPath.Len() - 1];
		if (LastChar != TEXT(';') && LastChar != TEXT(','))
		{
			break;
		}
		RawPath.LeftChopInline(1);
		RawPath.TrimEndInline();
	}
	return RawPath;
}

void LoadTrackingFusionDatasetReplayCommand(const TArray<FString>& Args)
{
	const FString RawPath = CleanReplayPathArgument(
		Args.Num() > 0
			? Args[0]
			: CVarTrackingFusionDatasetReplayFile.GetValueOnGameThread());
	FString Error;
	if (!FMediaPipeTrackingFusionDatasetReplayRuntime::Get().LoadFromPath(RawPath, Error))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.LoadTrackingFusionDatasetReplay: failed path=%s error=%s"), *RawPath, *Error);
		return;
	}

	CVarTrackingFusionDatasetReplayFile.AsVariable()->Set(*RawPath, ECVF_SetByConsole);
	ApplyReplayPoseCVars();
	FMediaPipeTrackingFusionDatasetReplayRuntime::Get().Start(FPlatformTime::Seconds());
	const FMediaPipeTrackingFusionReplayStatus Status =
		FMediaPipeTrackingFusionDatasetReplayRuntime::Get().GetStatus();
	UE_LOG(LogMediaPipePose, Log,
		TEXT("mp.LoadTrackingFusionDatasetReplay: active=1 samples=%d duration=%.3fs timeScale=%.3f startOffset=%.3fs loop=%d path=%s avatarScale=unchanged avatarProportions=authoritative"),
		Status.SampleCount,
		Status.DurationSeconds,
		Status.TimeScale,
		Status.StartOffsetSeconds,
		Status.bLoop ? 1 : 0,
		*Status.SourcePath);
}

void StopTrackingFusionDatasetReplayCommand()
{
	FMediaPipeTrackingFusionDatasetReplayRuntime::Get().Stop();
	UE_LOG(LogMediaPipePose, Log, TEXT("mp.StopTrackingFusionDatasetReplay: active=0"));
}

void TrackingFusionDatasetReplayStatusCommand()
{
	const FMediaPipeTrackingFusionReplayStatus Status =
		FMediaPipeTrackingFusionDatasetReplayRuntime::Get().GetStatus();
	UE_LOG(LogMediaPipePose, Log,
		TEXT("mp.TrackingFusionDatasetReplayStatus: loaded=%d active=%d samples=%d duration=%.3fs timeScale=%.3f startOffset=%.3fs loop=%d path=%s label=%s"),
		Status.bLoaded ? 1 : 0,
		Status.bActive ? 1 : 0,
		Status.SampleCount,
		Status.DurationSeconds,
		Status.TimeScale,
		Status.StartOffsetSeconds,
		Status.bLoop ? 1 : 0,
		*Status.SourcePath,
		*Status.Label);
}

void SeekTrackingFusionDatasetReplayCommand(const TArray<FString>& Args)
{
	if (Args.Num() <= 0)
	{
		TrackingFusionDatasetReplayStatusCommand();
		return;
	}

	const double StartOffsetSeconds = FMath::Max(0.0, FCString::Atod(*Args[0]));
	CVarTrackingFusionDatasetReplayStartOffsetSeconds.AsVariable()->Set(
		static_cast<float>(StartOffsetSeconds),
		ECVF_SetByConsole);
	FMediaPipeTrackingFusionDatasetReplayRuntime::Get().Start(FPlatformTime::Seconds());
	const FMediaPipeTrackingFusionReplayStatus Status =
		FMediaPipeTrackingFusionDatasetReplayRuntime::Get().GetStatus();
	UE_LOG(LogMediaPipePose, Log,
		TEXT("mp.SeekTrackingFusionDatasetReplay: active=%d startOffset=%.3fs duration=%.3fs timeScale=%.3f loop=%d"),
		Status.bActive ? 1 : 0,
		Status.StartOffsetSeconds,
		Status.DurationSeconds,
		Status.TimeScale,
		Status.bLoop ? 1 : 0);
}

void PrepareTrackingFusionDatasetReplayOutputCaptureCommand(const TArray<FString>& Args)
{
	double DurationSeconds = 210.0;
	double ChunkMegabytes = 128.0;
	FString Label(TEXT("replay_avatar_output"));
	FString OutputPath;
	FString ReplayPath;
	bool bAnalyzeAfterWrite = true;

	for (const FString& Arg : Args)
	{
		FString Key;
		FString Value;
		if (!Arg.Split(TEXT("="), &Key, &Value))
		{
			continue;
		}

		if (Key.Equals(TEXT("duration"), ESearchCase::IgnoreCase))
		{
			DurationSeconds = FMath::Clamp(FCString::Atod(*Value), 0.1, 240.0);
		}
		else if (Key.Equals(TEXT("chunkMB"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("chunk_mb"), ESearchCase::IgnoreCase))
		{
			ChunkMegabytes = FMath::Clamp(FCString::Atod(*Value), 8.0, 1024.0);
		}
		else if (Key.Equals(TEXT("label"), ESearchCase::IgnoreCase))
		{
			Label = FMediaPipeTrackingFusionDataset::SanitizeLabel(Value);
		}
		else if (Key.Equals(TEXT("path"), ESearchCase::IgnoreCase))
		{
			OutputPath = Value;
		}
		else if (Key.Equals(TEXT("replay"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("manifest"), ESearchCase::IgnoreCase))
		{
			ReplayPath = CleanReplayPathArgument(Value);
		}
		else if (Key.Equals(TEXT("analyze"), ESearchCase::IgnoreCase))
		{
			bAnalyzeAfterWrite = FCString::Atoi(*Value) != 0;
		}
	}

	CVarTrackingFusionDatasetReplayAllowTrackingFusionCapture.AsVariable()->Set(1, ECVF_SetByConsole);
	if (!ReplayPath.IsEmpty())
	{
		CVarTrackingFusionDatasetReplayFile.AsVariable()->Set(*ReplayPath, ECVF_SetByConsole);
	}

	SetConsoleInt(TEXT("mp.RecordMannyHeadOnPlay"), 0);
	SetConsoleInt(TEXT("mp.RecordMPQShadowFusionOnPlay"), 0);
	SetConsoleInt(TEXT("mp.RecordMPQShadowFusionStage1TorsoPelvisHintOnPlay"), 0);
	SetConsoleInt(TEXT("mp.RecordMPQShadowFusionStage2ShoulderClavicleHintOnPlay"), 0);
	SetConsoleInt(TEXT("r.VSync"), 0);
	SetConsoleInt(TEXT("t.IdleWhenNotForeground"), 0);
	SetConsoleInt(TEXT("Slate.bAllowThrottling"), 0);
	SetConsoleFloat(TEXT("t.MaxFPS"), 90.0f);
	SetConsoleInt(TEXT("mp.QuestHandTracking"), 1);
	SetConsoleInt(TEXT("mp.QuestHandDriveFingerBones"), 1);
	SetConsoleInt(TEXT("mp.QuestWristCalibrationHud"), 0);
	SetConsoleInt(TEXT("mp.QuestArmLengthCalibrationHud"), 0);
	SetConsoleInt(TEXT("mp.QuestArmLengthCalibrationStartup"), 0);
	SetConsoleInt(TEXT("mp.QuestArmDropoutDownFallback"), 0);
	SetConsoleInt(TEXT("mp.QuestConstrainedArmBodyFallback"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeArmHoldOnQuestHandLoss"), 0);
	FMediaPipeTrackingFusionDatasetReplayRuntime::ApplyReplayPoseCVars_GameThread();

	SetConsoleFloat(TEXT("mp.RecordTrackingFusionDatasetDuration"), static_cast<float>(DurationSeconds));
	SetConsoleFloat(TEXT("mp.RecordTrackingFusionDatasetSampleRate"), 30.0f);
	SetConsoleString(TEXT("mp.RecordTrackingFusionDatasetBoneMode"), TEXT("all"));
	SetConsoleString(
		TEXT("mp.RecordTrackingFusionDatasetPhasePreset"),
		FMediaPipeTrackingFusionDataset::AvatarLockedSyncCalibrationPreset);
	SetConsoleFloat(TEXT("mp.RecordTrackingFusionDatasetChunkMegabytes"), static_cast<float>(ChunkMegabytes));
	SetConsoleString(TEXT("mp.RecordTrackingFusionDatasetLabel"), Label);
	SetConsoleString(TEXT("mp.RecordTrackingFusionDatasetPath"), OutputPath);
	SetConsoleInt(TEXT("mp.RecordTrackingFusionDatasetAnalyzeAfterWrite"), bAnalyzeAfterWrite ? 1 : 0);
	SetConsoleInt(TEXT("mp.RecordTrackingFusionDatasetOnPlay"), 1);

	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.PrepareTrackingFusionDatasetReplayOutputCapture: armed duration=%.3fs sampleRate=30.000Hz boneMode=all phasePreset=%s chunkMB=%.1f label=%s path=%s analyze=%d replay=%s BodyFusion=(Enable=1 WritePose=1 MediaPipeAuthority=2 FullBodyMediaPipeAuthority=1) lowerBody=(DriveLegs=1 UseLegIK=0 UseLegIKFootPlant=0 UseFkRootGrounding=1 DirectSegmentLegs=1 DriveFootRotation=1 footUp=measured_heel_to_toe) tickPolicy=(t.MaxFPS=90 r.VSync=0 t.IdleWhenNotForeground=0 Slate.bAllowThrottling=0) avatarScale=unchanged avatarProportions=authoritative otherRecorders=off"),
		DurationSeconds,
		FMediaPipeTrackingFusionDataset::AvatarLockedSyncCalibrationPreset,
		ChunkMegabytes,
		*Label,
		OutputPath.IsEmpty() ? TEXT("<auto>") : *OutputPath,
		bAnalyzeAfterWrite ? 1 : 0,
		ReplayPath.IsEmpty() ? TEXT("<map actor/default>") : *ReplayPath);
}

FAutoConsoleCommand GLoadTrackingFusionDatasetReplayCommand(
	TEXT("mp.LoadTrackingFusionDatasetReplay"),
	TEXT("Load and play a tracking-fusion dataset manifest/JSONL as live Quest+MediaPipe source input. Usage: mp.LoadTrackingFusionDatasetReplay Saved/CodexAgent/Diagnostics/capture.json"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&LoadTrackingFusionDatasetReplayCommand));

FAutoConsoleCommand GStopTrackingFusionDatasetReplayCommand(
	TEXT("mp.StopTrackingFusionDatasetReplay"),
	TEXT("Stop tracking-fusion dataset replay input."),
	FConsoleCommandDelegate::CreateStatic(&StopTrackingFusionDatasetReplayCommand));

FAutoConsoleCommand GTrackingFusionDatasetReplayStatusCommand(
	TEXT("mp.TrackingFusionDatasetReplayStatus"),
	TEXT("Log the current tracking-fusion dataset replay status."),
	FConsoleCommandDelegate::CreateStatic(&TrackingFusionDatasetReplayStatusCommand));

FAutoConsoleCommand GSeekTrackingFusionDatasetReplayCommand(
	TEXT("mp.SeekTrackingFusionDatasetReplay"),
	TEXT("Restart active tracking-fusion dataset replay from a replay-time offset in seconds. Usage: mp.SeekTrackingFusionDatasetReplay 150"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&SeekTrackingFusionDatasetReplayCommand));

FAutoConsoleCommand GPrepareTrackingFusionDatasetReplayOutputCaptureCommand(
	TEXT("mp.PrepareTrackingFusionDatasetReplayOutputCapture"),
	TEXT("Arm the next PIE run to record deterministic replay-driven avatar output at 30 Hz/all bones without live Quest or MediaPipe hardware. Usage: mp.PrepareTrackingFusionDatasetReplayOutputCapture duration=210 label=replay_avatar_output path=Saved/CodexAgent/Diagnostics/replay_output.json analyze=1"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&PrepareTrackingFusionDatasetReplayOutputCaptureCommand));
}

FMediaPipeTrackingFusionDatasetReplayRuntime& FMediaPipeTrackingFusionDatasetReplayRuntime::Get()
{
	static FMediaPipeTrackingFusionDatasetReplayRuntime Runtime;
	return Runtime;
}

void FMediaPipeTrackingFusionDatasetReplayRuntime::Reset()
{
	Samples.Reset();
	Status = FMediaPipeTrackingFusionReplayStatus();
}

bool FMediaPipeTrackingFusionDatasetReplayRuntime::LoadFromPath(const FString& RawPath, FString& OutError)
{
	const FString ResolvedPath = ResolveReplayPath(RawPath);
	if (ResolvedPath.IsEmpty())
	{
		OutError = TEXT("empty path");
		return false;
	}
	if (!FPaths::FileExists(ResolvedPath))
	{
		OutError = FString::Printf(TEXT("file does not exist: %s"), *ResolvedPath);
		return false;
	}

	TArray<FString> SampleFiles;
	FString Label;
	if (FPaths::GetExtension(ResolvedPath).Equals(TEXT("jsonl"), ESearchCase::IgnoreCase))
	{
		SampleFiles.Add(ResolvedPath);
		Label = FPaths::GetBaseFilename(ResolvedPath);
	}
	else if (!LoadManifestSampleFiles(ResolvedPath, SampleFiles, Label, OutError))
	{
		return false;
	}

	TArray<FReplaySample> LoadedSamples;
	for (const FString& SampleFile : SampleFiles)
	{
		if (!LoadJsonlSamples(SampleFile, LoadedSamples, OutError))
		{
			return false;
		}
	}

	LoadedSamples.Sort(
		[](const FReplaySample& A, const FReplaySample& B)
		{
			return A.TimeSeconds < B.TimeSeconds;
		});
	if (LoadedSamples.Num() <= 0)
	{
		OutError = TEXT("no usable source samples found");
		return false;
	}

	FScopeLock Lock(&CriticalSection);
	Reset();
	Samples = MoveTemp(LoadedSamples);
	Status.bLoaded = true;
	Status.bActive = false;
	Status.SourcePath = ResolvedPath;
	Status.Label = Label;
	Status.SampleCount = Samples.Num();
	Status.DurationSeconds = Samples.Last().TimeSeconds;
	Status.TimeScale = FMath::Max(0.01, static_cast<double>(CVarTrackingFusionDatasetReplayTimeScale.GetValueOnGameThread()));
	Status.StartOffsetSeconds = ResolveReplayStartOffsetSeconds(Status.DurationSeconds);
	Status.bLoop = CVarTrackingFusionDatasetReplayLoop.GetValueOnGameThread() != 0;
	return true;
}

bool FMediaPipeTrackingFusionDatasetReplayRuntime::LoadAndStartFromPath(
	const FString& RawPath,
	const double NowSeconds,
	FString& OutError)
{
	if (!LoadFromPath(RawPath, OutError))
	{
		return false;
	}

	ApplyReplayPoseCVars_GameThread();
	Start(NowSeconds);
	return IsActive();
}

void FMediaPipeTrackingFusionDatasetReplayRuntime::ApplyReplayPoseCVars_GameThread()
{
	// REPLAY POLICY CONTRACT: this table is the verified replay policy (2026-06-10 baseline plus
	// the 2026-06-12 lower-body scaffold entries, a deliberate behavior change). It is applied
	// through the priority stack so no lower-priority writer (live profiles, capture scopes) can
	// stomp it while the replay layer is active.
	FMediaPipeCVarPolicyLayer Layer;
	Layer.PolicyId = FName(TEXT("ReplayEvaluation"));
	Layer.Priority = EMediaPipeCVarPolicyPriority::ReplayEvaluation;
	Layer.Settings = {
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.BodyFusion.Enable"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.BodyFusion.WritePose"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.BodyFusion.Debug"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.BodyFusion.MediaPipeAuthority"), 2),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.BodyFusion.FullBodyMediaPipeAuthority"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.BodyFusion.CalibrationStableFrames"), 1),
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.BodyFusion.CalibrationHoldSeconds"), 0.0f),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeDriveSpine"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeDrivePelvisTranslation"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeDriveLegs"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeUseLegIK"), 0),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeUseLegIKFootPlant"), 0),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeUseFkRootGrounding"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeDriveFootRotation"), 1),
		// Depth-cautious lower-body quality policy for replay-output evaluation: rotate
		// implausible backward knee poles toward forward/lateral and reference grounded feet
		// to the floor.
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeLegKneeBackwardPoleSuppression"), 0.6f),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeFootGroundedWorldUp"), 1),
		// Lower-body scaffold: the recorded Quest HMD height DETERMINES metric squat depth (the
		// monocular landmark frame is hip-centered and cannot observe it; weight 1.0 means
		// monocular compression only covers HMD dropouts and the confidence ramp-in).
		// Grounded-leg flexion is corrected toward that metric target on each avatar's own
		// thigh/calf lengths, and the grounded bend is redistributed so the femur, not the shin,
		// carries its natural share (monocular capture cannot see femur depth rotation and sinks
		// the knee). MediaPipe keeps owning leg timing, phase, momentum, lateral swing, and
		// lifted-foot motion.
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeLegScaffoldHmdWeight"), 1.0f),
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeLegScaffoldFlexionWeight"), 0.8f),
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeLegScaffoldFlexionMaxAdjustDeg"), 40.0f),
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeLegScaffoldBendRedistributionWeight"), 0.8f),
		// Keep grounded soles flat: the sloped reference foot basis pitches planarized feet
		// toe-up (ankle sinks to ball height) without this clamp.
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeFootGroundedPitchClamp"), 1),
		// Recorded Quest hand skeletons (schema-v2 replay caches) drive wrist rotation and
		// fingers during replay; the arm position solve keeps following the recorded arm chain.
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.QuestHandTracking"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.QuestHandDriveFingerBones"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeLegScaffoldLog"), 1),
		// Region-quality diagnostics: ownership/confidence/evidence rows in the log plus JSONL
		// timelines for offline plots. Diagnostics only; no pose authority changes.
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.BodyFusion.RegionQualityLog"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.BodyFusion.RegionQualityCapture"), 1),
		// Replay evaluation runs in desktop PIE, not on a Quest. The Auto Quest VR performance
		// profile turns shadows off, which makes grounded feet look like they hover in evidence
		// screenshots; restore desktop shadow/resolution quality whenever the replay policy
		// applies.
		FMediaPipeCVarSetting::MakeInt(TEXT("sg.ShadowQuality"), 3),
		FMediaPipeCVarSetting::MakeInt(TEXT("r.ShadowQuality"), 5),
		FMediaPipeCVarSetting::MakeInt(TEXT("r.Shadow.MaxResolution"), 2048),
		FMediaPipeCVarSetting::MakeFloat(TEXT("r.ScreenPercentage"), 100.0f),
		// Replay seeks and loop wraps teleport the head every time; groom physics cannot
		// survive those jumps and long MetaHuman hair explodes into giant ribbons. Render
		// strands statically (bound pose follows the head, no simulation) while replay
		// evaluation is active.
		FMediaPipeCVarSetting::MakeInt(TEXT("r.HairStrands.Simulation"), 0),
	};

	if (CVarTrackingFusionDatasetReplayLiveParity.GetValueOnGameThread() != 0)
	{
		// Live-parity scoring mode (2026-07-04): replay the dataset under the SAME corrective
		// stack the user runs live, so external-reference scoring (MHA offline solves) measures
		// the system the user actually feels. Folding the trial settings into this layer (rather
		// than raw-setting them) closes the stomp hole observed 2026-07-04: a live profile apply
		// during PIE startup re-asserted mp.MediaPipeArmReliabilityGate=1 over a pre-PIE raw set.
		auto ReplaceOrAdd = [&Layer](const FMediaPipeCVarSetting& NewSetting)
		{
			for (FMediaPipeCVarSetting& Existing : Layer.Settings)
			{
				if (Existing.Name == NewSetting.Name)
				{
					Existing = NewSetting;
					return;
				}
			}
			Layer.Settings.Add(NewSetting);
		};
		// Settings consolidation (2026-07-06): fold the CAPTURED live profile first - the
		// imperative SetConsole* writes recorded via the capture sink, so parity sees the
		// exact live values (mp.MediaPipeClavicleShrugWeight read 0 in every scoring replay
		// while live ran 0.20; that class of silent divergence ends here). Non-mp settings
		// (VR perf r.*/sg.*) are dropped: replay keeps desktop quality. Then the trial layer
		// for the active mp.MediaPipeSettingsVariant, mirroring the live layering order.
		for (const FMediaPipeCVarSetting& ProfileSetting :
			MediaPipeDriverRuntime::CaptureLiveProfileSettings())
		{
			if (ProfileSetting.Name.StartsWith(TEXT("mp.")))
			{
				ReplaceOrAdd(ProfileSetting);
			}
		}
		for (const FMediaPipeCVarSetting& TrialSetting :
			MediaPipeDriverRuntime::GetLiveLowerBodyTrialSettingsForActiveVariant())
		{
			ReplaceOrAdd(TrialSetting);
		}
		ReplaceOrAdd(FMediaPipeCVarSetting::MakeInt(TEXT("mp.BodyFusion.MediaPipeAuthority"), 0));
		ReplaceOrAdd(FMediaPipeCVarSetting::MakeInt(TEXT("mp.BodyFusion.FullBodyMediaPipeAuthority"), 0));
		ReplaceOrAdd(FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeDriveSpine"), 0));
		// WritePose=1 is the replay-evaluation output path; LIVE runs WritePose=0 with the anim
		// node driving bones directly, and the arm solve gates live-only features (overhead
		// rescue, camera hand ownership) on !WritePose. Parity must match live here or those
		// paths stay dead in scoring replays.
		ReplaceOrAdd(FMediaPipeCVarSetting::MakeInt(TEXT("mp.BodyFusion.WritePose"), 0));
		UE_LOG(LogMediaPipePose, Warning,
			TEXT("Replay policy: LIVE-PARITY mode active (mp.TrackingFusionDatasetReplayLiveParity=1). Not byte-stable; scoring use only."));
	}

	FMediaPipeCVarPolicyStack::Get().Apply(Layer);
}

bool FMediaPipeTrackingFusionDatasetReplayRuntime::IsLiveParityEnabled()
{
	return CVarTrackingFusionDatasetReplayLiveParity.GetValueOnAnyThread() != 0;
}

void FMediaPipeTrackingFusionDatasetReplayRuntime::Start(const double NowSeconds)
{
	FScopeLock Lock(&CriticalSection);
	if (!Status.bLoaded || Samples.Num() <= 0)
	{
		return;
	}

	Status.bActive = true;
	Status.PlaybackStartWorldSeconds = NowSeconds;
	Status.TimeScale = FMath::Max(0.01, static_cast<double>(CVarTrackingFusionDatasetReplayTimeScale.GetValueOnGameThread()));
	Status.StartOffsetSeconds = ResolveReplayStartOffsetSeconds(Status.DurationSeconds);
	Status.bLoop = CVarTrackingFusionDatasetReplayLoop.GetValueOnGameThread() != 0;
}

void FMediaPipeTrackingFusionDatasetReplayRuntime::Stop()
{
	FScopeLock Lock(&CriticalSection);
	Status.bActive = false;
	Status.PlaybackStartWorldSeconds = -1.0;
}

bool FMediaPipeTrackingFusionDatasetReplayRuntime::IsActive() const
{
	FScopeLock Lock(&CriticalSection);
	return Status.bLoaded && Status.bActive && Samples.Num() > 0;
}

bool FMediaPipeTrackingFusionDatasetReplayRuntime::GetCurrentObservations(
	const double NowSeconds,
	FEmbodiedFusionSourceObservations& OutObservations,
	FString* OutPhaseName)
{
	FScopeLock Lock(&CriticalSection);
	if (!Status.bLoaded || !Status.bActive || Samples.Num() <= 0 || Status.PlaybackStartWorldSeconds < 0.0)
	{
		return false;
	}

	const double RawElapsedSeconds =
		FMath::Max(0.0, (NowSeconds - Status.PlaybackStartWorldSeconds) * FMath::Max(Status.TimeScale, 0.01));
	double ReplayElapsedSeconds = Status.StartOffsetSeconds + RawElapsedSeconds;
	if (Status.bLoop && Status.DurationSeconds > UE_SMALL_NUMBER)
	{
		ReplayElapsedSeconds = FMath::Fmod(ReplayElapsedSeconds, Status.DurationSeconds);
	}
	else
	{
		ReplayElapsedSeconds = FMath::Min(ReplayElapsedSeconds, Status.DurationSeconds);
	}

	const int32 SampleIndex = FindSampleIndexForElapsed(ReplayElapsedSeconds);
	if (!Samples.IsValidIndex(SampleIndex))
	{
		return false;
	}

	OutObservations = Samples[SampleIndex].Observations;
	RetimestampObservations(OutObservations, NowSeconds);
	if (OutPhaseName)
	{
		*OutPhaseName = Samples[SampleIndex].PhaseName;
	}
	return true;
}

bool FMediaPipeTrackingFusionDatasetReplayRuntime::GetObservationsAtDatasetTime(
	const double DatasetTimeSeconds,
	const double StampNowSeconds,
	FEmbodiedFusionSourceObservations& OutObservations,
	FString* OutPhaseName) const
{
	FScopeLock Lock(&CriticalSection);
	if (!Status.bLoaded || Samples.Num() <= 0)
	{
		return false;
	}

	const int32 SampleIndex = FindSampleIndexForElapsed(
		FMath::Clamp(DatasetTimeSeconds, 0.0, Status.DurationSeconds));
	if (!Samples.IsValidIndex(SampleIndex))
	{
		return false;
	}

	OutObservations = Samples[SampleIndex].Observations;
	RetimestampObservations(OutObservations, StampNowSeconds);
	if (OutPhaseName)
	{
		*OutPhaseName = Samples[SampleIndex].PhaseName;
	}
	return true;
}

FMediaPipeTrackingFusionReplayStatus FMediaPipeTrackingFusionDatasetReplayRuntime::GetStatus() const
{
	FScopeLock Lock(&CriticalSection);
	return Status;
}

bool FMediaPipeTrackingFusionDatasetReplayRuntime::LoadManifestSampleFiles(
	const FString& ManifestPath,
	TArray<FString>& OutSampleFiles,
	FString& OutLabel,
	FString& OutError)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *ManifestPath))
	{
		OutError = FString::Printf(TEXT("failed to read manifest: %s"), *ManifestPath);
		return false;
	}

	TSharedPtr<FJsonObject> ManifestObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, ManifestObject) || !ManifestObject.IsValid())
	{
		OutError = FString::Printf(TEXT("failed to parse manifest JSON: %s"), *ManifestPath);
		return false;
	}

	ManifestObject->TryGetStringField(TEXT("label"), OutLabel);
	const FString ManifestDir = FPaths::GetPath(ManifestPath);
	const TArray<TSharedPtr<FJsonValue>>* SampleFiles = nullptr;
	if (!ManifestObject->TryGetArrayField(TEXT("sample_files"), SampleFiles) || !SampleFiles || SampleFiles->Num() <= 0)
	{
		OutError = TEXT("manifest has no sample_files");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : *SampleFiles)
	{
		const TSharedPtr<FJsonObject> FileObject = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!FileObject.IsValid())
		{
			continue;
		}

		FString RelativePath;
		if (!FileObject->TryGetStringField(TEXT("relative_path"), RelativePath) || RelativePath.IsEmpty())
		{
			continue;
		}

		const FString SamplePath = FPaths::ConvertRelativePathToFull(
			FPaths::IsRelative(RelativePath)
				? FPaths::Combine(ManifestDir, RelativePath)
				: RelativePath);
		if (FPaths::FileExists(SamplePath))
		{
			OutSampleFiles.Add(SamplePath);
		}
	}

	if (OutSampleFiles.Num() <= 0)
	{
		OutError = TEXT("manifest sample_files did not resolve to existing files");
		return false;
	}
	return true;
}

bool FMediaPipeTrackingFusionDatasetReplayRuntime::LoadJsonlSamples(
	const FString& SampleFilePath,
	TArray<FReplaySample>& OutSamples,
	FString& OutError)
{
	TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*SampleFilePath));
	if (!Reader.IsValid())
	{
		OutError = FString::Printf(TEXT("failed to read sample JSONL: %s"), *SampleFilePath);
		return false;
	}

	const int64 FileSize = Reader->TotalSize();
	constexpr int32 ChunkSize = 256 * 1024;
	TArray<uint8> Chunk;
	Chunk.SetNumUninitialized(ChunkSize);
	TArray<uint8> LineBytes;
	LineBytes.Reserve(64 * 1024);

	int32 ParsedCount = 0;
	auto ParseLineBytes = [&ParsedCount, &OutSamples](const TArray<uint8>& Bytes)
	{
		if (Bytes.Num() <= 0)
		{
			return;
		}

		const FUTF8ToTCHAR ConvertedLine(
			reinterpret_cast<const ANSICHAR*>(Bytes.GetData()),
			Bytes.Num());
		FString TrimmedLine(ConvertedLine.Length(), ConvertedLine.Get());
		TrimmedLine = TrimmedLine.TrimStartAndEnd();
		if (TrimmedLine.IsEmpty())
		{
			return;
		}

		TSharedPtr<FJsonObject> SampleObject = BuildCompactReplaySampleObjectFromLine(TrimmedLine);
		if (!SampleObject.IsValid())
		{
			const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(TrimmedLine);
			FJsonSerializer::Deserialize(JsonReader, SampleObject);
		}

		FReplaySample Sample;
		if (SampleObject.IsValid() && ParseSampleObject(SampleObject, Sample))
		{
			OutSamples.Add(MoveTemp(Sample));
			++ParsedCount;
		}
	};

	while (Reader->Tell() < FileSize)
	{
		const int64 RemainingBytes = FileSize - Reader->Tell();
		const int32 BytesToRead = static_cast<int32>(FMath::Min<int64>(ChunkSize, RemainingBytes));
		if (BytesToRead <= 0)
		{
			break;
		}

		Reader->Serialize(Chunk.GetData(), BytesToRead);
		int32 SegmentStart = 0;
		for (int32 Index = 0; Index < BytesToRead; ++Index)
		{
			const uint8 Byte = Chunk[Index];
			if (Byte == static_cast<uint8>('\n'))
			{
				const int32 SegmentLength = Index - SegmentStart;
				if (SegmentLength > 0)
				{
					LineBytes.Append(Chunk.GetData() + SegmentStart, SegmentLength);
				}
				ParseLineBytes(LineBytes);
				LineBytes.Reset();
				SegmentStart = Index + 1;
			}
			else if (Byte == static_cast<uint8>('\r'))
			{
				const int32 SegmentLength = Index - SegmentStart;
				if (SegmentLength > 0)
				{
					LineBytes.Append(Chunk.GetData() + SegmentStart, SegmentLength);
				}
				SegmentStart = Index + 1;
			}
		}

		if (SegmentStart < BytesToRead)
		{
			LineBytes.Append(Chunk.GetData() + SegmentStart, BytesToRead - SegmentStart);
		}
	}

	ParseLineBytes(LineBytes);

	if (ParsedCount <= 0)
	{
		OutError = FString::Printf(TEXT("no replayable samples in %s"), *SampleFilePath);
		return false;
	}
	return true;
}

bool FMediaPipeTrackingFusionDatasetReplayRuntime::ParseSampleObject(
	const TSharedPtr<FJsonObject>& SampleObject,
	FReplaySample& OutSample)
{
	if (!SampleObject.IsValid())
	{
		return false;
	}

	OutSample = FReplaySample();
	OutSample.TimeSeconds = ReadNumber(SampleObject, TEXT("t"), 0.0);
	OutSample.Observations.NowSeconds = OutSample.TimeSeconds;

	TSharedPtr<FJsonObject> PhaseObject;
	if (TryGetObjectField(SampleObject, TEXT("phase"), PhaseObject))
	{
		PhaseObject->TryGetStringField(TEXT("phase_name"), OutSample.PhaseName);
	}

	TSharedPtr<FJsonObject> SourceObject;
	if (!TryGetObjectPath(SampleObject, { TEXT("fusion"), TEXT("source") }, SourceObject))
	{
		return false;
	}

	bool bHasAnySource = false;
	TSharedPtr<FJsonObject> HmdObject;
	if (TryGetObjectField(SourceObject, TEXT("hmd"), HmdObject) &&
		TryReadBool(HmdObject, TEXT("has_pose")))
	{
		OutSample.Observations.HmdPose.bHasPose = true;
		TryReadVectorArray(HmdObject, TEXT("loc"), OutSample.Observations.HmdPose.LocationWorld);
		TryReadQuatArray(HmdObject, TEXT("quat"), OutSample.Observations.HmdPose.RotationWorld);
		TryReadVectorArray(HmdObject, TEXT("tracking_up"), OutSample.Observations.HmdPose.TrackingUpWorld);
		OutSample.Observations.HmdPose.TimestampSeconds = OutSample.TimeSeconds;
		bHasAnySource = true;
	}

	// Schema v2 caches additionally carry the full 26-keypoint hand skeleton per side
	// (positions + [x,y,z,w] quats) so dataset replay can drive wrist rotation and fingers.
	auto TryReadHandKeypoints = [](
		const TSharedPtr<FJsonObject>& HandObject,
		TStaticArray<FVector, MediaPipeTrackingHandKeypointCount>& OutPositions,
		TStaticArray<FQuat, MediaPipeTrackingHandKeypointCount>& OutRotations) -> bool
	{
		const TArray<TSharedPtr<FJsonValue>>* PositionValues = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* RotationValues = nullptr;
		if (!HandObject->TryGetArrayField(TEXT("keypoints_world"), PositionValues) ||
			!HandObject->TryGetArrayField(TEXT("keypoint_quats"), RotationValues) ||
			!PositionValues || !RotationValues ||
			PositionValues->Num() != MediaPipeTrackingHandKeypointCount ||
			RotationValues->Num() != MediaPipeTrackingHandKeypointCount)
		{
			return false;
		}

		for (int32 Index = 0; Index < MediaPipeTrackingHandKeypointCount; ++Index)
		{
			const TArray<TSharedPtr<FJsonValue>>* Position = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Rotation = nullptr;
			if (!(*PositionValues)[Index].IsValid() || !(*PositionValues)[Index]->TryGetArray(Position) ||
				!Position || Position->Num() < 3 ||
				!(*RotationValues)[Index].IsValid() || !(*RotationValues)[Index]->TryGetArray(Rotation) ||
				!Rotation || Rotation->Num() < 4)
			{
				return false;
			}
			OutPositions[Index] = FVector(
				(*Position)[0]->AsNumber(), (*Position)[1]->AsNumber(), (*Position)[2]->AsNumber());
			OutRotations[Index] = FQuat(
				(*Rotation)[0]->AsNumber(), (*Rotation)[1]->AsNumber(),
				(*Rotation)[2]->AsNumber(), (*Rotation)[3]->AsNumber()).GetNormalized();
		}
		return true;
	};

	TSharedPtr<FJsonObject> LeftHandObject;
	if (TryGetObjectField(SourceObject, TEXT("left_hand"), LeftHandObject) &&
		TryReadBool(LeftHandObject, TEXT("has_hand")))
	{
		FVector WristWorld = FVector::ZeroVector;
		if (TryReadVectorArray(LeftHandObject, TEXT("wrist_world"), WristWorld))
		{
			OutSample.Observations.Hands.ProviderCount = FMath::Max(OutSample.Observations.Hands.ProviderCount, 1);
			OutSample.Observations.Hands.ValidProviderCount = FMath::Max(OutSample.Observations.Hands.ValidProviderCount, 1);
			OutSample.Observations.Hands.bHasLeft = 1;
			OutSample.Observations.Hands.bLeftTracked = 1;
			OutSample.Observations.Hands.LeftTimestampSeconds = OutSample.TimeSeconds;
			OutSample.Observations.Hands.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::Wrist)] = WristWorld;
			if (TryReadHandKeypoints(
				LeftHandObject,
				OutSample.Observations.Hands.LeftPositionsWorld,
				OutSample.Observations.Hands.LeftRotationsWorld))
			{
				bool bKeypointsTracked = true;
				LeftHandObject->TryGetBoolField(TEXT("keypoints_tracked"), bKeypointsTracked);
				OutSample.Observations.Hands.bLeftHasFullKeypoints = bKeypointsTracked ? 1 : 0;
			}
			bHasAnySource = true;
		}
	}

	TSharedPtr<FJsonObject> RightHandObject;
	if (TryGetObjectField(SourceObject, TEXT("right_hand"), RightHandObject) &&
		TryReadBool(RightHandObject, TEXT("has_hand")))
	{
		FVector WristWorld = FVector::ZeroVector;
		if (TryReadVectorArray(RightHandObject, TEXT("wrist_world"), WristWorld))
		{
			OutSample.Observations.Hands.ProviderCount = FMath::Max(OutSample.Observations.Hands.ProviderCount, 1);
			OutSample.Observations.Hands.ValidProviderCount = FMath::Max(OutSample.Observations.Hands.ValidProviderCount, 1);
			OutSample.Observations.Hands.bHasRight = 1;
			OutSample.Observations.Hands.bRightTracked = 1;
			OutSample.Observations.Hands.RightTimestampSeconds = OutSample.TimeSeconds;
			OutSample.Observations.Hands.RightPositionsWorld[static_cast<int32>(EHandKeypoint::Wrist)] = WristWorld;
			if (TryReadHandKeypoints(
				RightHandObject,
				OutSample.Observations.Hands.RightPositionsWorld,
				OutSample.Observations.Hands.RightRotationsWorld))
			{
				bool bKeypointsTracked = true;
				RightHandObject->TryGetBoolField(TEXT("keypoints_tracked"), bKeypointsTracked);
				OutSample.Observations.Hands.bRightHasFullKeypoints = bKeypointsTracked ? 1 : 0;
			}
			bHasAnySource = true;
		}
	}

	auto ParseArmChainSide = [&SourceObject, &OutSample, &bHasAnySource](
		const TCHAR* FieldName,
		FMediaPipeTrackingArmChainSideSnapshot& Side)
	{
		TSharedPtr<FJsonObject> ArmObject;
		if (!TryGetObjectField(SourceObject, FieldName, ArmObject) ||
			!TryReadBool(ArmObject, TEXT("has_chain")))
		{
			return;
		}

		FVector Shoulder = FVector::ZeroVector;
		FVector Elbow = FVector::ZeroVector;
		FVector Wrist = FVector::ZeroVector;
		if (TryReadVectorArray(ArmObject, TEXT("shoulder_world"), Shoulder) &&
			TryReadVectorArray(ArmObject, TEXT("elbow_world"), Elbow) &&
			TryReadVectorArray(ArmObject, TEXT("wrist_world"), Wrist))
		{
			Side.bHasChain = true;
			Side.ShoulderWorld = Shoulder;
			Side.ElbowWorld = Elbow;
			Side.WristWorld = Wrist;
			Side.TimestampSeconds = OutSample.TimeSeconds;
			Side.Confidence = static_cast<float>(ReadNumber(ArmObject, TEXT("confidence"), 1.0));
			bHasAnySource = true;
		}
	};
	ParseArmChainSide(TEXT("left_arm_chain"), OutSample.Observations.ArmChain.Left);
	ParseArmChainSide(TEXT("right_arm_chain"), OutSample.Observations.ArmChain.Right);

	// Schema v3 (2026-07-04): recorded Quest body-tracking hips pose - the full-turn
	// body-yaw source. Missing from older caches (treated as absent).
	TSharedPtr<FJsonObject> BodyHipsObject;
	if (TryGetObjectField(SourceObject, TEXT("body_hips"), BodyHipsObject) &&
		TryReadBool(BodyHipsObject, TEXT("has_hips")))
	{
		FVector HipsLoc = FVector::ZeroVector;
		if (TryReadVectorArray(BodyHipsObject, TEXT("loc"), HipsLoc))
		{
			OutSample.Observations.ArmChain.bHasHips = true;
			OutSample.Observations.ArmChain.HipsLocationWorld = HipsLoc;
			const TArray<TSharedPtr<FJsonValue>>* QuatArray = nullptr;
			if (BodyHipsObject->TryGetArrayField(TEXT("quat"), QuatArray) &&
				QuatArray && QuatArray->Num() == 4)
			{
				OutSample.Observations.ArmChain.HipsRotationWorld = FQuat(
					(*QuatArray)[0]->AsNumber(),
					(*QuatArray)[1]->AsNumber(),
					(*QuatArray)[2]->AsNumber(),
					(*QuatArray)[3]->AsNumber()).GetNormalized();
			}
			bool bOrientValid = false;
			BodyHipsObject->TryGetBoolField(TEXT("orientation_valid"), bOrientValid);
			OutSample.Observations.ArmChain.bHipsOrientationValid = bOrientValid ? 1 : 0;
			OutSample.Observations.ArmChain.HipsTimestampSeconds = OutSample.TimeSeconds;
			OutSample.Observations.ArmChain.HipsConfidence =
				static_cast<float>(ReadNumber(BodyHipsObject, TEXT("confidence"), 1.0));
			bHasAnySource = true;
		}
	}

	TSharedPtr<FJsonObject> BodyPoseObject;
	if (TryGetObjectField(SourceObject, TEXT("body_pose"), BodyPoseObject) &&
		TryReadBool(BodyPoseObject, TEXT("has_body_pose")))
	{
		OutSample.Observations.BodyPose.TimestampSeconds = OutSample.TimeSeconds;
		TSharedPtr<FJsonObject> LandmarksObject;
		if (TryGetObjectField(BodyPoseObject, TEXT("landmarks"), LandmarksObject))
		{
			for (const TPair<FString, EMediaPipePoseLandmark>& Entry : LandmarkNameMap())
			{
				TSharedPtr<FJsonObject> LandmarkObject;
				if (!TryGetObjectField(LandmarksObject, *Entry.Key, LandmarkObject) ||
					!TryReadBool(LandmarkObject, TEXT("valid")))
				{
					continue;
				}

				FVector Position = FVector::ZeroVector;
				if (TryReadVectorArray(LandmarkObject, TEXT("pos"), Position))
				{
					const float Reliability = static_cast<float>(ReadNumber(LandmarkObject, TEXT("reliability"), 1.0));
					OutSample.Observations.BodyPose.SetLandmark(Entry.Value, Position, Reliability);
					bHasAnySource = true;
				}
			}
		}
	}

	return bHasAnySource;
}

void FMediaPipeTrackingFusionDatasetReplayRuntime::RetimestampObservations(
	FEmbodiedFusionSourceObservations& Observations,
	const double NowSeconds)
{
	Observations.NowSeconds = NowSeconds;
	if (Observations.HmdPose.bHasPose)
	{
		Observations.HmdPose.TimestampSeconds = NowSeconds;
	}
	if (Observations.Hands.bHasLeft != 0)
	{
		Observations.Hands.LeftTimestampSeconds = NowSeconds;
	}
	if (Observations.Hands.bHasRight != 0)
	{
		Observations.Hands.RightTimestampSeconds = NowSeconds;
	}
	if (Observations.ArmChain.Left.bHasChain)
	{
		Observations.ArmChain.Left.TimestampSeconds = NowSeconds;
	}
	if (Observations.ArmChain.Right.bHasChain)
	{
		Observations.ArmChain.Right.TimestampSeconds = NowSeconds;
	}
	Observations.BodyPose.TimestampSeconds = NowSeconds;
}

int32 FMediaPipeTrackingFusionDatasetReplayRuntime::FindSampleIndexForElapsed(const double ElapsedSeconds) const
{
	if (Samples.Num() <= 0)
	{
		return INDEX_NONE;
	}

	int32 Low = 0;
	int32 High = Samples.Num() - 1;
	int32 Best = 0;
	while (Low <= High)
	{
		const int32 Mid = Low + (High - Low) / 2;
		if (Samples[Mid].TimeSeconds <= ElapsedSeconds)
		{
			Best = Mid;
			Low = Mid + 1;
		}
		else
		{
			High = Mid - 1;
		}
	}
	return Best;
}
