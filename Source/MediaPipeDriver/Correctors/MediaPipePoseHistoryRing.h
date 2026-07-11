#pragma once

#include "CoreMinimal.h"

// TIMESTAMPED POSE HISTORY RING (TRACKING_QUALITY_PLAN Phase 1, 2026-07-11).
//
// Out-of-sequence-measurement fix (Bar-Shalom OOSM literature; the 2026-07-11 research
// report): a webcam measurement is ~80-130 ms old by the time the solver fuses it, so a
// corrector that compares it against the CURRENT pose reads phantom residuals during
// motion - the exact transient class the July arm arc kept re-fixing downstream. These
// rings buffer the solver's own recent outputs so Learn() can compare a measurement
// against the pose at the measurement's own effective capture time. Apply() never
// changes - corrections still apply to the current pose.
//
// House rules honored here:
// - Fixed capacity, plain arrays, no allocation: instances live inside the KEYED runtime
//   stores (FQuestWristSideRuntimeState) or the field-proven FMediaPipeBodySolverState -
//   never anim-node members that CacheBones wipes.
// - Pushes are gated on mp.MediaPipeTimestampAlignedResiduals at the call sites: with
//   the CVar at its 0 default nothing is ever written or read (byte-identical disarmed).
// - Non-monotonic pushes are refused (zero-dt re-evaluations must not corrupt history).
namespace MediaPipePoseHistory
{
	// Query older than the oldest entry by more than this = missing history -> the
	// caller falls back to the current pose (plan-specified fallback).
	constexpr double MaxClampBehindSeconds = 0.05;
	// Query ahead of the newest entry by more than this = stale buffer (paused world,
	// long gap) -> fallback. Small overshoot happens legitimately when the conditioner's
	// forward prediction exceeds the measurement age (effective age < 0; measured
	// possible in the Phase 0 WebcamAgeTrace unit cases).
	constexpr double MaxClampAheadSeconds = 0.25;

	// 32 samples at the 72-120 Hz evaluation cadence spans ~270-440 ms - comfortably
	// covering the ~250 ms depth the plan asks for.
	constexpr int32 DefaultHistoryCapacity = 32;

	struct FArmChainHistorySample
	{
		FVector ShoulderWorld = FVector::ZeroVector;
		FVector ElbowWorld = FVector::ZeroVector;
		FVector WristWorld = FVector::ZeroVector;

		static FArmChainHistorySample Lerp(
			const FArmChainHistorySample& A, const FArmChainHistorySample& B, const float Alpha)
		{
			FArmChainHistorySample Out;
			Out.ShoulderWorld = FMath::Lerp(A.ShoulderWorld, B.ShoulderWorld, static_cast<double>(Alpha));
			Out.ElbowWorld = FMath::Lerp(A.ElbowWorld, B.ElbowWorld, static_cast<double>(Alpha));
			Out.WristWorld = FMath::Lerp(A.WristWorld, B.WristWorld, static_cast<double>(Alpha));
			return Out;
		}
	};

	struct FYawHistorySample
	{
		float YawDeg = 0.0f;

		// Wrap-aware: interpolating 179 -> -179 must cross 180, not swing through 0.
		static FYawHistorySample Lerp(const FYawHistorySample& A, const FYawHistorySample& B, const float Alpha)
		{
			FYawHistorySample Out;
			Out.YawDeg = FRotator::NormalizeAxis(
				A.YawDeg + FMath::FindDeltaAngleDegrees(A.YawDeg, B.YawDeg) * Alpha);
			return Out;
		}
	};

	template <typename SampleType, int32 CapacityValue>
	struct TMediaPipeTimedHistoryRing
	{
		static constexpr int32 Capacity = CapacityValue;

		double TimesSeconds[CapacityValue] = {};
		SampleType Samples[CapacityValue];
		// Head = index of the NEWEST valid sample (when Count > 0); entries walk
		// backwards from Head through Count slots.
		int32 Head = 0;
		int32 Count = 0;

		void Reset()
		{
			Head = 0;
			Count = 0;
		}

		void Push(const double TimeSeconds, const SampleType& Sample)
		{
			if (Count > 0)
			{
				const double NewestTime = TimesSeconds[Head];
				if (TimeSeconds < NewestTime)
				{
					// Time went backwards (clock anomaly): refuse rather than corrupt.
					return;
				}
				if (TimeSeconds == NewestTime)
				{
					// Same-instant re-evaluation: keep the freshest values, no new slot.
					Samples[Head] = Sample;
					return;
				}
				Head = (Head + 1) % CapacityValue;
			}
			TimesSeconds[Head] = TimeSeconds;
			Samples[Head] = Sample;
			Count = FMath::Min(Count + 1, CapacityValue);
		}

		// Interpolated lookup at QueryTimeSeconds. Returns false when history cannot
		// answer (empty, too old, or buffer stale) - the caller must fall back to the
		// current pose.
		bool TrySample(const double QueryTimeSeconds, SampleType& Out) const
		{
			if (Count <= 0)
			{
				return false;
			}
			const double NewestTime = TimesSeconds[Head];
			if (QueryTimeSeconds >= NewestTime)
			{
				if (QueryTimeSeconds - NewestTime > MaxClampAheadSeconds)
				{
					return false;
				}
				Out = Samples[Head];
				return true;
			}
			const int32 OldestIndex = (Head - (Count - 1) + CapacityValue * 2) % CapacityValue;
			const double OldestTime = TimesSeconds[OldestIndex];
			if (QueryTimeSeconds <= OldestTime)
			{
				if (OldestTime - QueryTimeSeconds > MaxClampBehindSeconds)
				{
					return false;
				}
				Out = Samples[OldestIndex];
				return true;
			}
			// Bracketed: walk back from the newest until the lower bound passes the query.
			int32 UpperIndex = Head;
			for (int32 StepBack = 1; StepBack < Count; ++StepBack)
			{
				const int32 LowerIndex = (Head - StepBack + CapacityValue * 2) % CapacityValue;
				if (TimesSeconds[LowerIndex] <= QueryTimeSeconds)
				{
					const double LowerTime = TimesSeconds[LowerIndex];
					const double UpperTime = TimesSeconds[UpperIndex];
					const float Alpha = UpperTime > LowerTime
						? static_cast<float>((QueryTimeSeconds - LowerTime) / (UpperTime - LowerTime))
						: 0.0f;
					Out = SampleType::Lerp(Samples[LowerIndex], Samples[UpperIndex], Alpha);
					return true;
				}
				UpperIndex = LowerIndex;
			}
			// Unreachable: the oldest-time check above bounds the walk.
			return false;
		}
	};

	using FMediaPipeArmChainHistoryRing =
		TMediaPipeTimedHistoryRing<FArmChainHistorySample, DefaultHistoryCapacity>;
	using FMediaPipeYawHistoryRing =
		TMediaPipeTimedHistoryRing<FYawHistorySample, DefaultHistoryCapacity>;

	// Effective capture time of a conditioned webcam measurement: the source conditioner
	// already advanced the landmarks PredictionHorizonMs forward from the capture
	// timestamp, so residual alignment must query capture + prediction (the Phase 0
	// WebcamAgeTrace effective-age complement). Returns false when no usable timestamp.
	inline bool TryGetEffectiveMeasurementTimeSeconds(
		const double CaptureTimestampSeconds, const float PredictionHorizonMs, double& OutTimeSeconds)
	{
		if (CaptureTimestampSeconds <= 0.0)
		{
			return false;
		}
		OutTimeSeconds = CaptureTimestampSeconds + FMath::Max(PredictionHorizonMs, 0.0f) / 1000.0;
		return true;
	}
}
