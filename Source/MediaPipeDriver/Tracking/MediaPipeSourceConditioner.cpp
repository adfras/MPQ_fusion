#include "MediaPipeSourceConditioner.h"

#include "MediaPipePoseCoordinate.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeSolvedPose.h"

#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarMediaPipeSourceConditioning(
		TEXT("mp.MediaPipeSourceConditioning"),
		1,
		TEXT("When non-zero, condition MediaPipe source landmarks before Manny retargeting."));

	TAutoConsoleVariable<int32> CVarMediaPipeSourceHoldBadLandmarks(
		TEXT("mp.MediaPipeSourceHoldBadLandmarks"),
		1,
		TEXT("When non-zero, low-confidence MediaPipe landmarks hold their last good source position."));

	TAutoConsoleVariable<int32> CVarMediaPipeSourceSmoothLandmarks(
		TEXT("mp.MediaPipeSourceSmoothLandmarks"),
		1,
		TEXT("When non-zero, smooth conditioned MediaPipe source landmarks with speed-adaptive smoothing."));

	TAutoConsoleVariable<int32> CVarMediaPipeAdaptivePoseConditioning(
		TEXT("mp.MediaPipeAdaptivePoseConditioning"),
		1,
		TEXT("When non-zero, produce a render-time conditioned full MediaPipe pose before Manny retargeting."));

	TAutoConsoleVariable<int32> CVarMediaPipeAdaptivePosePrediction(
		TEXT("mp.MediaPipeAdaptivePosePrediction"),
		1,
		TEXT("When non-zero, predict the full conditioned pose forward from recent unique MediaPipe frames to the current animation tick."));

	TAutoConsoleVariable<float> CVarMediaPipeAdaptivePoseMaxPredictionMs(
		TEXT("mp.MediaPipeAdaptivePoseMaxPredictionMs"),
		50.0f,
		TEXT("Maximum adaptive pose prediction horizon in milliseconds. The effective horizon is also limited by measured source cadence and pose quality."));

	TAutoConsoleVariable<float> CVarMediaPipeAdaptivePoseMinCutoff(
		TEXT("mp.MediaPipeAdaptivePoseMinCutoff"),
		1.8f,
		TEXT("One-Euro style minimum cutoff for render-time full-pose adaptive conditioning."));

	TAutoConsoleVariable<float> CVarMediaPipeAdaptivePoseBeta(
		TEXT("mp.MediaPipeAdaptivePoseBeta"),
		0.25f,
		TEXT("One-Euro style velocity beta for render-time full-pose adaptive conditioning."));

	TAutoConsoleVariable<int32> CVarMediaPipeAdaptivePoseQualityDebug(
		TEXT("mp.MediaPipeAdaptivePoseQualityDebug"),
		0,
		TEXT("When non-zero, expose adaptive pose quality diagnostics in pose frames even if log output is disabled."));

	TAutoConsoleVariable<int32> CVarMediaPipeAdaptivePoseLog(
		TEXT("mp.MediaPipeAdaptivePoseLog"),
		0,
		TEXT("When non-zero, log adaptive full-pose conditioning cadence, prediction, quality, and repeated-frame diagnostics."));

	TAutoConsoleVariable<int32> CVarMediaPipeSourceAdaptiveLengths(
		TEXT("mp.MediaPipeSourceAdaptiveLengths"),
		1,
		TEXT("When non-zero, slowly adapt performer limb widths/lengths and apply them to source landmarks."));

	TAutoConsoleVariable<int32> CVarMediaPipeSourceFootHemisphere(
		TEXT("mp.MediaPipeSourceFootHemisphere"),
		1,
		TEXT("When non-zero, keep MediaPipe ankle-to-toe source vectors in a stable forward hemisphere."));

	TAutoConsoleVariable<int32> CVarMediaPipeSourceOcclusionArmHold(
		TEXT("mp.MediaPipeSourceOcclusionArmHold"),
		0,
		TEXT("When non-zero, hold and torso-carry occluded shoulder/arm source landmarks instead of accepting collapsed MediaPipe arm updates. Disabled by default because the pose-specific rescue regressed other clips."));

	TAutoConsoleVariable<int32> CVarMediaPipeSourceOcclusionShoulderReconstruct(
		TEXT("mp.MediaPipeSourceOcclusionShoulderReconstruct"),
		0,
		TEXT("When non-zero, reconstruct collapsed side-view shoulder anchors from a stable torso-local shoulder girdle before retargeting. Disabled by default because the pose-specific rescue regressed other clips."));

	TAutoConsoleVariable<float> CVarMediaPipeSourceMinReliability(
		TEXT("mp.MediaPipeSourceMinReliability"),
		0.10f,
		TEXT("Minimum landmark reliability for accepting a new MediaPipe source landmark sample."));

	TAutoConsoleVariable<float> CVarMediaPipeSourceSmoothingHalfLife(
		TEXT("mp.MediaPipeSourceSmoothingHalfLife"),
		0.16f,
		TEXT("Low-speed source landmark smoothing half-life in seconds. 0 disables smoothing lag."));

	TAutoConsoleVariable<float> CVarMediaPipeSourceSmoothingFastSpeed(
		TEXT("mp.MediaPipeSourceSmoothingFastSpeed"),
		6.0f,
		TEXT("World-landmark speed in meters/sec where source smoothing opens up to follow motion closely."));

	TAutoConsoleVariable<float> CVarMediaPipeSourceLengthAdaptAlpha(
		TEXT("mp.MediaPipeSourceLengthAdaptAlpha"),
		0.025f,
		TEXT("Per-frame adaptation alpha for source limb length targets."));

	TAutoConsoleVariable<float> CVarMediaPipeSourceOcclusionArmHoldAcquireScore(
		TEXT("mp.MediaPipeSourceOcclusionArmHoldAcquireScore"),
		2.05f,
		TEXT("Shoulder/arm occlusion score required to start holding an arm."));

	TAutoConsoleVariable<float> CVarMediaPipeSourceOcclusionArmHoldReleaseScore(
		TEXT("mp.MediaPipeSourceOcclusionArmHoldReleaseScore"),
		1.35f,
		TEXT("Shoulder/arm occlusion score below which held arms can release after hysteresis frames."));

	TAutoConsoleVariable<float> CVarMediaPipeSourceOcclusionArmHoldBlendInHalfLife(
		TEXT("mp.MediaPipeSourceOcclusionArmHoldBlendInHalfLife"),
		0.10f,
		TEXT("Half-life in seconds for blending into occluded arm hold."));

	TAutoConsoleVariable<float> CVarMediaPipeSourceOcclusionArmHoldBlendOutHalfLife(
		TEXT("mp.MediaPipeSourceOcclusionArmHoldBlendOutHalfLife"),
		0.18f,
		TEXT("Half-life in seconds for blending out of occluded arm hold."));

	TAutoConsoleVariable<float> CVarMediaPipeSourceOcclusionArmHoldShoulderWeight(
		TEXT("mp.MediaPipeSourceOcclusionArmHoldShoulderWeight"),
		1.0f,
		TEXT("How strongly occlusion hold carries the shoulder landmark. 1 means full hold target, 0 disables shoulder hold."));

	TAutoConsoleVariable<float> CVarMediaPipeSourceOcclusionArmHoldElbowWeight(
		TEXT("mp.MediaPipeSourceOcclusionArmHoldElbowWeight"),
		0.65f,
		TEXT("How strongly occlusion hold carries the elbow landmark. Lower values reduce arm snapping during real motion."));

	TAutoConsoleVariable<float> CVarMediaPipeSourceOcclusionArmHoldWristWeight(
		TEXT("mp.MediaPipeSourceOcclusionArmHoldWristWeight"),
		0.25f,
		TEXT("How strongly occlusion hold carries the wrist and hand landmarks. Lower values reduce unnatural wrist/hand snapping."));

	TAutoConsoleVariable<float> CVarMediaPipeSourceOcclusionShoulderReconstructAcquireScore(
		TEXT("mp.MediaPipeSourceOcclusionShoulderReconstructAcquireScore"),
		1.70f,
		TEXT("Occlusion score required to start reconstructing shoulder anchors from the stable torso-local shoulder girdle."));

	TAutoConsoleVariable<float> CVarMediaPipeSourceOcclusionShoulderReconstructReleaseScore(
		TEXT("mp.MediaPipeSourceOcclusionShoulderReconstructReleaseScore"),
		1.20f,
		TEXT("Occlusion score below which shoulder reconstruction can release after hysteresis frames."));

	TAutoConsoleVariable<float> CVarMediaPipeSourceOcclusionShoulderReconstructWeight(
		TEXT("mp.MediaPipeSourceOcclusionShoulderReconstructWeight"),
		1.0f,
		TEXT("How strongly occluded shoulder anchors are reconstructed from the stable torso-local shoulder girdle."));

	TAutoConsoleVariable<float> CVarMediaPipeSourceOcclusionShoulderReconstructElbowFollowWeight(
		TEXT("mp.MediaPipeSourceOcclusionShoulderReconstructElbowFollowWeight"),
		0.50f,
		TEXT("How much elbow landmarks follow reconstructed shoulder movement during side-view occlusion."));

	TAutoConsoleVariable<float> CVarMediaPipeSourceOcclusionShoulderReconstructWristFollowWeight(
		TEXT("mp.MediaPipeSourceOcclusionShoulderReconstructWristFollowWeight"),
		0.20f,
		TEXT("How much wrist and hand landmarks follow reconstructed shoulder movement during side-view occlusion."));

	TAutoConsoleVariable<int32> CVarMediaPipeSourceOcclusionArmHoldAcquireFrames(
		TEXT("mp.MediaPipeSourceOcclusionArmHoldAcquireFrames"),
		2,
		TEXT("Consecutive occlusion frames required before arm hold activates."));

	TAutoConsoleVariable<int32> CVarMediaPipeSourceOcclusionArmHoldReleaseFrames(
		TEXT("mp.MediaPipeSourceOcclusionArmHoldReleaseFrames"),
		8,
		TEXT("Consecutive recovered frames required before arm hold releases."));

	int32 LandmarkIndex(const EMediaPipePoseLandmark Landmark)
	{
		return static_cast<int32>(Landmark);
	}

	FVector GetLandmarkVector(const FMediaPipePoseLandmarks& Landmarks, const EMediaPipePoseLandmark Landmark)
	{
		const FMediaPipePoseLandmark& Point = Landmarks.Points[LandmarkIndex(Landmark)];
		return FVector(Point.X, Point.Y, Point.Z);
	}

	FVector GetLandmarkVector(const TStaticArray<FMediaPipePoseLandmark, MediaPipePoseLandmarkCount>& Landmarks, const EMediaPipePoseLandmark Landmark)
	{
		const FMediaPipePoseLandmark& Point = Landmarks[LandmarkIndex(Landmark)];
		return FVector(Point.X, Point.Y, Point.Z);
	}

	void SetLandmarkVector(FMediaPipePoseLandmarks& Landmarks, const EMediaPipePoseLandmark Landmark, const FVector& Value)
	{
		FMediaPipePoseLandmark& Point = Landmarks.Points[LandmarkIndex(Landmark)];
		Point.X = Value.X;
		Point.Y = Value.Y;
		Point.Z = Value.Z;
	}

	float GetLandmarkReliability(const FMediaPipePoseFrame& Frame, const EMediaPipePoseLandmark Landmark)
	{
		const int32 Index = LandmarkIndex(Landmark);
		const FMediaPipePoseLandmark& World = Frame.World.Points[Index];
		const FMediaPipePoseLandmark& Normalized = Frame.Normalized.Points[Index];
		return FMath::Max(
			FMath::Max(World.Reliability, Normalized.Reliability),
			FMath::Max(World.Visibility * World.Presence, Normalized.Visibility * Normalized.Presence));
	}

	void OffsetLandmarkSet(
		FMediaPipePoseFrame& Frame,
		const TArray<EMediaPipePoseLandmark>& Landmarks,
		const FVector& WorldOffset)
	{
		if (WorldOffset.IsNearlyZero())
		{
			return;
		}

		for (const EMediaPipePoseLandmark Landmark : Landmarks)
		{
			SetLandmarkVector(Frame.World, Landmark, GetLandmarkVector(Frame.World, Landmark) + WorldOffset);
		}
	}

	struct FWidthDef
	{
		EMediaPipePoseLandmark Left;
		EMediaPipePoseLandmark Right;
		TArray<EMediaPipePoseLandmark> LeftDescendants;
		TArray<EMediaPipePoseLandmark> RightDescendants;
	};

	const TArray<FWidthDef>& GetWidthDefs()
	{
		static const TArray<FWidthDef> Defs = {
			{
				EMediaPipePoseLandmark::LeftShoulder,
				EMediaPipePoseLandmark::RightShoulder,
				{
					EMediaPipePoseLandmark::LeftShoulder,
					EMediaPipePoseLandmark::LeftElbow,
					EMediaPipePoseLandmark::LeftWrist,
					EMediaPipePoseLandmark::LeftPinky,
					EMediaPipePoseLandmark::LeftIndex,
					EMediaPipePoseLandmark::LeftThumb,
				},
				{
					EMediaPipePoseLandmark::RightShoulder,
					EMediaPipePoseLandmark::RightElbow,
					EMediaPipePoseLandmark::RightWrist,
					EMediaPipePoseLandmark::RightPinky,
					EMediaPipePoseLandmark::RightIndex,
					EMediaPipePoseLandmark::RightThumb,
				},
			},
			{
				EMediaPipePoseLandmark::LeftHip,
				EMediaPipePoseLandmark::RightHip,
				{
					EMediaPipePoseLandmark::LeftHip,
					EMediaPipePoseLandmark::LeftKnee,
					EMediaPipePoseLandmark::LeftAnkle,
					EMediaPipePoseLandmark::LeftHeel,
					EMediaPipePoseLandmark::LeftFootIndex,
				},
				{
					EMediaPipePoseLandmark::RightHip,
					EMediaPipePoseLandmark::RightKnee,
					EMediaPipePoseLandmark::RightAnkle,
					EMediaPipePoseLandmark::RightHeel,
					EMediaPipePoseLandmark::RightFootIndex,
				},
			},
		};
		return Defs;
	}

	struct FSegmentDef
	{
		EMediaPipePoseLandmark Parent;
		EMediaPipePoseLandmark Child;
		TArray<EMediaPipePoseLandmark> Descendants;
	};

	const TArray<FSegmentDef>& GetSegmentDefs()
	{
		static const TArray<FSegmentDef> Defs = {
			{ EMediaPipePoseLandmark::LeftShoulder, EMediaPipePoseLandmark::LeftElbow, { EMediaPipePoseLandmark::LeftElbow, EMediaPipePoseLandmark::LeftWrist, EMediaPipePoseLandmark::LeftPinky, EMediaPipePoseLandmark::LeftIndex, EMediaPipePoseLandmark::LeftThumb } },
			{ EMediaPipePoseLandmark::LeftElbow, EMediaPipePoseLandmark::LeftWrist, { EMediaPipePoseLandmark::LeftWrist, EMediaPipePoseLandmark::LeftPinky, EMediaPipePoseLandmark::LeftIndex, EMediaPipePoseLandmark::LeftThumb } },
			{ EMediaPipePoseLandmark::RightShoulder, EMediaPipePoseLandmark::RightElbow, { EMediaPipePoseLandmark::RightElbow, EMediaPipePoseLandmark::RightWrist, EMediaPipePoseLandmark::RightPinky, EMediaPipePoseLandmark::RightIndex, EMediaPipePoseLandmark::RightThumb } },
			{ EMediaPipePoseLandmark::RightElbow, EMediaPipePoseLandmark::RightWrist, { EMediaPipePoseLandmark::RightWrist, EMediaPipePoseLandmark::RightPinky, EMediaPipePoseLandmark::RightIndex, EMediaPipePoseLandmark::RightThumb } },
			{ EMediaPipePoseLandmark::LeftHip, EMediaPipePoseLandmark::LeftKnee, { EMediaPipePoseLandmark::LeftKnee, EMediaPipePoseLandmark::LeftAnkle, EMediaPipePoseLandmark::LeftHeel, EMediaPipePoseLandmark::LeftFootIndex } },
			{ EMediaPipePoseLandmark::LeftKnee, EMediaPipePoseLandmark::LeftAnkle, { EMediaPipePoseLandmark::LeftAnkle, EMediaPipePoseLandmark::LeftHeel, EMediaPipePoseLandmark::LeftFootIndex } },
			{ EMediaPipePoseLandmark::LeftAnkle, EMediaPipePoseLandmark::LeftHeel, { EMediaPipePoseLandmark::LeftHeel } },
			{ EMediaPipePoseLandmark::LeftAnkle, EMediaPipePoseLandmark::LeftFootIndex, { EMediaPipePoseLandmark::LeftFootIndex } },
			{ EMediaPipePoseLandmark::RightHip, EMediaPipePoseLandmark::RightKnee, { EMediaPipePoseLandmark::RightKnee, EMediaPipePoseLandmark::RightAnkle, EMediaPipePoseLandmark::RightHeel, EMediaPipePoseLandmark::RightFootIndex } },
			{ EMediaPipePoseLandmark::RightKnee, EMediaPipePoseLandmark::RightAnkle, { EMediaPipePoseLandmark::RightAnkle, EMediaPipePoseLandmark::RightHeel, EMediaPipePoseLandmark::RightFootIndex } },
			{ EMediaPipePoseLandmark::RightAnkle, EMediaPipePoseLandmark::RightHeel, { EMediaPipePoseLandmark::RightHeel } },
			{ EMediaPipePoseLandmark::RightAnkle, EMediaPipePoseLandmark::RightFootIndex, { EMediaPipePoseLandmark::RightFootIndex } },
		};
		return Defs;
	}

	float ComputeAdaptiveAlpha(const float HalfLifeSeconds, const float FastSpeedMps, const float SpeedMps, const double DeltaSeconds)
	{
		if (DeltaSeconds <= UE_DOUBLE_SMALL_NUMBER)
		{
			return 1.0f;
		}

		if (HalfLifeSeconds <= UE_SMALL_NUMBER)
		{
			return 1.0f;
		}

		const float BaseAlpha = 1.0f - FMath::Pow(0.5f, static_cast<float>(DeltaSeconds) / HalfLifeSeconds);
		const float SpeedAlpha = FastSpeedMps > UE_SMALL_NUMBER
			? FMath::Clamp(SpeedMps / FastSpeedMps, 0.0f, 1.0f)
			: 1.0f;
		return FMath::Clamp(FMath::Lerp(BaseAlpha, 1.0f, SpeedAlpha), 0.0f, 1.0f);
	}

	FVector2D GetNormalizedPoint(const FMediaPipePoseFrame& Frame, const EMediaPipePoseLandmark Landmark)
	{
		const FMediaPipePoseLandmark& Point = Frame.Normalized.Points[LandmarkIndex(Landmark)];
		return FVector2D(Point.X, Point.Y);
	}

	float Distance2D(const FVector2D& A, const FVector2D& B)
	{
		return FVector2D::Distance(A, B);
	}

	FVector2D Midpoint2D(const FVector2D& A, const FVector2D& B)
	{
		return (A + B) * 0.5f;
	}

	float PointSegmentDistance2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D Segment = B - A;
		const float Denom = Segment.SizeSquared();
		if (Denom <= UE_SMALL_NUMBER)
		{
			return Distance2D(Point, A);
		}
		const float T = FMath::Clamp(FVector2D::DotProduct(Point - A, Segment) / Denom, 0.0f, 1.0f);
		return Distance2D(Point, A + Segment * T);
	}

	bool PointInTorsoPolygon2D(const FVector2D& Point, const FVector2D& LS, const FVector2D& RS, const FVector2D& RH, const FVector2D& LH)
	{
		const FVector2D Poly[4] = { LS, RS, RH, LH };
		bool bInside = false;
		int32 J = 3;
		for (int32 I = 0; I < 4; ++I)
		{
			const FVector2D& Pi = Poly[I];
			const FVector2D& Pj = Poly[J];
			if (((Pi.Y > Point.Y) != (Pj.Y > Point.Y)) &&
				(Point.X < (Pj.X - Pi.X) * (Point.Y - Pi.Y) / FMath::Max(Pj.Y - Pi.Y, UE_SMALL_NUMBER) + Pi.X))
			{
				bInside = !bInside;
			}
			J = I;
		}
		return bInside;
	}

	float TorsoOverlapScore2D(
		const FVector2D& Elbow,
		const FVector2D& Wrist,
		const FVector2D& LS,
		const FVector2D& RS,
		const FVector2D& LH,
		const FVector2D& RH,
		const FVector2D& ShoulderMid,
		const FVector2D& HipMid,
		const float TorsoHeight)
	{
		float Score = 0.0f;
	const FVector2D Points[2] = { Elbow, Wrist };
	for (const FVector2D& Point : Points)
		{
			if (PointInTorsoPolygon2D(Point, LS, RS, RH, LH))
			{
				Score = FMath::Max(Score, 1.0f);
				continue;
			}
			const float DistanceRatio = PointSegmentDistance2D(Point, ShoulderMid, HipMid) / FMath::Max(TorsoHeight, UE_SMALL_NUMBER);
			Score = FMath::Max(Score, FMath::Clamp((0.28f - DistanceRatio) / 0.20f, 0.0f, 1.0f));
		}
		return Score;
	}

	float SmoothStepAlpha(const float HalfLifeSeconds, const double DeltaSeconds)
	{
		if (DeltaSeconds <= UE_DOUBLE_SMALL_NUMBER)
		{
			return 1.0f;
		}
		if (HalfLifeSeconds <= UE_SMALL_NUMBER)
		{
			return 1.0f;
		}
		return FMath::Clamp(1.0f - FMath::Pow(0.5f, static_cast<float>(DeltaSeconds) / HalfLifeSeconds), 0.0f, 1.0f);
	}

	float OneEuroAlpha(const float CutoffHz, const double DeltaSeconds)
	{
		if (DeltaSeconds <= UE_DOUBLE_SMALL_NUMBER)
		{
			return 1.0f;
		}

		const float SafeCutoffHz = FMath::Max(0.01f, CutoffHz);
		const float Tau = 1.0f / (2.0f * UE_PI * SafeCutoffHz);
		return FMath::Clamp(1.0f / (1.0f + Tau / static_cast<float>(DeltaSeconds)), 0.0f, 1.0f);
	}

	float ClampReliability(const float Reliability)
	{
		return FMath::Clamp(Reliability, 0.0f, 1.0f);
	}

	float ComputeMeanConfidence(const FMediaPipePoseFrame& Frame)
	{
		float Sum = 0.0f;
		for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
		{
			const FMediaPipePoseLandmark& World = Frame.World.Points[Index];
			const FMediaPipePoseLandmark& Normalized = Frame.Normalized.Points[Index];
			Sum += ClampReliability(FMath::Max(
				FMath::Max(World.Reliability, Normalized.Reliability),
				FMath::Max(World.Visibility * World.Presence, Normalized.Visibility * Normalized.Presence)));
		}
		return Sum / static_cast<float>(MediaPipePoseLandmarkCount);
	}

	float ComputeRegionQuality(const FMediaPipePoseFrame& Frame, const EMediaPipePoseLandmark* Landmarks, const int32 Count)
	{
		if (!Landmarks || Count <= 0)
		{
			return 0.0f;
		}

		float Sum = 0.0f;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Sum += ClampReliability(GetLandmarkReliability(Frame, Landmarks[Index]));
		}
		return Sum / static_cast<float>(Count);
	}

	FVector ComputePoseCenterWorld(const FMediaPipePoseFrame& Frame)
	{
		const EMediaPipePoseLandmark CoreLandmarks[] = {
			EMediaPipePoseLandmark::LeftShoulder,
			EMediaPipePoseLandmark::RightShoulder,
			EMediaPipePoseLandmark::LeftHip,
			EMediaPipePoseLandmark::RightHip,
		};

		FVector Sum = FVector::ZeroVector;
		for (const EMediaPipePoseLandmark Landmark : CoreLandmarks)
		{
			Sum += GetLandmarkVector(Frame.World, Landmark);
		}
		return Sum / static_cast<float>(UE_ARRAY_COUNT(CoreLandmarks));
	}

	float ComputeBodyScaleWorld(const FMediaPipePoseFrame& Frame)
	{
		const FVector LeftShoulder = GetLandmarkVector(Frame.World, EMediaPipePoseLandmark::LeftShoulder);
		const FVector RightShoulder = GetLandmarkVector(Frame.World, EMediaPipePoseLandmark::RightShoulder);
		const FVector LeftHip = GetLandmarkVector(Frame.World, EMediaPipePoseLandmark::LeftHip);
		const FVector RightHip = GetLandmarkVector(Frame.World, EMediaPipePoseLandmark::RightHip);
		const FVector ShoulderMid = (LeftShoulder + RightShoulder) * 0.5f;
		const FVector HipMid = (LeftHip + RightHip) * 0.5f;
		return FMath::Max3(
			FVector::Distance(LeftShoulder, RightShoulder),
			FVector::Distance(LeftHip, RightHip),
			FVector::Distance(ShoulderMid, HipMid));
	}

	float ComputeTimestampFps(const double CadenceSeconds)
	{
		return CadenceSeconds > UE_DOUBLE_SMALL_NUMBER ? static_cast<float>(1.0 / CadenceSeconds) : 0.0f;
	}

	void ScaleLandmarkConfidence(FMediaPipePoseLandmark& Landmark, const float Scale)
	{
		const float SafeScale = FMath::Clamp(Scale, 0.0f, 1.0f);
		Landmark.Visibility = FMath::Clamp(Landmark.Visibility * SafeScale, 0.0f, 1.0f);
		Landmark.Presence = FMath::Clamp(Landmark.Presence * SafeScale, 0.0f, 1.0f);
		Landmark.Reliability = FMath::Clamp(Landmark.Reliability * SafeScale, 0.0f, 1.0f);
	}
}

FMediaPipeSourceConditionerOptions FMediaPipeSourceConditioner::MakeDefaultOptions()
{
	FMediaPipeSourceConditionerOptions Options;
	Options.bEnabled = CVarMediaPipeSourceConditioning.GetValueOnAnyThread() != 0;
	Options.bHoldBadLandmarks = CVarMediaPipeSourceHoldBadLandmarks.GetValueOnAnyThread() != 0;
	Options.bSmoothLandmarks = CVarMediaPipeSourceSmoothLandmarks.GetValueOnAnyThread() != 0;
	Options.bAdaptivePoseConditioning = CVarMediaPipeAdaptivePoseConditioning.GetValueOnAnyThread() != 0;
	Options.bAdaptivePosePrediction = CVarMediaPipeAdaptivePosePrediction.GetValueOnAnyThread() != 0;
	Options.bAdaptivePoseQualityDebug = CVarMediaPipeAdaptivePoseQualityDebug.GetValueOnAnyThread() != 0;
	Options.bAdaptivePoseLog = CVarMediaPipeAdaptivePoseLog.GetValueOnAnyThread() != 0;
	Options.bAdaptiveSegmentLengths = CVarMediaPipeSourceAdaptiveLengths.GetValueOnAnyThread() != 0;
	Options.bFootForwardHemisphere = CVarMediaPipeSourceFootHemisphere.GetValueOnAnyThread() != 0;
	Options.bOcclusionArmHold = CVarMediaPipeSourceOcclusionArmHold.GetValueOnAnyThread() != 0;
	Options.bOcclusionShoulderReconstruct = CVarMediaPipeSourceOcclusionShoulderReconstruct.GetValueOnAnyThread() != 0;
	Options.MinLandmarkReliability = FMath::Max(0.0f, CVarMediaPipeSourceMinReliability.GetValueOnAnyThread());
	Options.LandmarkSmoothingHalfLifeSeconds = FMath::Max(0.0f, CVarMediaPipeSourceSmoothingHalfLife.GetValueOnAnyThread());
	Options.LandmarkSmoothingFastSpeedMps = FMath::Max(0.0f, CVarMediaPipeSourceSmoothingFastSpeed.GetValueOnAnyThread());
	Options.AdaptivePoseMaxPredictionMs = FMath::Max(0.0f, CVarMediaPipeAdaptivePoseMaxPredictionMs.GetValueOnAnyThread());
	Options.AdaptivePoseMinCutoff = FMath::Max(0.01f, CVarMediaPipeAdaptivePoseMinCutoff.GetValueOnAnyThread());
	Options.AdaptivePoseBeta = FMath::Max(0.0f, CVarMediaPipeAdaptivePoseBeta.GetValueOnAnyThread());
	Options.SegmentLengthAdaptAlpha = FMath::Clamp(CVarMediaPipeSourceLengthAdaptAlpha.GetValueOnAnyThread(), 0.0f, 1.0f);
	Options.OcclusionArmHoldAcquireScore = FMath::Max(0.0f, CVarMediaPipeSourceOcclusionArmHoldAcquireScore.GetValueOnAnyThread());
	Options.OcclusionArmHoldReleaseScore = FMath::Max(0.0f, CVarMediaPipeSourceOcclusionArmHoldReleaseScore.GetValueOnAnyThread());
	Options.OcclusionArmHoldBlendInHalfLifeSeconds = FMath::Max(0.0f, CVarMediaPipeSourceOcclusionArmHoldBlendInHalfLife.GetValueOnAnyThread());
	Options.OcclusionArmHoldBlendOutHalfLifeSeconds = FMath::Max(0.0f, CVarMediaPipeSourceOcclusionArmHoldBlendOutHalfLife.GetValueOnAnyThread());
	Options.OcclusionArmHoldShoulderWeight = FMath::Clamp(CVarMediaPipeSourceOcclusionArmHoldShoulderWeight.GetValueOnAnyThread(), 0.0f, 1.0f);
	Options.OcclusionArmHoldElbowWeight = FMath::Clamp(CVarMediaPipeSourceOcclusionArmHoldElbowWeight.GetValueOnAnyThread(), 0.0f, 1.0f);
	Options.OcclusionArmHoldWristWeight = FMath::Clamp(CVarMediaPipeSourceOcclusionArmHoldWristWeight.GetValueOnAnyThread(), 0.0f, 1.0f);
	Options.OcclusionShoulderReconstructAcquireScore = FMath::Max(0.0f, CVarMediaPipeSourceOcclusionShoulderReconstructAcquireScore.GetValueOnAnyThread());
	Options.OcclusionShoulderReconstructReleaseScore = FMath::Max(0.0f, CVarMediaPipeSourceOcclusionShoulderReconstructReleaseScore.GetValueOnAnyThread());
	Options.OcclusionShoulderReconstructWeight = FMath::Clamp(CVarMediaPipeSourceOcclusionShoulderReconstructWeight.GetValueOnAnyThread(), 0.0f, 1.0f);
	Options.OcclusionShoulderReconstructElbowFollowWeight = FMath::Clamp(CVarMediaPipeSourceOcclusionShoulderReconstructElbowFollowWeight.GetValueOnAnyThread(), 0.0f, 1.0f);
	Options.OcclusionShoulderReconstructWristFollowWeight = FMath::Clamp(CVarMediaPipeSourceOcclusionShoulderReconstructWristFollowWeight.GetValueOnAnyThread(), 0.0f, 1.0f);
	Options.OcclusionArmHoldAcquireFrames = FMath::Max(1, CVarMediaPipeSourceOcclusionArmHoldAcquireFrames.GetValueOnAnyThread());
	Options.OcclusionArmHoldReleaseFrames = FMath::Max(1, CVarMediaPipeSourceOcclusionArmHoldReleaseFrames.GetValueOnAnyThread());
	return Options;
}

FMediaPipeSourceConditioner::FMediaPipeSourceConditioner()
{
	Reset();
}

void FMediaPipeSourceConditioner::Reset()
{
	bHasLastTimestamp = false;
	LastTimestampUs = 0;
	ResetHistory();
	ResetAdaptiveHistory();

	SegmentLengthStates.SetNum(GetSegmentDefs().Num());
	WidthLengthStates.SetNum(GetWidthDefs().Num());
	for (FLengthState& State : SegmentLengthStates)
	{
		State = FLengthState();
	}
	for (FLengthState& State : WidthLengthStates)
	{
		State = FLengthState();
	}

	for (int32 Index = 0; Index < 2; ++Index)
	{
		bHasStableFootForwardLocal[Index] = false;
		StableFootForwardLocal[Index] = FVector::ForwardVector;
		ArmHoldStates[Index] = FArmOcclusionHoldState();
	}
	bHasReferenceShoulderRatio = false;
	ReferenceShoulderWidthRatio = 0.42f;
	ShoulderGirdleState = FShoulderGirdleOcclusionState();
}

void FMediaPipeSourceConditioner::ResetHistory()
{
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		bHasPreviousLandmark[Index] = false;
		PreviousWorld[Index] = FMediaPipePoseLandmark();
		PreviousNormalized[Index] = FMediaPipePoseLandmark();
	}
}

void FMediaPipeSourceConditioner::ResetAdaptiveHistory()
{
	for (FAdaptiveLandmarkFilterState& State : AdaptiveFilters)
	{
		State = FAdaptiveLandmarkFilterState();
	}

	for (FPoseHistorySample& Sample : RecentSamples)
	{
		Sample = FPoseHistorySample();
	}

	RecentSampleCount = 0;
	SourceCadenceSeconds = 0.0;
	UniquePoseCount = 0;
	RepeatedFrameRunLength = 0;
	DroppedFrameCount = 0;
	LastAdaptivePoseLogSeconds = -1.0;
}

bool FMediaPipeSourceConditioner::ConditionFrame(
	const FMediaPipePoseFrame& RawFrame,
	const float WorldScaleCm,
	const bool bMirrorLandmarksLR,
	const FMediaPipeSourceConditionerOptions& Options,
	FMediaPipePoseFrame& OutFrame,
	const double QueryTimeSeconds)
{
	OutFrame = RawFrame;
	if (!RawFrame.bValid)
	{
		return false;
	}

	const double EffectiveQueryTimeSeconds = QueryTimeSeconds >= 0.0
		? QueryTimeSeconds
		: FPlatformTime::Seconds();
	OutFrame.ConditionedQueryWallSeconds = EffectiveQueryTimeSeconds;

	if (!Options.bEnabled)
	{
		return OutFrame.bValid;
	}

	bool bTimestampDiscontinuity = false;
	if (bHasLastTimestamp && RawFrame.TimestampUs < LastTimestampUs)
	{
		bTimestampDiscontinuity = true;
		Reset();
	}

	const bool bIsUniqueFrame = !bHasLastTimestamp || RawFrame.TimestampUs > LastTimestampUs;
	if (bIsUniqueFrame)
	{
		const double DeltaSeconds = bHasLastTimestamp
			? FMath::Max(0.0, static_cast<double>(RawFrame.TimestampUs - LastTimestampUs) * 1.0e-6)
			: 0.0;

		if (DeltaSeconds > UE_DOUBLE_SMALL_NUMBER)
		{
			if (SourceCadenceSeconds > UE_DOUBLE_SMALL_NUMBER)
			{
				if (DeltaSeconds > SourceCadenceSeconds * 1.75)
				{
					DroppedFrameCount += FMath::Max(1, FMath::RoundToInt(static_cast<float>(DeltaSeconds / SourceCadenceSeconds)) - 1);
				}
				SourceCadenceSeconds = FMath::Lerp(SourceCadenceSeconds, DeltaSeconds, 0.12);
			}
			else
			{
				SourceCadenceSeconds = DeltaSeconds;
			}
		}

		OutFrame = RawFrame;
		if (!RawFrame.bSourceConditioned)
		{
			ApplyHoldAndSmoothing(OutFrame, DeltaSeconds, Options);
			ApplyAdaptiveSegmentLengths(OutFrame, Options);
			ApplyOcclusionArmHold(OutFrame, DeltaSeconds, Options);
			ApplyFootForwardHemisphere(OutFrame, WorldScaleCm, bMirrorLandmarksLR, Options);
			OutFrame.bSourceConditioned = true;
		}

		StorePreviousFrame(OutFrame);
		StoreUniqueSample(OutFrame, EffectiveQueryTimeSeconds, DeltaSeconds, bTimestampDiscontinuity);
		LastTimestampUs = RawFrame.TimestampUs;
		bHasLastTimestamp = true;
		RepeatedFrameRunLength = 0;
	}
	else
	{
		++RepeatedFrameRunLength;
	}

	const bool bUseAdaptiveOutput = Options.bAdaptivePoseConditioning || Options.bAdaptivePosePrediction;
	if (!bUseAdaptiveOutput)
	{
		if (RecentSampleCount > 0 && RecentSamples[0].bValid)
		{
			OutFrame = RecentSamples[0].Frame;
			OutFrame.ConditioningDiagnostics.RepeatedPoseRunLength = RepeatedFrameRunLength;
			OutFrame.ConditioningDiagnostics.bRepeatedPose = RepeatedFrameRunLength > 0 ? 1 : 0;
		}
		return OutFrame.bValid;
	}

	BuildRenderTimeFrame(EffectiveQueryTimeSeconds, Options, OutFrame);
	return OutFrame.bValid;
}

void FMediaPipeSourceConditioner::StoreUniqueSample(
	const FMediaPipePoseFrame& Frame,
	const double ArrivalSeconds,
	const double SourceDeltaSeconds,
	const bool bTimestampDiscontinuity)
{
	for (int32 Index = UE_ARRAY_COUNT(RecentSamples) - 1; Index > 0; --Index)
	{
		RecentSamples[Index] = RecentSamples[Index - 1];
	}

	RecentSamples[0] = FPoseHistorySample();
	RecentSamples[0].bValid = Frame.bValid;
	RecentSamples[0].Frame = Frame;
	RecentSamples[0].ArrivalSeconds = ArrivalSeconds;
	RecentSamples[0].SourceTimestampSeconds = static_cast<double>(Frame.TimestampUs) * 1.0e-6;
	RecentSamples[0].SourceDeltaSeconds = SourceDeltaSeconds;
	RecentSamples[0].bTimestampDiscontinuity = bTimestampDiscontinuity;
	RecentSampleCount = FMath::Min(RecentSampleCount + 1, static_cast<int32>(UE_ARRAY_COUNT(RecentSamples)));
	++UniquePoseCount;

	UpdateSampleDiagnostics(RecentSamples[0]);
}

void FMediaPipeSourceConditioner::UpdateSampleDiagnostics(FPoseHistorySample& Sample)
{
	FMediaPipePoseFrame& Frame = Sample.Frame;
	FMediaPipePoseFrame::FConditioningDiagnostics& Diagnostics = Frame.ConditioningDiagnostics;

	const float MeanConfidence = ComputeMeanConfidence(Frame);
	float MeanJitter = 0.0f;
	float MaxJitter = 0.0f;
	float WholePoseSpikeScore = 0.0f;
	bool bConfidenceCollapse = false;

	const float BodyScale = FMath::Max(ComputeBodyScaleWorld(Frame), 0.15f);
	if (RecentSampleCount > 1 && RecentSamples[1].bValid)
	{
		const FMediaPipePoseFrame& PreviousFrame = RecentSamples[1].Frame;
		float JitterSum = 0.0f;
		for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
		{
			const FVector Current = FVector(
				Frame.World.Points[Index].X,
				Frame.World.Points[Index].Y,
				Frame.World.Points[Index].Z);
			const FVector Previous = FVector(
				PreviousFrame.World.Points[Index].X,
				PreviousFrame.World.Points[Index].Y,
				PreviousFrame.World.Points[Index].Z);
			const float NormalizedJump = FVector::Distance(Current, Previous) / BodyScale;
			JitterSum += NormalizedJump;
			MaxJitter = FMath::Max(MaxJitter, NormalizedJump);
		}
		MeanJitter = JitterSum / static_cast<float>(MediaPipePoseLandmarkCount);

		const float PreviousMeanConfidence = RecentSamples[1].MeanConfidence;
		bConfidenceCollapse = PreviousMeanConfidence > 0.0f && (PreviousMeanConfidence - MeanConfidence) > 0.35f;

		const float CenterJump = FVector::Distance(ComputePoseCenterWorld(Frame), ComputePoseCenterWorld(PreviousFrame)) / BodyScale;
		WholePoseSpikeScore = FMath::Max3(
			FMath::Clamp((CenterJump - 0.35f) / 0.85f, 0.0f, 1.0f),
			FMath::Clamp((MeanJitter - 0.22f) / 0.65f, 0.0f, 1.0f),
			FMath::Clamp((MaxJitter - 0.75f) / 1.25f, 0.0f, 1.0f));

		if (RecentSampleCount > 2 && RecentSamples[2].bValid)
		{
			const double CurrentDt = Sample.SourceDeltaSeconds;
			const double PreviousDt = RecentSamples[1].SourceDeltaSeconds;
			if (CurrentDt > UE_DOUBLE_SMALL_NUMBER && PreviousDt > UE_DOUBLE_SMALL_NUMBER)
			{
				const FVector CurrentVelocity = (ComputePoseCenterWorld(Frame) - ComputePoseCenterWorld(PreviousFrame)) / CurrentDt;
				const FVector PreviousVelocity = (ComputePoseCenterWorld(PreviousFrame) - ComputePoseCenterWorld(RecentSamples[2].Frame)) / PreviousDt;
				const float AccelScore = static_cast<float>((CurrentVelocity - PreviousVelocity).Size() / FMath::Max(BodyScale, UE_SMALL_NUMBER));
				WholePoseSpikeScore = FMath::Max(WholePoseSpikeScore, FMath::Clamp((AccelScore - 35.0f) / 85.0f, 0.0f, 1.0f));
			}
		}
	}

	const bool bWholePoseSpike = WholePoseSpikeScore > 0.65f;
	float QualityScore = MeanConfidence;
	QualityScore *= FMath::Lerp(1.0f, 0.35f, WholePoseSpikeScore);
	if (bConfidenceCollapse)
	{
		QualityScore *= 0.65f;
	}
	QualityScore = FMath::Clamp(QualityScore, 0.0f, 1.0f);

	Sample.QualityScore = QualityScore;
	Sample.MeanConfidence = MeanConfidence;
	Sample.MeanJitter = MeanJitter;
	Sample.MaxJitter = MaxJitter;
	Sample.WholePoseSpikeScore = WholePoseSpikeScore;
	Sample.bConfidenceCollapse = bConfidenceCollapse;
	Sample.bWholePoseSpike = bWholePoseSpike;

	const EMediaPipePoseLandmark RootPelvis[] = { EMediaPipePoseLandmark::LeftHip, EMediaPipePoseLandmark::RightHip };
	const EMediaPipePoseLandmark TorsoSpine[] = {
		EMediaPipePoseLandmark::LeftShoulder,
		EMediaPipePoseLandmark::RightShoulder,
		EMediaPipePoseLandmark::LeftHip,
		EMediaPipePoseLandmark::RightHip,
	};
	const EMediaPipePoseLandmark HeadNeck[] = {
		EMediaPipePoseLandmark::Nose,
		EMediaPipePoseLandmark::LeftEye,
		EMediaPipePoseLandmark::RightEye,
		EMediaPipePoseLandmark::LeftEar,
		EMediaPipePoseLandmark::RightEar,
		EMediaPipePoseLandmark::MouthLeft,
		EMediaPipePoseLandmark::MouthRight,
	};
	const EMediaPipePoseLandmark Shoulders[] = { EMediaPipePoseLandmark::LeftShoulder, EMediaPipePoseLandmark::RightShoulder };
	const EMediaPipePoseLandmark Arms[] = {
		EMediaPipePoseLandmark::LeftShoulder,
		EMediaPipePoseLandmark::LeftElbow,
		EMediaPipePoseLandmark::LeftWrist,
		EMediaPipePoseLandmark::RightShoulder,
		EMediaPipePoseLandmark::RightElbow,
		EMediaPipePoseLandmark::RightWrist,
	};
	const EMediaPipePoseLandmark HandsWrists[] = {
		EMediaPipePoseLandmark::LeftWrist,
		EMediaPipePoseLandmark::LeftPinky,
		EMediaPipePoseLandmark::LeftIndex,
		EMediaPipePoseLandmark::LeftThumb,
		EMediaPipePoseLandmark::RightWrist,
		EMediaPipePoseLandmark::RightPinky,
		EMediaPipePoseLandmark::RightIndex,
		EMediaPipePoseLandmark::RightThumb,
	};
	const EMediaPipePoseLandmark Hips[] = { EMediaPipePoseLandmark::LeftHip, EMediaPipePoseLandmark::RightHip };
	const EMediaPipePoseLandmark Legs[] = {
		EMediaPipePoseLandmark::LeftHip,
		EMediaPipePoseLandmark::LeftKnee,
		EMediaPipePoseLandmark::LeftAnkle,
		EMediaPipePoseLandmark::RightHip,
		EMediaPipePoseLandmark::RightKnee,
		EMediaPipePoseLandmark::RightAnkle,
	};
	const EMediaPipePoseLandmark FeetAnkles[] = {
		EMediaPipePoseLandmark::LeftAnkle,
		EMediaPipePoseLandmark::LeftHeel,
		EMediaPipePoseLandmark::LeftFootIndex,
		EMediaPipePoseLandmark::RightAnkle,
		EMediaPipePoseLandmark::RightHeel,
		EMediaPipePoseLandmark::RightFootIndex,
	};

	Diagnostics.MediaPipeOutputFps = ComputeTimestampFps(SourceCadenceSeconds);
	Diagnostics.UniquePoseTimestampFps = Diagnostics.MediaPipeOutputFps;
	Diagnostics.QualityScore = QualityScore;
	Diagnostics.MeanLandmarkConfidence = MeanConfidence;
	Diagnostics.MeanLandmarkJitter = MeanJitter;
	Diagnostics.MaxLandmarkJitter = MaxJitter;
	Diagnostics.WholePoseSpikeScore = WholePoseSpikeScore;
	Diagnostics.RootPelvisQuality = ComputeRegionQuality(Frame, RootPelvis, UE_ARRAY_COUNT(RootPelvis));
	Diagnostics.TorsoSpineQuality = ComputeRegionQuality(Frame, TorsoSpine, UE_ARRAY_COUNT(TorsoSpine));
	Diagnostics.HeadNeckQuality = ComputeRegionQuality(Frame, HeadNeck, UE_ARRAY_COUNT(HeadNeck));
	Diagnostics.ShoulderClavicleQuality = ComputeRegionQuality(Frame, Shoulders, UE_ARRAY_COUNT(Shoulders));
	Diagnostics.ArmsQuality = ComputeRegionQuality(Frame, Arms, UE_ARRAY_COUNT(Arms));
	Diagnostics.HandsWristsQuality = ComputeRegionQuality(Frame, HandsWrists, UE_ARRAY_COUNT(HandsWrists));
	Diagnostics.HipsQuality = ComputeRegionQuality(Frame, Hips, UE_ARRAY_COUNT(Hips));
	Diagnostics.LegsQuality = ComputeRegionQuality(Frame, Legs, UE_ARRAY_COUNT(Legs));
	Diagnostics.FeetAnklesQuality = ComputeRegionQuality(Frame, FeetAnkles, UE_ARRAY_COUNT(FeetAnkles));
	Diagnostics.DroppedFrameCount = DroppedFrameCount;
	Diagnostics.bTimestampDiscontinuity = Sample.bTimestampDiscontinuity ? 1 : 0;
	Diagnostics.bConfidenceCollapse = bConfidenceCollapse ? 1 : 0;
	Diagnostics.bWholePoseSpike = bWholePoseSpike ? 1 : 0;
}

FVector FMediaPipeSourceConditioner::ApplyAdaptiveOneEuroFilter(
	FAdaptiveLandmarkFilterState& State,
	const FVector& Target,
	const double QueryTimeSeconds,
	const float QualityScore,
	const float MeanJitter,
	const FMediaPipeSourceConditionerOptions& Options,
	const bool bWorld)
{
	bool& bHasValue = bWorld ? State.bHasWorld : State.bHasNormalized;
	FVector& Value = bWorld ? State.WorldValue : State.NormalizedValue;
	FVector& Derivative = bWorld ? State.WorldDerivative : State.NormalizedDerivative;
	double& LastUpdateSeconds = bWorld ? State.LastWorldUpdateSeconds : State.LastNormalizedUpdateSeconds;

	if (!bHasValue || LastUpdateSeconds < 0.0 || QueryTimeSeconds <= LastUpdateSeconds)
	{
		bHasValue = true;
		Value = Target;
		Derivative = FVector::ZeroVector;
		LastUpdateSeconds = QueryTimeSeconds;
		return Value;
	}

	const double DeltaSeconds = FMath::Clamp(QueryTimeSeconds - LastUpdateSeconds, 1.0e-4, 0.25);
	const FVector RawDerivative = (Target - Value) / DeltaSeconds;
	const float DerivativeAlpha = OneEuroAlpha(1.0f, DeltaSeconds);
	Derivative = FMath::Lerp(Derivative, RawDerivative, DerivativeAlpha);

	const float ClampedQuality = FMath::Clamp(QualityScore, 0.0f, 1.0f);
	const float QualityCutoffScale = FMath::Lerp(0.45f, 1.20f, ClampedQuality);
	const float VelocityScale = FMath::Lerp(0.35f, 1.0f, ClampedQuality);
	const float JitterCutoffScale = FMath::Lerp(1.0f, 0.45f, FMath::Clamp(MeanJitter / 0.35f, 0.0f, 1.0f));
	const float CutoffHz = FMath::Max(
		0.01f,
		Options.AdaptivePoseMinCutoff * QualityCutoffScale * JitterCutoffScale
			+ Options.AdaptivePoseBeta * Derivative.Size() * VelocityScale);
	const float ValueAlpha = OneEuroAlpha(CutoffHz, DeltaSeconds);
	Value = FMath::Lerp(Value, Target, ValueAlpha);
	LastUpdateSeconds = QueryTimeSeconds;
	return Value;
}

void FMediaPipeSourceConditioner::BuildRenderTimeFrame(
	const double QueryTimeSeconds,
	const FMediaPipeSourceConditionerOptions& Options,
	FMediaPipePoseFrame& OutFrame)
{
	if (RecentSampleCount <= 0 || !RecentSamples[0].bValid)
	{
		return;
	}

	const FPoseHistorySample& Latest = RecentSamples[0];
	const FPoseHistorySample* Previous = (RecentSampleCount > 1 && RecentSamples[1].bValid) ? &RecentSamples[1] : nullptr;
	OutFrame = Latest.Frame;
	OutFrame.bSourceConditioned = true;
	OutFrame.ConditionedQueryWallSeconds = QueryTimeSeconds;

	const float QualityPredictionScale = FMath::Clamp((Latest.QualityScore - 0.15f) / 0.85f, 0.0f, 1.0f);
	const double SourceAgeSeconds = FMath::Max(0.0, QueryTimeSeconds - Latest.ArrivalSeconds);
	const double CadenceSeconds = SourceCadenceSeconds > UE_DOUBLE_SMALL_NUMBER
		? SourceCadenceSeconds
		: Latest.SourceDeltaSeconds;
	const float CVarMaxPredictionSeconds = FMath::Max(0.0f, Options.AdaptivePoseMaxPredictionMs) * 0.001f;
	const float CadencePredictionSeconds = CadenceSeconds > UE_DOUBLE_SMALL_NUMBER
		? static_cast<float>(CadenceSeconds * 1.25)
		: CVarMaxPredictionSeconds;
	const float MaxPredictionSeconds = FMath::Min(CVarMaxPredictionSeconds, CadencePredictionSeconds) * QualityPredictionScale;
	const bool bCanPredict = Options.bAdaptivePosePrediction
		&& Previous
		&& Previous->Frame.TimestampUs < Latest.Frame.TimestampUs
		&& MaxPredictionSeconds > UE_SMALL_NUMBER;
	const float PredictionHorizonSeconds = bCanPredict
		? FMath::Clamp(static_cast<float>(SourceAgeSeconds), 0.0f, MaxPredictionSeconds)
		: 0.0f;
	const float PredictionHorizonRatio = MaxPredictionSeconds > UE_SMALL_NUMBER
		? FMath::Clamp(PredictionHorizonSeconds / MaxPredictionSeconds, 0.0f, 1.0f)
		: 0.0f;
	const double VelocityDeltaSeconds = bCanPredict
		? FMath::Max(
			static_cast<double>(Latest.Frame.TimestampUs - Previous->Frame.TimestampUs) * 1.0e-6,
			UE_DOUBLE_SMALL_NUMBER)
		: 0.0;

	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		const EMediaPipePoseLandmark Landmark = static_cast<EMediaPipePoseLandmark>(Index);
		FVector WorldTarget = GetLandmarkVector(Latest.Frame.World, Landmark);
		FVector NormalizedTarget = GetLandmarkVector(Latest.Frame.Normalized, Landmark);

		if (PredictionHorizonSeconds > UE_SMALL_NUMBER && Previous)
		{
			const FVector PreviousWorldTarget = GetLandmarkVector(Previous->Frame.World, Landmark);
			const FVector PreviousNormalizedTarget = GetLandmarkVector(Previous->Frame.Normalized, Landmark);
			const float LandmarkConfidence = ClampReliability(GetLandmarkReliability(Latest.Frame, Landmark));
			const float LandmarkPredictionScale =
				QualityPredictionScale
				* FMath::Clamp((LandmarkConfidence - 0.05f) / 0.95f, 0.0f, 1.0f)
				* FMath::Lerp(1.0f, 0.65f, Latest.WholePoseSpikeScore);
			const FVector WorldVelocity = (WorldTarget - PreviousWorldTarget) / VelocityDeltaSeconds;
			const FVector NormalizedVelocity = (NormalizedTarget - PreviousNormalizedTarget) / VelocityDeltaSeconds;
			WorldTarget += WorldVelocity * PredictionHorizonSeconds * LandmarkPredictionScale;
			NormalizedTarget += NormalizedVelocity * PredictionHorizonSeconds * LandmarkPredictionScale;
		}

		if (Options.bAdaptivePoseConditioning)
		{
			WorldTarget = ApplyAdaptiveOneEuroFilter(
				AdaptiveFilters[Index],
				WorldTarget,
				QueryTimeSeconds,
				Latest.QualityScore,
				Latest.MeanJitter,
				Options,
				true);
			NormalizedTarget = ApplyAdaptiveOneEuroFilter(
				AdaptiveFilters[Index],
				NormalizedTarget,
				QueryTimeSeconds,
				Latest.QualityScore,
				Latest.MeanJitter,
				Options,
				false);
		}

		SetLandmarkVector(OutFrame.World, Landmark, WorldTarget);
		SetLandmarkVector(OutFrame.Normalized, Landmark, NormalizedTarget);

		const float ConfidenceScale =
			FMath::Clamp(0.55f + 0.45f * Latest.QualityScore, 0.35f, 1.0f)
			* (1.0f - 0.25f * PredictionHorizonRatio);
		ScaleLandmarkConfidence(OutFrame.World.Points[Index], ConfidenceScale);
		ScaleLandmarkConfidence(OutFrame.Normalized.Points[Index], ConfidenceScale);
	}

	FMediaPipePoseFrame::FConditioningDiagnostics& Diagnostics = OutFrame.ConditioningDiagnostics;
	Diagnostics.SourceAgeMs = static_cast<float>(SourceAgeSeconds * 1000.0);
	Diagnostics.PredictionHorizonMs = PredictionHorizonSeconds * 1000.0f;
	Diagnostics.MaxPredictionHorizonMs = MaxPredictionSeconds * 1000.0f;
	Diagnostics.EffectiveAddedLatencyMs = 0.0f;
	Diagnostics.MediaPipeOutputFps = ComputeTimestampFps(CadenceSeconds);
	Diagnostics.UniquePoseTimestampFps = Diagnostics.MediaPipeOutputFps;
	Diagnostics.RepeatedPoseRunLength = RepeatedFrameRunLength;
	Diagnostics.DroppedFrameCount = DroppedFrameCount;
	Diagnostics.bPredicted = PredictionHorizonSeconds > UE_SMALL_NUMBER ? 1 : 0;
	Diagnostics.bRepeatedPose = RepeatedFrameRunLength > 0 ? 1 : 0;

	if (Options.bAdaptivePoseLog)
	{
		if (LastAdaptivePoseLogSeconds < 0.0 || QueryTimeSeconds - LastAdaptivePoseLogSeconds >= 1.0)
		{
			LastAdaptivePoseLogSeconds = QueryTimeSeconds;
			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.AdaptivePoseConditioning: poseTs=%lld sourceFps=%.2f uniqueFps=%.2f repeatedRun=%d dropped=%d ageMs=%.1f predictMs=%.1f maxPredictMs=%.1f quality=%.3f confidence=%.3f jitterMean=%.3f jitterMax=%.3f spike=%.3f latencyMs=%.1f predicted=%d"),
				static_cast<long long>(OutFrame.TimestampUs),
				Diagnostics.MediaPipeOutputFps,
				Diagnostics.UniquePoseTimestampFps,
				Diagnostics.RepeatedPoseRunLength,
				Diagnostics.DroppedFrameCount,
				Diagnostics.SourceAgeMs,
				Diagnostics.PredictionHorizonMs,
				Diagnostics.MaxPredictionHorizonMs,
				Diagnostics.QualityScore,
				Diagnostics.MeanLandmarkConfidence,
				Diagnostics.MeanLandmarkJitter,
				Diagnostics.MaxLandmarkJitter,
				Diagnostics.WholePoseSpikeScore,
				Diagnostics.EffectiveAddedLatencyMs,
				Diagnostics.bPredicted);
		}
	}
}

bool FMediaPipeSourceConditioner::IsLandmarkReliable(
	const FMediaPipePoseFrame& Frame,
	const EMediaPipePoseLandmark Landmark,
	const FMediaPipeSourceConditionerOptions& Options) const
{
	if (Options.MinLandmarkReliability <= 0.0f)
	{
		return true;
	}
	return GetLandmarkReliability(Frame, Landmark) >= Options.MinLandmarkReliability;
}

void FMediaPipeSourceConditioner::ApplyHoldAndSmoothing(
	FMediaPipePoseFrame& Frame,
	const double DeltaSeconds,
	const FMediaPipeSourceConditionerOptions& Options)
{
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		const EMediaPipePoseLandmark Landmark = static_cast<EMediaPipePoseLandmark>(Index);
		const bool bReliable = IsLandmarkReliable(Frame, Landmark, Options);
		if (!bReliable && Options.bHoldBadLandmarks && bHasPreviousLandmark[Index])
		{
			Frame.World.Points[Index] = PreviousWorld[Index];
			Frame.Normalized.Points[Index] = PreviousNormalized[Index];
			Frame.World.Points[Index].Reliability = 0.0f;
			Frame.Normalized.Points[Index].Reliability = 0.0f;
			continue;
		}

		if (!Options.bSmoothLandmarks || !bReliable || !bHasPreviousLandmark[Index])
		{
			continue;
		}

		const FVector PreviousWorldPoint = GetLandmarkVector(PreviousWorld, Landmark);
		const FVector CurrentWorldPoint = GetLandmarkVector(Frame.World, Landmark);
		const float SpeedMps = DeltaSeconds > UE_DOUBLE_SMALL_NUMBER
			? static_cast<float>((CurrentWorldPoint - PreviousWorldPoint).Size() / DeltaSeconds)
			: 0.0f;
		const float Alpha = ComputeAdaptiveAlpha(
			Options.LandmarkSmoothingHalfLifeSeconds,
			Options.LandmarkSmoothingFastSpeedMps,
			SpeedMps,
			DeltaSeconds);

		SetLandmarkVector(Frame.World, Landmark, FMath::Lerp(PreviousWorldPoint, CurrentWorldPoint, Alpha));

		const FVector PreviousNormalizedPoint = GetLandmarkVector(PreviousNormalized, Landmark);
		const FVector CurrentNormalizedPoint = GetLandmarkVector(Frame.Normalized, Landmark);
		SetLandmarkVector(Frame.Normalized, Landmark, FMath::Lerp(PreviousNormalizedPoint, CurrentNormalizedPoint, Alpha));
	}
}

void FMediaPipeSourceConditioner::ApplyAdaptiveSegmentLengths(
	FMediaPipePoseFrame& Frame,
	const FMediaPipeSourceConditionerOptions& Options)
{
	if (!Options.bAdaptiveSegmentLengths)
	{
		return;
	}

	const TArray<FWidthDef>& WidthDefs = GetWidthDefs();
	if (WidthLengthStates.Num() != WidthDefs.Num())
	{
		WidthLengthStates.SetNum(WidthDefs.Num());
	}

	for (int32 DefIndex = 0; DefIndex < WidthDefs.Num(); ++DefIndex)
	{
		const FWidthDef& Def = WidthDefs[DefIndex];
		FLengthState& State = WidthLengthStates[DefIndex];
		const FVector Left = GetLandmarkVector(Frame.World, Def.Left);
		const FVector Right = GetLandmarkVector(Frame.World, Def.Right);
		const FVector Across = Right - Left;
		const float Length = Across.Size();
		if (Length <= UE_SMALL_NUMBER)
		{
			continue;
		}

		if (IsLandmarkReliable(Frame, Def.Left, Options) && IsLandmarkReliable(Frame, Def.Right, Options))
		{
			if (!State.bHasTarget)
			{
				State.TargetLength = Length;
				State.bHasTarget = true;
			}
			else
			{
				State.TargetLength = FMath::Lerp(State.TargetLength, Length, Options.SegmentLengthAdaptAlpha);
			}
		}

		if (!State.bHasTarget)
		{
			continue;
		}

		const FVector AcrossDir = Across / Length;
		const FVector Center = (Left + Right) * 0.5f;
		const FVector TargetLeft = Center - AcrossDir * (State.TargetLength * 0.5f);
		const FVector TargetRight = Center + AcrossDir * (State.TargetLength * 0.5f);
		OffsetLandmarkSet(Frame, Def.LeftDescendants, TargetLeft - Left);
		OffsetLandmarkSet(Frame, Def.RightDescendants, TargetRight - Right);
	}

	const TArray<FSegmentDef>& SegmentDefs = GetSegmentDefs();
	if (SegmentLengthStates.Num() != SegmentDefs.Num())
	{
		SegmentLengthStates.SetNum(SegmentDefs.Num());
	}

	for (int32 DefIndex = 0; DefIndex < SegmentDefs.Num(); ++DefIndex)
	{
		const FSegmentDef& Def = SegmentDefs[DefIndex];
		FLengthState& State = SegmentLengthStates[DefIndex];
		const FVector Parent = GetLandmarkVector(Frame.World, Def.Parent);
		const FVector Child = GetLandmarkVector(Frame.World, Def.Child);
		const FVector Segment = Child - Parent;
		const float Length = Segment.Size();
		if (Length <= UE_SMALL_NUMBER)
		{
			continue;
		}

		if (IsLandmarkReliable(Frame, Def.Parent, Options) && IsLandmarkReliable(Frame, Def.Child, Options))
		{
			if (!State.bHasTarget)
			{
				State.TargetLength = Length;
				State.bHasTarget = true;
			}
			else
			{
				State.TargetLength = FMath::Lerp(State.TargetLength, Length, Options.SegmentLengthAdaptAlpha);
			}
		}

		if (!State.bHasTarget)
		{
			continue;
		}

		const FVector TargetChild = Parent + (Segment / Length) * State.TargetLength;
		OffsetLandmarkSet(Frame, Def.Descendants, TargetChild - Child);
	}
}

bool FMediaPipeSourceConditioner::BuildTorsoBasis(const FMediaPipePoseFrame& Frame, FTorsoBasis& OutBasis) const
{
	const FVector LeftHip = GetLandmarkVector(Frame.World, EMediaPipePoseLandmark::LeftHip);
	const FVector RightHip = GetLandmarkVector(Frame.World, EMediaPipePoseLandmark::RightHip);
	const FVector LeftShoulder = GetLandmarkVector(Frame.World, EMediaPipePoseLandmark::LeftShoulder);
	const FVector RightShoulder = GetLandmarkVector(Frame.World, EMediaPipePoseLandmark::RightShoulder);
	const FVector HipMid = (LeftHip + RightHip) * 0.5f;
	const FVector ShoulderMid = (LeftShoulder + RightShoulder) * 0.5f;

	FVector AxisX = (RightHip - LeftHip).GetSafeNormal();
	if (AxisX.IsNearlyZero())
	{
		AxisX = (RightShoulder - LeftShoulder).GetSafeNormal();
	}
	const FVector UpSeed = (ShoulderMid - HipMid).GetSafeNormal();
	if (AxisX.IsNearlyZero() || UpSeed.IsNearlyZero())
	{
		return false;
	}

	FVector AxisZ = FVector::CrossProduct(AxisX, UpSeed).GetSafeNormal();
	if (AxisZ.IsNearlyZero())
	{
		return false;
	}
	FVector AxisY = FVector::CrossProduct(AxisZ, AxisX).GetSafeNormal();
	if (AxisY.IsNearlyZero())
	{
		return false;
	}

	OutBasis.Root = HipMid;
	OutBasis.AxisX = AxisX;
	OutBasis.AxisY = AxisY;
	OutBasis.AxisZ = AxisZ;
	return true;
}

FVector FMediaPipeSourceConditioner::ToTorsoLocal(const FTorsoBasis& Basis, const FVector& WorldPoint) const
{
	const FVector Delta = WorldPoint - Basis.Root;
	return FVector(
		FVector::DotProduct(Delta, Basis.AxisX),
		FVector::DotProduct(Delta, Basis.AxisY),
		FVector::DotProduct(Delta, Basis.AxisZ));
}

FVector FMediaPipeSourceConditioner::FromTorsoLocal(const FTorsoBasis& Basis, const FVector& LocalPoint) const
{
	return Basis.Root
		+ Basis.AxisX * LocalPoint.X
		+ Basis.AxisY * LocalPoint.Y
		+ Basis.AxisZ * LocalPoint.Z;
}

void FMediaPipeSourceConditioner::UpdateShoulderGirdleAnchor(
	FMediaPipePoseFrame& Frame,
	const FTorsoBasis& Basis,
	const FMediaPipeSourceConditionerOptions& Options)
{
	const FVector LeftShoulderLocal = ToTorsoLocal(Basis, GetLandmarkVector(Frame.World, EMediaPipePoseLandmark::LeftShoulder));
	const FVector RightShoulderLocal = ToTorsoLocal(Basis, GetLandmarkVector(Frame.World, EMediaPipePoseLandmark::RightShoulder));

	if (!ShoulderGirdleState.bHasAnchor)
	{
		ShoulderGirdleState.LeftShoulderLocal = LeftShoulderLocal;
		ShoulderGirdleState.RightShoulderLocal = RightShoulderLocal;
		ShoulderGirdleState.bHasAnchor = true;
		return;
	}

	ShoulderGirdleState.LeftShoulderLocal = FMath::Lerp(
		ShoulderGirdleState.LeftShoulderLocal,
		LeftShoulderLocal,
		Options.SegmentLengthAdaptAlpha);
	ShoulderGirdleState.RightShoulderLocal = FMath::Lerp(
		ShoulderGirdleState.RightShoulderLocal,
		RightShoulderLocal,
		Options.SegmentLengthAdaptAlpha);
}

void FMediaPipeSourceConditioner::ApplyShoulderGirdleReconstruction(
	FMediaPipePoseFrame& Frame,
	const FTorsoBasis& Basis,
	const FMediaPipeSourceConditionerOptions& Options)
{
	if (!ShoulderGirdleState.bHasAnchor || ShoulderGirdleState.Alpha <= UE_SMALL_NUMBER)
	{
		return;
	}

	const float Alpha = FMath::Clamp(ShoulderGirdleState.Alpha * Options.OcclusionShoulderReconstructWeight, 0.0f, 1.0f);
	if (Alpha <= UE_SMALL_NUMBER)
	{
		return;
	}

	const FVector RawLeftShoulder = GetLandmarkVector(Frame.World, EMediaPipePoseLandmark::LeftShoulder);
	const FVector RawRightShoulder = GetLandmarkVector(Frame.World, EMediaPipePoseLandmark::RightShoulder);
	const FVector TargetLeftShoulder = FMath::Lerp(
		RawLeftShoulder,
		FromTorsoLocal(Basis, ShoulderGirdleState.LeftShoulderLocal),
		Alpha);
	const FVector TargetRightShoulder = FMath::Lerp(
		RawRightShoulder,
		FromTorsoLocal(Basis, ShoulderGirdleState.RightShoulderLocal),
		Alpha);
	const FVector LeftShoulderOffset = TargetLeftShoulder - RawLeftShoulder;
	const FVector RightShoulderOffset = TargetRightShoulder - RawRightShoulder;

	SetLandmarkVector(Frame.World, EMediaPipePoseLandmark::LeftShoulder, TargetLeftShoulder);
	SetLandmarkVector(Frame.World, EMediaPipePoseLandmark::RightShoulder, TargetRightShoulder);

	OffsetLandmarkSet(
		Frame,
		{ EMediaPipePoseLandmark::LeftElbow },
		LeftShoulderOffset * Options.OcclusionShoulderReconstructElbowFollowWeight);
	OffsetLandmarkSet(
		Frame,
		{ EMediaPipePoseLandmark::RightElbow },
		RightShoulderOffset * Options.OcclusionShoulderReconstructElbowFollowWeight);
	OffsetLandmarkSet(
		Frame,
		{
			EMediaPipePoseLandmark::LeftWrist,
			EMediaPipePoseLandmark::LeftPinky,
			EMediaPipePoseLandmark::LeftIndex,
			EMediaPipePoseLandmark::LeftThumb,
		},
		LeftShoulderOffset * Options.OcclusionShoulderReconstructWristFollowWeight);
	OffsetLandmarkSet(
		Frame,
		{
			EMediaPipePoseLandmark::RightWrist,
			EMediaPipePoseLandmark::RightPinky,
			EMediaPipePoseLandmark::RightIndex,
			EMediaPipePoseLandmark::RightThumb,
		},
		RightShoulderOffset * Options.OcclusionShoulderReconstructWristFollowWeight);

	const float ShoulderHalfWidth = FMath::Max(
		FMath::Abs(ShoulderGirdleState.RightShoulderLocal.X - ShoulderGirdleState.LeftShoulderLocal.X) * 0.5f,
		UE_SMALL_NUMBER);
	auto ApplyArmSideRail = [&](const bool bIsLeft)
	{
		const float SideSign = bIsLeft ? -1.0f : 1.0f;
		const EMediaPipePoseLandmark Elbow = bIsLeft ? EMediaPipePoseLandmark::LeftElbow : EMediaPipePoseLandmark::RightElbow;
		const EMediaPipePoseLandmark Wrist = bIsLeft ? EMediaPipePoseLandmark::LeftWrist : EMediaPipePoseLandmark::RightWrist;
		const EMediaPipePoseLandmark Pinky = bIsLeft ? EMediaPipePoseLandmark::LeftPinky : EMediaPipePoseLandmark::RightPinky;
		const EMediaPipePoseLandmark Index = bIsLeft ? EMediaPipePoseLandmark::LeftIndex : EMediaPipePoseLandmark::RightIndex;
		const EMediaPipePoseLandmark Thumb = bIsLeft ? EMediaPipePoseLandmark::LeftThumb : EMediaPipePoseLandmark::RightThumb;

		const FVector ElbowWorld = GetLandmarkVector(Frame.World, Elbow);
		const FVector WristWorld = GetLandmarkVector(Frame.World, Wrist);
		FVector ElbowLocal = ToTorsoLocal(Basis, ElbowWorld);
		FVector WristLocal = ToTorsoLocal(Basis, WristWorld);

		const float MinElbowSide = ShoulderHalfWidth * 0.62f;
		const float MinWristSide = ShoulderHalfWidth * 0.44f;
		const float ElbowSide = ElbowLocal.X * SideSign;
		const float WristSide = WristLocal.X * SideSign;

		if (ElbowSide < MinElbowSide)
		{
			ElbowLocal.X += SideSign * (MinElbowSide - ElbowSide);
			const FVector TargetElbow = FromTorsoLocal(Basis, ElbowLocal);
			SetLandmarkVector(
				Frame.World,
				Elbow,
				FMath::Lerp(ElbowWorld, TargetElbow, FMath::Clamp(ShoulderGirdleState.Alpha * 0.75f, 0.0f, 1.0f)));
		}

		if (WristSide < MinWristSide)
		{
			WristLocal.X += SideSign * (MinWristSide - WristSide);
			const FVector TargetWrist = FromTorsoLocal(Basis, WristLocal);
			const FVector CurrentWrist = GetLandmarkVector(Frame.World, Wrist);
			const FVector NewWrist = FMath::Lerp(CurrentWrist, TargetWrist, FMath::Clamp(ShoulderGirdleState.Alpha * 0.45f, 0.0f, 1.0f));
			SetLandmarkVector(Frame.World, Wrist, NewWrist);
			OffsetLandmarkSet(Frame, { Pinky, Index, Thumb }, NewWrist - CurrentWrist);
		}
	};

	ApplyArmSideRail(true);
	ApplyArmSideRail(false);
}

void FMediaPipeSourceConditioner::UpdateArmAnchor(FMediaPipePoseFrame& Frame, const FTorsoBasis& Basis, const bool bIsLeft)
{
	FArmOcclusionHoldState& State = ArmHoldStates[bIsLeft ? 0 : 1];
	const EMediaPipePoseLandmark Shoulder = bIsLeft ? EMediaPipePoseLandmark::LeftShoulder : EMediaPipePoseLandmark::RightShoulder;
	const EMediaPipePoseLandmark Elbow = bIsLeft ? EMediaPipePoseLandmark::LeftElbow : EMediaPipePoseLandmark::RightElbow;
	const EMediaPipePoseLandmark Wrist = bIsLeft ? EMediaPipePoseLandmark::LeftWrist : EMediaPipePoseLandmark::RightWrist;
	State.ShoulderLocal = ToTorsoLocal(Basis, GetLandmarkVector(Frame.World, Shoulder));
	State.ElbowLocal = ToTorsoLocal(Basis, GetLandmarkVector(Frame.World, Elbow));
	State.WristLocal = ToTorsoLocal(Basis, GetLandmarkVector(Frame.World, Wrist));
	State.bHasAnchor = true;
}

void FMediaPipeSourceConditioner::ApplyArmHold(
	FMediaPipePoseFrame& Frame,
	const FTorsoBasis& Basis,
	const bool bIsLeft,
	const float Alpha,
	const FMediaPipeSourceConditionerOptions& Options)
{
	if (Alpha <= UE_SMALL_NUMBER)
	{
		return;
	}

	FArmOcclusionHoldState& State = ArmHoldStates[bIsLeft ? 0 : 1];
	if (!State.bHasAnchor)
	{
		return;
	}

	const EMediaPipePoseLandmark Shoulder = bIsLeft ? EMediaPipePoseLandmark::LeftShoulder : EMediaPipePoseLandmark::RightShoulder;
	const EMediaPipePoseLandmark Elbow = bIsLeft ? EMediaPipePoseLandmark::LeftElbow : EMediaPipePoseLandmark::RightElbow;
	const EMediaPipePoseLandmark Wrist = bIsLeft ? EMediaPipePoseLandmark::LeftWrist : EMediaPipePoseLandmark::RightWrist;
	const EMediaPipePoseLandmark Pinky = bIsLeft ? EMediaPipePoseLandmark::LeftPinky : EMediaPipePoseLandmark::RightPinky;
	const EMediaPipePoseLandmark Index = bIsLeft ? EMediaPipePoseLandmark::LeftIndex : EMediaPipePoseLandmark::RightIndex;
	const EMediaPipePoseLandmark Thumb = bIsLeft ? EMediaPipePoseLandmark::LeftThumb : EMediaPipePoseLandmark::RightThumb;

	const FVector RawShoulder = GetLandmarkVector(Frame.World, Shoulder);
	const FVector RawElbow = GetLandmarkVector(Frame.World, Elbow);
	const FVector RawWrist = GetLandmarkVector(Frame.World, Wrist);
	const FVector HeldShoulder = FromTorsoLocal(Basis, State.ShoulderLocal);
	const FVector HeldElbow = FromTorsoLocal(Basis, State.ElbowLocal);
	const FVector HeldWrist = FromTorsoLocal(Basis, State.WristLocal);
	const FVector TargetShoulder = FMath::Lerp(RawShoulder, HeldShoulder, FMath::Clamp(Alpha * Options.OcclusionArmHoldShoulderWeight, 0.0f, 1.0f));
	const FVector TargetElbow = FMath::Lerp(RawElbow, HeldElbow, FMath::Clamp(Alpha * Options.OcclusionArmHoldElbowWeight, 0.0f, 1.0f));
	const FVector TargetWrist = FMath::Lerp(RawWrist, HeldWrist, FMath::Clamp(Alpha * Options.OcclusionArmHoldWristWeight, 0.0f, 1.0f));
	const FVector WristOffset = TargetWrist - RawWrist;

	SetLandmarkVector(Frame.World, Shoulder, TargetShoulder);
	SetLandmarkVector(Frame.World, Elbow, TargetElbow);
	SetLandmarkVector(Frame.World, Wrist, TargetWrist);
	OffsetLandmarkSet(Frame, { Pinky, Index, Thumb }, WristOffset);
}

void FMediaPipeSourceConditioner::ApplyOcclusionArmHold(
	FMediaPipePoseFrame& Frame,
	const double DeltaSeconds,
	const FMediaPipeSourceConditionerOptions& Options)
{
	if (!Options.bOcclusionArmHold)
	{
		return;
	}

	const FVector2D LS = GetNormalizedPoint(Frame, EMediaPipePoseLandmark::LeftShoulder);
	const FVector2D RS = GetNormalizedPoint(Frame, EMediaPipePoseLandmark::RightShoulder);
	const FVector2D LE = GetNormalizedPoint(Frame, EMediaPipePoseLandmark::LeftElbow);
	const FVector2D RE = GetNormalizedPoint(Frame, EMediaPipePoseLandmark::RightElbow);
	const FVector2D LW = GetNormalizedPoint(Frame, EMediaPipePoseLandmark::LeftWrist);
	const FVector2D RW = GetNormalizedPoint(Frame, EMediaPipePoseLandmark::RightWrist);
	const FVector2D LH = GetNormalizedPoint(Frame, EMediaPipePoseLandmark::LeftHip);
	const FVector2D RH = GetNormalizedPoint(Frame, EMediaPipePoseLandmark::RightHip);
	const FVector2D ShoulderMid = Midpoint2D(LS, RS);
	const FVector2D HipMid = Midpoint2D(LH, RH);
	const float TorsoHeight = FMath::Max(Distance2D(ShoulderMid, HipMid), UE_SMALL_NUMBER);
	const float ShoulderRatio = Distance2D(LS, RS) / TorsoHeight;

	if (!bHasReferenceShoulderRatio)
	{
		ReferenceShoulderWidthRatio = ShoulderRatio >= 0.36f ? FMath::Clamp(ShoulderRatio, 0.36f, 0.72f) : 0.42f;
		bHasReferenceShoulderRatio = true;
	}

	const float SideViewScore = FMath::Clamp(
		(ReferenceShoulderWidthRatio * 0.75f - ShoulderRatio) / FMath::Max(ReferenceShoulderWidthRatio * 0.45f, UE_SMALL_NUMBER),
		0.0f,
		1.0f);
	const float ReliabilityScore = FMath::Clamp(
		(0.94f - FMath::Min3(
			GetLandmarkReliability(Frame, EMediaPipePoseLandmark::LeftShoulder),
			GetLandmarkReliability(Frame, EMediaPipePoseLandmark::LeftElbow),
			GetLandmarkReliability(Frame, EMediaPipePoseLandmark::LeftWrist))) / 0.34f,
		0.0f,
		1.0f);
	const float LeftOverlap = TorsoOverlapScore2D(LE, LW, LS, RS, LH, RH, ShoulderMid, HipMid, TorsoHeight);
	const float RightOverlap = TorsoOverlapScore2D(RE, RW, LS, RS, LH, RH, ShoulderMid, HipMid, TorsoHeight);

	const float LeftScore = 1.35f * LeftOverlap + 1.10f * SideViewScore + 0.55f * ReliabilityScore;
	const float RightReliabilityScore = FMath::Clamp(
		(0.94f - FMath::Min3(
			GetLandmarkReliability(Frame, EMediaPipePoseLandmark::RightShoulder),
			GetLandmarkReliability(Frame, EMediaPipePoseLandmark::RightElbow),
			GetLandmarkReliability(Frame, EMediaPipePoseLandmark::RightWrist))) / 0.34f,
		0.0f,
		1.0f);
	const float RightScore = 1.35f * RightOverlap + 1.10f * SideViewScore + 0.55f * RightReliabilityScore;
	const float MaxArmScore = FMath::Max(LeftScore, RightScore);

	FTorsoBasis Basis;
	if (!BuildTorsoBasis(Frame, Basis))
	{
		return;
	}

	const bool bShoulderCollapseEvidence = SideViewScore >= 0.25f || ShoulderRatio < ReferenceShoulderWidthRatio * 0.70f;
	if (Options.bOcclusionShoulderReconstruct && !ShoulderGirdleState.bHasAnchor && (bShoulderCollapseEvidence || MaxArmScore >= Options.OcclusionShoulderReconstructReleaseScore))
	{
		const FVector LeftHipWorld = GetLandmarkVector(Frame.World, EMediaPipePoseLandmark::LeftHip);
		const FVector RightHipWorld = GetLandmarkVector(Frame.World, EMediaPipePoseLandmark::RightHip);
		const FVector LeftShoulderWorld = GetLandmarkVector(Frame.World, EMediaPipePoseLandmark::LeftShoulder);
		const FVector RightShoulderWorld = GetLandmarkVector(Frame.World, EMediaPipePoseLandmark::RightShoulder);
		const FVector LeftShoulderLocal = ToTorsoLocal(Basis, LeftShoulderWorld);
		const FVector RightShoulderLocal = ToTorsoLocal(Basis, RightShoulderWorld);
		const FVector ShoulderMidLocal = (LeftShoulderLocal + RightShoulderLocal) * 0.5f;
		const float HipWidth = (RightHipWorld - LeftHipWorld).Size();
		const float CurrentShoulderWidth = (RightShoulderWorld - LeftShoulderWorld).Size();
		float TargetShoulderWidth = FMath::Max(CurrentShoulderWidth, HipWidth * 1.12f);
		if (WidthLengthStates.Num() > 0 && WidthLengthStates[0].bHasTarget)
		{
			TargetShoulderWidth = FMath::Max(TargetShoulderWidth, WidthLengthStates[0].TargetLength);
		}
		ShoulderGirdleState.LeftShoulderLocal = ShoulderMidLocal - FVector(TargetShoulderWidth * 0.5f, 0.0f, 0.0f);
		ShoulderGirdleState.RightShoulderLocal = ShoulderMidLocal + FVector(TargetShoulderWidth * 0.5f, 0.0f, 0.0f);
		ShoulderGirdleState.bHasAnchor = true;
	}

	if (Options.bOcclusionShoulderReconstruct)
	{
		const bool bAcquireShoulder = ShoulderGirdleState.bHasAnchor
			&& (MaxArmScore >= Options.OcclusionShoulderReconstructAcquireScore
				|| (bShoulderCollapseEvidence && MaxArmScore >= Options.OcclusionShoulderReconstructReleaseScore));
		const bool bReleaseShoulder = MaxArmScore <= Options.OcclusionShoulderReconstructReleaseScore && SideViewScore < 0.15f;

		if (bAcquireShoulder)
		{
			ShoulderGirdleState.AcquireCount++;
			ShoulderGirdleState.ReleaseCount = 0;
		}
		else if (bReleaseShoulder)
		{
			ShoulderGirdleState.ReleaseCount++;
			ShoulderGirdleState.AcquireCount = 0;
		}
		else
		{
			ShoulderGirdleState.AcquireCount = 0;
			ShoulderGirdleState.ReleaseCount = 0;
		}

		if (!ShoulderGirdleState.bActive && ShoulderGirdleState.AcquireCount >= Options.OcclusionArmHoldAcquireFrames)
		{
			ShoulderGirdleState.bActive = true;
		}
		if (ShoulderGirdleState.bActive && ShoulderGirdleState.ReleaseCount >= Options.OcclusionArmHoldReleaseFrames)
		{
			ShoulderGirdleState.bActive = false;
		}

		const float ShoulderTargetAlpha = ShoulderGirdleState.bActive ? 1.0f : 0.0f;
		const float ShoulderBlendAlpha = SmoothStepAlpha(
			ShoulderGirdleState.bActive ? Options.OcclusionArmHoldBlendInHalfLifeSeconds : Options.OcclusionArmHoldBlendOutHalfLifeSeconds,
			DeltaSeconds);
		ShoulderGirdleState.Alpha = FMath::Lerp(ShoulderGirdleState.Alpha, ShoulderTargetAlpha, ShoulderBlendAlpha);
		ApplyShoulderGirdleReconstruction(Frame, Basis, Options);

		if (ShoulderGirdleState.Alpha > UE_SMALL_NUMBER)
		{
			BuildTorsoBasis(Frame, Basis);
		}
	}

	auto UpdateSide = [&](const bool bIsLeft, const float Score)
	{
		FArmOcclusionHoldState& State = ArmHoldStates[bIsLeft ? 0 : 1];
		if (Score >= Options.OcclusionArmHoldAcquireScore && State.bHasAnchor)
		{
			State.AcquireCount++;
			State.ReleaseCount = 0;
		}
		else if (Score <= Options.OcclusionArmHoldReleaseScore)
		{
			State.ReleaseCount++;
			State.AcquireCount = 0;
		}
		else
		{
			State.AcquireCount = 0;
			State.ReleaseCount = 0;
		}

		if (!State.bActive && State.AcquireCount >= Options.OcclusionArmHoldAcquireFrames)
		{
			State.bActive = true;
		}
		if (State.bActive && State.ReleaseCount >= Options.OcclusionArmHoldReleaseFrames)
		{
			State.bActive = false;
		}

		const float TargetAlpha = State.bActive ? 1.0f : 0.0f;
		const float BlendAlpha = SmoothStepAlpha(
			State.bActive ? Options.OcclusionArmHoldBlendInHalfLifeSeconds : Options.OcclusionArmHoldBlendOutHalfLifeSeconds,
			DeltaSeconds);
		State.HoldAlpha = FMath::Lerp(State.HoldAlpha, TargetAlpha, BlendAlpha);
		ApplyArmHold(Frame, Basis, bIsLeft, State.HoldAlpha, Options);

		if (!State.bActive && State.HoldAlpha <= 0.05f && Score < Options.OcclusionArmHoldAcquireScore)
		{
			UpdateArmAnchor(Frame, Basis, bIsLeft);
		}
	};

	UpdateSide(true, LeftScore);
	UpdateSide(false, RightScore);

	const bool bShoulderLooksOpen = SideViewScore < 0.20f
		&& LeftScore < Options.OcclusionArmHoldReleaseScore
		&& RightScore < Options.OcclusionArmHoldReleaseScore;
	if (bShoulderLooksOpen)
	{
		UpdateShoulderGirdleAnchor(Frame, Basis, Options);
		ReferenceShoulderWidthRatio = FMath::Lerp(ReferenceShoulderWidthRatio, ShoulderRatio, Options.SegmentLengthAdaptAlpha);
	}
	else if (!ShoulderGirdleState.bHasAnchor && MaxArmScore < Options.OcclusionShoulderReconstructAcquireScore)
	{
		UpdateShoulderGirdleAnchor(Frame, Basis, Options);
	}
}

void FMediaPipeSourceConditioner::ApplyFootForwardHemisphere(
	FMediaPipePoseFrame& Frame,
	const float WorldScaleCm,
	const bool bMirrorLandmarksLR,
	const FMediaPipeSourceConditionerOptions& Options)
{
	if (!Options.bFootForwardHemisphere)
	{
		return;
	}

	FMediaPipeSolvedPoseOptions SolvedOptions = MediaPipeSolvedPose::MakeDefaultOptions(WorldScaleCm, bMirrorLandmarksLR);
	SolvedOptions.bConstrainLegSource = false;

	FMediaPipeSolvedPose SolvedPose;
	if (!MediaPipeSolvedPose::BuildLocal(Frame, SolvedOptions, SolvedPose) || !SolvedPose.bHasTorsoBasis)
	{
		return;
	}

	const FVector ForwardHint = (SolvedPose.ForwardLocal - FVector::DotProduct(SolvedPose.ForwardLocal, FVector::UpVector) * FVector::UpVector).GetSafeNormal();
	auto ApplyFoot = [&](const bool bIsLeft)
	{
		const EMediaPipePoseLandmark AnkleLm = bIsLeft ? EMediaPipePoseLandmark::LeftAnkle : EMediaPipePoseLandmark::RightAnkle;
		const EMediaPipePoseLandmark ToeLm = bIsLeft ? EMediaPipePoseLandmark::LeftFootIndex : EMediaPipePoseLandmark::RightFootIndex;
		if (!IsLandmarkReliable(Frame, AnkleLm, Options) || !IsLandmarkReliable(Frame, ToeLm, Options))
		{
			return;
		}

		const int32 FootIndex = bIsLeft ? 0 : 1;
		const FVector AnkleLocal = SolvedPose.LandmarksLocal[LandmarkIndex(AnkleLm)];
		const FVector ToeLocal = SolvedPose.LandmarksLocal[LandmarkIndex(ToeLm)];
		const FVector RawFoot = ToeLocal - AnkleLocal;
		const FVector RawForward = RawFoot.GetSafeNormal();
		if (RawForward.IsNearlyZero())
		{
			return;
		}

		FVector TargetForward = RawForward;
		const FVector ForwardProbe = (TargetForward - FVector::DotProduct(TargetForward, FVector::UpVector) * FVector::UpVector).GetSafeNormal();
		if (!ForwardHint.IsNearlyZero() && !ForwardProbe.IsNearlyZero() && FVector::DotProduct(ForwardProbe, ForwardHint) < 0.0f)
		{
			TargetForward *= -1.0f;
		}

		if (bHasStableFootForwardLocal[FootIndex] && FVector::DotProduct(TargetForward, StableFootForwardLocal[FootIndex]) < 0.0f)
		{
			TargetForward *= -1.0f;
		}

		const bool bNeedsFlip = FVector::DotProduct(TargetForward, RawForward) < 0.0f;
		if (bNeedsFlip)
		{
			const FVector TargetToeLocal = AnkleLocal - RawFoot;
			const FVector TargetToeMp = MediaPipePoseCoordinate::UeLocalUnscaledToMpWorld(TargetToeLocal / FMath::Max(WorldScaleCm, UE_SMALL_NUMBER), bMirrorLandmarksLR);
			SetLandmarkVector(Frame.World, ToeLm, TargetToeMp);
		}

		StableFootForwardLocal[FootIndex] = TargetForward.GetSafeNormal();
		bHasStableFootForwardLocal[FootIndex] = !StableFootForwardLocal[FootIndex].IsNearlyZero();
	};

	ApplyFoot(true);
	ApplyFoot(false);
}

void FMediaPipeSourceConditioner::StorePreviousFrame(const FMediaPipePoseFrame& Frame)
{
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		PreviousWorld[Index] = Frame.World.Points[Index];
		PreviousNormalized[Index] = Frame.Normalized.Points[Index];
		bHasPreviousLandmark[Index] = true;
	}
}
