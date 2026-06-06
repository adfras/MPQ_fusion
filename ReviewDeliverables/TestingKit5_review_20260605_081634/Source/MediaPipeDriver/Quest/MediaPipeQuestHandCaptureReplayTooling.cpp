#include "MediaPipeQuestHandCaptureReplayTooling.h"

#include "MediaPipePoseLog.h"
#include "MediaPipeQuestHandDebugReporter.h"

#include "DrawDebugHelpers.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"

namespace
{
	FQuestHandTrackingSnapshot GQuestHandReplaySnapshot;
	bool bQuestHandReplaySnapshotLoaded = false;
	FString GQuestHandReplayPath;

	struct FQuestHandCaptureGuideState
	{
		bool bActive = false;
		double StartRealSeconds = 0.0;
		double LastNoHandsLogRealSeconds = -1000.0;
		FString RunId;
		TSet<FString> CapturedKeys;
	};

	FQuestHandCaptureGuideState GQuestHandCaptureGuide;
	uint64 GQuestHandCaptureGuideLastFrame = 0;
}

bool FMediaPipeQuestHandCaptureReplayTooling::LoadReplayFile(const FString& RawPathOrName)
{
	const FString ResolvedPath = FMediaPipeQuestHandDebugReporter::ResolveReplayPath(RawPathOrName);
	if (ResolvedPath.IsEmpty())
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.QuestHandReplayFile: missing replay name or path."));
		return false;
	}

	FQuestHandTrackingSnapshot LoadedSnapshot;
	if (!FMediaPipeQuestHandDebugReporter::LoadSnapshotFromFile(ResolvedPath, LoadedSnapshot))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.QuestHandReplayFile: failed to load '%s'."), *ResolvedPath);
		return false;
	}

	GQuestHandReplaySnapshot = LoadedSnapshot;
	GQuestHandReplayPath = ResolvedPath;
	bQuestHandReplaySnapshotLoaded = true;
	FMediaPipeQuestHandDebugReporter::EmitReplayLoadedLog(ResolvedPath, GQuestHandReplaySnapshot);
	return true;
}

bool FMediaPipeQuestHandCaptureReplayTooling::TryApplyReplaySnapshot(
	const bool bReplayEnabled,
	FQuestHandTrackingSnapshot& InOutSnapshot,
	FString* OutReplayPath)
{
	if (!bReplayEnabled || !bQuestHandReplaySnapshotLoaded)
	{
		return false;
	}

	InOutSnapshot = GQuestHandReplaySnapshot;
	if (OutReplayPath)
	{
		*OutReplayPath = GQuestHandReplayPath;
	}
	return true;
}

const FString& FMediaPipeQuestHandCaptureReplayTooling::GetReplayPath()
{
	return GQuestHandReplayPath;
}

bool FMediaPipeQuestHandCaptureReplayTooling::IsReplayLoaded()
{
	return bQuestHandReplaySnapshotLoaded;
}

void FMediaPipeQuestHandCaptureReplayTooling::StartCaptureGuide(const FString& RawPrefix)
{
	const FString Prefix = FMediaPipeQuestHandDebugReporter::SanitizeReplayName(RawPrefix.IsEmpty() ? TEXT("fist") : RawPrefix);
	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	GQuestHandCaptureGuide.bActive = true;
	GQuestHandCaptureGuide.StartRealSeconds = FPlatformTime::Seconds();
	GQuestHandCaptureGuide.LastNoHandsLogRealSeconds = -1000.0;
	GQuestHandCaptureGuide.RunId = FMediaPipeQuestHandDebugReporter::SanitizeReplayName(FString::Printf(TEXT("%s_%s"), *Prefix, *Timestamp));
	GQuestHandCaptureGuide.CapturedKeys.Reset();

	FMediaPipeQuestHandDebugReporter::EmitCaptureGuideStartedLog(GQuestHandCaptureGuide.RunId);
}

void FMediaPipeQuestHandCaptureReplayTooling::StopCaptureGuide()
{
	if (GQuestHandCaptureGuide.bActive)
	{
		UE_LOG(LogMediaPipePose, Log, TEXT("mp.StopQuestHandCaptureGuide: stopped run '%s'."), *GQuestHandCaptureGuide.RunId);
	}
	GQuestHandCaptureGuide.bActive = false;
	GQuestHandCaptureGuide.CapturedKeys.Reset();
}

void FMediaPipeQuestHandCaptureReplayTooling::TickCaptureGuide(
	UWorld* World,
	const FQuestHandTrackingSnapshot& Snapshot,
	const FVector& ViewLocationWorld,
	const FQuat& ViewRotationWorld)
{
	if (!World || !GQuestHandCaptureGuide.bActive)
	{
		return;
	}

	if (GQuestHandCaptureGuideLastFrame == GFrameCounter)
	{
		return;
	}
	GQuestHandCaptureGuideLastFrame = GFrameCounter;

	const double NowRealSeconds = FPlatformTime::Seconds();
	const double ElapsedSeconds = FMath::Max(0.0, NowRealSeconds - GQuestHandCaptureGuide.StartRealSeconds);

	FString PoseName;
	FString DisplayName;
	double PhaseStartSeconds = 0.0;
	double PhaseEndSeconds = 0.0;
	bool bCapturePhase = false;
	FColor TextColor = FColor::White;
	if (!FMediaPipeQuestHandDebugReporter::GetCaptureGuidePhase(ElapsedSeconds, PoseName, DisplayName, PhaseStartSeconds, PhaseEndSeconds, bCapturePhase, TextColor))
	{
		UE_LOG(LogMediaPipePose, Log, TEXT("mp.StartQuestHandCaptureGuide: completed run '%s'."), *GQuestHandCaptureGuide.RunId);
		GQuestHandCaptureGuide.bActive = false;
		return;
	}

	const bool bAnyHandTracked = Snapshot.bLeftTracked != 0 || Snapshot.bRightTracked != 0;
	const bool bBothHandsTracked = Snapshot.bLeftTracked != 0 && Snapshot.bRightTracked != 0;
	if (bCapturePhase)
	{
		const double PoseElapsedSeconds = ElapsedSeconds - PhaseStartSeconds;
		constexpr double SampleOffsetsSeconds[] = { 1.5, 3.0, 4.5 };
		for (int32 SampleIndex = 0; SampleIndex < UE_ARRAY_COUNT(SampleOffsetsSeconds); ++SampleIndex)
		{
			if (PoseElapsedSeconds < SampleOffsetsSeconds[SampleIndex])
			{
				continue;
			}

			const FString CaptureKey = FString::Printf(TEXT("%s_%02d"), *PoseName, SampleIndex);
			if (GQuestHandCaptureGuide.CapturedKeys.Contains(CaptureKey))
			{
				continue;
			}

			if (!bAnyHandTracked)
			{
				if (NowRealSeconds - GQuestHandCaptureGuide.LastNoHandsLogRealSeconds >= 1.0)
				{
					GQuestHandCaptureGuide.LastNoHandsLogRealSeconds = NowRealSeconds;
					UE_LOG(LogMediaPipePose, Warning, TEXT("mp.StartQuestHandCaptureGuide: waiting to capture '%s' because no Quest hand is tracked."), *CaptureKey);
				}
				continue;
			}

			const FString CaptureName = FString::Printf(TEXT("%s_%s_%02d"), *PoseName, *GQuestHandCaptureGuide.RunId, SampleIndex);
			const FString OutputPath = BuildCaptureOutputPath(CaptureName);
			if (FMediaPipeQuestHandDebugReporter::SaveSnapshotToFile(Snapshot, OutputPath))
			{
				GQuestHandCaptureGuide.CapturedKeys.Add(CaptureKey);
				FMediaPipeQuestHandDebugReporter::EmitCaptureWriteLog(TEXT("mp.StartQuestHandCaptureGuide"), OutputPath, Snapshot);
			}
			else
			{
				GQuestHandCaptureGuide.CapturedKeys.Add(CaptureKey);
				UE_LOG(LogMediaPipePose, Error, TEXT("mp.StartQuestHandCaptureGuide: failed to write '%s'."), *OutputPath);
			}
		}
	}

	const double RemainingSeconds = FMath::Max(0.0, PhaseEndSeconds - ElapsedSeconds);
	const FString GuideText = FMediaPipeQuestHandDebugReporter::BuildCaptureGuideText(
		DisplayName,
		RemainingSeconds,
		bAnyHandTracked,
		bBothHandsTracked);

	const FVector TextLocationWorld =
		ViewLocationWorld +
		ViewRotationWorld.GetForwardVector() * 185.0 +
		ViewRotationWorld.GetUpVector() * -18.0;
	DrawDebugString(World, TextLocationWorld, GuideText, nullptr, bAnyHandTracked ? TextColor : FColor::Red, 0.05f, true, 2.6f);
}

FString FMediaPipeQuestHandCaptureReplayTooling::BuildCaptureOutputPath(const FString& CaptureName)
{
	const FString FileName = FMediaPipeQuestHandDebugReporter::SanitizeReplayName(CaptureName) + TEXT(".json");
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FMediaPipeQuestHandDebugReporter::GetReplayDirectory(), FileName));
}

bool FMediaPipeQuestHandCaptureReplayTooling::CapturePose(
	const FString& CaptureName,
	const FQuestHandTrackingSnapshot& Snapshot,
	const bool bReadAny)
{
	const FString OutputPath = BuildCaptureOutputPath(CaptureName);
	if (!bReadAny)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.CaptureQuestHandPose: no live Quest/OpenXR hand data was available; writing snapshot anyway for diagnosis."));
	}

	if (!FMediaPipeQuestHandDebugReporter::SaveSnapshotToFile(Snapshot, OutputPath))
	{
		UE_LOG(LogMediaPipePose, Error, TEXT("mp.CaptureQuestHandPose: failed to write '%s'."), *OutputPath);
		return false;
	}

	FMediaPipeQuestHandDebugReporter::EmitCaptureWriteLog(TEXT("mp.CaptureQuestHandPose"), OutputPath, Snapshot);
	return true;
}
