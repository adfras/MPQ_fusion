#pragma once

#include "CoreMinimal.h"
#include "MediaPipeQuestHandTypes.h"

namespace MediaPipeQuestFingerSolver
{
	inline constexpr int32 QuestFingerCount = 5;
	inline constexpr int32 QuestFingerSegmentsPerFinger = 3;
	inline constexpr int32 QuestMetacarpalBoneCount = QuestFingerMetacarpalBoneCount;

	extern MEDIAPIPEDRIVER_API const TCHAR* const QuestFingerBoneNamesL[QuestFingerBoneCount];
	extern MEDIAPIPEDRIVER_API const TCHAR* const QuestFingerBoneNamesR[QuestFingerBoneCount];
	extern MEDIAPIPEDRIVER_API const TCHAR* const QuestFingerMetacarpalBoneNamesL[QuestMetacarpalBoneCount];
	extern MEDIAPIPEDRIVER_API const TCHAR* const QuestFingerMetacarpalBoneNamesR[QuestMetacarpalBoneCount];

	struct FMediaPipeQuestFingerCurlSettings
	{
		float OpenAngleDeg = 20.0f;
		float FullAngleDeg = 85.0f;
	};

	// Plausibility gate for a whole tracked hand. Quest hand tracking collapses to garbage
	// full-fist poses when fingers self-occlude (live evidence 2026-06-12: open hand snapped
	// 0.09 -> 1.00 mean curl in a single 98 ms frame, and 23% of frames carried tracked=0 with
	// stale joints). A real fist measures ~4 curl-units/s, the garbage ~9+/s: frames faster
	// than the rate limit, or untracked frames, are rejected and the last good pose holds; a
	// rejected pose is accepted once it has been STABLE for the recovery window (so a genuine
	// instant pose change costs at most that window of latency).
	struct FMediaPipeQuestHandPoseGateState
	{
		bool bHasLastSample = false;
		float LastSampleMeanCurl01 = 0.0f;
		bool bRecovering = false;
		float StableSeconds = 0.0f;

		void Reset()
		{
			bHasLastSample = false;
			LastSampleMeanCurl01 = 0.0f;
			bRecovering = false;
			StableSeconds = 0.0f;
		}
	};

	struct FMediaPipeQuestHandPoseGateSettings
	{
		float MaxCurlRatePerSec = 5.0f;
		float StableRatePerSec = 1.5f;
		float RecoverSeconds = 0.25f;
	};

	// Returns true when this frame's hand pose must be HELD (apply the previous smoothed pose,
	// do not consume the new joints).
	MEDIAPIPEDRIVER_API bool UpdateQuestHandPoseGate(
		FMediaPipeQuestHandPoseGateState& State,
		float MeanCurl01,
		bool bTracked,
		float DeltaSeconds,
		const FMediaPipeQuestHandPoseGateSettings& Settings);

	MEDIAPIPEDRIVER_API int32 QuestFingerBoneIndex(int32 FingerIndex, int32 SegmentIndex);
	MEDIAPIPEDRIVER_API int32 QuestFingerMetacarpalBoneIndex(int32 FingerIndex);
	MEDIAPIPEDRIVER_API EHandKeypoint QuestFingerStartKeypoint(int32 FingerIndex, int32 SegmentIndex);
	MEDIAPIPEDRIVER_API EHandKeypoint QuestFingerEndKeypoint(int32 FingerIndex, int32 SegmentIndex);
	MEDIAPIPEDRIVER_API EHandKeypoint QuestFingerMetacarpalStartKeypoint(int32 FingerIndex);
	MEDIAPIPEDRIVER_API EHandKeypoint QuestFingerMetacarpalEndKeypoint(int32 FingerIndex);
	MEDIAPIPEDRIVER_API EHandKeypoint QuestFingerBoneSourceKeypoint(int32 FingerIndex, int32 SegmentIndex);
	MEDIAPIPEDRIVER_API EHandKeypoint QuestFingerMetacarpalSourceKeypoint(int32 FingerIndex);
	MEDIAPIPEDRIVER_API FVector GetQuestFingerSegmentWorld(const FQuestHandTrackingSnapshot& Snapshot, bool bIsLeft, int32 FingerIndex, int32 SegmentIndex);
	MEDIAPIPEDRIVER_API FVector GetQuestFingerMetacarpalSegmentWorld(const FQuestHandTrackingSnapshot& Snapshot, bool bIsLeft, int32 FingerIndex);
	MEDIAPIPEDRIVER_API FQuat ApplyQuestJointRestOffset(const FQuat& SourceReferenceComp, const FQuat& TargetReferenceComp, const FQuat& SourceLiveComp);
	MEDIAPIPEDRIVER_API FQuat MakeQuestJointLocalRotation(const FQuat& ParentComp, const FQuat& ChildComp);
	MEDIAPIPEDRIVER_API FQuat RetargetQuestJointLocalToComponent(
		const FQuat& SourceReferenceLocal,
		const FQuat& TargetReferenceLocal,
		const FQuat& SourceLiveLocal,
		const FQuat& TargetParentLiveComp);
	MEDIAPIPEDRIVER_API FQuat RetargetQuestSegmentDirectionToBone(
		const FQuat& CurrentHandDeltaCS,
		const FQuat& TargetReferenceComp,
		const FVector& TargetReferenceSegmentComp,
		const FVector& QuestSegmentComp);
	// Clamps a desired finger-segment direction's out-of-plane (splay/adduction) angle against
	// the finger's own curl plane (normal = the finger's hinge axis). The tracked human hand's
	// knuckle layout differs from the avatar's, so raw segment directions let curled middle and
	// ring fingers converge sideways until they interpenetrate; their middle/end joints are
	// anatomically pure hinges, so out-of-plane motion there is retarget error, not signal.
	MEDIAPIPEDRIVER_API FVector ClampQuestFingerSegmentSplay(
		const FVector& DesiredSegmentDir,
		const FVector& CurlPlaneNormal,
		float MaxSplayDeg);
	// Signed out-of-plane angle (degrees) of a segment direction against the curl plane. Pairs
	// with ApplyQuestFingerSegmentSplayDeg so a caller can measure splay, subtract a per-wearer
	// neutral (the structural offset between the tracked hand's knuckle layout and the avatar's
	// rig), and rebuild the direction with only the deliberate spread remaining.
	MEDIAPIPEDRIVER_API float MeasureQuestFingerSegmentSplayDeg(
		const FVector& SegmentDir,
		const FVector& CurlPlaneNormal);
	// Rebuilds a segment direction with the given out-of-plane angle, preserving its in-plane
	// (curl) component. Degenerate inputs return the direction unchanged.
	MEDIAPIPEDRIVER_API FVector ApplyQuestFingerSegmentSplayDeg(
		const FVector& SegmentDir,
		const FVector& CurlPlaneNormal,
		float SplayDeg);
	// Enforces a minimum signed separation between two adjacent finger segment directions by
	// rotating both symmetrically apart about the separation axis. The signed angle goes
	// negative when the pair has CROSSED (mesh interpenetration), so enforcement also uncrosses.
	// Rotation about the axis preserves each direction's curl; pairs already separated are left
	// untouched. This is convention-free: no joint axis or curl-plane estimate is needed, which
	// is what made plane-projection approaches corrupt curl on rigs whose reference curl
	// directions are only approximate.
	MEDIAPIPEDRIVER_API void EnforceQuestFingerPairSeparation(
		FVector& InOutDirA,
		FVector& InOutDirB,
		const FVector& SeparationAxis,
		float MinSeparationDeg);
	MEDIAPIPEDRIVER_API float RemapQuestFingerCurlAngle01(float AngleDeg, const FMediaPipeQuestFingerCurlSettings& Settings);
	MEDIAPIPEDRIVER_API float QuestFingerSegmentCurl01(const FVector& SegmentWorld, const FVector& HandForwardWorld, const FMediaPipeQuestFingerCurlSettings& Settings);
	MEDIAPIPEDRIVER_API float QuestAngleBetweenSegmentsDeg(const FVector& A, const FVector& B);
	MEDIAPIPEDRIVER_API float QuestFingerChainCurl01(
		const FQuestHandTrackingSnapshot& Snapshot,
		bool bIsLeft,
		int32 FingerIndex,
		int32 SegmentIndex,
		const FMediaPipeQuestFingerCurlSettings& Settings,
		float& OutJointAngleDeg);
	MEDIAPIPEDRIVER_API float QuestThumbChainCurl01(
		const FQuestHandTrackingSnapshot& Snapshot,
		bool bIsLeft,
		int32 SegmentIndex,
		const FMediaPipeQuestFingerCurlSettings& Settings,
		float& OutJointAngleDeg);
	MEDIAPIPEDRIVER_API int32 CountValidQuestFingerRefs(const bool* bHasRefFinger);
	MEDIAPIPEDRIVER_API int32 CountValidQuestMetacarpalRefs(const bool* bHasRefFingerMetacarpal);
}
