#pragma once

#include "CoreMinimal.h"

struct FQuestWristSideRuntimeState;

// QUEST-REACH EXTENDER (refactor/correctors Phase 5, 2026-07-10).
//
// The exact chain-reach extension block from FAnimNode_MediaPipePoseDriven::
// DriveArmCS (MediaPipePoseDrivenAnimInstance_QuestArmSolve.cpp): the real
// hand-tracking wrist's straightness fraction, the sustained-fraction dwell
// (0.35s) + apply ramp (0.85..0.97) + 8cm deficit cap + asymmetric easing
// (0.45s in / 0.8s out) from the round-3 transient fix, the stretch-only radial
// wrist extension, the two-bone cosine elbow re-solve in the existing swivel
// plane, and the mp.ChainReachExtend evidence row - character for character.
// Parameterized only enough to be callable; the wall clock and the pose/source
// reads are hoisted to the call site. State lives in the keyed runtime store
// (never key-0). Golden-locked by MediaPipeReachExtenderGoldenTests.
struct FMediaPipeReachExtenderInputs
{
	bool bIsLeft = false;
	FName TargetActorName;
	// mp.MediaPipeChainReachFromQuestHand pre-clamped 0..1 at the call site.
	float Weight = 0.0f;
	bool bQuestSideTrackedForArm = false;
	// FPlatformTime::Seconds(), hoisted for determinism under test.
	double NowSeconds = 0.0;
	// Chain-direct shoulder at block entry (the radial anchor).
	FVector ChainShoulderWorld = FVector::ZeroVector;
	// FullArmChainResult joints (avatar-frame; segment lengths + full reach).
	FVector ChainResultShoulderWorld = FVector::ZeroVector;
	FVector ChainResultElbowWorld = FVector::ZeroVector;
	FVector ChainResultWristWorld = FVector::ZeroVector;
	// FullArmChainSide source joints (tracking space; the fraction denominator).
	FVector ChainSrcShoulderWorld = FVector::ZeroVector;
	FVector ChainSrcElbowWorld = FVector::ZeroVector;
	FVector ChainSrcWristWorld = FVector::ZeroVector;
	// The REAL Quest hand-tracking wrist (tracking space; the fraction numerator).
	FVector RealHandWristWorld = FVector::ZeroVector;
};

// Mutates the keyed side state and the chain elbow/wrist targets exactly as the
// inline block did. The caller keeps the outer gate (Weight > 0 &&
// RuntimeStateKey != 0) and the ArmTraceWristAfterExtWorld stage capture.
MEDIAPIPEDRIVER_API void ApplyMediaPipeReachExtension(
	const FMediaPipeReachExtenderInputs& In,
	FQuestWristSideRuntimeState& SideState,
	FVector& InOutElbowWorld,
	FVector& InOutWristWorld);

// The dormant-path reset (the original else-branch): identical field writes.
MEDIAPIPEDRIVER_API void ResetMediaPipeReachExtension(FQuestWristSideRuntimeState& SideState);
