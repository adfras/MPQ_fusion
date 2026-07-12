#pragma once

#include "CoreMinimal.h"

// Avatar metric lock (Docs/AVATAR_METRIC_LOCK_PLAN.md): pure math for the
// mp.EmbodimentScaleTrace rows and the once-per-session embodiment scale latch
//
//     S = avatar reference height / user standing reference height.
//
// S is measured once-per-session-latch, never a live learner (iron rule 2): it
// latches when the HMD height scaffold's standing reference is trusted, then
// holds until an explicit Reset(). No engine state here; unit-tested in
// Tests/MediaPipeEmbodimentScaleMetricsTests.cpp.
namespace MediaPipeEmbodimentScale
{
	// Sane embodiment band. Outside it one of the reference heights is garbage
	// (a desk-resting HMD, an uncached skeleton), so no latch may happen.
	inline constexpr float MinEmbodimentScale = 0.3f;
	inline constexpr float MaxEmbodimentScale = 3.0f;

	// Reference heights must land in a plausible human/avatar band (cm) before
	// they may participate in S.
	inline constexpr float MinReferenceHeightCm = 40.0f;
	inline constexpr float MaxReferenceHeightCm = 260.0f;

	// The HMD height scaffold's confidence ramps 0.25 -> 1.0 as its rolling
	// baseline window fills; the standing reference counts as trusted at 0.75+
	// (6 of 8 ramp slots), which needs sustained worn samples - donning noise
	// never reaches it.
	inline constexpr float LatchMinUserRefConfidence01 = 0.75f;

	// Driven/native span ratio; 0 when the native span is degenerate so a
	// missing reference can never masquerade as a confirmed 1.0.
	MEDIAPIPEDRIVER_API float ComputeSpanRatio(float DrivenCm, float NativeCm);

	// Component-space BIND position of a bone from its inverse-bind matrix
	// (USkeletalMesh::GetRefBasesInvMatrix element). The bind pose is the pose the
	// vertices were skinned at - the avatar's TRUE authored geometry. Measured
	// 2026-07-12: Emory's reference skeleton is short-adult (head 147.2) while his
	// bind/authored geometry is a genuine child (pelvis ~62); every "avatar-native"
	// reference derived from the ref skeleton inherits that inflation.
	MEDIAPIPEDRIVER_API FVector BindComponentPositionFromInverseBind(const FMatrix44f& InverseBindMatrix);

	// Segment-sum length of the pelvis->chest->neck->head chain (cm).
	MEDIAPIPEDRIVER_API float ComputeTorsoChainLengthCm(
		const FVector& PelvisComp,
		const FVector& ChestComp,
		const FVector& NeckComp,
		const FVector& HeadComp);

	// S clamped to the sane band; 0 when either reference height is outside the
	// plausible band (0 = "no scale available", callers must treat as unlatched).
	MEDIAPIPEDRIVER_API float ComputeEmbodimentScale(float AvatarRefHeightCm, float UserStandingRefHeightCm);

	// Which reference measured the user side of S.
	inline constexpr uint8 LatchSourceNone = 0;
	// Worn HMD height scaffold standing baseline (metric SLAM; preferred).
	inline constexpr uint8 LatchSourceHmd = 1;
	// Camera standing source hip estimate (headset-free sessions).
	inline constexpr uint8 LatchSourceCamera = 2;

	struct FMediaPipeEmbodimentScaleLatchInput
	{
		float AvatarRefHeightCm = 0.0f;
		float UserStandingRefHeightCm = 0.0f;
		// Confidence in the user standing reference (0..1).
		float UserRefConfidence01 = 0.0f;
		uint8 Source = LatchSourceNone;
		double NowSeconds = 0.0;
	};

	struct FMediaPipeEmbodimentScaleLatchState
	{
		bool bLatched = false;
		float LatchedS = 1.0f;
		float LatchedAvatarRefHeightCm = 0.0f;
		float LatchedUserStandingRefHeightCm = 0.0f;
		uint8 LatchedSource = LatchSourceNone;
		double LatchTimeSeconds = 0.0;

		void Reset()
		{
			bLatched = false;
			LatchedS = 1.0f;
			LatchedAvatarRefHeightCm = 0.0f;
			LatchedUserStandingRefHeightCm = 0.0f;
			LatchedSource = LatchSourceNone;
			LatchTimeSeconds = 0.0;
		}
	};

	// The HMD scaffold pair wins when it is trusted (metric SLAM beats the
	// monocular camera estimate); otherwise the camera pair may stand in, so a
	// headset-free session can still latch. Pure selection - no state.
	MEDIAPIPEDRIVER_API FMediaPipeEmbodimentScaleLatchInput SelectEmbodimentScaleLatchInput(
		const FMediaPipeEmbodimentScaleLatchInput& HmdPair,
		const FMediaPipeEmbodimentScaleLatchInput& CameraPair);

	// Latches exactly once: after the first success the inputs are ignored until
	// Reset() (explicit recalibration / solver continuity reset). Returns true on
	// the update that latches.
	MEDIAPIPEDRIVER_API bool UpdateEmbodimentScaleLatch(
		FMediaPipeEmbodimentScaleLatchState& State,
		const FMediaPipeEmbodimentScaleLatchInput& Input);
}
