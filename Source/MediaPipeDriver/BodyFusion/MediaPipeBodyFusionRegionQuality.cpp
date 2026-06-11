#include "MediaPipeBodyFusionRegionQuality.h"

#include "EmbodiedFusionComponent.h"
#include "HAL/IConsoleManager.h"
#include "MediaPipePoseLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
TAutoConsoleVariable<int32> CVarBodyFusionRegionQualityLog(
	TEXT("mp.BodyFusion.RegionQualityLog"),
	0,
	TEXT("When non-zero, emit throttled per-region BodyFusion quality/ownership rows (owner, state, confidence, amplitude, dropouts, depth reliability, may-influence policy)."));

TAutoConsoleVariable<float> CVarBodyFusionRegionQualityLogInterval(
	TEXT("mp.BodyFusion.RegionQualityLogInterval"),
	1.0f,
	TEXT("Seconds between mp.BodyFusion.RegionQuality log emissions. Diagnostic cadence only; does not affect the solve."));

TAutoConsoleVariable<int32> CVarBodyFusionRegionQualityCapture(
	TEXT("mp.BodyFusion.RegionQualityCapture"),
	0,
	TEXT("When non-zero, append per-region BodyFusion quality rows as JSONL under Saved/CodexAgent/Diagnostics for offline ownership/confidence timeline plots."));

TAutoConsoleVariable<float> CVarBodyFusionRegionQualityCaptureInterval(
	TEXT("mp.BodyFusion.RegionQualityCaptureInterval"),
	0.10f,
	TEXT("Seconds between JSONL region-quality capture rows. Diagnostic cadence only."));

TAutoConsoleVariable<float> CVarBodyFusionRegionQualityWindowSeconds(
	TEXT("mp.BodyFusion.RegionQualityWindowSeconds"),
	2.0f,
	TEXT("Rolling evidence window, in seconds, for region amplitude/dropout/freshness/depth-variance statistics."));

TAutoConsoleVariable<float> CVarBodyFusionRegionQualityDepthVarRatioThreshold(
	TEXT("mp.BodyFusion.RegionQualityDepthVarRatioThreshold"),
	4.0f,
	TEXT("Forward-vs-lateral variance ratio above which a MediaPipe-owned region is flagged depth-weak. Diagnostic threshold only; configurable, not a hard-coded success constant."));

TAutoConsoleVariable<float> CVarBodyFusionRegionQualityDepthMinAmplitudeCm(
	TEXT("mp.BodyFusion.RegionQualityDepthMinAmplitudeCm"),
	1.5f,
	TEXT("Minimum window amplitude, in centimeters, before the depth-weak flag is evaluated; below this the motion is too small to classify."));

const TCHAR* SourceStateName(const EMediaPipeBodyFusionSourceState State)
{
	switch (State)
	{
	case EMediaPipeBodyFusionSourceState::Missing: return TEXT("Missing");
	case EMediaPipeBodyFusionSourceState::Stale: return TEXT("Stale");
	case EMediaPipeBodyFusionSourceState::Invalid: return TEXT("Invalid");
	case EMediaPipeBodyFusionSourceState::Fresh: return TEXT("Fresh");
	default: return TEXT("Unknown");
	}
}

// Aggregates up to two fused points (left/right side) into one region observation.
struct FRegionPointAccumulator
{
	FVector PositionSum = FVector::ZeroVector;
	int32 ValidCount = 0;
	float ConfidenceSum = 0.0f;
	EMediaPipeBodyFusionOwner Owner = EMediaPipeBodyFusionOwner::None;
	EMediaPipeBodyFusionSourceState State = EMediaPipeBodyFusionSourceState::Missing;

	void Add(const FMediaPipeFusedBodyPoint& Point)
	{
		if (!Point.bValid)
		{
			return;
		}
		PositionSum += Point.LocationWorld;
		ConfidenceSum += Point.Confidence;
		++ValidCount;
		// Prefer reporting the strongest concrete owner; sides should agree in practice.
		if (Owner == EMediaPipeBodyFusionOwner::None)
		{
			Owner = Point.Owner;
		}
		if (State == EMediaPipeBodyFusionSourceState::Missing)
		{
			State = Point.SourceState;
		}
	}
};
}

void FMediaPipeBodyFusionRegionQualityTracker::Reset()
{
	for (int32 Index = 0; Index < MediaPipeBodyFusionQualityRegionCount; ++Index)
	{
		Histories[Index].Samples.Reset();
		Stats[Index] = FMediaPipeBodyFusionRegionQualityStats();
	}
	LastLogTimeSeconds = -1.0;
	LastCaptureTimeSeconds = -1.0;
}

const TCHAR* FMediaPipeBodyFusionRegionQualityTracker::RegionName(const EMediaPipeBodyFusionQualityRegion Region)
{
	switch (Region)
	{
	case EMediaPipeBodyFusionQualityRegion::Head: return TEXT("head");
	case EMediaPipeBodyFusionQualityRegion::Hands: return TEXT("hands");
	case EMediaPipeBodyFusionQualityRegion::Arms: return TEXT("arms");
	case EMediaPipeBodyFusionQualityRegion::Shoulders: return TEXT("shoulders");
	case EMediaPipeBodyFusionQualityRegion::ChestSpine: return TEXT("chest_spine");
	case EMediaPipeBodyFusionQualityRegion::PelvisHips: return TEXT("pelvis_hips");
	case EMediaPipeBodyFusionQualityRegion::Legs: return TEXT("legs");
	case EMediaPipeBodyFusionQualityRegion::Feet: return TEXT("feet");
	default: return TEXT("unknown");
	}
}

const TCHAR* FMediaPipeBodyFusionRegionQualityTracker::OwnerName(const EMediaPipeBodyFusionOwner Owner)
{
	switch (Owner)
	{
	case EMediaPipeBodyFusionOwner::None: return TEXT("None");
	case EMediaPipeBodyFusionOwner::Hmd: return TEXT("Hmd");
	case EMediaPipeBodyFusionOwner::Quest: return TEXT("Quest");
	case EMediaPipeBodyFusionOwner::MediaPipe: return TEXT("MediaPipe");
	case EMediaPipeBodyFusionOwner::AvatarProfile: return TEXT("AvatarProfile");
	case EMediaPipeBodyFusionOwner::Fused: return TEXT("Fused");
	default: return TEXT("Unknown");
	}
}

const FMediaPipeBodyFusionRegionQualityStats& FMediaPipeBodyFusionRegionQualityTracker::GetStats(
	const EMediaPipeBodyFusionQualityRegion Region) const
{
	const int32 Index = FMath::Clamp(static_cast<int32>(Region), 0, MediaPipeBodyFusionQualityRegionCount - 1);
	return Stats[Index];
}

void FMediaPipeBodyFusionRegionQualityTracker::BuildRegionSample(
	const FMediaPipeBodyFusionRegionQualityUpdateInput& Input,
	const EMediaPipeBodyFusionQualityRegion Region,
	FRegionHistorySample& OutSample,
	EMediaPipeBodyFusionOwner& OutOwner,
	EMediaPipeBodyFusionSourceState& OutState) const
{
	const FMediaPipeFusedAvatarPose& Pose = Input.Frame->Pose;
	FRegionPointAccumulator Accumulator;
	switch (Region)
	{
	case EMediaPipeBodyFusionQualityRegion::Head:
		Accumulator.Add(Pose.Head);
		break;
	case EMediaPipeBodyFusionQualityRegion::Hands:
		Accumulator.Add(Pose.LeftWrist);
		Accumulator.Add(Pose.RightWrist);
		break;
	case EMediaPipeBodyFusionQualityRegion::Arms:
		Accumulator.Add(Pose.LeftElbow);
		Accumulator.Add(Pose.RightElbow);
		break;
	case EMediaPipeBodyFusionQualityRegion::Shoulders:
		Accumulator.Add(Pose.LeftShoulder);
		Accumulator.Add(Pose.RightShoulder);
		break;
	case EMediaPipeBodyFusionQualityRegion::ChestSpine:
		Accumulator.Add(Pose.Chest);
		Accumulator.Add(Pose.Spine);
		break;
	case EMediaPipeBodyFusionQualityRegion::PelvisHips:
		Accumulator.Add(Pose.Pelvis);
		Accumulator.Add(Pose.LeftHip);
		Accumulator.Add(Pose.RightHip);
		break;
	case EMediaPipeBodyFusionQualityRegion::Legs:
		Accumulator.Add(Pose.LeftKnee);
		Accumulator.Add(Pose.RightKnee);
		break;
	case EMediaPipeBodyFusionQualityRegion::Feet:
		Accumulator.Add(Pose.LeftFoot);
		Accumulator.Add(Pose.RightFoot);
		Accumulator.Add(Pose.LeftHeel);
		Accumulator.Add(Pose.RightHeel);
		break;
	default:
		break;
	}

	OutSample.TimeSeconds = Input.NowSeconds;
	OutSample.bValid = Accumulator.ValidCount > 0;
	OutSample.bFresh = Accumulator.State == EMediaPipeBodyFusionSourceState::Fresh;
	OutSample.Confidence = Accumulator.ValidCount > 0
		? Accumulator.ConfidenceSum / static_cast<float>(Accumulator.ValidCount)
		: 0.0f;
	OutSample.PositionWorld = Accumulator.ValidCount > 0
		? Accumulator.PositionSum / static_cast<float>(Accumulator.ValidCount)
		: FVector::ZeroVector;
	OutOwner = Accumulator.Owner;
	OutState = Accumulator.State;
}

void FMediaPipeBodyFusionRegionQualityTracker::RecomputeWindowStats(
	const FMediaPipeBodyFusionRegionQualityUpdateInput& Input,
	const EMediaPipeBodyFusionQualityRegion Region)
{
	const int32 Index = static_cast<int32>(Region);
	FRegionHistory& History = Histories[Index];
	FMediaPipeBodyFusionRegionQualityStats& RegionStats = Stats[Index];

	const double WindowSeconds = FMath::Max(0.25f, CVarBodyFusionRegionQualityWindowSeconds.GetValueOnGameThread());
	const double WindowStart = Input.NowSeconds - WindowSeconds;
	int32 FirstKept = 0;
	while (FirstKept < History.Samples.Num() && History.Samples[FirstKept].TimeSeconds < WindowStart)
	{
		++FirstKept;
	}
	if (FirstKept > 0)
	{
		History.Samples.RemoveAt(0, FirstKept, EAllowShrinking::No);
	}

	RegionStats.WindowSampleCount = History.Samples.Num();
	RegionStats.AmplitudeCm = 0.0f;
	RegionStats.MeanSpeedCmPerSecond = 0.0f;
	RegionStats.DropoutCount = 0;
	RegionStats.FreshRatio = 0.0f;
	RegionStats.DepthVarianceRatio = 0.0f;
	RegionStats.bDepthWeak = false;
	if (History.Samples.Num() <= 0)
	{
		return;
	}

	const FVector ForwardAxis = Input.AvatarForwardWorld.GetSafeNormal();
	FVector LateralAxis = FVector::CrossProduct(Input.AvatarUpWorld.GetSafeNormal(), ForwardAxis).GetSafeNormal();
	if (LateralAxis.IsNearlyZero())
	{
		LateralAxis = FVector::RightVector;
	}

	FVector MinPos(TNumericLimits<float>::Max());
	FVector MaxPos(-TNumericLimits<float>::Max());
	int32 ValidCount = 0;
	int32 FreshCount = 0;
	double SpeedSum = 0.0;
	int32 SpeedSamples = 0;
	double ForwardSum = 0.0, ForwardSqSum = 0.0;
	double LateralSum = 0.0, LateralSqSum = 0.0;
	const FRegionHistorySample* PrevValid = nullptr;
	bool bPrevValidFlag = History.Samples[0].bValid;
	for (const FRegionHistorySample& Sample : History.Samples)
	{
		if (Sample.bFresh)
		{
			++FreshCount;
		}
		if (bPrevValidFlag && !Sample.bValid)
		{
			++RegionStats.DropoutCount;
		}
		bPrevValidFlag = Sample.bValid;
		if (!Sample.bValid)
		{
			continue;
		}

		++ValidCount;
		MinPos = MinPos.ComponentMin(Sample.PositionWorld);
		MaxPos = MaxPos.ComponentMax(Sample.PositionWorld);
		const double ForwardCm = FVector::DotProduct(Sample.PositionWorld, ForwardAxis);
		const double LateralCm = FVector::DotProduct(Sample.PositionWorld, LateralAxis);
		ForwardSum += ForwardCm;
		ForwardSqSum += ForwardCm * ForwardCm;
		LateralSum += LateralCm;
		LateralSqSum += LateralCm * LateralCm;
		if (PrevValid)
		{
			const double Dt = FMath::Max(Sample.TimeSeconds - PrevValid->TimeSeconds, 1.0 / 240.0);
			SpeedSum += FVector::Distance(Sample.PositionWorld, PrevValid->PositionWorld) / Dt;
			++SpeedSamples;
		}
		PrevValid = &Sample;
	}

	RegionStats.FreshRatio = static_cast<float>(FreshCount) / static_cast<float>(History.Samples.Num());
	if (ValidCount > 0)
	{
		RegionStats.AmplitudeCm = static_cast<float>((MaxPos - MinPos).Size());
	}
	if (SpeedSamples > 0)
	{
		RegionStats.MeanSpeedCmPerSecond = static_cast<float>(SpeedSum / SpeedSamples);
	}
	if (ValidCount >= 8)
	{
		const double InvCount = 1.0 / static_cast<double>(ValidCount);
		const double ForwardVar = FMath::Max(0.0, ForwardSqSum * InvCount - FMath::Square(ForwardSum * InvCount));
		const double LateralVar = FMath::Max(0.0, LateralSqSum * InvCount - FMath::Square(LateralSum * InvCount));
		RegionStats.DepthVarianceRatio = LateralVar > 1.0e-4
			? static_cast<float>(ForwardVar / LateralVar)
			: (ForwardVar > 1.0e-4 ? CVarBodyFusionRegionQualityDepthVarRatioThreshold.GetValueOnGameThread() + 1.0f : 0.0f);
		const bool bMediaPipeEvidence =
			RegionStats.Owner == EMediaPipeBodyFusionOwner::MediaPipe ||
			RegionStats.Owner == EMediaPipeBodyFusionOwner::Fused;
		RegionStats.bDepthWeak =
			bMediaPipeEvidence &&
			RegionStats.AmplitudeCm >= CVarBodyFusionRegionQualityDepthMinAmplitudeCm.GetValueOnGameThread() &&
			RegionStats.DepthVarianceRatio >= CVarBodyFusionRegionQualityDepthVarRatioThreshold.GetValueOnGameThread();
	}
}

void FMediaPipeBodyFusionRegionQualityTracker::ResolveInfluencePolicy(
	const FMediaPipeBodyFusionRegionQualityUpdateInput& Input,
	const EMediaPipeBodyFusionQualityRegion Region,
	FMediaPipeBodyFusionRegionQualityStats& InOutStats) const
{
	const bool bLowerBodyRegion =
		Region == EMediaPipeBodyFusionQualityRegion::PelvisHips ||
		Region == EMediaPipeBodyFusionQualityRegion::Legs ||
		Region == EMediaPipeBodyFusionQualityRegion::Feet;

	if (Input.bAvatarLockedReplayActive && bLowerBodyRegion)
	{
		InOutStats.bMayInfluencePose = false;
		InOutStats.InfluenceReason = TEXT("avatar-locked replay: lower body is avatar-local (BodyFusion diagnostics only)");
		return;
	}

	if (!Input.bPoseWriteEnabled)
	{
		InOutStats.bMayInfluencePose = false;
		InOutStats.InfluenceReason = TEXT("pose write disabled");
		return;
	}

	if (!InOutStats.bValid)
	{
		InOutStats.bMayInfluencePose = false;
		InOutStats.InfluenceReason = TEXT("no valid fused point");
		return;
	}

	InOutStats.bMayInfluencePose = true;
	InOutStats.InfluenceReason = FString::Printf(TEXT("owner=%s"), OwnerName(InOutStats.Owner));
}

void FMediaPipeBodyFusionRegionQualityTracker::Update(const FMediaPipeBodyFusionRegionQualityUpdateInput& Input)
{
	if (!Input.Frame)
	{
		return;
	}

	for (int32 Index = 0; Index < MediaPipeBodyFusionQualityRegionCount; ++Index)
	{
		const EMediaPipeBodyFusionQualityRegion Region = static_cast<EMediaPipeBodyFusionQualityRegion>(Index);
		FRegionHistorySample Sample;
		EMediaPipeBodyFusionOwner Owner = EMediaPipeBodyFusionOwner::None;
		EMediaPipeBodyFusionSourceState State = EMediaPipeBodyFusionSourceState::Missing;
		BuildRegionSample(Input, Region, Sample, Owner, State);
		Histories[Index].Samples.Add(Sample);

		FMediaPipeBodyFusionRegionQualityStats& RegionStats = Stats[Index];
		RegionStats.Owner = Owner;
		RegionStats.SourceState = State;
		RegionStats.Confidence = Sample.Confidence;
		RegionStats.bValid = Sample.bValid;
		RecomputeWindowStats(Input, Region);
		ResolveInfluencePolicy(Input, Region, RegionStats);
	}
}

void FMediaPipeBodyFusionRegionQualityTracker::EmitDiagnostics(const FMediaPipeBodyFusionRegionQualityUpdateInput& Input)
{
	const bool bLogEnabled = CVarBodyFusionRegionQualityLog.GetValueOnGameThread() != 0;
	const bool bCaptureEnabled = CVarBodyFusionRegionQualityCapture.GetValueOnGameThread() != 0;
	if (!bLogEnabled && !bCaptureEnabled)
	{
		return;
	}

	if (bLogEnabled)
	{
		const double Interval = FMath::Max(0.1f, CVarBodyFusionRegionQualityLogInterval.GetValueOnGameThread());
		if (LastLogTimeSeconds < 0.0 || Input.NowSeconds - LastLogTimeSeconds >= Interval)
		{
			LastLogTimeSeconds = Input.NowSeconds;
			for (int32 Index = 0; Index < MediaPipeBodyFusionQualityRegionCount; ++Index)
			{
				const FMediaPipeBodyFusionRegionQualityStats& RegionStats = Stats[Index];
				UE_LOG(LogMediaPipePose, Log,
					TEXT("mp.BodyFusion.RegionQuality actor=%s region=%s owner=%s state=%s valid=%d conf=%.2f ampCm=%.1f speedCmS=%.1f dropouts=%d freshRatio=%.2f depthVarRatio=%.2f depthWeak=%d mayInfluence=%d reason=\"%s\""),
					*Input.TargetActorName.ToString(),
					RegionName(static_cast<EMediaPipeBodyFusionQualityRegion>(Index)),
					OwnerName(RegionStats.Owner),
					SourceStateName(RegionStats.SourceState),
					RegionStats.bValid ? 1 : 0,
					RegionStats.Confidence,
					RegionStats.AmplitudeCm,
					RegionStats.MeanSpeedCmPerSecond,
					RegionStats.DropoutCount,
					RegionStats.FreshRatio,
					RegionStats.DepthVarianceRatio,
					RegionStats.bDepthWeak ? 1 : 0,
					RegionStats.bMayInfluencePose ? 1 : 0,
					*RegionStats.InfluenceReason);
			}
		}
	}

	if (bCaptureEnabled)
	{
		const double Interval = FMath::Max(0.02f, CVarBodyFusionRegionQualityCaptureInterval.GetValueOnGameThread());
		if (LastCaptureTimeSeconds < 0.0 || Input.NowSeconds - LastCaptureTimeSeconds >= Interval)
		{
			LastCaptureTimeSeconds = Input.NowSeconds;
			if (CapturePath.IsEmpty())
			{
				CapturePath = FPaths::Combine(
					FPaths::ProjectSavedDir(),
					TEXT("CodexAgent/Diagnostics"),
					FString::Printf(
						TEXT("bodyfusion_region_quality_%s_%s.jsonl"),
						*Input.TargetActorName.ToString(),
						*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
				UE_LOG(LogMediaPipePose, Log,
					TEXT("mp.BodyFusion.RegionQualityCapture: writing %s"), *CapturePath);
			}

			FString Lines;
			for (int32 Index = 0; Index < MediaPipeBodyFusionQualityRegionCount; ++Index)
			{
				const FMediaPipeBodyFusionRegionQualityStats& RegionStats = Stats[Index];
				Lines += FString::Printf(
					TEXT("{\"t\":%.4f,\"actor\":\"%s\",\"region\":\"%s\",\"owner\":\"%s\",\"state\":\"%s\",\"valid\":%d,\"conf\":%.3f,\"amp_cm\":%.2f,\"speed_cm_s\":%.2f,\"dropouts\":%d,\"fresh_ratio\":%.3f,\"depth_var_ratio\":%.3f,\"depth_weak\":%d,\"may_influence\":%d,\"reason\":\"%s\"}\n"),
					Input.NowSeconds,
					*Input.TargetActorName.ToString(),
					RegionName(static_cast<EMediaPipeBodyFusionQualityRegion>(Index)),
					OwnerName(RegionStats.Owner),
					SourceStateName(RegionStats.SourceState),
					RegionStats.bValid ? 1 : 0,
					RegionStats.Confidence,
					RegionStats.AmplitudeCm,
					RegionStats.MeanSpeedCmPerSecond,
					RegionStats.DropoutCount,
					RegionStats.FreshRatio,
					RegionStats.DepthVarianceRatio,
					RegionStats.bDepthWeak ? 1 : 0,
					RegionStats.bMayInfluencePose ? 1 : 0,
					*RegionStats.InfluenceReason.ReplaceCharWithEscapedChar());
			}
			FFileHelper::SaveStringToFile(
				Lines,
				*CapturePath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
				&IFileManager::Get(),
				EFileWrite::FILEWRITE_Append);
		}
	}
}
