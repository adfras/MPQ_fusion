#include "MediaPipePoseDrivenAnimInstance.h"

#include "MediaPipeArmGuardPolicy.h"
#include "MediaPipeArmTwistSolver.h"
#include "MediaPipeAvatarRigProfile.h"
#include "MediaPipeBodyDiagnostics.h"
#include "MediaPipeBodySolverMath.h"
#include "MediaPipeMetaHumanArmRetargeter.h"
#include "MediaPipePoseCoordinate.h"
#include "MediaPipePoseDiagnosticReporter.h"
#include "MediaPipePoseDiagnostics.h"
#include "MediaPipePoseFrameContinuity.h"
#include "MediaPipeRuntimeCVars.h"
#include "MediaPipeQuestHandCaptureReplayTooling.h"
#include "MediaPipeQuestHandDebugReporter.h"
#include "MediaPipeQuestHandCompareDiagnostics.h"
#include "MediaPipeQuestFingerSolver.h"
#include "MediaPipeQuestConstrainedArmSolver.h"
#include "MediaPipeQuestWristApplyPolicy.h"
#include "MediaPipeQuestWristDebugReporter.h"
#include "MediaPipeQuestWristCalibrationState.h"
#include "MediaPipeQuestWristDiagnosticFormatter.h"
#include "MediaPipeShoulderRollbackDiagnostics.h"
#include "MediaPipePoseTrackerComponent.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeSolvedPose.h"

#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "HeadMountedDisplayTypes.h"
#include "IHandTracker.h"
#include "InputCoreTypes.h"
#include "IXRTrackingSystem.h"
#include "Math/RotationMatrix.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
	using namespace MediaPipeRuntimeCVars;
	using namespace MediaPipeBodySolverMath;
	using namespace MediaPipeQuestFingerSolver;

	struct FPoseYawAlignRuntimeState
	{
		bool bWasEnabled = false;
		bool bHasState = false;
		double LastLogTimeSeconds = -1.0;
		double LastUpdateTimeSeconds = -1.0;
		float SmoothedDeltaDeg = 0.0f;
		float LastRawYawDeg = 0.0f;
		float LastDesiredYawDeg = 0.0f;

		void Reset()
		{
			bHasState = false;
			LastLogTimeSeconds = -1.0;
			LastUpdateTimeSeconds = -1.0;
			SmoothedDeltaDeg = 0.0f;
			LastRawYawDeg = 0.0f;
			LastDesiredYawDeg = 0.0f;
		}
	};

	TMap<uint32, FPoseYawAlignRuntimeState> GPoseYawAlignRuntimeStates;

	FPoseYawAlignRuntimeState& GetPoseYawAlignRuntimeState(uint32 Key)
	{
		return GPoseYawAlignRuntimeStates.FindOrAdd(Key);
	}

	FPoseYawAlignRuntimeState& GetPoseYawAlignRuntimeState(const UObject* KeyObject)
	{
		const uint32 Key = IsValid(KeyObject) ? KeyObject->GetUniqueID() : 0u;
		return GetPoseYawAlignRuntimeState(Key);
	}

	void ResetPoseYawAlignRuntimeState(uint32 Key)
	{
		GetPoseYawAlignRuntimeState(Key).Reset();
	}

	void ResetPoseYawAlignRuntimeState(const UObject* KeyObject)
	{
		const uint32 Key = IsValid(KeyObject) ? KeyObject->GetUniqueID() : 0u;
		ResetPoseYawAlignRuntimeState(Key);
	}

	float QuestPlanarYawDeg(const FVector& ForwardWorld)
	{
		const FVector FlatForward(ForwardWorld.X, ForwardWorld.Y, 0.0f);
		if (FlatForward.IsNearlyZero())
		{
			return 0.0f;
		}
		return FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(FlatForward.Y, FlatForward.X)));
	}

	TMap<uint32, FQuestWristRuntimeState> GQuestWristRuntimeStates;
	FThreadSafeCounter GQuestWristManualResetSerial;
	FThreadSafeCounter GBodyFusionCalibrationResetSerial;

	FQuestWristRuntimeState& GetQuestWristRuntimeState(uint32 Key)
	{
		return GQuestWristRuntimeStates.FindOrAdd(Key);
	}

	void ResetQuestWristRuntimeState(uint32 Key)
	{
		GetQuestWristRuntimeState(Key).Reset();
	}

	void ResetQuestWristRuntimeState(const UObject* KeyObject)
	{
		const uint32 Key = IsValid(KeyObject) ? KeyObject->GetUniqueID() : 0u;
		ResetQuestWristRuntimeState(Key);
	}

	void RequestQuestWristManualCalibrationReset()
	{
		const int32 Serial = GQuestWristManualResetSerial.Increment();
		FMediaPipeQuestWristDebugReporter::EmitManualCalibrationResetRequestedLog(Serial);
	}

	float ResolveReferencePoseUpperBodyFollowAlpha(
		const float HeadToChestCm,
		const float ChestToPelvisCm,
		const float FallbackAlpha)
	{
		const float SafeFallback = FMath::Clamp(
			FMath::IsFinite(FallbackAlpha) ? FallbackAlpha : 1.0f,
			0.0f,
			1.0f);
		if (!FMath::IsFinite(HeadToChestCm) ||
			!FMath::IsFinite(ChestToPelvisCm) ||
			HeadToChestCm <= KINDA_SMALL_NUMBER ||
			ChestToPelvisCm <= KINDA_SMALL_NUMBER)
		{
			return SafeFallback;
		}

		const float ChainLengthCm = HeadToChestCm + ChestToPelvisCm;
		if (ChainLengthCm <= KINDA_SMALL_NUMBER)
		{
			return SafeFallback;
		}

		const float HeadChainFraction = HeadToChestCm / ChainLengthCm;
		return FMath::Clamp(1.0f - HeadChainFraction * 0.65f, 0.55f, 0.90f);
	}

	FAutoConsoleCommand CmdResetQuestWristCalibration(
		TEXT("mp.ResetQuestWristCalibration"),
		TEXT("Reset Quest wrist rotation/position calibration. Run while holding palms forward in VR Preview to define wrist zero from that frame."),
		FConsoleCommandDelegate::CreateStatic(&RequestQuestWristManualCalibrationReset));

	void RequestBodyFusionCalibrationReset()
	{
		const int32 Serial = GBodyFusionCalibrationResetSerial.Increment();
		UE_LOG(LogMediaPipePose, Log, TEXT("mp.BodyFusion.ResetCalibration requested serial=%d"), Serial);
	}

	FAutoConsoleCommand CmdResetBodyFusionCalibration(
		TEXT("mp.BodyFusion.ResetCalibration"),
		TEXT("Reset only the BodyFusion neutral calibration. Does not reset Quest wrist, finger, or arm calibration state."),
		FConsoleCommandDelegate::CreateStatic(&RequestBodyFusionCalibrationReset));

	constexpr int32 HandLm_Wrist = 0;
	constexpr int32 HandLm_IndexMcp = 5;
	constexpr int32 HandLm_MiddleMcp = 9;
	constexpr int32 HandLm_PinkyMcp = 17;

	const TCHAR* const MetaHumanClavicleHelperBoneNamesL[MediaPipeMetaHumanClavicleHelperCount] = {
		TEXT("clavicle_out_l"),
		TEXT("clavicle_scap_l"),
		TEXT("clavicle_pec_l")
	};
	const TCHAR* const MetaHumanClavicleHelperBoneNamesR[MediaPipeMetaHumanClavicleHelperCount] = {
		TEXT("clavicle_out_r"),
		TEXT("clavicle_scap_r"),
		TEXT("clavicle_pec_r")
	};
	const TCHAR* const MetaHumanUpperArmHelperBoneNamesL[MediaPipeMetaHumanUpperArmHelperCount] = {
		TEXT("upperarm_twistCor_01_l"),
		TEXT("upperarm_twistCor_02_l"),
		TEXT("upperarm_bicep_l"),
		TEXT("upperarm_tricep_l"),
		TEXT("upperarm_correctiveRoot_l"),
		TEXT("upperarm_bck_l"),
		TEXT("upperarm_fwd_l"),
		TEXT("upperarm_in_l"),
		TEXT("upperarm_out_l")
	};
	const TCHAR* const MetaHumanUpperArmHelperBoneNamesR[MediaPipeMetaHumanUpperArmHelperCount] = {
		TEXT("upperarm_twistCor_01_r"),
		TEXT("upperarm_twistCor_02_r"),
		TEXT("upperarm_bicep_r"),
		TEXT("upperarm_tricep_r"),
		TEXT("upperarm_correctiveRoot_r"),
		TEXT("upperarm_bck_r"),
		TEXT("upperarm_fwd_r"),
		TEXT("upperarm_in_r"),
		TEXT("upperarm_out_r")
	};
	const TCHAR* const MetaHumanLowerArmHelperBoneNamesL[MediaPipeMetaHumanLowerArmHelperCount] = {
		TEXT("lowerarm_correctiveRoot_l"),
		TEXT("lowerarm_in_l"),
		TEXT("lowerarm_out_l"),
		TEXT("lowerarm_fwd_l"),
		TEXT("lowerarm_bck_l"),
		TEXT("wrist_inner_l"),
		TEXT("wrist_outer_l")
	};
	const TCHAR* const MetaHumanLowerArmHelperBoneNamesR[MediaPipeMetaHumanLowerArmHelperCount] = {
		TEXT("lowerarm_correctiveRoot_r"),
		TEXT("lowerarm_in_r"),
		TEXT("lowerarm_out_r"),
		TEXT("lowerarm_fwd_r"),
		TEXT("lowerarm_bck_r"),
		TEXT("wrist_inner_r"),
		TEXT("wrist_outer_r")
	};

	bool IsFiniteVector(const FVector& Vector)
	{
		return FMath::IsFinite(Vector.X) && FMath::IsFinite(Vector.Y) && FMath::IsFinite(Vector.Z);
	}

	bool TryResolveChainAlpha(
		const FVector& ChainStart,
		const FVector& ChainEnd,
		const FVector& Point,
		float& OutAlpha)
	{
		if (!IsFiniteVector(ChainStart) || !IsFiniteVector(ChainEnd) || !IsFiniteVector(Point))
		{
			return false;
		}

		const FVector Chain = ChainEnd - ChainStart;
		const float ChainLenSq = Chain.SizeSquared();
		if (ChainLenSq <= KINDA_SMALL_NUMBER)
		{
			return false;
		}

		OutAlpha = FMath::Clamp(FVector::DotProduct(Point - ChainStart, Chain) / ChainLenSq, 0.0f, 1.0f);
		return FMath::IsFinite(OutAlpha);
	}

	void ResolveBodyFusionNeckChainAlphas(
		const float ReferenceNeckAlpha,
		const float ReferenceNeck02Alpha,
		float& OutNeckAlpha,
		float& OutNeck02Alpha)
	{
		OutNeckAlpha = FMath::Clamp(ReferenceNeckAlpha, 0.0f, 1.0f);
		OutNeck02Alpha = FMath::Clamp(FMath::Max(ReferenceNeck02Alpha, OutNeckAlpha), 0.0f, 1.0f);
	}

#if WITH_DEV_AUTOMATION_TESTS
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FMediaPipePoseDrivenMetaHumanNoMediaPipeNeckAlphaAutomationTest,
		"TestingKit3.MediaPipe.PoseDriven.MetaHumanNoMediaPipeNeckAlpha",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FMediaPipePoseDrivenMetaHumanNoMediaPipeNeckAlphaAutomationTest::RunTest(const FString& Parameters)
	{
		float NeckAlpha = 0.0f;
		float Neck02Alpha = 0.0f;
		ResolveBodyFusionNeckChainAlphas(0.63f, 0.74f, NeckAlpha, Neck02Alpha);

		TestEqual(TEXT("Neck alpha preserves the reference MetaHuman chain spacing"), NeckAlpha, 0.63f);
		TestEqual(TEXT("Neck02 alpha preserves the reference MetaHuman chain spacing"), Neck02Alpha, 0.74f);
		TestTrue(TEXT("No-MediaPipe MetaHuman fallback does not force neck_01 up near the head"),
			NeckAlpha < 0.72f);
		TestTrue(TEXT("No-MediaPipe MetaHuman fallback does not force neck_02 up near the head"),
			Neck02Alpha < 0.88f);

		float ReorderedNeckAlpha = 0.0f;
		float ReorderedNeck02Alpha = 0.0f;
		ResolveBodyFusionNeckChainAlphas(0.72f, 0.68f, ReorderedNeckAlpha, ReorderedNeck02Alpha);
		TestEqual(TEXT("Neck02 remains above neck when source data is reordered"), ReorderedNeck02Alpha, ReorderedNeckAlpha);
		return true;
	}
#endif

	bool IsUsableQuestWristPosition(const FVector& WristWorld)
	{
		return IsFiniteVector(WristWorld) && !WristWorld.IsNearlyZero();
	}

	bool IsQuestHandSideTracked(const FQuestHandTrackingSnapshot& Snapshot, const bool bIsLeft)
	{
		return bIsLeft ? (Snapshot.bHasLeft != 0 && Snapshot.bLeftTracked != 0) : (Snapshot.bHasRight != 0 && Snapshot.bRightTracked != 0);
	}

	bool IsQuestHandSideAvailable(const FQuestHandTrackingSnapshot& Snapshot, const bool bIsLeft)
	{
		return bIsLeft ? (Snapshot.bHasLeft != 0) : (Snapshot.bHasRight != 0);
	}

	bool IsQuestHandSideUsableForWrist(const FQuestHandTrackingSnapshot& Snapshot, const bool bIsLeft)
	{
		const bool bHasSide = bIsLeft ? (Snapshot.bHasLeft != 0) : (Snapshot.bHasRight != 0);
		if (!bHasSide)
		{
			return false;
		}

		const TStaticArray<FVector, QuestHandKeypointCount>& Positions = bIsLeft ? Snapshot.LeftPositionsWorld : Snapshot.RightPositionsWorld;
		return IsUsableQuestWristPosition(Positions[static_cast<int32>(EHandKeypoint::Wrist)]);
	}

	const TStaticArray<FVector, QuestHandKeypointCount>& GetQuestHandPositions(const FQuestHandTrackingSnapshot& Snapshot, const bool bIsLeft)
	{
		return bIsLeft ? Snapshot.LeftPositionsWorld : Snapshot.RightPositionsWorld;
	}

	const TCHAR* BodyFusionSourceStateName(const EMediaPipeBodyFusionSourceState State)
	{
		switch (State)
		{
		case EMediaPipeBodyFusionSourceState::Missing:
			return TEXT("missing");
		case EMediaPipeBodyFusionSourceState::Stale:
			return TEXT("stale");
		case EMediaPipeBodyFusionSourceState::Invalid:
			return TEXT("invalid");
		case EMediaPipeBodyFusionSourceState::Fresh:
			return TEXT("fresh");
		default:
			return TEXT("unknown");
		}
	}

	const TCHAR* BodyFusionAuthorityStateName(const EMediaPipeBodyFusionAuthorityState State)
	{
		switch (State)
		{
		case EMediaPipeBodyFusionAuthorityState::NoMediaPipe:
			return TEXT("NoMediaPipe");
		case EMediaPipeBodyFusionAuthorityState::MediaPipeCalibrating:
			return TEXT("MediaPipeCalibrating");
		case EMediaPipeBodyFusionAuthorityState::MediaPipeStable:
			return TEXT("MediaPipeStable");
		case EMediaPipeBodyFusionAuthorityState::MediaPipeRejected:
			return TEXT("MediaPipeRejected");
		default:
			return TEXT("Unknown");
		}
	}

	FString BodyFusionStatusString(const FMediaPipeBodyFusionSourceStatus& Status)
	{
		return FString::Printf(
			TEXT("%s age=%.3f conf=%.2f"),
			BodyFusionSourceStateName(Status.State),
			Status.AgeSeconds,
			Status.Confidence);
	}

	FString BodyFusionVectorString(const FVector& Value)
	{
		return FString::Printf(TEXT("(%.1f,%.1f,%.1f)"), Value.X, Value.Y, Value.Z);
	}

	bool TryBodyFusionLandmarkMidpoint(
		const FMediaPipeTrackingSourceFrame& SourceFrame,
		const EMediaPipePoseLandmark A,
		const EMediaPipePoseLandmark B,
		FVector& OutMidpoint,
		float* OutReliability = nullptr)
	{
		FVector PointA = FVector::ZeroVector;
		FVector PointB = FVector::ZeroVector;
		float ReliabilityA = 0.0f;
		float ReliabilityB = 0.0f;
		if (!SourceFrame.TryGetMediaPipeLandmark(A, PointA, &ReliabilityA) ||
			!SourceFrame.TryGetMediaPipeLandmark(B, PointB, &ReliabilityB))
		{
			return false;
		}

		OutMidpoint = (PointA + PointB) * 0.5f;
		if (OutReliability)
		{
			*OutReliability = (ReliabilityA + ReliabilityB) * 0.5f;
		}
		return true;
	}

	float BodyFusionAverageReliability(
		const TStaticArray<float, MediaPipePoseLandmarkCount>& Reliabilities,
		const TStaticArray<uint8, MediaPipePoseLandmarkCount>& ValidFlags,
		const TArray<EMediaPipePoseLandmark>& Landmarks)
	{
		float Sum = 0.0f;
		int32 Count = 0;
		for (const EMediaPipePoseLandmark Landmark : Landmarks)
		{
			const int32 Index = static_cast<int32>(Landmark);
			if (Index >= 0 && Index < MediaPipePoseLandmarkCount && ValidFlags[Index] != 0)
			{
				Sum += Reliabilities[Index];
				++Count;
			}
		}
		return Count > 0 ? Sum / static_cast<float>(Count) : 0.0f;
	}

	float EstimateBodyFusionObservedHeightCm(const FMediaPipeTrackingSourceFrame& SourceFrame)
	{
		FVector HeadWorld = FVector::ZeroVector;
		float HeadReliability = 0.0f;
		if (!SourceFrame.TryGetMediaPipeLandmark(EMediaPipePoseLandmark::Nose, HeadWorld, &HeadReliability))
		{
			return 0.0f;
		}

		float FloorZ = TNumericLimits<float>::Max();
		bool bHasFloor = false;
		auto ConsiderFloorLandmark = [&](const EMediaPipePoseLandmark Landmark)
		{
			FVector PointWorld = FVector::ZeroVector;
			float Reliability = 0.0f;
			if (SourceFrame.TryGetMediaPipeLandmark(Landmark, PointWorld, &Reliability))
			{
				FloorZ = bHasFloor ? FMath::Min(FloorZ, PointWorld.Z) : PointWorld.Z;
				bHasFloor = true;
			}
		};

		ConsiderFloorLandmark(EMediaPipePoseLandmark::LeftAnkle);
		ConsiderFloorLandmark(EMediaPipePoseLandmark::RightAnkle);
		ConsiderFloorLandmark(EMediaPipePoseLandmark::LeftHeel);
		ConsiderFloorLandmark(EMediaPipePoseLandmark::RightHeel);
		ConsiderFloorLandmark(EMediaPipePoseLandmark::LeftFootIndex);
		ConsiderFloorLandmark(EMediaPipePoseLandmark::RightFootIndex);

		return bHasFloor ? FMath::Max(0.0f, HeadWorld.Z - FloorZ) : 0.0f;
	}

	bool TryReadQuestHandSide(const EControllerHand Hand, FQuestHandTrackingSnapshot& OutSnapshot)
	{
		TArray<IHandTracker*> HandTrackers = IModularFeatures::Get().GetModularFeatureImplementations<IHandTracker>(IHandTracker::GetModularFeatureName());
		OutSnapshot.HandTrackerCount = HandTrackers.Num();
		OutSnapshot.ValidHandTrackerCount = 0;
		for (const IHandTracker* HandTracker : HandTrackers)
		{
			if (!HandTracker || !HandTracker->IsHandTrackingStateValid())
			{
				continue;
			}
			++OutSnapshot.ValidHandTrackerCount;

			TArray<FVector> Positions;
			TArray<FQuat> Rotations;
			TArray<float> Radii;
			bool bTracked = false;
			if (!HandTracker->GetAllKeypointStates(Hand, Positions, Rotations, Radii, bTracked) ||
				Positions.Num() < QuestHandKeypointCount ||
				Rotations.Num() < QuestHandKeypointCount)
			{
				continue;
			}

			const bool bIsLeft = Hand == EControllerHand::Left;
			TStaticArray<FVector, QuestHandKeypointCount>& OutPositions = bIsLeft ? OutSnapshot.LeftPositionsWorld : OutSnapshot.RightPositionsWorld;
			TStaticArray<FQuat, QuestHandKeypointCount>& OutRotations = bIsLeft ? OutSnapshot.LeftRotationsWorld : OutSnapshot.RightRotationsWorld;
			TStaticArray<float, QuestHandKeypointCount>& OutRadii = bIsLeft ? OutSnapshot.LeftRadii : OutSnapshot.RightRadii;
			for (int32 Index = 0; Index < QuestHandKeypointCount; ++Index)
			{
				OutPositions[Index] = Positions[Index];
				OutRotations[Index] = Rotations[Index].GetNormalized();
				OutRadii[Index] = Radii.IsValidIndex(Index) ? Radii[Index] : 0.0f;
			}

			if (bIsLeft)
			{
				OutSnapshot.bHasLeft = 1;
				OutSnapshot.bLeftTracked = bTracked ? 1 : 0;
			}
			else
			{
				OutSnapshot.bHasRight = 1;
				OutSnapshot.bRightTracked = bTracked ? 1 : 0;
			}
			return true;
		}

		return false;
	}

	bool ReadQuestHandTrackingSnapshot(FQuestHandTrackingSnapshot& OutSnapshot)
	{
		OutSnapshot.Reset();
		const bool bLeft = TryReadQuestHandSide(EControllerHand::Left, OutSnapshot);
		const bool bRight = TryReadQuestHandSide(EControllerHand::Right, OutSnapshot);
		return bLeft || bRight;
	}

	FAutoConsoleCommandWithWorldAndArgs CmdCaptureQuestHandPose(
		TEXT("mp.CaptureQuestHandPose"),
		TEXT("Capture the current Quest/OpenXR hand snapshot to Saved/QuestHandReplays/<name>.json. Usage: mp.CaptureQuestHandPose closed_fist"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			const FString CaptureName = Args.Num() > 0 ? Args[0] : TEXT("quest_hand_pose");
			FQuestHandTrackingSnapshot Snapshot;
			const bool bReadAny = ReadQuestHandTrackingSnapshot(Snapshot);
			FMediaPipeQuestHandCaptureReplayTooling::CapturePose(CaptureName, Snapshot, bReadAny);
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdQuestHandReplayFile(
		TEXT("mp.QuestHandReplayFile"),
		TEXT("Load a Quest hand replay by capture name or path. Usage: mp.QuestHandReplayFile closed_fist, then mp.QuestHandReplay 1."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			if (Args.Num() <= 0)
			{
				UE_LOG(LogMediaPipePose, Warning, TEXT("mp.QuestHandReplayFile: usage mp.QuestHandReplayFile <name-or-path>."));
				return;
			}

			FMediaPipeQuestHandCaptureReplayTooling::LoadReplayFile(Args[0]);
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdStartQuestHandCaptureGuide(
		TEXT("mp.StartQuestHandCaptureGuide"),
		TEXT("Show VR text prompts and auto-capture Quest/OpenXR hand poses. Optional usage: mp.StartQuestHandCaptureGuide fist"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			const FString Prefix = Args.Num() > 0 ? Args[0] : TEXT("fist");
			FMediaPipeQuestHandCaptureReplayTooling::StartCaptureGuide(Prefix);
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdStopQuestHandCaptureGuide(
		TEXT("mp.StopQuestHandCaptureGuide"),
		TEXT("Stop the VR text Quest/OpenXR hand capture guide."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld*)
		{
			FMediaPipeQuestHandCaptureReplayTooling::StopCaptureGuide();
		}));

	bool TryBuildQuestHandBasisWorld(const FQuestHandTrackingSnapshot& Snapshot, const bool bIsLeft, FVector& OutForwardWorld, FVector& OutUpWorld, float& OutBasisSin, const bool bUseMannyHandBasisConvention = true)
	{
		if (!IsQuestHandSideAvailable(Snapshot, bIsLeft))
		{
			return false;
		}

		const TStaticArray<FVector, QuestHandKeypointCount>& Positions = GetQuestHandPositions(Snapshot, bIsLeft);
		const FVector Wrist = Positions[static_cast<int32>(EHandKeypoint::Wrist)];
		const FVector IndexProximal = Positions[static_cast<int32>(EHandKeypoint::IndexProximal)];
		const FVector LittleProximal = Positions[static_cast<int32>(EHandKeypoint::LittleProximal)];
		const FVector MiddleProximal = Positions[static_cast<int32>(EHandKeypoint::MiddleProximal)];

		FVector Forward = ((IndexProximal + LittleProximal) * 0.5f - Wrist).GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			Forward = (MiddleProximal - Wrist).GetSafeNormal();
		}

		const FVector Across = (IndexProximal - LittleProximal).GetSafeNormal();
		if (Forward.IsNearlyZero() || Across.IsNearlyZero())
		{
			return false;
		}

		FVector Up = FVector::CrossProduct(Forward, Across).GetSafeNormal();
		OutBasisSin = FVector::CrossProduct(Forward, Across).Size();
		if (bUseMannyHandBasisConvention && !bIsLeft)
		{
			Forward *= -1.0f;
		}
		if (Up.IsNearlyZero())
		{
			return false;
		}

		OutForwardWorld = Forward;
		OutUpWorld = Up;
		return true;
	}

	bool DecomposeSwingTwistDegAroundAxis(const FQuat& InRot, const FVector& Axis, FQuat& OutSwing, float& OutTwistDeg)
	{
		const FVector AxisN = Axis.GetSafeNormal();
		if (AxisN.IsNearlyZero())
		{
			return false;
		}

		const FQuat Q = InRot.GetNormalized();
		const FVector V(Q.X, Q.Y, Q.Z);
		const FVector Proj = FVector::DotProduct(V, AxisN) * AxisN;

		FQuat Twist(Proj.X, Proj.Y, Proj.Z, Q.W);
		const float TwistSizeSq = Twist.SizeSquared();
		if (TwistSizeSq <= KINDA_SMALL_NUMBER)
		{
			OutSwing = Q;
			OutTwistDeg = 0.0f;
			return true;
		}
		Twist.Normalize();

		OutSwing = (Q * Twist.Inverse()).GetNormalized();

		FVector TwistAxis = FVector::ZeroVector;
		float TwistRad = 0.0f;
		Twist.ToAxisAndAngle(TwistAxis, TwistRad);
		if (FVector::DotProduct(TwistAxis, AxisN) < 0.0f)
		{
			TwistRad *= -1.0f;
		}
		OutTwistDeg = FRotator::NormalizeAxis(FMath::RadiansToDegrees(TwistRad));
		return true;
	}

	FQuat FilterTargetRotationSwingTwist(
		const FQuat& RefRot,
		const FVector& TwistAxis,
		const FQuat& TargetRot,
		const float TwistWeight,
		const float TwistClampDegrees)
	{
		const FVector AxisN = TwistAxis.GetSafeNormal();
		if (AxisN.IsNearlyZero())
		{
			return TargetRot.GetNormalized();
		}

		const FQuat Delta = (TargetRot * RefRot.Inverse()).GetNormalized();
		FQuat Swing = FQuat::Identity;
		float TwistDeg = 0.0f;
		if (!DecomposeSwingTwistDegAroundAxis(Delta, AxisN, Swing, TwistDeg))
		{
			return TargetRot.GetNormalized();
		}

		float FilteredTwistDeg = TwistDeg;
		if (TwistClampDegrees > 0.0f)
		{
			FilteredTwistDeg = FMath::Clamp(FilteredTwistDeg, -TwistClampDegrees, TwistClampDegrees);
		}

		const float TwistW = FMath::Clamp(TwistWeight, 0.0f, 1.0f);
		FilteredTwistDeg *= TwistW;

		const FQuat TwistScaled(AxisN, FMath::DegreesToRadians(FilteredTwistDeg));
		return (Swing * TwistScaled * RefRot).GetNormalized();
	}

}

const FName FAnimNode_MediaPipePoseDriven::Bone_Root(TEXT("root"));
const FName FAnimNode_MediaPipePoseDriven::Bone_Pelvis(TEXT("pelvis"));
const FName FAnimNode_MediaPipePoseDriven::Bone_Spine01(TEXT("spine_01"));
const FName FAnimNode_MediaPipePoseDriven::Bone_Spine02(TEXT("spine_02"));
const FName FAnimNode_MediaPipePoseDriven::Bone_Spine03(TEXT("spine_03"));
const FName FAnimNode_MediaPipePoseDriven::Bone_Spine04(TEXT("spine_04"));
const FName FAnimNode_MediaPipePoseDriven::Bone_Spine05(TEXT("spine_05"));
const FName FAnimNode_MediaPipePoseDriven::Bone_Neck(TEXT("neck_01"));
const FName FAnimNode_MediaPipePoseDriven::Bone_Neck02(TEXT("neck_02"));
const FName FAnimNode_MediaPipePoseDriven::Bone_Head(TEXT("head"));

const FName FAnimNode_MediaPipePoseDriven::Bone_ClavicleL(TEXT("clavicle_l"));
const FName FAnimNode_MediaPipePoseDriven::Bone_UpperArmL(TEXT("upperarm_l"));
const FName FAnimNode_MediaPipePoseDriven::Bone_UpperArmTwist01L(TEXT("upperarm_twist_01_l"));
const FName FAnimNode_MediaPipePoseDriven::Bone_UpperArmTwist02L(TEXT("upperarm_twist_02_l"));
const FName FAnimNode_MediaPipePoseDriven::Bone_LowerArmL(TEXT("lowerarm_l"));
const FName FAnimNode_MediaPipePoseDriven::Bone_LowerArmTwist01L(TEXT("lowerarm_twist_01_l"));
const FName FAnimNode_MediaPipePoseDriven::Bone_LowerArmTwist02L(TEXT("lowerarm_twist_02_l"));
const FName FAnimNode_MediaPipePoseDriven::Bone_HandL(TEXT("hand_l"));

const FName FAnimNode_MediaPipePoseDriven::Bone_ClavicleR(TEXT("clavicle_r"));
const FName FAnimNode_MediaPipePoseDriven::Bone_UpperArmR(TEXT("upperarm_r"));
const FName FAnimNode_MediaPipePoseDriven::Bone_UpperArmTwist01R(TEXT("upperarm_twist_01_r"));
const FName FAnimNode_MediaPipePoseDriven::Bone_UpperArmTwist02R(TEXT("upperarm_twist_02_r"));
const FName FAnimNode_MediaPipePoseDriven::Bone_LowerArmR(TEXT("lowerarm_r"));
const FName FAnimNode_MediaPipePoseDriven::Bone_LowerArmTwist01R(TEXT("lowerarm_twist_01_r"));
const FName FAnimNode_MediaPipePoseDriven::Bone_LowerArmTwist02R(TEXT("lowerarm_twist_02_r"));
const FName FAnimNode_MediaPipePoseDriven::Bone_HandR(TEXT("hand_r"));

const FName FAnimNode_MediaPipePoseDriven::Bone_ThighL(TEXT("thigh_l"));
const FName FAnimNode_MediaPipePoseDriven::Bone_CalfL(TEXT("calf_l"));
const FName FAnimNode_MediaPipePoseDriven::Bone_FootL(TEXT("foot_l"));
const FName FAnimNode_MediaPipePoseDriven::Bone_BallL(TEXT("ball_l"));

const FName FAnimNode_MediaPipePoseDriven::Bone_ThighR(TEXT("thigh_r"));
const FName FAnimNode_MediaPipePoseDriven::Bone_CalfR(TEXT("calf_r"));
const FName FAnimNode_MediaPipePoseDriven::Bone_FootR(TEXT("foot_r"));
const FName FAnimNode_MediaPipePoseDriven::Bone_BallR(TEXT("ball_r"));

static FVector LockVectorToHemisphere(const FVector& Vector, const FVector& Reference);

FAnimNode_MediaPipePoseDriven::FAnimNode_MediaPipePoseDriven()
{
	Root.BoneName = Bone_Root;
	Pelvis.BoneName = Bone_Pelvis;
	Spine01.BoneName = Bone_Spine01;
	Spine02.BoneName = Bone_Spine02;
	Spine03.BoneName = Bone_Spine03;
	Spine04.BoneName = Bone_Spine04;
	Spine05.BoneName = Bone_Spine05;
	Neck.BoneName = Bone_Neck;
	Neck02.BoneName = Bone_Neck02;
	Head.BoneName = Bone_Head;

	ClavicleL.BoneName = Bone_ClavicleL;
	UpperArmL.BoneName = Bone_UpperArmL;
	UpperArmTwist01L.BoneName = Bone_UpperArmTwist01L;
	UpperArmTwist02L.BoneName = Bone_UpperArmTwist02L;
	LowerArmL.BoneName = Bone_LowerArmL;
	LowerArmTwist01L.BoneName = Bone_LowerArmTwist01L;
	LowerArmTwist02L.BoneName = Bone_LowerArmTwist02L;
	HandL.BoneName = Bone_HandL;
	for (int32 Index = 0; Index < MediaPipeMetaHumanClavicleHelperCount; ++Index)
	{
		MetaHumanClavicleHelpersL[Index].BoneName = FName(MetaHumanClavicleHelperBoneNamesL[Index]);
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanUpperArmHelperCount; ++Index)
	{
		MetaHumanUpperArmHelpersL[Index].BoneName = FName(MetaHumanUpperArmHelperBoneNamesL[Index]);
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanLowerArmHelperCount; ++Index)
	{
		MetaHumanLowerArmHelpersL[Index].BoneName = FName(MetaHumanLowerArmHelperBoneNamesL[Index]);
	}
	for (int32 Index = 0; Index < QuestFingerMetacarpalBoneCount; ++Index)
	{
		FingerMetacarpalBonesL[Index].BoneName = FName(QuestFingerMetacarpalBoneNamesL[Index]);
	}
	for (int32 Index = 0; Index < QuestFingerBoneCount; ++Index)
	{
		FingerBonesL[Index].BoneName = FName(QuestFingerBoneNamesL[Index]);
	}

	ClavicleR.BoneName = Bone_ClavicleR;
	UpperArmR.BoneName = Bone_UpperArmR;
	UpperArmTwist01R.BoneName = Bone_UpperArmTwist01R;
	UpperArmTwist02R.BoneName = Bone_UpperArmTwist02R;
	LowerArmR.BoneName = Bone_LowerArmR;
	LowerArmTwist01R.BoneName = Bone_LowerArmTwist01R;
	LowerArmTwist02R.BoneName = Bone_LowerArmTwist02R;
	HandR.BoneName = Bone_HandR;
	for (int32 Index = 0; Index < MediaPipeMetaHumanClavicleHelperCount; ++Index)
	{
		MetaHumanClavicleHelpersR[Index].BoneName = FName(MetaHumanClavicleHelperBoneNamesR[Index]);
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanUpperArmHelperCount; ++Index)
	{
		MetaHumanUpperArmHelpersR[Index].BoneName = FName(MetaHumanUpperArmHelperBoneNamesR[Index]);
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanLowerArmHelperCount; ++Index)
	{
		MetaHumanLowerArmHelpersR[Index].BoneName = FName(MetaHumanLowerArmHelperBoneNamesR[Index]);
	}
	for (int32 Index = 0; Index < QuestFingerMetacarpalBoneCount; ++Index)
	{
		FingerMetacarpalBonesR[Index].BoneName = FName(QuestFingerMetacarpalBoneNamesR[Index]);
	}
	for (int32 Index = 0; Index < QuestFingerBoneCount; ++Index)
	{
		FingerBonesR[Index].BoneName = FName(QuestFingerBoneNamesR[Index]);
	}

	ThighL.BoneName = Bone_ThighL;
	CalfL.BoneName = Bone_CalfL;
	FootL.BoneName = Bone_FootL;
	BallL.BoneName = Bone_BallL;

	ThighR.BoneName = Bone_ThighR;
	CalfR.BoneName = Bone_CalfR;
	FootR.BoneName = Bone_FootR;
	BallR.BoneName = Bone_BallR;
}

void FAnimNode_MediaPipePoseDriven::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);
	CachedDeltaTimeSeconds = 0.0f;
}

void FAnimNode_MediaPipePoseDriven::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	FAnimNode_Base::CacheBones_AnyThread(Context);

	const FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();

	Root.Initialize(RequiredBones);
	Pelvis.Initialize(RequiredBones);
	Spine01.Initialize(RequiredBones);
	Spine02.Initialize(RequiredBones);
	Spine03.Initialize(RequiredBones);
	Spine04.Initialize(RequiredBones);
	Spine05.Initialize(RequiredBones);
	Neck.Initialize(RequiredBones);
	Neck02.Initialize(RequiredBones);
	Head.Initialize(RequiredBones);

	ClavicleL.Initialize(RequiredBones);
	UpperArmL.Initialize(RequiredBones);
	UpperArmTwist01L.Initialize(RequiredBones);
	UpperArmTwist02L.Initialize(RequiredBones);
	LowerArmL.Initialize(RequiredBones);
	LowerArmTwist01L.Initialize(RequiredBones);
	LowerArmTwist02L.Initialize(RequiredBones);
	HandL.Initialize(RequiredBones);
	for (int32 Index = 0; Index < MediaPipeMetaHumanClavicleHelperCount; ++Index)
	{
		MetaHumanClavicleHelpersL[Index].Initialize(RequiredBones);
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanUpperArmHelperCount; ++Index)
	{
		MetaHumanUpperArmHelpersL[Index].Initialize(RequiredBones);
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanLowerArmHelperCount; ++Index)
	{
		MetaHumanLowerArmHelpersL[Index].Initialize(RequiredBones);
	}
	for (int32 Index = 0; Index < QuestFingerMetacarpalBoneCount; ++Index)
	{
		FingerMetacarpalBonesL[Index].Initialize(RequiredBones);
	}
	for (int32 Index = 0; Index < QuestFingerBoneCount; ++Index)
	{
		FingerBonesL[Index].Initialize(RequiredBones);
	}

	ClavicleR.Initialize(RequiredBones);
	UpperArmR.Initialize(RequiredBones);
	UpperArmTwist01R.Initialize(RequiredBones);
	UpperArmTwist02R.Initialize(RequiredBones);
	LowerArmR.Initialize(RequiredBones);
	LowerArmTwist01R.Initialize(RequiredBones);
	LowerArmTwist02R.Initialize(RequiredBones);
	HandR.Initialize(RequiredBones);
	for (int32 Index = 0; Index < MediaPipeMetaHumanClavicleHelperCount; ++Index)
	{
		MetaHumanClavicleHelpersR[Index].Initialize(RequiredBones);
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanUpperArmHelperCount; ++Index)
	{
		MetaHumanUpperArmHelpersR[Index].Initialize(RequiredBones);
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanLowerArmHelperCount; ++Index)
	{
		MetaHumanLowerArmHelpersR[Index].Initialize(RequiredBones);
	}
	for (int32 Index = 0; Index < QuestFingerMetacarpalBoneCount; ++Index)
	{
		FingerMetacarpalBonesR[Index].Initialize(RequiredBones);
	}
	for (int32 Index = 0; Index < QuestFingerBoneCount; ++Index)
	{
		FingerBonesR[Index].Initialize(RequiredBones);
	}

	ThighL.Initialize(RequiredBones);
	CalfL.Initialize(RequiredBones);
	FootL.Initialize(RequiredBones);
	BallL.Initialize(RequiredBones);

	ThighR.Initialize(RequiredBones);
	CalfR.Initialize(RequiredBones);
	FootR.Initialize(RequiredBones);
	BallR.Initialize(RequiredBones);

	bHasReferencePose = BuildReferencePoseCache(RequiredBones);
}

void FAnimNode_MediaPipePoseDriven::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	FAnimNode_Base::Update_AnyThread(Context);
	GetEvaluateGraphExposedInputs().Execute(Context);
	CachedDeltaTimeSeconds += Context.GetDeltaTime();
}

void FAnimNode_MediaPipePoseDriven::PreUpdate(const UAnimInstance* InAnimInstance)
{
	TargetActorName = NAME_None;
	TargetEmbodimentProfile = FMediaPipeAvatarEmbodimentProfile();
	bHasTargetEmbodimentProfile = false;
	bUseTargetFaceForwardAxis = false;
	bHasTargetEyeLocalOffset = false;
	TargetEyeLocalOffset = FVector::ZeroVector;
	TargetEmbodiedCameraForwardOffsetCm = 0.0f;
	RuntimeStateKey = 0;
	QuestHands.Reset();
	FullArmChain.Reset();
	BodyFusionSourceFrame.Reset();
	LastBodyFusionPose.Reset();
	LastBodyFusionAuthority = FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly();
	LastBodyFusionAuthorityState = EMediaPipeBodyFusionAuthorityState::NoMediaPipe;
	LastBodyFusionAuthorityReason.Reset();
	bLastBodyFusionMediaPipeAuthorityAllowed = 0;
	TargetMetaHumanProfile.Reset();
	bHasCachedQuestHmdPose = false;
	CachedQuestHmdWorld = FVector::ZeroVector;
	CachedQuestHmdRotWorld = FQuat::Identity;
	CachedQuestTrackingUpWorld = FVector::UpVector;

	const USkeletalMeshComponent* SkelComp = InAnimInstance ? InAnimInstance->GetSkelMeshComponent() : nullptr;
	if (!SkelComp)
	{
		return;
	}
	RuntimeStateKey = SkelComp->GetUniqueID();
	TargetCompTransform = SkelComp->GetComponentTransform();

	if (AActor* TargetActor = SkelComp->GetOwner())
	{
		TargetActorName = FName(*TargetActor->GetActorNameOrLabel());
	}
	if (ResolveMediaPipeMetaHumanProfileForComponent(const_cast<USkeletalMeshComponent*>(SkelComp), TargetMetaHumanProfile))
	{
		TargetEmbodimentProfile = BuildMediaPipeAvatarEmbodimentProfileFromMetaHumanProfile(TargetMetaHumanProfile.Profile);
		bHasTargetEmbodimentProfile = TargetEmbodimentProfile.IsValid();
		bUseTargetFaceForwardAxis = TargetEmbodimentProfile.bUseTargetFaceForwardAxis;
		TargetEyeLocalOffset = TargetEmbodimentProfile.DefaultEyeLocalOffset;
		bHasTargetEyeLocalOffset = !TargetEyeLocalOffset.ContainsNaN();
		TargetEmbodiedCameraForwardOffsetCm = TargetEmbodimentProfile.EmbodiedCameraForwardOffsetCm;
		TargetActorName = TargetMetaHumanProfile.TargetActorName.IsNone()
			? TargetActorName
			: TargetMetaHumanProfile.TargetActorName;
	}
	else
	{
		FMediaPipeAvatarRigProfile AvatarRigProfile;
		if (TryResolveMediaPipeAvatarRigProfileForMesh(SkelComp->GetSkeletalMeshAsset(), AvatarRigProfile))
		{
			TargetEmbodimentProfile = BuildMediaPipeAvatarEmbodimentProfileFromRigProfile(AvatarRigProfile);
			bHasTargetEmbodimentProfile = TargetEmbodimentProfile.IsValid();
			bUseTargetFaceForwardAxis = TargetEmbodimentProfile.bUseTargetFaceForwardAxis;
			TargetEyeLocalOffset = TargetEmbodimentProfile.DefaultEyeLocalOffset;
			bHasTargetEyeLocalOffset = !TargetEyeLocalOffset.ContainsNaN();
			TargetEmbodiedCameraForwardOffsetCm = TargetEmbodimentProfile.EmbodiedCameraForwardOffsetCm;
		}
	}
	ApplyReferencePoseProportionsToTargetProfile();

	if (bResetPoseStateNextUpdate)
	{
		BodyState.bHasReferenceHipHeight = false;
		BodyState.ReferenceHipHeightCm = 0.0f;
		BodyState.bHasSmoothedPelvisOffset = false;
		BodyState.SmoothedPelvisOffsetComp = FVector::ZeroVector;
		BodyState.bHasSmoothedFkRootGroundOffset = false;
		BodyState.SmoothedFkRootGroundOffsetComp = FVector::ZeroVector;
		LeftArmState.bHasSmoothedArmIK = false;
		RightArmState.bHasSmoothedArmIK = false;
		LeftLegState.bHasSmoothedLegPlane = false;
		RightLegState.bHasSmoothedLegPlane = false;
		ResetFootPlantState();
		ResetPoseYawAlignRuntimeState(SkelComp);
		ResetQuestWristRuntimeState(SkelComp);
		ResetRotationSmoothing();
		MediaPipePoseFrameContinuity::ResetHeldFrame(
			bHasPoseFrame,
			PoseFrame,
			PoseTimestampSeconds,
			bHasPoseHands,
			PoseHands,
			PoseHandsTimestampUs);
		for (uint8& B : EverMeasured)
		{
			B = 0;
		}
		bResetPoseStateNextUpdate = false;
	}

	UWorld* World = InAnimInstance ? InAnimInstance->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	if (TargetMetaHumanProfile.bIsMetaHuman)
	{
		static TMap<uint32, FString> LastProfileLogStateByStateKey;
		static TMap<uint32, double> LastProfileLogTimeByStateKey;
		const FString CurrentProfileLogState = FString::Printf(
			TEXT("%s:%d:%d:%s"),
			*TargetMetaHumanProfile.ProfileId.ToString(),
			TargetMetaHumanProfile.bIsActiveProfile ? 1 : 0,
			TargetMetaHumanProfile.bValidationPassed ? 1 : 0,
			*TargetMetaHumanProfile.ValidationSummary);
		FString& LastProfileLogState = LastProfileLogStateByStateKey.FindOrAdd(RuntimeStateKey);
		double& LastProfileLogTime = LastProfileLogTimeByStateKey.FindOrAdd(RuntimeStateKey, -1.0);
		const double NowSeconds = World->GetTimeSeconds();
		if (LastProfileLogTime < 0.0 ||
			!LastProfileLogState.Equals(CurrentProfileLogState, ESearchCase::CaseSensitive) ||
			NowSeconds - LastProfileLogTime >= 5.0)
		{
			LastProfileLogState = CurrentProfileLogState;
			LastProfileLogTime = NowSeconds;
			UE_LOG(LogMediaPipePose, Log, TEXT("%s"), *FormatMediaPipeMetaHumanProfileResolutionLog(TargetMetaHumanProfile));
		}
	}

	if (TargetMetaHumanProfile.bIsMetaHuman && !TargetMetaHumanProfile.bValidationPassed)
	{
		static TMap<uint32, double> LastValidationLogTimeByStateKey;
		double& LastValidationLogTime = LastValidationLogTimeByStateKey.FindOrAdd(RuntimeStateKey, -1.0);
		const double NowSeconds = World->GetTimeSeconds();
		if (LastValidationLogTime < 0.0 || NowSeconds - LastValidationLogTime >= 5.0)
		{
			LastValidationLogTime = NowSeconds;
			UE_LOG(LogMediaPipePose, Warning, TEXT("mp.MetaHumanProfile: invalid profile=%s actor=%s validation=\"%s\"; full-chain MetaHuman solve is disabled until the target validates."),
				*TargetMetaHumanProfile.ProfileId.ToString(),
				*TargetActorName.ToString(),
				*TargetMetaHumanProfile.ValidationSummary);
		}
	}

	if (bUseQuestHandTracking && CVarQuestHandTracking.GetValueOnGameThread() != 0)
	{
		ReadQuestHandTrackingSnapshot(QuestHands);
		FString QuestHandReplayPath;
		const bool bUsingQuestHandReplay = FMediaPipeQuestHandCaptureReplayTooling::TryApplyReplaySnapshot(
			CVarQuestHandReplay.GetValueOnGameThread() != 0,
			QuestHands,
			&QuestHandReplayPath);
		bHasCachedQuestHmdPose = TryReadQuestHmdWorldPose_GameThread(CachedQuestHmdWorld, CachedQuestHmdRotWorld, &CachedQuestTrackingUpWorld);

		FVector GuideViewWorld = SkelComp->GetComponentLocation() + FVector(0.0, 0.0, 160.0);
		FQuat GuideViewRotWorld = SkelComp->GetComponentQuat();
		if (bHasCachedQuestHmdPose)
		{
			GuideViewWorld = CachedQuestHmdWorld;
			GuideViewRotWorld = CachedQuestHmdRotWorld;
		}
		FMediaPipeQuestHandCaptureReplayTooling::TickCaptureGuide(World, QuestHands, GuideViewWorld, GuideViewRotWorld);

		if (CVarQuestHandDebug.GetValueOnGameThread() != 0 ||
			CVarQuestWristDebug.GetValueOnGameThread() != 0 ||
			CVarQuestWristTrace.GetValueOnGameThread() != 0)
		{
			const double NowSeconds = World->GetTimeSeconds();
			FMediaPipeQuestWristDebugReporter::EmitSnapshotLogs(
				TargetActorName,
				QuestHands,
				bHasCachedQuestHmdPose,
				CachedQuestHmdWorld,
				bUsingQuestHandReplay,
				QuestHandReplayPath,
				NowSeconds,
				DiagnosticsState.LastQuestHandDebugLogTimeSeconds);
		}

		if (CVarQuestHandHud.GetValueOnGameThread() != 0)
		{
			const double NowSeconds = World->GetTimeSeconds();
			FMediaPipeQuestHandDebugReporter::DisplayHud(NowSeconds, DiagnosticsState.LastQuestHandHudTimeSeconds, QuestHands);
		}

		if (CVarQuestHandCompare.GetValueOnGameThread() >= 3)
		{
			FMediaPipeQuestHandDebugReporter::DrawSkeletonWorld(World, QuestHands, true);
			FMediaPipeQuestHandDebugReporter::DrawSkeletonWorld(World, QuestHands, false);
		}
	}

	if (TargetMetaHumanProfile.IsValidForPoseDriving() &&
		ResolveMediaPipeMetaHumanArmSourceMode(TargetMetaHumanProfile) == 1)
	{
		ReadLatestMediaPipeFullArmChainSnapshot(FullArmChain);
	}

	const bool bBodyFusionRuntimeActive = CVarBodyFusionEnable.GetValueOnGameThread() != 0;
	if (bBodyFusionRuntimeActive && !bHasCachedQuestHmdPose)
	{
		bHasCachedQuestHmdPose = TryReadQuestHmdWorldPose_GameThread(
			CachedQuestHmdWorld,
			CachedQuestHmdRotWorld,
			&CachedQuestTrackingUpWorld);
	}

	if (CVarQuestWristCalibrationHud.GetValueOnGameThread() != 0)
	{
		const FQuestWristRuntimeState& WristState = GetQuestWristRuntimeState(RuntimeStateKey);
		const FQuestWristSideRuntimeState& LeftState = WristState.Left;
		const FQuestWristSideRuntimeState& RightState = WristState.Right;
		const FVector StatusWorld = SkelComp->GetComponentLocation() + FVector(0.0, 0.0, 185.0);
		FMediaPipeQuestWristDebugReporter::DisplayCalibrationHud(
			World, StatusWorld,
			FMediaPipeQuestWristCalibrationSideFormatInput(
					QuestHands.bLeftTracked != 0,
					LeftState.RotationCalibrationState,
					LeftState.RotationCalibrationRejectReason,
					LeftState.RotationCalibrationStableFrameCount,
					LeftState.RotationCalibrationLastBasisErrorDeg,
					LeftState.RotationCalibrationLastNeutralTwistDeg),
			FMediaPipeQuestWristCalibrationSideFormatInput(
					QuestHands.bRightTracked != 0,
					RightState.RotationCalibrationState,
					RightState.RotationCalibrationRejectReason,
					RightState.RotationCalibrationStableFrameCount,
					RightState.RotationCalibrationLastBasisErrorDeg,
					RightState.RotationCalibrationLastNeutralTwistDeg));
	}

	const bool bQuestArmLengthCalibrationHudOwner =
		TargetMetaHumanProfile.bIsMetaHuman && TargetMetaHumanProfile.bIsActiveProfile;
	if (bQuestArmLengthCalibrationHudOwner &&
		CVarQuestArmLengthCalibrationHud.GetValueOnGameThread() != 0 &&
		CVarQuestArmLengthCalibrationStartup.GetValueOnGameThread() != 0)
	{
		const FQuestWristRuntimeState& WristState = GetQuestWristRuntimeState(RuntimeStateKey);
		const double NowSeconds = World->GetTimeSeconds();
		const bool bShowAccepted =
			WristState.ArmLengthCalibrationStage != QuestArmLengthCalibrationStage_Accepted ||
			WristState.ArmLengthCalibrationAcceptedTimeSeconds < 0.0 ||
			NowSeconds - WristState.ArmLengthCalibrationAcceptedTimeSeconds <= 2.5;
		if (bShowAccepted)
		{
			const FVector UpWorld = CachedQuestTrackingUpWorld.IsNearlyZero() ? FVector::UpVector : CachedQuestTrackingUpWorld.GetSafeNormal();
			FVector StatusWorld = SkelComp->GetComponentLocation() + FVector(0.0, 0.0, 185.0);
			if (bHasCachedQuestHmdPose)
			{
				const FVector ForwardWorld = CachedQuestHmdRotWorld.RotateVector(FVector::ForwardVector).GetSafeNormal();
				StatusWorld = CachedQuestHmdWorld + ForwardWorld * 95.0f - UpWorld * 18.0f;
			}

			const float ReachFraction = FMath::Clamp(CVarQuestConstrainedArmMaxReachFraction.GetValueOnGameThread(), 0.50f, 0.999f);
			float TargetReachCm = 0.0f;
			int32 TargetReachCount = 0;
			if (bHasRefArmL)
			{
				TargetReachCm += (RefUpperLenCompL + RefLowerLenCompL) * ReachFraction;
				++TargetReachCount;
			}
			if (bHasRefArmR)
			{
				TargetReachCm += (RefUpperLenCompR + RefLowerLenCompR) * ReachFraction;
				++TargetReachCount;
			}
			if (TargetReachCount > 0)
			{
				TargetReachCm /= static_cast<float>(TargetReachCount);
			}

			FMediaPipeQuestWristDebugReporter::DisplayArmLengthCalibrationHud(
				World,
				StatusWorld,
				WristState.ArmLengthCalibrationStage,
				WristState.ArmLengthCalibrationStableFrameCount,
				WristState.ArmLengthCalibrationStableSeconds,
				FMath::Max(0.0f, CVarQuestArmLengthCalibrationHoldSeconds.GetValueOnGameThread()),
				WristState.Left.ArmLengthCalibrationForwardReachCm,
				WristState.Right.ArmLengthCalibrationForwardReachCm,
				WristState.Left.ArmLengthCalibrationDownDropCm,
				WristState.Right.ArmLengthCalibrationDownDropCm,
				TargetReachCm);
		}
	}

	if (!SourceActor)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Candidate = *It;
			if (!Candidate)
			{
				continue;
			}

			if (Candidate->FindComponentByClass<UMediaPipePoseTrackerComponent>())
			{
				SourceActor = Candidate;
				break;
			}
		}
	}

	UMediaPipePoseTrackerComponent* TrackerComp = SourceActor ? SourceActor->FindComponentByClass<UMediaPipePoseTrackerComponent>() : nullptr;
	FMediaPipePoseFrame Frame;
	const bool bHasLivePoseFrame = TrackerComp && TrackerComp->GetLatestFrame(Frame) && Frame.bValid;
	const MediaPipePoseFrameContinuity::EFrameAvailability FrameAvailability =
		MediaPipePoseFrameContinuity::ResolveFrameAvailability(bHasLivePoseFrame ? &Frame : nullptr, bHasPoseFrame);
	if (FrameAvailability != MediaPipePoseFrameContinuity::EFrameAvailability::Live)
	{
		return;
	}

	const FTransform NewTargetCompTransform = SkelComp->GetComponentTransform();
	const FTransform NewPoseToWorldTransform = SourceActor->GetActorTransform();
	const float NewWorldScale = TrackerComp->WorldScale;
	const bool bNewMirrorLandmarksLR = TrackerComp->bMirrorLandmarksLR;
	if (bUseQuestHandTracking &&
		CVarQuestHandTracking.GetValueOnGameThread() != 0 &&
		CVarQuestHandCompare.GetValueOnGameThread() >= 2 &&
		bHasCachedQuestHmdPose)
	{
		const float DebugHandPositionScale = FMath::Max(0.0f, CVarQuestWristPositionScale.GetValueOnGameThread());
		const float DebugHandMaxOffsetCm = FMath::Max(0.0f, CVarQuestWristMaxOffsetCm.GetValueOnGameThread());
		FMediaPipeAvatarEmbodimentProfile DebugTargetProfile = bHasTargetEmbodimentProfile
			? TargetEmbodimentProfile
			: FMediaPipeAvatarEmbodimentProfile();
		DebugTargetProfile.bUseTargetFaceForwardAxis = bUseTargetFaceForwardAxis;
		DebugTargetProfile.DefaultEyeLocalOffset = bHasTargetEyeLocalOffset
			? TargetEyeLocalOffset
			: DebugTargetProfile.DefaultEyeLocalOffset;
		DebugTargetProfile.EmbodiedCameraForwardOffsetCm = TargetEmbodiedCameraForwardOffsetCm;
		FMediaPipeQuestHandDebugReporter::DrawSkeletonHmdRelativeAvatarWorld(
			World,
			QuestHands,
			true,
			CachedQuestHmdWorld,
			CachedQuestHmdRotWorld,
			CachedQuestTrackingUpWorld,
			NewTargetCompTransform,
			DebugTargetProfile,
			DebugHandPositionScale,
			DebugHandMaxOffsetCm);
		FMediaPipeQuestHandDebugReporter::DrawSkeletonHmdRelativeAvatarWorld(
			World,
			QuestHands,
			false,
			CachedQuestHmdWorld,
			CachedQuestHmdRotWorld,
			CachedQuestTrackingUpWorld,
			NewTargetCompTransform,
			DebugTargetProfile,
			DebugHandPositionScale,
			DebugHandMaxOffsetCm);
	}
	FPoseYawAlignRuntimeState& PoseYawAlignState = GetPoseYawAlignRuntimeState(SkelComp);
	const bool bPoseYawAlignToActor = CVarMediaPipePoseYawAlignToActor.GetValueOnGameThread() != 0;
	if (bPoseYawAlignToActor != PoseYawAlignState.bWasEnabled)
	{
		BodyState.bHasStableTorsoForwardWorld = false;
		BodyState.StableTorsoForwardWorld = FVector::ZeroVector;
		BodyState.bHasStableTorsoUpWorld = false;
		BodyState.StableTorsoUpWorld = FVector::ZeroVector;
		PoseYawAlignState.Reset();
		PoseYawAlignState.bWasEnabled = bPoseYawAlignToActor;
	}

	FMediaPipeSolvedPose SolvedPose;
	const FMediaPipeSolvedPoseOptions SolvedOptions = MediaPipeSolvedPose::MakeDefaultOptions(NewWorldScale, bNewMirrorLandmarksLR);
	if (!MediaPipeSolvedPose::BuildLocal(Frame, SolvedOptions, SolvedPose))
	{
		return;
	}

	PoseFrame = Frame;
	PoseTimestampSeconds = static_cast<double>(Frame.TimestampUs) * 1.0e-6;
	TargetCompTransform = NewTargetCompTransform;
	PoseToWorldTransform = NewPoseToWorldTransform;
	WorldScale = NewWorldScale;
	bMirrorLandmarksLR = bNewMirrorLandmarksLR;
	PoseHands = Frame.Hands;
	PoseHandsTimestampUs = Frame.TimestampUs;
	bHasPoseHands = Frame.bHasHands && (Frame.Hands.bHasLeft != 0 || Frame.Hands.bHasRight != 0);

	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		EverMeasured[Index] = 0;
		PoseWorld[Index] = FVector::ZeroVector;
	}

	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		EverMeasured[Index] = 1;
		PoseWorld[Index] = PoseToWorldTransform.TransformPosition(SolvedPose.LandmarksLocal[Index]);
	}

	if (bPoseYawAlignToActor)
	{
		constexpr int32 LShoulderIdx = static_cast<int32>(EMediaPipePoseLandmark::LeftShoulder);
		constexpr int32 RShoulderIdx = static_cast<int32>(EMediaPipePoseLandmark::RightShoulder);
		constexpr int32 LHipIdx = static_cast<int32>(EMediaPipePoseLandmark::LeftHip);
		constexpr int32 RHipIdx = static_cast<int32>(EMediaPipePoseLandmark::RightHip);
		constexpr int32 NoseIdx = static_cast<int32>(EMediaPipePoseLandmark::Nose);

		const FVector LShoulder = PoseWorld[LShoulderIdx];
		const FVector RShoulder = PoseWorld[RShoulderIdx];
		const FVector LHip = PoseWorld[LHipIdx];
		const FVector RHip = PoseWorld[RHipIdx];
		const FVector Nose = PoseWorld[NoseIdx];
		const FVector ShoulderMid = (LShoulder + RShoulder) * 0.5f;
		const FVector HipMid = (LHip + RHip) * 0.5f;
		const FVector Anchor = (ShoulderMid + HipMid) * 0.5f;

		FVector RawUp = (ShoulderMid - HipMid).GetSafeNormal();
		FVector RawHipRight = (RHip - LHip).GetSafeNormal();
		RawHipRight = (RawHipRight - FVector::DotProduct(RawHipRight, RawUp) * RawUp).GetSafeNormal();
		FVector RawForward = FVector::CrossProduct(RawHipRight, RawUp).GetSafeNormal();
		if (!RawForward.IsNearlyZero())
		{
			FVector NoseReference = Nose - ShoulderMid;
			NoseReference = (NoseReference - FVector::DotProduct(NoseReference, RawUp) * RawUp).GetSafeNormal();
			if (NoseReference.IsNearlyZero())
			{
				NoseReference = PoseToWorldTransform.GetUnitAxis(EAxis::X);
			}
			RawForward = LockVectorToHemisphere(RawForward, NoseReference);
		}

		auto ProjectHorizontal = [](const FVector& Vector) -> FVector
		{
			FVector Horizontal(Vector.X, Vector.Y, 0.0f);
			return Horizontal.GetSafeNormal();
		};

		const FVector RawForwardHorizontal = ProjectHorizontal(RawForward);
		const FVector DesiredActorForwardHorizontal = ProjectHorizontal(GetTargetForwardWorld());
		const bool bCanYawAlign = !RawUp.IsNearlyZero() && !RawHipRight.IsNearlyZero() && !RawForwardHorizontal.IsNearlyZero() && !DesiredActorForwardHorizontal.IsNearlyZero();

		float RawYawDeg = 0.0f;
		float DesiredYawDeg = 0.0f;
		float TargetDeltaYawDeg = 0.0f;
		float AppliedDeltaYawDeg = 0.0f;
		float RemainingYawErrorDeg = 0.0f;
		float RawYawJumpDeg = 0.0f;
		float DesiredYawJumpDeg = 0.0f;
		float TargetDeltaJumpDeg = 0.0f;
		float AlignDeltaSeconds = 0.0f;
		bool bRejectedYawJump = false;
		bool bRecenteredYawState = false;
		bool bAppliedYawAlignment = false;
		FVector CorrectedForwardHorizontal = RawForwardHorizontal;
		if (bCanYawAlign)
		{
			RawYawDeg = FMath::RadiansToDegrees(FMath::Atan2(RawForwardHorizontal.Y, RawForwardHorizontal.X));
			DesiredYawDeg = FMath::RadiansToDegrees(FMath::Atan2(DesiredActorForwardHorizontal.Y, DesiredActorForwardHorizontal.X));
			TargetDeltaYawDeg = FMath::FindDeltaAngleDegrees(RawYawDeg, DesiredYawDeg);

			const double NowSeconds = FPlatformTime::Seconds();
			AlignDeltaSeconds = PoseYawAlignState.LastUpdateTimeSeconds >= 0.0
				? FMath::Clamp(static_cast<float>(NowSeconds - PoseYawAlignState.LastUpdateTimeSeconds), 0.0f, 0.25f)
				: 0.0f;

			const float RejectJumpDegrees = FMath::Max(0.0f, CVarMediaPipePoseYawAlignRejectJumpDegrees.GetValueOnGameThread());
			if (!PoseYawAlignState.bHasState)
			{
				PoseYawAlignState.SmoothedDeltaDeg = TargetDeltaYawDeg;
				PoseYawAlignState.bHasState = true;
				bRecenteredYawState = true;
			}
			else
			{
				RawYawJumpDeg = FMath::Abs(FMath::FindDeltaAngleDegrees(PoseYawAlignState.LastRawYawDeg, RawYawDeg));
				DesiredYawJumpDeg = FMath::Abs(FMath::FindDeltaAngleDegrees(PoseYawAlignState.LastDesiredYawDeg, DesiredYawDeg));
				TargetDeltaJumpDeg = FMath::Abs(FMath::FindDeltaAngleDegrees(PoseYawAlignState.SmoothedDeltaDeg, TargetDeltaYawDeg));

				if (RejectJumpDegrees > KINDA_SMALL_NUMBER && DesiredYawJumpDeg > RejectJumpDegrees)
				{
					PoseYawAlignState.SmoothedDeltaDeg = TargetDeltaYawDeg;
					bRecenteredYawState = true;
				}
				else if (RejectJumpDegrees > KINDA_SMALL_NUMBER && TargetDeltaJumpDeg > RejectJumpDegrees)
				{
					bRejectedYawJump = true;
				}
				else
				{
					const float HalfLifeSeconds = FMath::Max(0.0f, CVarMediaPipePoseYawAlignHalfLife.GetValueOnGameThread());
					const float Alpha = HalfLifeToAlpha(HalfLifeSeconds, AlignDeltaSeconds);
					float StepDeg = FMath::FindDeltaAngleDegrees(PoseYawAlignState.SmoothedDeltaDeg, TargetDeltaYawDeg) * Alpha;

					const float MaxSpeedDegPerSecond = FMath::Max(0.0f, CVarMediaPipePoseYawAlignMaxSpeedDegreesPerSecond.GetValueOnGameThread());
					if (MaxSpeedDegPerSecond > KINDA_SMALL_NUMBER && AlignDeltaSeconds > KINDA_SMALL_NUMBER)
					{
						const float MaxStepDeg = MaxSpeedDegPerSecond * AlignDeltaSeconds;
						StepDeg = FMath::Clamp(StepDeg, -MaxStepDeg, MaxStepDeg);
					}

					PoseYawAlignState.SmoothedDeltaDeg = FRotator::NormalizeAxis(PoseYawAlignState.SmoothedDeltaDeg + StepDeg);
				}
			}

			if (!bRejectedYawJump)
			{
				PoseYawAlignState.LastRawYawDeg = RawYawDeg;
				PoseYawAlignState.LastDesiredYawDeg = DesiredYawDeg;
				PoseYawAlignState.LastUpdateTimeSeconds = NowSeconds;
				AppliedDeltaYawDeg = PoseYawAlignState.SmoothedDeltaDeg;
				bAppliedYawAlignment = true;
			}
			else
			{
				AppliedDeltaYawDeg = 0.0f;
			}

			const FQuat YawDeltaQuat(FVector::UpVector, FMath::DegreesToRadians(AppliedDeltaYawDeg));
			if (bAppliedYawAlignment && FMath::Abs(AppliedDeltaYawDeg) > KINDA_SMALL_NUMBER)
			{
				for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
				{
					PoseWorld[Index] = Anchor + YawDeltaQuat.RotateVector(PoseWorld[Index] - Anchor);
				}
			}

			CorrectedForwardHorizontal = ProjectHorizontal(YawDeltaQuat.RotateVector(RawForward));
			const float CorrectedYawDeg = FMath::RadiansToDegrees(FMath::Atan2(CorrectedForwardHorizontal.Y, CorrectedForwardHorizontal.X));
			RemainingYawErrorDeg = FMath::FindDeltaAngleDegrees(CorrectedYawDeg, DesiredYawDeg);
		}

		if (CVarMediaPipeTorsoDebug.GetValueOnGameThread() != 0)
		{
			const double NowSeconds = FPlatformTime::Seconds();
			if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, PoseYawAlignState.LastLogTimeSeconds))
			{
				FMediaPipePoseYawAlignLogInput YawLogInput;
				YawLogInput.TargetActorName = TargetActorName;
				YawLogInput.bAppliedYawAlignment = bAppliedYawAlignment;
				YawLogInput.bRejectedYawJump = bRejectedYawJump;
				YawLogInput.bRecenteredYawState = bRecenteredYawState;
				YawLogInput.RawForwardHorizontal = RawForwardHorizontal;
				YawLogInput.DesiredActorForwardHorizontal = DesiredActorForwardHorizontal;
				YawLogInput.CorrectedForwardHorizontal = CorrectedForwardHorizontal;
				YawLogInput.RawYawDeg = RawYawDeg;
				YawLogInput.DesiredYawDeg = DesiredYawDeg;
				YawLogInput.TargetDeltaYawDeg = TargetDeltaYawDeg;
				YawLogInput.AppliedDeltaYawDeg = AppliedDeltaYawDeg;
				YawLogInput.RemainingYawErrorDeg = RemainingYawErrorDeg;
				YawLogInput.RawYawJumpDeg = RawYawJumpDeg;
				YawLogInput.DesiredYawJumpDeg = DesiredYawJumpDeg;
				YawLogInput.TargetDeltaJumpDeg = TargetDeltaJumpDeg;
				YawLogInput.AlignDeltaSeconds = AlignDeltaSeconds;
				YawLogInput.Anchor = Anchor;
				YawLogInput.TargetActorYawDeg = TargetCompTransform.Rotator().Yaw;
				YawLogInput.SourceYawDeg = PoseToWorldTransform.Rotator().Yaw;
				FMediaPipeBodyDiagnostics::EmitPoseYawAlignLog(YawLogInput);
			}
		}
	}

	if (bBodyFusionRuntimeActive)
	{
		const double BodyFusionNowSeconds = FPlatformTime::Seconds();
		BuildBodyFusionSourceFrame_GameThread(BodyFusionNowSeconds);
		TryUpdateBodyFusionCalibration_GameThread(BodyFusionNowSeconds);

		FMediaPipeAvatarEmbodimentProfile BodyFusionProfile = bHasTargetEmbodimentProfile
			? TargetEmbodimentProfile
			: FMediaPipeAvatarEmbodimentProfile();
		BodyFusionProfile.bUseTargetFaceForwardAxis = bUseTargetFaceForwardAxis;
		BodyFusionProfile.DefaultEyeLocalOffset = bHasTargetEyeLocalOffset
			? TargetEyeLocalOffset
			: BodyFusionProfile.DefaultEyeLocalOffset;
		BodyFusionProfile.EmbodiedCameraForwardOffsetCm = TargetEmbodiedCameraForwardOffsetCm;

		const int32 MediaPipeAuthorityMode = CVarBodyFusionMediaPipeAuthority.GetValueOnGameThread();
		LastBodyFusionAuthority = FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly();
		LastBodyFusionAuthorityReason.Reset();
		LastBodyFusionAuthorityState = EMediaPipeBodyFusionAuthorityState::NoMediaPipe;
		bLastBodyFusionMediaPipeAuthorityAllowed = 0;
		if (MediaPipeAuthorityMode <= 0)
		{
			LastBodyFusionAuthorityReason = TEXT("trace-only");
		}
		else if (!BodyFusionCalibration.IsUsable())
		{
			LastBodyFusionAuthorityState = BodyFusionSourceFrame.MediaPipePoseStatus.IsFresh()
				? EMediaPipeBodyFusionAuthorityState::MediaPipeCalibrating
				: EMediaPipeBodyFusionAuthorityState::MediaPipeRejected;
			LastBodyFusionAuthorityReason = BodyFusionCalibration.LastRejectReason.IsEmpty()
				? TEXT("waiting for calibration")
				: BodyFusionCalibration.LastRejectReason;
		}
		else if (!BodyFusionSourceFrame.MediaPipePoseStatus.IsFresh())
		{
			LastBodyFusionAuthorityState = EMediaPipeBodyFusionAuthorityState::MediaPipeRejected;
			LastBodyFusionAuthorityReason = FString::Printf(
				TEXT("mediaPipe %s"),
				BodyFusionSourceStateName(BodyFusionSourceFrame.MediaPipePoseStatus.State));
		}
		else
		{
			LastBodyFusionAuthorityState = EMediaPipeBodyFusionAuthorityState::MediaPipeStable;
			LastBodyFusionAuthorityReason = MediaPipeAuthorityMode >= 2 ? TEXT("legacy calibrated fresh") : TEXT("stable calibrated fresh");
			bLastBodyFusionMediaPipeAuthorityAllowed = 1;
		}

		FMediaPipeBodyFusionSolveInput BodyFusionInput;
		BodyFusionInput.SourceFrame = BodyFusionSourceFrame;
		BodyFusionInput.Calibration = BodyFusionCalibration;
		BodyFusionInput.Authority = LastBodyFusionAuthority;
		BodyFusionInput.Profile = BodyFusionProfile;
		BodyFusionInput.AvatarWorldTransform = TargetCompTransform;
		BodyFusionInput.UserCameraForwardOffsetCm = 0.0f;
		BodyFusionInput.bAllowMediaPipePoseAuthority = bLastBodyFusionMediaPipeAuthorityAllowed != 0;
		BodyFusionInput.BodyAuthorityState = LastBodyFusionAuthorityState;
		FMediaPipeBodyFusionSolver::Solve(BodyFusionInput, LastBodyFusionPose);

		if (CVarBodyFusionDebug.GetValueOnGameThread() != 0)
		{
			EmitBodyFusionDebugLog_GameThread(BodyFusionNowSeconds);
		}
	}

	bHasPoseFrame = true;
}

void FAnimNode_MediaPipePoseDriven::BuildBodyFusionSourceFrame_GameThread(const double NowSeconds)
{
	BodyFusionSourceFrame.Reset();
	BodyFusionSourceFrame.FrameTimeSeconds = NowSeconds;

	if (!bHasCachedQuestHmdPose)
	{
		bHasCachedQuestHmdPose = TryReadQuestHmdWorldPose_GameThread(
			CachedQuestHmdWorld,
			CachedQuestHmdRotWorld,
			&CachedQuestTrackingUpWorld);
	}

	if (bHasCachedQuestHmdPose)
	{
		BodyFusionSourceFrame.bHasHmdPose = true;
		BodyFusionSourceFrame.HmdLocationWorld = CachedQuestHmdWorld;
		BodyFusionSourceFrame.HmdRotationWorld = CachedQuestHmdRotWorld;
		BodyFusionSourceFrame.TrackingUpWorld = CachedQuestTrackingUpWorld.IsNearlyZero()
			? FVector::UpVector
			: CachedQuestTrackingUpWorld.GetSafeNormal();
		BodyFusionSourceFrame.HmdTimestampSeconds = NowSeconds;
		BodyFusionSourceFrame.HmdConfidence = 1.0f;
	}

	auto PopulateQuestHandSide = [&](const bool bIsLeft)
	{
		if (!IsQuestHandSideUsableForWrist(QuestHands, bIsLeft))
		{
			return;
		}

		const TStaticArray<FVector, QuestHandKeypointCount>& Positions = GetQuestHandPositions(QuestHands, bIsLeft);
		const FVector WristWorld = Positions[static_cast<int32>(EHandKeypoint::Wrist)];
		const float Confidence = IsQuestHandSideTracked(QuestHands, bIsLeft) ? 1.0f : 0.5f;
		if (bIsLeft)
		{
			BodyFusionSourceFrame.bHasQuestLeftHand = true;
			BodyFusionSourceFrame.QuestLeftHandWorld = WristWorld;
			BodyFusionSourceFrame.QuestLeftHandTimestampSeconds = NowSeconds;
			BodyFusionSourceFrame.QuestLeftHandConfidence = Confidence;
		}
		else
		{
			BodyFusionSourceFrame.bHasQuestRightHand = true;
			BodyFusionSourceFrame.QuestRightHandWorld = WristWorld;
			BodyFusionSourceFrame.QuestRightHandTimestampSeconds = NowSeconds;
			BodyFusionSourceFrame.QuestRightHandConfidence = Confidence;
		}
	};
	PopulateQuestHandSide(true);
	PopulateQuestHandSide(false);

	auto PopulateFullArmChainSide = [&](const bool bIsLeft)
	{
		const FMediaPipeFullArmChainSideSnapshot& Side = FullArmChain.GetSide(bIsLeft);
		if (FullArmChain.bActive == 0 || Side.bActive == 0 || !Side.HasRequiredPositionChain())
		{
			return;
		}

		const FVector ShoulderWorld = Side.Shoulder.WorldTransform.GetLocation();
		const FVector ElbowWorld = Side.LowerArm.WorldTransform.GetLocation();
		const FVector WristWorld = Side.WristOrPalm.WorldTransform.GetLocation();
		const float Confidence = FMath::Max(Side.Confidence, FullArmChain.Confidence);
		if (bIsLeft)
		{
			BodyFusionSourceFrame.bHasQuestLeftFullArmChain = true;
			BodyFusionSourceFrame.QuestLeftShoulderWorld = ShoulderWorld;
			BodyFusionSourceFrame.QuestLeftElbowWorld = ElbowWorld;
			BodyFusionSourceFrame.QuestLeftWristWorld = WristWorld;
			BodyFusionSourceFrame.QuestLeftFullArmChainTimestampSeconds = Side.TimestampSeconds;
			BodyFusionSourceFrame.QuestLeftFullArmChainConfidence = Confidence;
		}
		else
		{
			BodyFusionSourceFrame.bHasQuestRightFullArmChain = true;
			BodyFusionSourceFrame.QuestRightShoulderWorld = ShoulderWorld;
			BodyFusionSourceFrame.QuestRightElbowWorld = ElbowWorld;
			BodyFusionSourceFrame.QuestRightWristWorld = WristWorld;
			BodyFusionSourceFrame.QuestRightFullArmChainTimestampSeconds = Side.TimestampSeconds;
			BodyFusionSourceFrame.QuestRightFullArmChainConfidence = Confidence;
		}
	};
	PopulateFullArmChainSide(true);
	PopulateFullArmChainSide(false);

	BodyFusionSourceFrame.bHasMediaPipePose = true;
	BodyFusionSourceFrame.MediaPipePoseTimestampSeconds = NowSeconds;
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		FVector LandmarkWorld = FVector::ZeroVector;
		if (IsMeasured(Index) && TryGetLmWorld(Index, LandmarkWorld))
		{
			BodyFusionSourceFrame.SetMediaPipeLandmark(
				static_cast<EMediaPipePoseLandmark>(Index),
				LandmarkWorld,
				GetLandmarkReliability(Index));
		}
	}

	const TArray<EMediaPipePoseLandmark> CoreReliabilityLandmarks = {
		EMediaPipePoseLandmark::Nose,
		EMediaPipePoseLandmark::LeftShoulder,
		EMediaPipePoseLandmark::RightShoulder,
		EMediaPipePoseLandmark::LeftHip,
		EMediaPipePoseLandmark::RightHip,
		EMediaPipePoseLandmark::LeftKnee,
		EMediaPipePoseLandmark::RightKnee,
		EMediaPipePoseLandmark::LeftAnkle,
		EMediaPipePoseLandmark::RightAnkle
	};
	BodyFusionSourceFrame.MediaPipePoseConfidence = BodyFusionAverageReliability(
		BodyFusionSourceFrame.MediaPipeLandmarkReliability,
		BodyFusionSourceFrame.MediaPipeLandmarkValid,
		CoreReliabilityLandmarks);

	FMediaPipeBodyFusionFreshnessThresholds Thresholds;
	Thresholds.QuestFullArmChainMaxAgeSeconds =
		TargetMetaHumanProfile.IsValidForPoseDriving()
			? ResolveMediaPipeMetaHumanFullArmChainMaxAgeSeconds(TargetMetaHumanProfile)
			: Thresholds.QuestFullArmChainMaxAgeSeconds;
	BodyFusionSourceFrame.UpdateFreshness(Thresholds);
}

bool FAnimNode_MediaPipePoseDriven::TryUpdateBodyFusionCalibration_GameThread(const double NowSeconds)
{
	const int32 ResetSerial = GBodyFusionCalibrationResetSerial.GetValue();
	if (LastBodyFusionCalibrationResetSerial != ResetSerial)
	{
		LastBodyFusionCalibrationResetSerial = ResetSerial;
		BodyFusionCalibration.Reset();
		BodyFusionCalibrationStableFrameCount = 0;
		BodyFusionCalibrationStableSeconds = 0.0f;
		LastBodyFusionCalibrationUpdateTimeSeconds = -1.0;
		if (CVarBodyFusionDebug.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.BodyFusion.Calibration actor=%s reset serial=%d"),
				*TargetActorName.ToString(),
				ResetSerial);
		}
	}

	if (BodyFusionCalibration.IsUsable())
	{
		return true;
	}

	FVector HipCenterWorld = FVector::ZeroVector;
	FVector ShoulderCenterWorld = FVector::ZeroVector;
	float HipReliability = 0.0f;
	float ShoulderReliability = 0.0f;
	const bool bHasHipCenter = TryBodyFusionLandmarkMidpoint(
		BodyFusionSourceFrame,
		EMediaPipePoseLandmark::LeftHip,
		EMediaPipePoseLandmark::RightHip,
		HipCenterWorld,
		&HipReliability);
	const bool bHasShoulderCenter = TryBodyFusionLandmarkMidpoint(
		BodyFusionSourceFrame,
		EMediaPipePoseLandmark::LeftShoulder,
		EMediaPipePoseLandmark::RightShoulder,
		ShoulderCenterWorld,
		&ShoulderReliability);

	FVector LeftHipWorld = FVector::ZeroVector;
	FVector RightHipWorld = FVector::ZeroVector;
	float LeftHipReliability = 0.0f;
	float RightHipReliability = 0.0f;
	const bool bHasLeftHip = BodyFusionSourceFrame.TryGetMediaPipeLandmark(
		EMediaPipePoseLandmark::LeftHip,
		LeftHipWorld,
		&LeftHipReliability);
	const bool bHasRightHip = BodyFusionSourceFrame.TryGetMediaPipeLandmark(
		EMediaPipePoseLandmark::RightHip,
		RightHipWorld,
		&RightHipReliability);

	FVector NoseWorld = FVector::ZeroVector;
	float NoseReliability = 0.0f;
	BodyFusionSourceFrame.TryGetMediaPipeLandmark(EMediaPipePoseLandmark::Nose, NoseWorld, &NoseReliability);

	FMediaPipeAvatarEmbodimentProfile CalibrationProfile = bHasTargetEmbodimentProfile
		? TargetEmbodimentProfile
		: FMediaPipeAvatarEmbodimentProfile();
	CalibrationProfile.bUseTargetFaceForwardAxis = bUseTargetFaceForwardAxis;
	CalibrationProfile.DefaultEyeLocalOffset = bHasTargetEyeLocalOffset
		? TargetEyeLocalOffset
		: CalibrationProfile.DefaultEyeLocalOffset;

	const FVector AvatarForwardWorld = GetTargetForwardWorld().GetSafeNormal();
	FVector AvatarUpWorld = FMediaPipeAvatarEmbodimentSolver::GetAvatarUpWorld(TargetCompTransform, AvatarForwardWorld).GetSafeNormal();
	if (AvatarUpWorld.IsNearlyZero())
	{
		AvatarUpWorld = FVector::UpVector;
	}

	FVector MediaPipeForwardWorld = AvatarForwardWorld;
	if (bHasHipCenter && bHasShoulderCenter && bHasLeftHip && bHasRightHip)
	{
		const FVector MediaPipeUpWorld = (ShoulderCenterWorld - HipCenterWorld).GetSafeNormal();
		FVector MediaPipeRightWorld = (RightHipWorld - LeftHipWorld).GetSafeNormal();
		MediaPipeRightWorld = (MediaPipeRightWorld - FVector::DotProduct(MediaPipeRightWorld, MediaPipeUpWorld) * MediaPipeUpWorld).GetSafeNormal();
		FVector CandidateForwardWorld = FVector::CrossProduct(MediaPipeRightWorld, MediaPipeUpWorld).GetSafeNormal();
		if (!CandidateForwardWorld.IsNearlyZero())
		{
			if (!AvatarForwardWorld.IsNearlyZero())
			{
				CandidateForwardWorld = LockVectorToHemisphere(CandidateForwardWorld, AvatarForwardWorld);
			}
			MediaPipeForwardWorld = CandidateForwardWorld;
		}
	}

	const FVector AvatarPelvisAnchorWorld = !RefPelvisTranslationComp.IsNearlyZero()
		? TargetCompTransform.TransformPosition(RefPelvisTranslationComp)
		: TargetCompTransform.TransformPosition(CalibrationProfile.DefaultPelvisLocalOffset);
	const float Confidence = FMath::Min(
		BodyFusionSourceFrame.MediaPipePoseStatus.Confidence,
		FMath::Min(HipReliability, ShoulderReliability));
	const float ObservedBodyHeightCm = EstimateBodyFusionObservedHeightCm(BodyFusionSourceFrame);
	const float AvatarBodyHeightCm = FMath::Max(CalibrationProfile.DefaultEyeLocalOffset.Z, ObservedBodyHeightCm);

	FMediaPipeEmbodimentCalibrationInput CalibrationInput;
	CalibrationInput.MediaPipeHipCenterWorld = HipCenterWorld;
	CalibrationInput.MediaPipeForwardWorld = MediaPipeForwardWorld;
	CalibrationInput.AvatarPelvisAnchorWorld = AvatarPelvisAnchorWorld;
	CalibrationInput.AvatarForwardWorld = AvatarForwardWorld.IsNearlyZero() ? FVector::ForwardVector : AvatarForwardWorld;
	CalibrationInput.AvatarUpWorld = AvatarUpWorld;
	CalibrationInput.HmdWorld = BodyFusionSourceFrame.HmdLocationWorld;
	CalibrationInput.ObservedBodyHeightCm = ObservedBodyHeightCm;
	CalibrationInput.AvatarBodyHeightCm = AvatarBodyHeightCm;
	CalibrationInput.Confidence = Confidence;
	CalibrationInput.bHmdStable = BodyFusionSourceFrame.HmdStatus.IsFresh();
	CalibrationInput.bMediaPipeStable =
		BodyFusionSourceFrame.MediaPipePoseStatus.IsFresh() &&
		bHasHipCenter &&
		bHasShoulderCenter &&
		!MediaPipeForwardWorld.IsNearlyZero();
	CalibrationInput.TimestampSeconds = NowSeconds;

	const float CalibrationDeltaSeconds = LastBodyFusionCalibrationUpdateTimeSeconds >= 0.0
		? FMath::Clamp(static_cast<float>(NowSeconds - LastBodyFusionCalibrationUpdateTimeSeconds), 0.0f, 0.25f)
		: 0.0f;
	LastBodyFusionCalibrationUpdateTimeSeconds = NowSeconds;
	const bool bCalibrationSampleStable =
		CalibrationInput.bHmdStable &&
		CalibrationInput.bMediaPipeStable &&
		CalibrationInput.Confidence >= 0.5f &&
		ObservedBodyHeightCm > KINDA_SMALL_NUMBER;
	if (bCalibrationSampleStable)
	{
		++BodyFusionCalibrationStableFrameCount;
		BodyFusionCalibrationStableSeconds += CalibrationDeltaSeconds;
	}
	else
	{
		BodyFusionCalibrationStableFrameCount = 0;
		BodyFusionCalibrationStableSeconds = 0.0f;
	}

	const int32 MediaPipeAuthorityMode = CVarBodyFusionMediaPipeAuthority.GetValueOnGameThread();
	const int32 RequiredStableFrames = MediaPipeAuthorityMode >= 2
		? 0
		: FMath::Max(0, CVarBodyFusionCalibrationStableFrames.GetValueOnGameThread());
	const float RequiredStableSeconds = MediaPipeAuthorityMode >= 2
		? 0.0f
		: FMath::Max(0.0f, CVarBodyFusionCalibrationHoldSeconds.GetValueOnGameThread());
	const bool bStableGateSatisfied =
		BodyFusionCalibrationStableFrameCount >= RequiredStableFrames &&
		BodyFusionCalibrationStableSeconds >= RequiredStableSeconds;

	if (!bStableGateSatisfied)
	{
		BodyFusionCalibration.Reset();
		BodyFusionCalibration.LastRejectReason = bCalibrationSampleStable
			? TEXT("Waiting for stable MediaPipe calibration")
			: (CalibrationInput.bMediaPipeStable ? TEXT("Low MediaPipe confidence") : TEXT("MediaPipe unstable"));
		if (CVarBodyFusionDebug.GetValueOnGameThread() != 0 &&
			FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(
				NowSeconds,
				1.0,
				DiagnosticsState.LastBodyFusionCalibrationLogTimeSeconds))
		{
			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.BodyFusion.Calibration actor=%s rejected reason=\"%s\" confidence=%.2f hmd=%s mediaPipe=%s hasHip=%d hasShoulder=%d observedHeight=%.1f stableFrames=%d/%d stableSeconds=%.2f/%.2f"),
				*TargetActorName.ToString(),
				*BodyFusionCalibration.LastRejectReason,
				Confidence,
				*BodyFusionStatusString(BodyFusionSourceFrame.HmdStatus),
				*BodyFusionStatusString(BodyFusionSourceFrame.MediaPipePoseStatus),
				bHasHipCenter ? 1 : 0,
				bHasShoulderCenter ? 1 : 0,
				ObservedBodyHeightCm,
				BodyFusionCalibrationStableFrameCount,
				RequiredStableFrames,
				BodyFusionCalibrationStableSeconds,
				RequiredStableSeconds);
		}
		return false;
	}

	const bool bAccepted = FMediaPipeEmbodimentCalibration::TryBuildNeutralCalibration(
		CalibrationInput,
		BodyFusionCalibration);
	if (CVarBodyFusionDebug.GetValueOnGameThread() != 0 &&
		FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(
			NowSeconds,
			1.0,
			DiagnosticsState.LastBodyFusionCalibrationLogTimeSeconds))
	{
		if (bAccepted)
		{
			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.BodyFusion.Calibration actor=%s accepted confidence=%.2f observedHeight=%.1f avatarHeight=%.1f stableFrames=%d/%d stableSeconds=%.2f/%.2f yaw=(%.3f,%.3f,%.3f,%.3f) translation=%s scale=%.3f hmd=%s mpHip=%s mpShoulder=%s mpForward=%s"),
				*TargetActorName.ToString(),
				BodyFusionCalibration.Confidence,
				ObservedBodyHeightCm,
				AvatarBodyHeightCm,
				BodyFusionCalibrationStableFrameCount,
				RequiredStableFrames,
				BodyFusionCalibrationStableSeconds,
				RequiredStableSeconds,
				BodyFusionCalibration.YawRotation.X,
				BodyFusionCalibration.YawRotation.Y,
				BodyFusionCalibration.YawRotation.Z,
				BodyFusionCalibration.YawRotation.W,
				*BodyFusionVectorString(BodyFusionCalibration.Translation),
				BodyFusionCalibration.Scale,
				*BodyFusionStatusString(BodyFusionSourceFrame.HmdStatus),
				*BodyFusionVectorString(HipCenterWorld),
				*BodyFusionVectorString(ShoulderCenterWorld),
				*BodyFusionVectorString(MediaPipeForwardWorld));
		}
		else
		{
			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.BodyFusion.Calibration actor=%s rejected reason=\"%s\" confidence=%.2f hmd=%s mediaPipe=%s hasHip=%d hasShoulder=%d observedHeight=%.1f stableFrames=%d/%d stableSeconds=%.2f/%.2f"),
				*TargetActorName.ToString(),
				*BodyFusionCalibration.LastRejectReason,
				Confidence,
				*BodyFusionStatusString(BodyFusionSourceFrame.HmdStatus),
				*BodyFusionStatusString(BodyFusionSourceFrame.MediaPipePoseStatus),
				bHasHipCenter ? 1 : 0,
				bHasShoulderCenter ? 1 : 0,
				ObservedBodyHeightCm,
				BodyFusionCalibrationStableFrameCount,
				RequiredStableFrames,
				BodyFusionCalibrationStableSeconds,
				RequiredStableSeconds);
		}
	}

	return bAccepted;
}

void FAnimNode_MediaPipePoseDriven::EmitBodyFusionDebugLog_GameThread(const double NowSeconds)
{
	if (!FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(
		NowSeconds,
		1.0,
		DiagnosticsState.LastBodyFusionDebugLogTimeSeconds))
	{
		return;
	}

	FMediaPipeAvatarEmbodimentProfile BodyFusionProfile = bHasTargetEmbodimentProfile
		? TargetEmbodimentProfile
		: FMediaPipeAvatarEmbodimentProfile();
	BodyFusionProfile.bUseTargetFaceForwardAxis = bUseTargetFaceForwardAxis;
	BodyFusionProfile.DefaultEyeLocalOffset = bHasTargetEyeLocalOffset
		? TargetEyeLocalOffset
		: BodyFusionProfile.DefaultEyeLocalOffset;
	BodyFusionProfile.EmbodiedCameraForwardOffsetCm = TargetEmbodiedCameraForwardOffsetCm;

	const FVector AvatarForwardWorld = GetTargetForwardWorld().GetSafeNormal();
	FVector AvatarUpWorld = FMediaPipeAvatarEmbodimentSolver::GetAvatarUpWorld(TargetCompTransform, AvatarForwardWorld).GetSafeNormal();
	if (AvatarUpWorld.IsNearlyZero())
	{
		AvatarUpWorld = FVector::UpVector;
	}

	const FVector AvatarRootWorld = TargetCompTransform.GetLocation();
	const FVector AvatarEyeWorld = TargetCompTransform.TransformPosition(BodyFusionProfile.DefaultEyeLocalOffset);
	const FVector AvatarHeadWorld = !RefHeadPosComp.IsNearlyZero()
		? TargetCompTransform.TransformPosition(RefHeadPosComp)
		: TargetCompTransform.TransformPosition(ResolveMediaPipeAvatarProfileHeadLocal(BodyFusionProfile));
	const FVector AvatarPelvisWorld = !RefPelvisTranslationComp.IsNearlyZero()
		? TargetCompTransform.TransformPosition(RefPelvisTranslationComp)
		: TargetCompTransform.TransformPosition(BodyFusionProfile.DefaultPelvisLocalOffset);
	const FVector AvatarChestWorld = !RefSpine05TransformComp.GetTranslation().IsNearlyZero()
		? TargetCompTransform.TransformPosition(RefSpine05TransformComp.GetTranslation())
		: TargetCompTransform.TransformPosition(BodyFusionProfile.DefaultChestLocalOffset);
	const FVector AvatarNeckWorld = TargetCompTransform.TransformPosition(BodyFusionProfile.DefaultNeckLocalOffset);

	FVector MediaPipeHipCenterWorld = FVector::ZeroVector;
	FVector MediaPipeShoulderCenterWorld = FVector::ZeroVector;
	FVector MediaPipeHeadWorld = FVector::ZeroVector;
	float MediaPipeHipReliability = 0.0f;
	float MediaPipeShoulderReliability = 0.0f;
	const bool bHasMediaPipeHip = TryBodyFusionLandmarkMidpoint(
		BodyFusionSourceFrame,
		EMediaPipePoseLandmark::LeftHip,
		EMediaPipePoseLandmark::RightHip,
		MediaPipeHipCenterWorld,
		&MediaPipeHipReliability);
	const bool bHasMediaPipeShoulder = TryBodyFusionLandmarkMidpoint(
		BodyFusionSourceFrame,
		EMediaPipePoseLandmark::LeftShoulder,
		EMediaPipePoseLandmark::RightShoulder,
		MediaPipeShoulderCenterWorld,
		&MediaPipeShoulderReliability);
	float MediaPipeHeadReliability = 0.0f;
	const bool bHasMediaPipeHead = BodyFusionSourceFrame.TryGetMediaPipeLandmark(
		EMediaPipePoseLandmark::Nose,
		MediaPipeHeadWorld,
		&MediaPipeHeadReliability);

	const bool bHasHmd = BodyFusionSourceFrame.HmdStatus.IsFresh();
	const float CameraToEyeCm = bHasHmd ? FVector::Distance(BodyFusionSourceFrame.HmdLocationWorld, AvatarEyeWorld) : -1.0f;
	const float CameraToChestCm = bHasHmd ? FVector::Distance(BodyFusionSourceFrame.HmdLocationWorld, AvatarChestWorld) : -1.0f;
	const float HeadToChestCm = FVector::Distance(AvatarHeadWorld, AvatarChestWorld);
	const float ChestToPelvisCm = FVector::Distance(AvatarChestWorld, AvatarPelvisWorld);
	const float HmdYawDeg = bHasHmd ? BodyFusionSourceFrame.HmdRotationWorld.Rotator().Yaw : 0.0f;

	UE_LOG(LogMediaPipePose, Log,
		TEXT("mp.BodyFusion.Debug actor=%s bodyAuthority=%s mediaPipeAuthority=%d reason=\"%s\" stableFrames=%d stableSeconds=%.2f hmd=%s qHandL=%s qHandR=%s fullChainL=%s fullChainR=%s mediaPipe=%s hmdYaw=%.1f hmd=%s trackingUp=%s avatarRoot=%s eye=%s head=%s neck=%s chest=%s pelvis=%s forward=%s mpHip=%s hipRel=%.2f mpShoulder=%s shoulderRel=%.2f mpHead=%s headRel=%.2f dist(cameraEye=%.1f cameraChest=%.1f headChest=%.1f chestPelvis=%.1f) hmdPlanar(offset=%.1f) solve=%d solveEye=%s solveHead=%s solveChest=%s solvePelvis=%s"),
		*TargetActorName.ToString(),
		BodyFusionAuthorityStateName(LastBodyFusionAuthorityState),
		bLastBodyFusionMediaPipeAuthorityAllowed ? 1 : 0,
		*LastBodyFusionAuthorityReason,
		BodyFusionCalibrationStableFrameCount,
		BodyFusionCalibrationStableSeconds,
		*BodyFusionStatusString(BodyFusionSourceFrame.HmdStatus),
		*BodyFusionStatusString(BodyFusionSourceFrame.QuestLeftHandStatus),
		*BodyFusionStatusString(BodyFusionSourceFrame.QuestRightHandStatus),
		*BodyFusionStatusString(BodyFusionSourceFrame.QuestLeftFullArmChainStatus),
		*BodyFusionStatusString(BodyFusionSourceFrame.QuestRightFullArmChainStatus),
		*BodyFusionStatusString(BodyFusionSourceFrame.MediaPipePoseStatus),
		HmdYawDeg,
		*BodyFusionVectorString(BodyFusionSourceFrame.HmdLocationWorld),
		*BodyFusionVectorString(BodyFusionSourceFrame.TrackingUpWorld),
		*BodyFusionVectorString(AvatarRootWorld),
		*BodyFusionVectorString(AvatarEyeWorld),
		*BodyFusionVectorString(AvatarHeadWorld),
		*BodyFusionVectorString(AvatarNeckWorld),
		*BodyFusionVectorString(AvatarChestWorld),
		*BodyFusionVectorString(AvatarPelvisWorld),
		*BodyFusionVectorString(AvatarForwardWorld),
		bHasMediaPipeHip ? *BodyFusionVectorString(MediaPipeHipCenterWorld) : TEXT("(missing)"),
		MediaPipeHipReliability,
		bHasMediaPipeShoulder ? *BodyFusionVectorString(MediaPipeShoulderCenterWorld) : TEXT("(missing)"),
		MediaPipeShoulderReliability,
		bHasMediaPipeHead ? *BodyFusionVectorString(MediaPipeHeadWorld) : TEXT("(missing)"),
		MediaPipeHeadReliability,
		CameraToEyeCm,
		CameraToChestCm,
		HeadToChestCm,
		ChestToPelvisCm,
		LastBodyFusionPose.DebugErrors.HmdHorizontalOffsetCm,
		LastBodyFusionPose.IsUsable() ? 1 : 0,
		*BodyFusionVectorString(LastBodyFusionPose.Eye.LocationWorld),
		*BodyFusionVectorString(LastBodyFusionPose.Head.LocationWorld),
		*BodyFusionVectorString(LastBodyFusionPose.Chest.LocationWorld),
		*BodyFusionVectorString(LastBodyFusionPose.Pelvis.LocationWorld));
}

FVector FAnimNode_MediaPipePoseDriven::LerpNormalized(const FVector& A, const FVector& B, float Alpha)
{
	const FVector V = FMath::Lerp(A, B, Alpha);
	return V.IsNearlyZero() ? A.GetSafeNormal() : V.GetSafeNormal();
}

FQuat FAnimNode_MediaPipePoseDriven::MakeQuatFromForwardUp(const FVector& Forward, const FVector& Up)
{
	const FVector X = Forward.GetSafeNormal();
	const FVector Z = Up.GetSafeNormal();
	if (X.IsNearlyZero() || Z.IsNearlyZero())
	{
		return FQuat::Identity;
	}

	const FMatrix M = FRotationMatrix::MakeFromXZ(X, Z);
	return M.ToQuat();
}

FVector FAnimNode_MediaPipePoseDriven::GetTargetForwardWorld() const
{
	FMediaPipeAvatarEmbodimentProfile ForwardProfile = bHasTargetEmbodimentProfile
		? TargetEmbodimentProfile
		: FMediaPipeAvatarEmbodimentProfile();
	ForwardProfile.bUseTargetFaceForwardAxis = bUseTargetFaceForwardAxis;
	return FMediaPipeAvatarEmbodimentSolver::GetAvatarForwardWorld(TargetCompTransform, ForwardProfile);
}

void FAnimNode_MediaPipePoseDriven::ApplyReferencePoseProportionsToTargetProfile()
{
	if (!bHasTargetEmbodimentProfile || !bHasReferencePose)
	{
		return;
	}

	auto AveragePositive = [](const float A, const bool bHasA, const float B, const bool bHasB) -> float
	{
		float Sum = 0.0f;
		int32 Count = 0;
		if (bHasA && A > KINDA_SMALL_NUMBER && FMath::IsFinite(A))
		{
			Sum += A;
			++Count;
		}
		if (bHasB && B > KINDA_SMALL_NUMBER && FMath::IsFinite(B))
		{
			Sum += B;
			++Count;
		}
		return Count > 0 ? Sum / static_cast<float>(Count) : 0.0f;
	};

	auto ApplyMeasuredDistance = [](const float Value, float& Expected)
	{
		if (Value <= KINDA_SMALL_NUMBER || !FMath::IsFinite(Value))
		{
			return;
		}
		Expected = Value;
	};
	auto ApplyMeasuredRangedLength = [&ApplyMeasuredDistance](
		const float Value,
		float& Expected,
		float& MinValue,
		float& MaxValue)
	{
		ApplyMeasuredDistance(Value, Expected);
		if (Value <= KINDA_SMALL_NUMBER || !FMath::IsFinite(Value))
		{
			return;
		}
		MinValue = 0.0f;
		MaxValue = BIG_NUMBER;
	};

	ApplyMeasuredRangedLength(
		AveragePositive(RefUpperLenCompL, bHasRefArmL, RefUpperLenCompR, bHasRefArmR),
		TargetEmbodimentProfile.ExpectedUpperArmLengthCm,
		TargetEmbodimentProfile.MinUpperArmLengthCm,
		TargetEmbodimentProfile.MaxUpperArmLengthCm);
	ApplyMeasuredRangedLength(
		AveragePositive(RefLowerLenCompL, bHasRefArmL, RefLowerLenCompR, bHasRefArmR),
		TargetEmbodimentProfile.ExpectedLowerArmLengthCm,
		TargetEmbodimentProfile.MinLowerArmLengthCm,
		TargetEmbodimentProfile.MaxLowerArmLengthCm);
	ApplyMeasuredRangedLength(
		AveragePositive(RefThighLenCompL, bHasRefLegL, RefThighLenCompR, bHasRefLegR),
		TargetEmbodimentProfile.ExpectedThighLengthCm,
		TargetEmbodimentProfile.MinThighLengthCm,
		TargetEmbodimentProfile.MaxThighLengthCm);
	ApplyMeasuredRangedLength(
		AveragePositive(RefCalfLenCompL, bHasRefLegL, RefCalfLenCompR, bHasRefLegR),
		TargetEmbodimentProfile.ExpectedCalfLengthCm,
		TargetEmbodimentProfile.MinCalfLengthCm,
		TargetEmbodimentProfile.MaxCalfLengthCm);

	if (bHasRefChestPosComp &&
		!RefHeadPosComp.IsNearlyZero() &&
		!RefPelvisTranslationComp.IsNearlyZero() &&
		!RefHeadPosComp.ContainsNaN() &&
		!RefChestPosComp.ContainsNaN() &&
		!RefPelvisTranslationComp.ContainsNaN())
	{
		FVector ReferenceUpComp = (RefHeadPosComp - RefPelvisTranslationComp).GetSafeNormal();
		if (ReferenceUpComp.IsNearlyZero())
		{
			ReferenceUpComp = FVector::UpVector;
		}

		ApplyMeasuredDistance(
			FMath::Abs(FVector::DotProduct(RefHeadPosComp - RefChestPosComp, ReferenceUpComp)),
			TargetEmbodimentProfile.ExpectedHeadToChestCm);
		ApplyMeasuredDistance(
			FMath::Abs(FVector::DotProduct(RefChestPosComp - RefPelvisTranslationComp, ReferenceUpComp)),
			TargetEmbodimentProfile.ExpectedChestToPelvisCm);
		const FVector ProfileHeadLocal = ResolveMediaPipeAvatarProfileHeadLocal(TargetEmbodimentProfile);
		float ProfileNeck02Alpha = 0.0f;
		const bool bHasProfileNeck02Alpha =
			TryResolveChainAlpha(
				TargetEmbodimentProfile.DefaultChestLocalOffset,
				ProfileHeadLocal,
				TargetEmbodimentProfile.DefaultNeck02LocalOffset,
				ProfileNeck02Alpha);
		const FVector ProfileEyeLocalOffset = TargetEmbodimentProfile.DefaultEyeLocalOffset;
		const float ProfileHeadFromEyeCm = TargetEmbodimentProfile.HeadBoneFromEyeOffsetCm;
		TargetEmbodimentProfile.DefaultChestLocalOffset = RefChestPosComp;
		TargetEmbodimentProfile.DefaultNeckLocalOffset = RefNeckPosComp;
		TargetEmbodimentProfile.DefaultNeck02LocalOffset = bHasRefNeck02PosComp
			? RefNeck02PosComp
			: (bHasProfileNeck02Alpha
				? FMath::Lerp(RefChestPosComp, RefHeadPosComp, ProfileNeck02Alpha)
				: RefNeckPosComp);
		TargetEmbodimentProfile.bHasDefaultHeadLocalOffset = true;
		TargetEmbodimentProfile.DefaultHeadLocalOffset = RefHeadPosComp;
		TargetEmbodimentProfile.DefaultPelvisLocalOffset = RefPelvisTranslationComp;

		if (TargetEmbodimentProfile.bAutoCalibrateUpperBodyFollowAlpha)
		{
			TargetEmbodimentProfile.UpperBodyFollowAlpha =
				ResolveReferencePoseUpperBodyFollowAlpha(
					TargetEmbodimentProfile.ExpectedHeadToChestCm,
					TargetEmbodimentProfile.ExpectedChestToPelvisCm,
					TargetEmbodimentProfile.UpperBodyFollowAlpha);
		}
		else
		{
			TargetEmbodimentProfile.UpperBodyFollowAlpha = FMath::Clamp(
				FMath::IsFinite(TargetEmbodimentProfile.UpperBodyFollowAlpha)
					? TargetEmbodimentProfile.UpperBodyFollowAlpha
					: 1.0f,
				0.0f,
				1.0f);
		}

		const FVector EyePlanarFromHeadComp = FVector::VectorPlaneProject(
			ProfileEyeLocalOffset - RefHeadPosComp,
			ReferenceUpComp);
		const FVector ResolvedEyeLocal =
			RefHeadPosComp - ReferenceUpComp * ProfileHeadFromEyeCm + EyePlanarFromHeadComp;
		if (FMath::IsFinite(ResolvedEyeLocal.X) &&
			FMath::IsFinite(ResolvedEyeLocal.Y) &&
			FMath::IsFinite(ResolvedEyeLocal.Z))
		{
			TargetEmbodimentProfile.DefaultEyeLocalOffset = ResolvedEyeLocal;
			TargetEmbodimentProfile.HeadBoneFromEyeOffsetCm = ProfileHeadFromEyeCm;
			TargetEyeLocalOffset = TargetEmbodimentProfile.DefaultEyeLocalOffset;
			bHasTargetEyeLocalOffset = true;

			UE_LOG(LogMediaPipePose, Verbose, TEXT("Avatar embodiment anchors resolved from reference pose actor=%s eyeLocal=%s headFromEye=%.2f chestLocal=%s pelvisLocal=%s upperBodyFollow=%.2f."),
				TargetMetaHumanProfile.TargetActor.IsValid() ? *GetNameSafe(TargetMetaHumanProfile.TargetActor.Get()) : TEXT("None"),
				*TargetEmbodimentProfile.DefaultEyeLocalOffset.ToCompactString(),
				TargetEmbodimentProfile.HeadBoneFromEyeOffsetCm,
				*TargetEmbodimentProfile.DefaultChestLocalOffset.ToCompactString(),
				*TargetEmbodimentProfile.DefaultPelvisLocalOffset.ToCompactString(),
				TargetEmbodimentProfile.UpperBodyFollowAlpha);
		}
		else
		{
			TargetEmbodimentProfile.HeadBoneFromEyeOffsetCm =
				FVector::DotProduct(RefHeadPosComp - TargetEmbodimentProfile.DefaultEyeLocalOffset, ReferenceUpComp);
		}

		TargetEmbodimentProfile.bHasDefaultEyeLocalInHeadOffset = true;
		TargetEmbodimentProfile.DefaultEyeLocalInHeadOffset =
			RefHeadBasisComp.Inverse().RotateVector(TargetEmbodimentProfile.DefaultEyeLocalOffset - RefHeadPosComp);
	}

	bHasTargetEmbodimentProfile = TargetEmbodimentProfile.IsValid();
}

static FVector QuestBasisAxis(const FQuat& Basis, const uint8 AxisIndex)
{
	if (AxisIndex == 0)
	{
		return Basis.RotateVector(FVector::ForwardVector).GetSafeNormal();
	}
	if (AxisIndex == 1)
	{
		return Basis.RotateVector(FVector::RightVector).GetSafeNormal();
	}
	return Basis.RotateVector(FVector::UpVector).GetSafeNormal();
}

static FVector QuestSignedBasisAxis(const FQuat& Basis, const uint8 AxisIndex, const float AxisSign)
{
	return (QuestBasisAxis(Basis, AxisIndex) * (AxisSign < 0.0f ? -1.0f : 1.0f)).GetSafeNormal();
}

static uint8 EncodeQuestSignedAxisIndex(const uint8 AxisIndex, const float AxisSign)
{
	return static_cast<uint8>(AxisIndex + (AxisSign < 0.0f ? 4 : 1));
}

static bool SelectQuestAnatomicalRollAxis(
	const FQuat& QuestWristJointComp,
	const FQuat& QuestHandBasisComp,
	const FVector& ForearmAxisComp,
	uint8& OutAxisIndex,
	float& OutAxisSign,
	float& OutPalmDot,
	float& OutForearmDot)
{
	const FVector PalmForwardComp = QuestHandBasisComp.RotateVector(FVector::ForwardVector).GetSafeNormal();
	const FVector ForearmAxisN = ForearmAxisComp.GetSafeNormal();
	if (QuestWristJointComp.IsIdentity() || PalmForwardComp.IsNearlyZero() || ForearmAxisN.IsNearlyZero())
	{
		return false;
	}

	uint8 BestAxisIndex = 0;
	float BestDot = 0.0f;
	float BestAbsDot = -1.0f;
	for (uint8 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		const FVector Axis = QuestBasisAxis(QuestWristJointComp, AxisIndex);
		if (Axis.IsNearlyZero())
		{
			continue;
		}

		const float Dot = FVector::DotProduct(Axis, ForearmAxisN);
		const float AbsDot = FMath::Abs(Dot);
		if (AbsDot > BestAbsDot)
		{
			BestAxisIndex = AxisIndex;
			BestDot = Dot;
			BestAbsDot = AbsDot;
		}
	}

	if (BestAbsDot < 0.50f)
	{
		return false;
	}

	OutAxisIndex = BestAxisIndex;
	OutAxisSign = BestDot < 0.0f ? -1.0f : 1.0f;
	const FVector SignedAxis = QuestSignedBasisAxis(QuestWristJointComp, OutAxisIndex, OutAxisSign);
	OutPalmDot = FVector::DotProduct(SignedAxis, PalmForwardComp);
	OutForearmDot = FVector::DotProduct(SignedAxis, ForearmAxisN);
	return true;
}

static FVector ProjectAxisToPlane(const FVector& Axis, const FVector& PlaneNormal)
{
	const FVector AxisN = Axis.GetSafeNormal();
	const FVector NormalN = PlaneNormal.GetSafeNormal();
	if (AxisN.IsNearlyZero() || NormalN.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	return (AxisN - FVector::DotProduct(AxisN, NormalN) * NormalN).GetSafeNormal();
}

static float SignedAngleDegAroundAxis(const FVector& From, const FVector& To, const FVector& Axis)
{
	const FVector FromN = From.GetSafeNormal();
	const FVector ToN = To.GetSafeNormal();
	const FVector AxisN = Axis.GetSafeNormal();
	if (FromN.IsNearlyZero() || ToN.IsNearlyZero() || AxisN.IsNearlyZero())
	{
		return 0.0f;
	}

	const float SinAngle = FVector::DotProduct(AxisN, FVector::CrossProduct(FromN, ToN));
	const float CosAngle = FMath::Clamp(FVector::DotProduct(FromN, ToN), -1.0f, 1.0f);
	return FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(SinAngle, CosAngle)));
}

static bool MeasureQuestSemanticRollOffsetDeg(
	const FQuat& QuestHandBasisComp,
	const FQuat& MediaPipeHandBasisComp,
	const FVector& ForearmAxisComp,
	const uint8 AxisIndex,
	float& OutOffsetDeg,
	float& OutAxisScore)
{
	const FVector ForearmAxisN = ForearmAxisComp.GetSafeNormal();
	if (QuestHandBasisComp.IsIdentity() || MediaPipeHandBasisComp.IsIdentity() || ForearmAxisN.IsNearlyZero())
	{
		return false;
	}

	const FVector QuestAxis = QuestBasisAxis(QuestHandBasisComp, AxisIndex);
	const FVector MediaAxis = QuestBasisAxis(MediaPipeHandBasisComp, AxisIndex);
	if (QuestAxis.IsNearlyZero() || MediaAxis.IsNearlyZero())
	{
		return false;
	}

	const float QuestForearmDot = FVector::DotProduct(QuestAxis, ForearmAxisN);
	const float MediaForearmDot = FVector::DotProduct(MediaAxis, ForearmAxisN);
	const float QuestProjectionScore = FMath::Clamp(1.0f - (QuestForearmDot * QuestForearmDot), 0.0f, 1.0f);
	const float MediaProjectionScore = FMath::Clamp(1.0f - (MediaForearmDot * MediaForearmDot), 0.0f, 1.0f);
	const float AxisPreference = AxisIndex == 0 ? 0.75f : 1.0f;
	OutAxisScore = FMath::Min(QuestProjectionScore, MediaProjectionScore) * AxisPreference;
	if (OutAxisScore < 0.12f)
	{
		return false;
	}

	const FVector QuestProjected = ProjectAxisToPlane(QuestAxis, ForearmAxisN);
	const FVector MediaProjected = ProjectAxisToPlane(MediaAxis, ForearmAxisN);
	if (QuestProjected.IsNearlyZero() || MediaProjected.IsNearlyZero())
	{
		return false;
	}

	OutOffsetDeg = SignedAngleDegAroundAxis(MediaProjected, QuestProjected, ForearmAxisN);
	return true;
}

static bool SelectQuestSemanticRollAxis(
	const FQuat& QuestHandBasisComp,
	const FQuat& MediaPipeHandBasisComp,
	const FVector& ForearmAxisComp,
	uint8& OutAxisIndex,
	float& OutOffsetDeg,
	float& OutAxisScore)
{
	bool bFound = false;
	uint8 BestAxisIndex = 0;
	float BestOffsetDeg = 0.0f;
	float BestScore = -1.0f;
	for (uint8 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		float CandidateOffsetDeg = 0.0f;
		float CandidateScore = 0.0f;
		if (!MeasureQuestSemanticRollOffsetDeg(
			QuestHandBasisComp,
			MediaPipeHandBasisComp,
			ForearmAxisComp,
			AxisIndex,
			CandidateOffsetDeg,
			CandidateScore))
		{
			continue;
		}

		if (CandidateScore > BestScore)
		{
			bFound = true;
			BestAxisIndex = AxisIndex;
			BestOffsetDeg = CandidateOffsetDeg;
			BestScore = CandidateScore;
		}
	}

	if (!bFound)
	{
		return false;
	}

	OutAxisIndex = BestAxisIndex;
	OutOffsetDeg = BestOffsetDeg;
	OutAxisScore = BestScore;
	return true;
}

static bool MeasureQuestSwingCorrectedRollDeg(
	const FQuat& QuestHandBasisComp,
	const FVector& CalibrationForwardComp,
	const FVector& CalibrationUpComp,
	float& OutRollDeg,
	float& OutAxisScore)
{
	const FVector CurrentForward = QuestBasisAxis(QuestHandBasisComp, 0);
	const FVector CurrentUp = QuestBasisAxis(QuestHandBasisComp, 2);
	const FVector CalibrationForward = CalibrationForwardComp.GetSafeNormal();
	const FVector CalibrationUp = CalibrationUpComp.GetSafeNormal();
	if (QuestHandBasisComp.IsIdentity() ||
		CurrentForward.IsNearlyZero() ||
		CurrentUp.IsNearlyZero() ||
		CalibrationForward.IsNearlyZero() ||
		CalibrationUp.IsNearlyZero())
	{
		return false;
	}

	const float ForwardDot = FMath::Clamp(FVector::DotProduct(CalibrationForward, CurrentForward), -1.0f, 1.0f);
	if (ForwardDot < -0.95f)
	{
		return false;
	}

	const FQuat SwingFromCalibration = FQuat::FindBetweenNormals(CalibrationForward, CurrentForward).GetNormalized();
	const FVector ReferenceUp = SwingFromCalibration.RotateVector(CalibrationUp).GetSafeNormal();
	const FVector ReferenceUpProjected = ProjectAxisToPlane(ReferenceUp, CurrentForward);
	const FVector CurrentUpProjected = ProjectAxisToPlane(CurrentUp, CurrentForward);
	if (ReferenceUpProjected.IsNearlyZero() || CurrentUpProjected.IsNearlyZero())
	{
		return false;
	}

	OutRollDeg = SignedAngleDegAroundAxis(ReferenceUpProjected, CurrentUpProjected, CurrentForward);
	OutAxisScore = FMath::Clamp((ForwardDot + 1.0f) * 0.5f, 0.0f, 1.0f);
	return true;
}

static bool MeasureProjectedBasisRollDeltaDeg(
	const FQuat& CalibrationBasis,
	const FQuat& CurrentBasis,
	const FVector& RollAxis,
	const uint8 BasisAxisIndex,
	float& OutRollDeg,
	float& OutAxisScore)
{
	const FVector AxisN = RollAxis.GetSafeNormal();
	if (CalibrationBasis.IsIdentity() || CurrentBasis.IsIdentity() || AxisN.IsNearlyZero())
	{
		return false;
	}

	const FVector CalibrationAxis = QuestBasisAxis(CalibrationBasis, BasisAxisIndex);
	const FVector CurrentAxis = QuestBasisAxis(CurrentBasis, BasisAxisIndex);
	if (CalibrationAxis.IsNearlyZero() || CurrentAxis.IsNearlyZero())
	{
		return false;
	}

	const float CalibrationDot = FVector::DotProduct(CalibrationAxis, AxisN);
	const float CurrentDot = FVector::DotProduct(CurrentAxis, AxisN);
	const float CalibrationProjectionScore = FMath::Clamp(1.0f - (CalibrationDot * CalibrationDot), 0.0f, 1.0f);
	const float CurrentProjectionScore = FMath::Clamp(1.0f - (CurrentDot * CurrentDot), 0.0f, 1.0f);
	OutAxisScore = FMath::Min(CalibrationProjectionScore, CurrentProjectionScore);

	const FVector CalibrationProjected = ProjectAxisToPlane(CalibrationAxis, AxisN);
	const FVector CurrentProjected = ProjectAxisToPlane(CurrentAxis, AxisN);
	if (CalibrationProjected.IsNearlyZero() || CurrentProjected.IsNearlyZero())
	{
		return false;
	}

	OutRollDeg = SignedAngleDegAroundAxis(CalibrationProjected, CurrentProjected, AxisN);
	return true;
}

static FVector LockVectorToHemisphere(const FVector& Vector, const FVector& Reference)
{
	const FVector Normalized = Vector.GetSafeNormal();
	const FVector Ref = Reference.GetSafeNormal();
	if (Normalized.IsNearlyZero() || Ref.IsNearlyZero())
	{
		return Normalized;
	}

	return FVector::DotProduct(Normalized, Ref) < 0.0f ? -Normalized : Normalized;
}

static FVector ProjectPoleReferenceToPlane(const FVector& PoleReference, const FVector& Forward, const FVector& FallbackA, const FVector& FallbackB)
{
	const FVector ForwardN = Forward.GetSafeNormal();
	if (ForwardN.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	auto Project = [&](const FVector& Candidate) -> FVector
	{
		return (Candidate - FVector::DotProduct(Candidate, ForwardN) * ForwardN).GetSafeNormal();
	};

	FVector Projected = Project(PoleReference);
	if (Projected.IsNearlyZero())
	{
		Projected = Project(FallbackA);
	}
	if (Projected.IsNearlyZero())
	{
		Projected = Project(FallbackB);
	}
	return Projected;
}

static float RemapClamped(float Value, float InMin, float InMax)
{
	if (FMath::IsNearlyEqual(InMin, InMax))
	{
		return Value >= InMax ? 1.0f : 0.0f;
	}

	return FMath::Clamp((Value - InMin) / (InMax - InMin), 0.0f, 1.0f);
}

static FVector BuildArmSurfaceUpHint(const FVector& BodyUp, const FVector& BodyForward, const FVector& SegmentDir)
{
	const FVector UpN = BodyUp.GetSafeNormal();
	const FVector ForwardN = BodyForward.GetSafeNormal();
	const FVector SegmentDirN = SegmentDir.GetSafeNormal();
	if (UpN.IsNearlyZero())
	{
		return FVector::UpVector;
	}

	if (ForwardN.IsNearlyZero() || SegmentDirN.IsNearlyZero())
	{
		return UpN;
	}

	const float DownAlpha = RemapClamped(-FVector::DotProduct(SegmentDirN, UpN), 0.05f, 0.75f);
	const float BackBias = 0.45f * DownAlpha;
	const FVector Hint = (UpN - BackBias * ForwardN).GetSafeNormal();
	return Hint.IsNearlyZero() ? UpN : Hint;
}

float FAnimNode_MediaPipePoseDriven::HalfLifeToAlpha(float HalfLifeSeconds, float DeltaSeconds)
{
	if (HalfLifeSeconds <= 0.0f || DeltaSeconds <= 0.0f)
	{
		return 1.0f;
	}
	const float A = 1.0f - FMath::Pow(0.5f, DeltaSeconds / HalfLifeSeconds);
	return A;
}

void FAnimNode_MediaPipePoseDriven::UpdateSmoothedRotation(bool& bInOutHasValue, FQuat& InOutValueCS, const FQuat& TargetCS, float Alpha, float MaxStepDegrees)
{
	FQuat Target = TargetCS.GetNormalized();
	if (!bInOutHasValue)
	{
		InOutValueCS = Target;
		bInOutHasValue = true;
		return;
	}

	const float Dot =
		InOutValueCS.X * Target.X +
		InOutValueCS.Y * Target.Y +
		InOutValueCS.Z * Target.Z +
		InOutValueCS.W * Target.W;
	if (Dot < 0.0f)
	{
		Target.X = -Target.X;
		Target.Y = -Target.Y;
		Target.Z = -Target.Z;
		Target.W = -Target.W;
	}

	FQuat Next = FQuat::Slerp(InOutValueCS, Target, FMath::Clamp(Alpha, 0.0f, 1.0f)).GetNormalized();
	if (MaxStepDegrees > 0.0f)
	{
		const float StepDeg = FMath::RadiansToDegrees(InOutValueCS.AngularDistance(Next));
		if (StepDeg > MaxStepDegrees && StepDeg > KINDA_SMALL_NUMBER)
		{
			Next = FQuat::Slerp(InOutValueCS, Next, MaxStepDegrees / StepDeg).GetNormalized();
		}
	}

	InOutValueCS = Next;
}

void FAnimNode_MediaPipePoseDriven::ResetRotationSmoothing()
{
	BodyState.ResetRotationSmoothing();
	LeftArmState.ResetSmoothing();
	RightArmState.ResetSmoothing();
	LeftLegState.ResetRotationSmoothing();
	RightLegState.ResetRotationSmoothing();
	QuestWristState.Reset();
	LeftQuestHandState.Reset();
	RightQuestHandState.Reset();
	DiagnosticsState.Reset();
}

#include "MediaPipePoseDrivenAnimInstance_BodyState.inl"

bool FAnimNode_MediaPipePoseDriven::TryGetLmWorld(int32 LmIdx, FVector& OutWorld) const
{
	if (LmIdx < 0 || LmIdx >= MediaPipePoseLandmarkCount)
	{
		return false;
	}

	if (!bHasPoseFrame)
	{
		return false;
	}

	OutWorld = PoseWorld[LmIdx];
	return true;
}

bool FAnimNode_MediaPipePoseDriven::IsMeasured(int32 LmIdx) const
{
	if (LmIdx < 0 || LmIdx >= MediaPipePoseLandmarkCount)
	{
		return false;
	}

	return PoseFrame.World.IsValidIndex(LmIdx) && PoseFrame.Normalized.IsValidIndex(LmIdx);
}

float FAnimNode_MediaPipePoseDriven::GetLandmarkReliability(int32 LmIdx) const
{
	if (LmIdx < 0 || LmIdx >= MediaPipePoseLandmarkCount || !bHasPoseFrame)
	{
		return 0.0f;
	}

	if (!PoseFrame.World.IsValidIndex(LmIdx) || !PoseFrame.Normalized.IsValidIndex(LmIdx))
	{
		return 0.0f;
	}

	const FMediaPipePoseLandmark& WorldLm = PoseFrame.World.Points[LmIdx];
	const FMediaPipePoseLandmark& NormalizedLm = PoseFrame.Normalized.Points[LmIdx];
	const float ExplicitReliability = FMath::Max(WorldLm.Reliability, NormalizedLm.Reliability);
	if (ExplicitReliability > 0.0f || PoseFrame.bSourceConditioned)
	{
		return FMath::Clamp(ExplicitReliability, 0.0f, 1.0f);
	}

	return FMath::Clamp(
		FMath::Max(WorldLm.Visibility * WorldLm.Presence, NormalizedLm.Visibility * NormalizedLm.Presence),
		0.0f,
		1.0f);
}

#include "MediaPipePoseDrivenAnimInstance_TorsoBasis.inl"

#include "MediaPipePoseDrivenAnimInstance_ReferenceCache.inl"

void FAnimNode_MediaPipePoseDriven::ApplyRotationCS(FCSPose<FCompactPose>& CSPose, const FBoneReference& Bone, const FQuat& TargetRotCS) const
{
	if (!Bone.IsValidToEvaluate())
	{
		return;
	}
	const FCompactPoseBoneIndex BoneIdx = Bone.CachedCompactPoseIndex;
	FTransform BoneCS = CSPose.GetComponentSpaceTransform(BoneIdx);
	BoneCS.SetRotation(TargetRotCS);
	const FBoneTransform BoneTransform(BoneIdx, BoneCS);
	CSPose.SafeSetCSBoneTransforms(MakeArrayView(&BoneTransform, 1));
}

void FAnimNode_MediaPipePoseDriven::ApplyTranslationDeltaCS(FCSPose<FCompactPose>& CSPose, const FBoneReference& Bone, const FVector& DeltaComp) const
{
	if (DeltaComp.IsNearlyZero() || !Bone.IsValidToEvaluate())
	{
		return;
	}

	const FCompactPoseBoneIndex BoneIdx = Bone.CachedCompactPoseIndex;
	// Only shift bones that are already in component space. If the bone is still in local space, it will be
	// converted using its (unchanged) local transform relative to the updated parents, so shifting here would double-apply.
	const auto& CSFlags = CSPose.GetComponentSpaceFlags();
	const int32 BoneIdxInt = BoneIdx.GetInt();
	if (!CSFlags.IsValidIndex(BoneIdxInt) || CSFlags[BoneIdx] == 0)
	{
		return;
	}

	FTransform BoneCS = CSPose.GetComponentSpaceTransform(BoneIdx);
	BoneCS.SetTranslation(BoneCS.GetTranslation() + DeltaComp);
	const FBoneTransform BoneTransform(BoneIdx, BoneCS);
	CSPose.SafeSetCSBoneTransforms(MakeArrayView(&BoneTransform, 1));
}

bool FAnimNode_MediaPipePoseDriven::ShouldUseBodyFusionPoseForEvaluation() const
{
	if (CVarBodyFusionEnable.GetValueOnAnyThread() == 0 || !LastBodyFusionPose.IsUsable())
	{
		return false;
	}

	const FMediaPipeAvatarEmbodimentProfile Profile = bHasTargetEmbodimentProfile
		? TargetEmbodimentProfile
		: FMediaPipeAvatarEmbodimentProfile();
	return FMediaPipeAvatarPoseWriter::CanWritePose(LastBodyFusionPose, Profile);
}

bool FAnimNode_MediaPipePoseDriven::DriveBodyFusionPoseCS(FCSPose<FCompactPose>& CSPose, float DeltaSeconds)
{
	if (!ShouldUseBodyFusionPoseForEvaluation())
	{
		return false;
	}

	const FTransform ComponentToWorld = TargetCompTransform;
	const FTransform WorldToComponent = ComponentToWorld.Inverse();
	const FMediaPipeFusedBodyPoint& PelvisPoint = LastBodyFusionPose.Pelvis;
	const FMediaPipeFusedBodyPoint& ChestPoint = LastBodyFusionPose.Chest;
	const FMediaPipeFusedBodyPoint& HeadPoint = LastBodyFusionPose.Head;
	FMediaPipeAvatarEmbodimentProfile ForwardProfile = bHasTargetEmbodimentProfile
		? TargetEmbodimentProfile
		: FMediaPipeAvatarEmbodimentProfile();
	ForwardProfile.bUseTargetFaceForwardAxis = bUseTargetFaceForwardAxis;

	// BodyFusion owns the fused torso/lower-body writes once enabled; the legacy
	// MediaPipeDrive* CVars only gate the raw landmark path.
	if (Pelvis.IsValidToEvaluate())
	{
		FVector TargetPelvisOffsetComp = FVector::ZeroVector;
		bool bHasTargetPelvisOffset = false;
		if (LastBodyFusionPose.Pelvis.bValid &&
			(LastBodyFusionPose.Pelvis.Owner == EMediaPipeBodyFusionOwner::MediaPipe ||
			 LastBodyFusionPose.Pelvis.Owner == EMediaPipeBodyFusionOwner::Fused))
		{
			const FVector TargetPelvisComp = WorldToComponent.TransformPosition(LastBodyFusionPose.Pelvis.LocationWorld);
			TargetPelvisOffsetComp = TargetPelvisComp - RefPelvisTranslationComp;
			bHasTargetPelvisOffset = true;
		}

		const float PelvisAlpha = HalfLifeToAlpha(PelvisTranslationHalfLifeSeconds, DeltaSeconds);
		if (!BodyState.bHasSmoothedPelvisOffset)
		{
			BodyState.SmoothedPelvisOffsetComp = bHasTargetPelvisOffset ? TargetPelvisOffsetComp : FVector::ZeroVector;
			BodyState.bHasSmoothedPelvisOffset = true;
		}
		else
		{
			BodyState.SmoothedPelvisOffsetComp = FMath::Lerp(
				BodyState.SmoothedPelvisOffsetComp,
				bHasTargetPelvisOffset ? TargetPelvisOffsetComp : FVector::ZeroVector,
				PelvisAlpha);
		}

		const FCompactPoseBoneIndex PelvisIdx = Pelvis.CachedCompactPoseIndex;
		FTransform PelvisCS = CSPose.GetComponentSpaceTransform(PelvisIdx);
		PelvisCS.SetTranslation(RefPelvisTranslationComp + BodyState.SmoothedPelvisOffsetComp);
		const FBoneTransform BoneTransform(PelvisIdx, PelvisCS);
		CSPose.SafeSetCSBoneTransforms(MakeArrayView(&BoneTransform, 1));
	}

	if (NumSpineBones <= 0)
	{
		return true;
	}

	if (!PelvisPoint.bValid || !ChestPoint.bValid || !HeadPoint.bValid)
	{
		return true;
	}

	FVector PelvisComp = WorldToComponent.TransformPosition(PelvisPoint.LocationWorld);
	if (Pelvis.IsValidToEvaluate())
	{
		PelvisComp = CSPose.GetComponentSpaceTransform(Pelvis.CachedCompactPoseIndex).GetTranslation();
	}
	FVector ChestComp = WorldToComponent.TransformPosition(ChestPoint.LocationWorld);
	FVector HeadComp = WorldToComponent.TransformPosition(HeadPoint.LocationWorld);

	FVector UpComp = (ChestComp - PelvisComp).GetSafeNormal();
	if (UpComp.IsNearlyZero())
	{
		UpComp = WorldToComponent.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	}
	if (UpComp.IsNearlyZero())
	{
		UpComp = FVector::UpVector;
	}

	FVector ForwardHintComp = WorldToComponent.TransformVectorNoScale(
		FMediaPipeAvatarEmbodimentSolver::GetAvatarForwardWorld(TargetCompTransform, ForwardProfile)).GetSafeNormal();
	ForwardHintComp = (ForwardHintComp - FVector::DotProduct(ForwardHintComp, UpComp) * UpComp).GetSafeNormal();
	if (ForwardHintComp.IsNearlyZero())
	{
		ForwardHintComp = FVector::ForwardVector;
	}

	FVector RightComp = FVector::CrossProduct(UpComp, ForwardHintComp).GetSafeNormal();
	if (RightComp.IsNearlyZero())
	{
		RightComp = FVector::RightVector;
	}

	auto MakeBasisFromAxes = [](const FVector& InRight, const FVector& InUp, const FVector& InForwardHint) -> FQuat
	{
		FMediaPipeSemanticBodyBasisInput BasisInput;
		BasisInput.Right = InRight;
		BasisInput.Up = InUp;
		BasisInput.ForwardHint = InForwardHint;
		return MakeSemanticBodyBasis(BasisInput);
	};

	auto MakeBasis = [&](const FVector& InUp) -> FQuat
	{
		return MakeBasisFromAxes(RightComp, InUp, ForwardHintComp);
	};

	auto ResolveSemanticBoneRotationCS = [&](const FBoneReference& Bone, const FQuat& RefBoneComp, const FQuat& RefBasisComp,
		const FQuat& TargetBasisComp, bool& bHasSmoothedRot, FQuat& InOutSmoothedRotCS, const float Alpha, FQuat& OutRotCS) -> bool
	{
		if (!Bone.IsValidToEvaluate() || TargetBasisComp.IsIdentity() || RefBasisComp.IsIdentity())
		{
			return false;
		}

		const FQuat TargetRotCS = ((TargetBasisComp * RefBasisComp.Inverse()) * RefBoneComp).GetNormalized();
		UpdateSmoothedRotation(bHasSmoothedRot, InOutSmoothedRotCS, TargetRotCS, Alpha);
		OutRotCS = InOutSmoothedRotCS;
		return true;
	};

	auto ApplySemanticBasisToBone = [&](const FBoneReference& Bone, const FQuat& RefBoneComp, const FQuat& RefBasisComp,
		const FQuat& TargetBasisComp, bool& bHasSmoothedRot, FQuat& InOutSmoothedRotCS, const float Alpha)
	{
		FQuat ResolvedRotCS = FQuat::Identity;
		if (ResolveSemanticBoneRotationCS(
			Bone,
			RefBoneComp,
			RefBasisComp,
			TargetBasisComp,
			bHasSmoothedRot,
			InOutSmoothedRotCS,
			Alpha,
			ResolvedRotCS))
		{
			ApplyRotationCS(CSPose, Bone, ResolvedRotCS);
		}
	};

	auto ApplyComponentTranslationToBone = [&](const FBoneReference& Bone, const FVector& TargetComp)
	{
		if (!Bone.IsValidToEvaluate() || TargetComp.ContainsNaN())
		{
			return;
		}

		const FCompactPoseBoneIndex BoneIdx = Bone.CachedCompactPoseIndex;
		FTransform BoneCS = CSPose.GetComponentSpaceTransform(BoneIdx);
		BoneCS.SetTranslation(TargetComp);
		const FBoneTransform BoneTransform(BoneIdx, BoneCS);
		CSPose.SafeSetCSBoneTransforms(MakeArrayView(&BoneTransform, 1));
	};

	const bool bHmdBodyFusionHeadAuthoritative =
		HeadPoint.Owner == EMediaPipeBodyFusionOwner::Hmd &&
		BodyFusionSourceFrame.HmdStatus.IsFresh();
	const float SpineRotAlpha = bHmdBodyFusionHeadAuthoritative
		? 1.0f
		: HalfLifeToAlpha(SpineRotationHalfLifeSeconds, FMath::Max(DeltaSeconds, 0.0f));
	const float HeadRotAlpha = bHmdBodyFusionHeadAuthoritative
		? 1.0f
		: HalfLifeToAlpha(HeadRotationHalfLifeSeconds, FMath::Max(DeltaSeconds, 0.0f));
	const FQuat PelvisTargetBasis = MakeBasis(UpComp);

	FVector ChestUpComp = UpComp;
	FQuat ChestTargetBasis = MakeBasis(ChestUpComp);

	ApplySemanticBasisToBone(Pelvis, RefPelvisComp, RefPelvisBasisComp, PelvisTargetBasis, BodyState.bHasSmoothedPelvisRotCS, BodyState.SmoothedPelvisRotCS, SpineRotAlpha);

	auto GetSpineRefBySlot = [&](const uint8 Slot) -> const FBoneReference&
	{
		switch (Slot)
		{
		case 1: return Spine01;
		case 2: return Spine02;
		case 3: return Spine03;
		case 4: return Spine04;
		case 5: return Spine05;
		default: return Spine03;
		}
	};

	auto ApplyBodyFusionSpineTranslationTargets = [&]()
	{
		if (ForwardProfile.SkeletonFamily != EMediaPipeAvatarSkeletonFamily::MetaHuman ||
			NumSpineBones <= 0 ||
			!bHasRefChestPosComp)
		{
			return;
		}

		const FVector RefPelvisToChestComp = RefChestPosComp - RefPelvisTranslationComp;
		const float RefPelvisToChestLenSq = RefPelvisToChestComp.SizeSquared();
		const FVector SolvedPelvisToChestComp = ChestComp - PelvisComp;
		if (RefPelvisToChestLenSq <= KINDA_SMALL_NUMBER ||
			SolvedPelvisToChestComp.IsNearlyZero() ||
			SolvedPelvisToChestComp.ContainsNaN())
		{
			return;
		}

		const float Denom = FMath::Max(1.0f, static_cast<float>(NumSpineBones));
		for (int32 i = 0; i < NumSpineBones; ++i)
		{
			const uint8 Slot = SpineBoneSlots[i];
			if (Slot == 0)
			{
				continue;
			}

			const FVector RefSpineOffsetComp = RefSpineTranslationComp[i] - RefPelvisTranslationComp;
			float SpineWeight =
				FVector::DotProduct(RefSpineOffsetComp, RefPelvisToChestComp) / RefPelvisToChestLenSq;
			if (!FMath::IsFinite(SpineWeight))
			{
				SpineWeight = static_cast<float>(i + 1) / Denom;
			}
			SpineWeight = FMath::Clamp(SpineWeight, 0.0f, 1.0f);
			if (SpineWeight <= KINDA_SMALL_NUMBER)
			{
				SpineWeight = FMath::Clamp(static_cast<float>(i + 1) / Denom, 0.0f, 1.0f);
			}

			const FVector TargetSpineComp = PelvisComp + SolvedPelvisToChestComp * SpineWeight;
			ApplyComponentTranslationToBone(GetSpineRefBySlot(Slot), TargetSpineComp);
		}

		const uint8 TopSpineSlot = SpineBoneSlots[NumSpineBones - 1];
		if (TopSpineSlot != 0)
		{
			ApplyComponentTranslationToBone(GetSpineRefBySlot(TopSpineSlot), ChestComp);
		}
	};

	ApplyBodyFusionSpineTranslationTargets();

	const float Denom = FMath::Max(1.0f, static_cast<float>(NumSpineBones));
	for (int32 i = 0; i < NumSpineBones; ++i)
	{
		const uint8 Slot = SpineBoneSlots[i];
		if (Slot == 0)
		{
			continue;
		}

		const FBoneReference& SpineBone = GetSpineRefBySlot(Slot);
		const float Weight = static_cast<float>(i + 1) / Denom;
		const FQuat TargetBasis = FQuat::Slerp(PelvisTargetBasis, ChestTargetBasis, Weight).GetNormalized();
		ApplySemanticBasisToBone(SpineBone, RefSpineComp[i], RefSpineBasisComp[i], TargetBasis, BodyState.bHasSmoothedSpineRotCS[i], BodyState.SmoothedSpineRotCS[i], SpineRotAlpha);
	}

	// Spine rotations can recompose child component-space positions. Refresh the
	// solved translations so the chest anchor remains authoritative before the
	// neck/head chain is derived from it.
	ApplyBodyFusionSpineTranslationTargets();

	const FQuat HmdRotationComp = WorldToComponent.TransformRotation(HeadPoint.RotationWorld).GetNormalized();
	FVector HmdForwardComp = HmdRotationComp.RotateVector(FVector::ForwardVector).GetSafeNormal();
	if (HmdForwardComp.IsNearlyZero())
	{
		HmdForwardComp = ForwardHintComp;
	}

	FVector HmdUpComp = HmdRotationComp.RotateVector(FVector::UpVector).GetSafeNormal();
	if (HmdUpComp.IsNearlyZero())
	{
		HmdUpComp = ChestUpComp;
	}

	FVector HmdRightComp = FVector::CrossProduct(HmdUpComp, HmdForwardComp).GetSafeNormal();
	if (HmdRightComp.IsNearlyZero())
	{
		HmdRightComp = RightComp;
	}

	float RefNeckAlpha = 0.0f;
	if (!bHasRefChestPosComp || !TryResolveChainAlpha(RefChestPosComp, RefHeadPosComp, RefNeckPosComp, RefNeckAlpha))
	{
		return true;
	}

	const FVector ProfileHeadLocal = ResolveMediaPipeAvatarProfileHeadLocal(ForwardProfile);
	float ProfileNeck02Alpha = RefNeckAlpha;
	TryResolveChainAlpha(
		ForwardProfile.DefaultChestLocalOffset,
		ProfileHeadLocal,
		ForwardProfile.DefaultNeck02LocalOffset,
		ProfileNeck02Alpha);

	float RefNeck02Alpha = ProfileNeck02Alpha;
	if (bHasRefNeck02PosComp)
	{
		TryResolveChainAlpha(RefChestPosComp, RefHeadPosComp, RefNeck02PosComp, RefNeck02Alpha);
	}
	ResolveBodyFusionNeckChainAlphas(RefNeckAlpha, RefNeck02Alpha, RefNeckAlpha, RefNeck02Alpha);

	const FQuat HeadTargetBasis = MakeBasisFromAxes(HmdRightComp, HmdUpComp, HmdForwardComp);
	FQuat HeadRotationCS = FQuat::Identity;
	const bool bHasHeadRotationCS = ResolveSemanticBoneRotationCS(
		Head,
		RefHeadComp,
		RefHeadBasisComp,
		HeadTargetBasis,
		BodyState.bHasSmoothedHeadRotCS,
		BodyState.SmoothedHeadRotCS,
		HeadRotAlpha,
		HeadRotationCS);
	FMediaPipeAvatarEmbodimentProfile HeadAnchorProfile = ForwardProfile;
	HeadAnchorProfile.DefaultEyeLocalOffset = bHasTargetEyeLocalOffset
		? TargetEyeLocalOffset
		: HeadAnchorProfile.DefaultEyeLocalOffset;
	const bool bCanAnchorHeadFromEye =
		bHasHeadRotationCS &&
		LastBodyFusionPose.Eye.bValid &&
		Head.IsValidToEvaluate() &&
		!RefHeadPosComp.IsNearlyZero() &&
		!HeadAnchorProfile.DefaultEyeLocalOffset.ContainsNaN();
	const FVector EyeLocalInHeadForSolve = bCanAnchorHeadFromEye
		? ResolveMediaPipeAvatarProfileEyeLocalInHead(HeadAnchorProfile)
		: FVector::ZeroVector;
	FVector EyeLocalInHeadForPose = FVector::ZeroVector;
	bool bHasEyeLocalInHeadForPose = false;
	if (bCanAnchorHeadFromEye)
	{
		const FVector EyeOffsetFromHeadComp = HeadAnchorProfile.DefaultEyeLocalOffset - RefHeadPosComp;
		if (!EyeOffsetFromHeadComp.ContainsNaN())
		{
			EyeLocalInHeadForPose = RefHeadComp.Inverse().RotateVector(EyeOffsetFromHeadComp);
			bHasEyeLocalInHeadForPose = !EyeLocalInHeadForPose.ContainsNaN();
		}
	}
	if (bCanAnchorHeadFromEye)
	{
		const FVector TargetEyeComp = WorldToComponent.TransformPosition(LastBodyFusionPose.Eye.LocationWorld);
		FVector EyeAnchoredHeadComp = FVector::ZeroVector;
		if (bHasEyeLocalInHeadForPose)
		{
			EyeAnchoredHeadComp = TargetEyeComp - HeadRotationCS.RotateVector(EyeLocalInHeadForPose);
		}
		else if (!EyeLocalInHeadForSolve.ContainsNaN())
		{
			EyeAnchoredHeadComp = TargetEyeComp - HmdRotationComp.RotateVector(EyeLocalInHeadForSolve);
		}
		if (!EyeAnchoredHeadComp.ContainsNaN())
		{
			HeadComp = EyeAnchoredHeadComp;
		}
	}

	const FQuat NeckTargetBasis = FQuat::Slerp(ChestTargetBasis, HeadTargetBasis, RefNeckAlpha).GetNormalized();
	const FQuat Neck02TargetBasis = FQuat::Slerp(ChestTargetBasis, HeadTargetBasis, RefNeck02Alpha).GetNormalized();
	const FVector NeckTargetComp = FMath::Lerp(ChestComp, HeadComp, RefNeckAlpha);
	const FVector Neck02TargetComp = FMath::Lerp(ChestComp, HeadComp, RefNeck02Alpha);

	ApplySemanticBasisToBone(Neck, RefNeckComp, RefNeckBasisComp, NeckTargetBasis, BodyState.bHasSmoothedNeckRotCS, BodyState.SmoothedNeckRotCS, HeadRotAlpha);
	ApplySemanticBasisToBone(Neck02, RefNeck02Comp, RefNeck02BasisComp, Neck02TargetBasis, BodyState.bHasSmoothedNeck02RotCS, BodyState.SmoothedNeck02RotCS, HeadRotAlpha);
	if (bHasHeadRotationCS)
	{
		ApplyRotationCS(CSPose, Head, HeadRotationCS);
	}
	// Parent neck rotations can recompose child component-space positions. Write
	// neck/head translations last so the HMD eye anchor remains authoritative.
	ApplyComponentTranslationToBone(Neck, NeckTargetComp);
	ApplyComponentTranslationToBone(Neck02, Neck02TargetComp);
	ApplyComponentTranslationToBone(Head, HeadComp);

	float EyeAnchorResidualCm = 0.0f;
	FVector EyeAnchorResidualDeltaComp = FVector::ZeroVector;
	if (bCanAnchorHeadFromEye)
	{
		const FTransform HeadPoseComp =
			CSPose.GetComponentSpaceTransform(Head.CachedCompactPoseIndex);
		const FVector PosedEyeComp = HeadPoseComp.TransformPosition(
			bHasEyeLocalInHeadForPose ? EyeLocalInHeadForPose : EyeLocalInHeadForSolve);
		const FVector TargetEyeComp = WorldToComponent.TransformPosition(LastBodyFusionPose.Eye.LocationWorld);
		FVector EyeLockDeltaComp = TargetEyeComp - PosedEyeComp;
		if (!EyeLockDeltaComp.ContainsNaN())
		{
			EyeAnchorResidualCm = EyeLockDeltaComp.Size();
			EyeAnchorResidualDeltaComp = EyeLockDeltaComp;
		}
	}
	if (CVarBodyFusionDebug.GetValueOnAnyThread() != 0)
	{
		const double NowSeconds = FPlatformTime::Seconds();
		if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(
			NowSeconds,
			1.0,
			DiagnosticsState.LastBodyFusionPoseWriteDebugLogTimeSeconds))
		{
			FMediaPipeAvatarEmbodimentProfile DebugProfile = bHasTargetEmbodimentProfile
				? TargetEmbodimentProfile
				: FMediaPipeAvatarEmbodimentProfile();
			DebugProfile.bUseTargetFaceForwardAxis = bUseTargetFaceForwardAxis;
			DebugProfile.DefaultEyeLocalOffset = bHasTargetEyeLocalOffset
				? TargetEyeLocalOffset
				: DebugProfile.DefaultEyeLocalOffset;
			DebugProfile.EmbodiedCameraForwardOffsetCm = TargetEmbodiedCameraForwardOffsetCm;

			FVector AvatarForwardWorld = GetTargetForwardWorld().GetSafeNormal();
			if (AvatarForwardWorld.IsNearlyZero())
			{
				AvatarForwardWorld = ComponentToWorld.TransformVectorNoScale(ForwardHintComp).GetSafeNormal();
			}
			if (AvatarForwardWorld.IsNearlyZero())
			{
				AvatarForwardWorld = FVector::ForwardVector;
			}
			FVector AvatarUpWorld =
				FMediaPipeAvatarEmbodimentSolver::GetAvatarUpWorld(TargetCompTransform, AvatarForwardWorld).GetSafeNormal();
			if (AvatarUpWorld.IsNearlyZero())
			{
				AvatarUpWorld = ComponentToWorld.TransformVectorNoScale(UpComp).GetSafeNormal();
			}
			if (AvatarUpWorld.IsNearlyZero())
			{
				AvatarUpWorld = FVector::UpVector;
			}
			FVector AvatarRightWorld = FVector::CrossProduct(AvatarUpWorld, AvatarForwardWorld).GetSafeNormal();
			if (AvatarRightWorld.IsNearlyZero())
			{
				AvatarRightWorld = FVector::RightVector;
			}

			auto GetPoseTransformComp = [&](const FBoneReference& Bone, const FTransform& Fallback) -> FTransform
			{
				return Bone.IsValidToEvaluate()
					? CSPose.GetComponentSpaceTransform(Bone.CachedCompactPoseIndex)
					: Fallback;
			};

			const FTransform HeadPoseComp = GetPoseTransformComp(
				Head,
				FTransform(RefHeadComp, HeadComp));
			const FTransform ChestPoseComp = GetPoseTransformComp(
				Spine05.IsValidToEvaluate() ? Spine05 : Spine03,
				FTransform(ChestTargetBasis, ChestComp));
			const FTransform PelvisPoseComp = GetPoseTransformComp(
				Pelvis,
				FTransform(PelvisTargetBasis, PelvisComp));
			const FTransform NeckPoseComp = GetPoseTransformComp(
				Neck,
				FTransform(NeckTargetBasis, NeckTargetComp));
			const FTransform Neck02PoseComp = GetPoseTransformComp(
				Neck02,
				FTransform(Neck02TargetBasis, Neck02TargetComp));

			FVector EyeLocalInHead = ResolveMediaPipeAvatarProfileEyeLocalInHead(DebugProfile);
			if (bCanAnchorHeadFromEye)
			{
				EyeLocalInHead = bHasEyeLocalInHeadForPose ? EyeLocalInHeadForPose : EyeLocalInHeadForSolve;
			}
			const FVector PosedEyeComp = HeadPoseComp.TransformPosition(EyeLocalInHead);
			const FVector PosedEyeWorld = ComponentToWorld.TransformPosition(PosedEyeComp);
			const FVector PosedHeadWorld = ComponentToWorld.TransformPosition(HeadPoseComp.GetTranslation());
			const FVector PosedChestWorld = ComponentToWorld.TransformPosition(ChestPoseComp.GetTranslation());
			const FVector PosedPelvisWorld = ComponentToWorld.TransformPosition(PelvisPoseComp.GetTranslation());
			const FVector PosedNeckWorld = ComponentToWorld.TransformPosition(NeckPoseComp.GetTranslation());
			const FVector PosedNeck02World = ComponentToWorld.TransformPosition(Neck02PoseComp.GetTranslation());
			const FVector ExpectedCameraFromPosedEye =
				PosedEyeWorld + AvatarForwardWorld * DebugProfile.EmbodiedCameraForwardOffsetCm;

			const bool bHasHmd = BodyFusionSourceFrame.HmdStatus.IsFresh();
			const FVector HmdWorld = BodyFusionSourceFrame.HmdLocationWorld;
			const float CameraToPosedEyeCameraCm = bHasHmd
				? FVector::Distance(HmdWorld, ExpectedCameraFromPosedEye)
				: -1.0f;
			const FVector SolverCameraFromEye =
				LastBodyFusionPose.Eye.LocationWorld + AvatarForwardWorld * DebugProfile.EmbodiedCameraForwardOffsetCm;
			const float CameraToSolverCameraCm = bHasHmd
				? FVector::Distance(HmdWorld, SolverCameraFromEye)
				: -1.0f;
			const float SolverEyeToPosedEyeCm =
				FVector::Distance(LastBodyFusionPose.Eye.LocationWorld, PosedEyeWorld);
			const float SolverHeadToPosedHeadCm =
				FVector::Distance(LastBodyFusionPose.Head.LocationWorld, PosedHeadWorld);
			const float SolverChestToPosedChestCm =
				FVector::Distance(LastBodyFusionPose.Chest.LocationWorld, PosedChestWorld);
			const FVector HmdToPosedChestWorld = PosedChestWorld - HmdWorld;
			const float HmdToPosedChestCm = bHasHmd ? HmdToPosedChestWorld.Size() : -1.0f;
			const float HmdToPosedChestForwardCm = bHasHmd
				? FVector::DotProduct(HmdToPosedChestWorld, AvatarForwardWorld)
				: 0.0f;
			const float HmdToPosedChestUpCm = bHasHmd
				? FVector::DotProduct(HmdToPosedChestWorld, AvatarUpWorld)
				: 0.0f;
			const float HmdToPosedChestRightCm = bHasHmd
				? FVector::DotProduct(HmdToPosedChestWorld, AvatarRightWorld)
				: 0.0f;
			const FVector HmdToPosedHeadWorld = PosedHeadWorld - HmdWorld;
			const float HmdToPosedHeadCm = bHasHmd ? HmdToPosedHeadWorld.Size() : -1.0f;
			const float HmdToPosedHeadForwardCm = bHasHmd
				? FVector::DotProduct(HmdToPosedHeadWorld, AvatarForwardWorld)
				: 0.0f;
			const float HmdToPosedHeadRightCm = bHasHmd
				? FVector::DotProduct(HmdToPosedHeadWorld, AvatarRightWorld)
				: 0.0f;
			const float HmdToPosedHeadUpCm = bHasHmd
				? FVector::DotProduct(HmdToPosedHeadWorld, AvatarUpWorld)
				: 0.0f;
			const FVector EyeAnchorResidualDeltaWorld =
				ComponentToWorld.TransformVectorNoScale(EyeAnchorResidualDeltaComp);
			const float EyeAnchorResidualForwardCm =
				FVector::DotProduct(EyeAnchorResidualDeltaWorld, AvatarForwardWorld);
			const float EyeAnchorResidualRightCm =
				FVector::DotProduct(EyeAnchorResidualDeltaWorld, AvatarRightWorld);
			const float EyeAnchorResidualUpCm =
				FVector::DotProduct(EyeAnchorResidualDeltaWorld, AvatarUpWorld);

			auto ForwardLeanDegrees = [&](const FVector& SegmentWorld) -> float
			{
				FVector Segment = SegmentWorld.GetSafeNormal();
				Segment = (Segment - FVector::DotProduct(Segment, AvatarRightWorld) * AvatarRightWorld).GetSafeNormal();
				if (Segment.IsNearlyZero())
				{
					return 0.0f;
				}
				return FMath::RadiansToDegrees(FMath::Atan2(
					FVector::DotProduct(Segment, AvatarForwardWorld),
					FVector::DotProduct(Segment, AvatarUpWorld)));
			};

			auto SideLeanDegrees = [&](const FVector& SegmentWorld) -> float
			{
				FVector Segment = SegmentWorld.GetSafeNormal();
				Segment = (Segment - FVector::DotProduct(Segment, AvatarForwardWorld) * AvatarForwardWorld).GetSafeNormal();
				if (Segment.IsNearlyZero())
				{
					return 0.0f;
				}
				return FMath::RadiansToDegrees(FMath::Atan2(
					FVector::DotProduct(Segment, AvatarRightWorld),
					FVector::DotProduct(Segment, AvatarUpWorld)));
			};

			auto SignedYawFromAvatarDegrees = [&](const FVector& SegmentWorld) -> float
			{
				FVector Segment = SegmentWorld.GetSafeNormal();
				Segment = (Segment - FVector::DotProduct(Segment, AvatarUpWorld) * AvatarUpWorld).GetSafeNormal();
				FVector ForwardPlanar = (AvatarForwardWorld - FVector::DotProduct(AvatarForwardWorld, AvatarUpWorld) * AvatarUpWorld).GetSafeNormal();
				if (Segment.IsNearlyZero() || ForwardPlanar.IsNearlyZero())
				{
					return 0.0f;
				}
				const float SignedSin =
					FVector::DotProduct(FVector::CrossProduct(ForwardPlanar, Segment), AvatarUpWorld);
				const float Cos = FVector::DotProduct(ForwardPlanar, Segment);
				return FMath::RadiansToDegrees(FMath::Atan2(SignedSin, Cos));
			};

			const FRotator HmdRot = bHasHmd ? BodyFusionSourceFrame.HmdRotationWorld.Rotator() : FRotator::ZeroRotator;
			const FVector HmdForwardWorld = bHasHmd
				? BodyFusionSourceFrame.HmdRotationWorld.RotateVector(FVector::ForwardVector).GetSafeNormal()
				: FVector::ZeroVector;
			const float HmdPitchDeg = bHasHmd ? HmdRot.Pitch : 0.0f;
			const float HmdYawDeg = bHasHmd ? HmdRot.Yaw : 0.0f;
			const float HmdRollDeg = bHasHmd ? HmdRot.Roll : 0.0f;
			const float HmdYawFromAvatarDeg = bHasHmd ? SignedYawFromAvatarDegrees(HmdForwardWorld) : 0.0f;
			const float PosedPelvisChestLeanDeg = ForwardLeanDegrees(PosedChestWorld - PosedPelvisWorld);
			const float PosedChestHeadLeanDeg = ForwardLeanDegrees(PosedHeadWorld - PosedChestWorld);
			const float SolverPelvisChestLeanDeg =
				ForwardLeanDegrees(LastBodyFusionPose.Chest.LocationWorld - LastBodyFusionPose.Pelvis.LocationWorld);
			const float SolverChestHeadLeanDeg =
				ForwardLeanDegrees(LastBodyFusionPose.Head.LocationWorld - LastBodyFusionPose.Chest.LocationWorld);
			const float PosedPelvisChestSideLeanDeg = SideLeanDegrees(PosedChestWorld - PosedPelvisWorld);
			const float PosedChestNeckSideLeanDeg = SideLeanDegrees(PosedNeckWorld - PosedChestWorld);
			const float PosedNeckHeadSideLeanDeg = SideLeanDegrees(PosedHeadWorld - PosedNeckWorld);
			const float PosedChestHeadSideLeanDeg = SideLeanDegrees(PosedHeadWorld - PosedChestWorld);
			const float SolverPelvisChestSideLeanDeg =
				SideLeanDegrees(LastBodyFusionPose.Chest.LocationWorld - LastBodyFusionPose.Pelvis.LocationWorld);
			const float SolverChestHeadSideLeanDeg =
				SideLeanDegrees(LastBodyFusionPose.Head.LocationWorld - LastBodyFusionPose.Chest.LocationWorld);
			const bool bHasSolverNeck = LastBodyFusionPose.Neck.bValid;
			const float SolverChestNeckSideLeanDeg = bHasSolverNeck
				? SideLeanDegrees(LastBodyFusionPose.Neck.LocationWorld - LastBodyFusionPose.Chest.LocationWorld)
				: 0.0f;
			const float SolverNeckHeadSideLeanDeg = bHasSolverNeck
				? SideLeanDegrees(LastBodyFusionPose.Head.LocationWorld - LastBodyFusionPose.Neck.LocationWorld)
				: 0.0f;
			const FVector SolverNeckToPosedNeckWorld = bHasSolverNeck
				? PosedNeckWorld - LastBodyFusionPose.Neck.LocationWorld
				: FVector::ZeroVector;
			const float SolverNeckToPosedNeckCm = bHasSolverNeck
				? SolverNeckToPosedNeckWorld.Size()
				: -1.0f;
			const float SolverNeckToPosedNeckRightCm = bHasSolverNeck
				? FVector::DotProduct(SolverNeckToPosedNeckWorld, AvatarRightWorld)
				: 0.0f;
			const float SolverNeckToPosedNeckForwardCm = bHasSolverNeck
				? FVector::DotProduct(SolverNeckToPosedNeckWorld, AvatarForwardWorld)
				: 0.0f;
			const float SolverNeckToPosedNeckUpCm = bHasSolverNeck
				? FVector::DotProduct(SolverNeckToPosedNeckWorld, AvatarUpWorld)
				: 0.0f;

			FVector MediaPipeHeadAvatarWorld = FVector::ZeroVector;
			FVector MediaPipeShoulderAvatarWorld = FVector::ZeroVector;
			float MediaPipeHeadReliability = 0.0f;
			float MediaPipeShoulderReliability = 0.0f;
			const bool bHasMediaPipeHead =
				BodyFusionCalibration.IsUsable() &&
				BodyFusionSourceFrame.TryGetMediaPipeLandmark(
					EMediaPipePoseLandmark::Nose,
					MediaPipeHeadAvatarWorld,
					&MediaPipeHeadReliability);
			if (bHasMediaPipeHead)
			{
				MediaPipeHeadAvatarWorld = BodyFusionCalibration.TransformMediaPipePoint(MediaPipeHeadAvatarWorld);
			}
			const bool bHasMediaPipeShoulder =
				BodyFusionCalibration.IsUsable() &&
				TryBodyFusionLandmarkMidpoint(
					BodyFusionSourceFrame,
					EMediaPipePoseLandmark::LeftShoulder,
					EMediaPipePoseLandmark::RightShoulder,
					MediaPipeShoulderAvatarWorld,
					&MediaPipeShoulderReliability);
			if (bHasMediaPipeShoulder)
			{
				MediaPipeShoulderAvatarWorld = BodyFusionCalibration.TransformMediaPipePoint(MediaPipeShoulderAvatarWorld);
			}

			const float MediaPipeHeadToHmdCm = bHasHmd && bHasMediaPipeHead
				? FVector::Distance(HmdWorld, MediaPipeHeadAvatarWorld)
				: -1.0f;
			const float MediaPipeHeadToSolverHeadCm = bHasMediaPipeHead
				? FVector::Distance(LastBodyFusionPose.Head.LocationWorld, MediaPipeHeadAvatarWorld)
				: -1.0f;
			const float MediaPipeShoulderToSolverChestCm = bHasMediaPipeShoulder
				? FVector::Distance(LastBodyFusionPose.Chest.LocationWorld, MediaPipeShoulderAvatarWorld)
				: -1.0f;

			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.BodyFusion.HeadAnchor actor=%s skeleton=%s bodyAuthority=%s mediaPipeAuthority=%d reason=\"%s\" directNeckChain=1 hmd=%s solverCamera=%s posedCamera=%s solverEye=%s posedEye=%s posedHead=%s posedChest=%s posedPelvis=%s eyeAnchor(residual=%.1f) err(cameraToSolverCamera=%.1f cameraToPosedCamera=%.1f solverEyeToPosedEye=%.1f solverHeadToPosedHead=%.1f solverChestToPosedChest=%.1f) ownerView(chestDist=%.1f chestForward=%.1f chestUp=%.1f) lean(hmdPitch=%.1f posedPelvisChest=%.1f posedChestHead=%.1f solverPelvisChest=%.1f solverChestHead=%.1f) mediapipe(calibrated=%d scale=%.3f stableFrames=%d stableSeconds=%.2f nose=%s noseRel=%.2f noseToHmd=%.1f noseToSolverHead=%.1f shoulders=%s shoulderRel=%.2f shoulderToSolverChest=%.1f)"),
				*TargetActorName.ToString(),
				DebugProfile.SkeletonFamily == EMediaPipeAvatarSkeletonFamily::MetaHuman ? TEXT("MetaHuman") : TEXT("Manny"),
				BodyFusionAuthorityStateName(LastBodyFusionAuthorityState),
				bLastBodyFusionMediaPipeAuthorityAllowed ? 1 : 0,
				*LastBodyFusionAuthorityReason,
				*BodyFusionVectorString(HmdWorld),
				*BodyFusionVectorString(SolverCameraFromEye),
				*BodyFusionVectorString(ExpectedCameraFromPosedEye),
				*BodyFusionVectorString(LastBodyFusionPose.Eye.LocationWorld),
				*BodyFusionVectorString(PosedEyeWorld),
				*BodyFusionVectorString(PosedHeadWorld),
				*BodyFusionVectorString(PosedChestWorld),
				*BodyFusionVectorString(PosedPelvisWorld),
				EyeAnchorResidualCm,
				CameraToSolverCameraCm,
				CameraToPosedEyeCameraCm,
				SolverEyeToPosedEyeCm,
				SolverHeadToPosedHeadCm,
				SolverChestToPosedChestCm,
				HmdToPosedChestCm,
				HmdToPosedChestForwardCm,
				HmdToPosedChestUpCm,
				HmdPitchDeg,
				PosedPelvisChestLeanDeg,
				PosedChestHeadLeanDeg,
				SolverPelvisChestLeanDeg,
				SolverChestHeadLeanDeg,
				BodyFusionCalibration.IsUsable() ? 1 : 0,
				BodyFusionCalibration.Scale,
				BodyFusionCalibrationStableFrameCount,
				BodyFusionCalibrationStableSeconds,
				bHasMediaPipeHead ? *BodyFusionVectorString(MediaPipeHeadAvatarWorld) : TEXT("(missing)"),
				MediaPipeHeadReliability,
				MediaPipeHeadToHmdCm,
				MediaPipeHeadToSolverHeadCm,
				bHasMediaPipeShoulder ? *BodyFusionVectorString(MediaPipeShoulderAvatarWorld) : TEXT("(missing)"),
				MediaPipeShoulderReliability,
				MediaPipeShoulderToSolverChestCm);

			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.BodyFusion.HeadAnchorSide actor=%s skeleton=%s hmdRot(pitch=%.1f yaw=%.1f roll=%.1f yawFromAvatar=%.1f) ownerView(chestRight=%.1f headDist=%.1f headForward=%.1f headRight=%.1f headUp=%.1f) sideLean(posedPelvisChest=%.1f posedChestNeck=%.1f posedNeckHead=%.1f posedChestHead=%.1f solverPelvisChest=%.1f solverChestNeck=%.1f solverNeckHead=%.1f solverChestHead=%.1f) neck(posed=%s posedNeck02=%s solver=%s solverToPosed=%.1f right=%.1f forward=%.1f up=%.1f) eyeAnchorResidual(vec=%s right=%.1f forward=%.1f up=%.1f)"),
				*TargetActorName.ToString(),
				DebugProfile.SkeletonFamily == EMediaPipeAvatarSkeletonFamily::MetaHuman ? TEXT("MetaHuman") : TEXT("Manny"),
				HmdPitchDeg,
				HmdYawDeg,
				HmdRollDeg,
				HmdYawFromAvatarDeg,
				HmdToPosedChestRightCm,
				HmdToPosedHeadCm,
				HmdToPosedHeadForwardCm,
				HmdToPosedHeadRightCm,
				HmdToPosedHeadUpCm,
				PosedPelvisChestSideLeanDeg,
				PosedChestNeckSideLeanDeg,
				PosedNeckHeadSideLeanDeg,
				PosedChestHeadSideLeanDeg,
				SolverPelvisChestSideLeanDeg,
				SolverChestNeckSideLeanDeg,
				SolverNeckHeadSideLeanDeg,
				SolverChestHeadSideLeanDeg,
				*BodyFusionVectorString(PosedNeckWorld),
				*BodyFusionVectorString(PosedNeck02World),
				bHasSolverNeck ? *BodyFusionVectorString(LastBodyFusionPose.Neck.LocationWorld) : TEXT("(missing)"),
				SolverNeckToPosedNeckCm,
				SolverNeckToPosedNeckRightCm,
				SolverNeckToPosedNeckForwardCm,
				SolverNeckToPosedNeckUpCm,
				*BodyFusionVectorString(EyeAnchorResidualDeltaWorld),
				EyeAnchorResidualRightCm,
				EyeAnchorResidualForwardCm,
				EyeAnchorResidualUpCm);
		}
	}

	return true;
}

#include "MediaPipePoseDrivenAnimInstance_ArmTwist.inl"

#include "MediaPipePoseDrivenAnimInstance_BodyPoseSolve.inl"

#include "MediaPipePoseDrivenAnimInstance_QuestArmWristSolve.inl"

#include "MediaPipePoseDrivenAnimInstance_LegSolve.inl"

void FAnimNode_MediaPipePoseDriven::Evaluate_AnyThread(FPoseContext& Output)
{
	const float DeltaSeconds = FMath::Max(CachedDeltaTimeSeconds, 0.0f);
	CachedDeltaTimeSeconds = 0.0f;

	Output.ResetToRefPose();

	if (!bHasReferencePose || !bHasPoseFrame)
	{
		return;
	}

	const int64 ActivePoseTimestampUs = PoseFrame.TimestampUs;

	if (bHasLastPoseTimestamp && ActivePoseTimestampUs < LastPoseTimestampUs)
	{
		BodyState.bHasReferenceHipHeight = false;
		BodyState.ReferenceHipHeightCm = 0.0f;
		BodyState.bHasSmoothedPelvisOffset = false;
		BodyState.SmoothedPelvisOffsetComp = FVector::ZeroVector;
		BodyState.bHasSmoothedFkRootGroundOffset = false;
		BodyState.SmoothedFkRootGroundOffsetComp = FVector::ZeroVector;
		LeftArmState.bHasSmoothedArmIK = false;
		RightArmState.bHasSmoothedArmIK = false;
		LeftLegState.bHasSmoothedLegPlane = false;
		RightLegState.bHasSmoothedLegPlane = false;
		ResetFootPlantState();
		ResetPoseYawAlignRuntimeState(RuntimeStateKey);
		ResetQuestWristRuntimeState(RuntimeStateKey);
		ResetRotationSmoothing();
		UE_LOG(
			LogMediaPipePose,
			Warning,
			TEXT("MediaPipe pose timestamp rewind: reset solver continuity actor=%s currentUs=%lld previousUs=%lld runtimeKey=%u."),
			*TargetActorName.ToString(),
			ActivePoseTimestampUs,
			LastPoseTimestampUs,
			RuntimeStateKey);
		for (uint8& B : EverMeasured)
		{
			B = 0;
		}
	}
	bHasLastPoseTimestamp = true;
	LastPoseTimestampUs = ActivePoseTimestampUs;

	FCSPose<FCompactPose> CSPose;
	CSPose.InitPose(Output.Pose);

	LeftLegState.bCurrentSourceFootGrounded = false;
	RightLegState.bCurrentSourceFootGrounded = false;

	if (!DriveBodyFusionPoseCS(CSPose, DeltaSeconds))
	{
		DrivePelvisTranslationCS(CSPose, DeltaSeconds);
		DriveSpineCS(CSPose, DeltaSeconds);
	}
	DriveLegCS(CSPose, true, DeltaSeconds);
	DriveLegCS(CSPose, false, DeltaSeconds);
	UpdateFkRootGroundingCS(CSPose, DeltaSeconds);
	DriveArmCS(CSPose, true, DeltaSeconds);
	DriveArmCS(CSPose, false, DeltaSeconds);
	DriveArmTwistBonesCS(CSPose, DeltaSeconds);

	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(CSPose, Output.Pose);

	if (!BodyState.SmoothedFkRootGroundOffsetComp.IsNearlyZero())
	{
		FBoneReference RootToTranslate = Root;
		if (!RootToTranslate.IsValidToEvaluate())
		{
			RootToTranslate = Pelvis;
		}

		if (RootToTranslate.IsValidToEvaluate())
		{
			const FCompactPoseBoneIndex RootIdx = RootToTranslate.CachedCompactPoseIndex;
			FTransform RootLocal = Output.Pose[RootIdx];
			RootLocal.SetTranslation(RootLocal.GetTranslation() + BodyState.SmoothedFkRootGroundOffsetComp);
			Output.Pose[RootIdx] = RootLocal;
		}
	}
}

void FAnimNode_MediaPipePoseDriven::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);
	DebugLine += FString::Printf(TEXT(" (Pose=%s, Ref=%s, Source=%s)"),
		bHasPoseFrame ? TEXT("yes") : TEXT("no"),
		bHasReferencePose ? TEXT("yes") : TEXT("no"),
		*GetNameSafe(SourceActor));
	DebugData.AddDebugItem(DebugLine);
}

void FMediaPipePoseDrivenAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);

	FAnimationInitializeContext InitContext(this);
	PoseNode.Initialize_AnyThread(InitContext);
}

void FMediaPipePoseDrivenAnimInstanceProxy::CacheBones()
{
	FAnimInstanceProxy::CacheBones();

	FAnimationCacheBonesContext CacheContext(this);
	PoseNode.CacheBones_AnyThread(CacheContext);
}

void FMediaPipePoseDrivenAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	Super::PreUpdate(InAnimInstance, DeltaSeconds);
	if (PoseNode.HasPreUpdate())
	{
		PoseNode.PreUpdate(InAnimInstance);
	}
}

bool FMediaPipePoseDrivenAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	PoseNode.Evaluate_AnyThread(Output);
	return true;
}

void FMediaPipePoseDrivenAnimInstanceProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	UpdateCounter.Increment();
	PoseNode.Update_AnyThread(InContext);
}

FAnimInstanceProxy* UMediaPipePoseDrivenAnimInstance::CreateAnimInstanceProxy()
{
	return new FMediaPipePoseDrivenAnimInstanceProxy(this);
}

void UMediaPipePoseDrivenAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	Super::DestroyAnimInstanceProxy(InProxy);
}

void UMediaPipePoseDrivenAnimInstance::SetSourceActor(AActor* InSource)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.SourceActor != InSource)
	{
		Proxy.PoseNode.SourceActor = InSource;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::ResetRetargetState()
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	Proxy.PoseNode.bResetPoseStateNextUpdate = true;
}

void UMediaPipePoseDrivenAnimInstance::ApplyRetargetQualitySettings()
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	FAnimNode_MediaPipePoseDriven& PoseNode = Proxy.PoseNode;

	const bool bNewDriveClavicles = CVarMediaPipeDriveClavicles.GetValueOnGameThread() != 0;
	const bool bNewDriveSpine = CVarMediaPipeDriveSpine.GetValueOnGameThread() != 0;
	const bool bNewDrivePelvisTranslation = CVarMediaPipeDrivePelvisTranslation.GetValueOnGameThread() != 0;
	const bool bNewDriveLegs = CVarMediaPipeDriveLegs.GetValueOnGameThread() != 0;
	const bool bNewUseArmIK = CVarMediaPipeUseArmIK.GetValueOnGameThread() != 0;
	const bool bNewUseLegIK = CVarMediaPipeUseLegIK.GetValueOnGameThread() != 0;
	const bool bNewUseFkRootGrounding = CVarMediaPipeUseFkRootGrounding.GetValueOnGameThread() != 0;
	const bool bNewDriveHandRotation = CVarMediaPipeDriveHandRotation.GetValueOnGameThread() != 0;
	const bool bNewUseQuestHandTracking = CVarQuestHandTracking.GetValueOnGameThread() != 0;
	const bool bNewDriveQuestFingerBones = CVarQuestHandDriveFingerBones.GetValueOnGameThread() != 0;
	const float NewQuestHandRotationBlend = FMath::Clamp(CVarQuestHandRotationBlend.GetValueOnGameThread(), 0.0f, 1.0f);
	const float NewQuestFingerRotationHalfLifeSeconds = FMath::Max(0.0f, CVarQuestFingerRotationHalfLife.GetValueOnGameThread());
	const float NewArmTargetHalfLifeSeconds = FMath::Max(0.0f, CVarMediaPipeArmTargetHalfLife.GetValueOnGameThread());
	const float NewArmRotationHalfLifeSeconds = FMath::Max(0.0f, CVarMediaPipeArmRotationHalfLife.GetValueOnGameThread());
	const float NewSpineRotationHalfLifeSeconds = FMath::Max(0.0f, CVarMediaPipeSpineRotationHalfLife.GetValueOnGameThread());
	const float NewHeadRotationHalfLifeSeconds = FMath::Max(0.0f, CVarMediaPipeHeadRotationHalfLife.GetValueOnGameThread());
	const float NewHeadTwistWeight = FMath::Clamp(CVarMediaPipeHeadTwistWeight.GetValueOnGameThread(), 0.0f, 1.0f);
	const float NewHeadFaceBlend = FMath::Clamp(CVarMediaPipeHeadFaceBlend.GetValueOnGameThread(), 0.0f, 1.0f);
	const float NewHeadRotationMaxStepDegrees = FMath::Max(0.0f, CVarMediaPipeHeadRotationMaxStepDegrees.GetValueOnGameThread());
	const float NewHeadRotationMaxSpeedDegreesPerSecond = FMath::Max(0.0f, CVarMediaPipeHeadRotationMaxSpeedDegreesPerSecond.GetValueOnGameThread());

	const bool bChanged =
		PoseNode.bDriveClavicles != bNewDriveClavicles ||
		PoseNode.bDriveSpine != bNewDriveSpine ||
		PoseNode.bDrivePelvisTranslation != bNewDrivePelvisTranslation ||
		PoseNode.bDriveLegs != bNewDriveLegs ||
		PoseNode.bUseArmIK != bNewUseArmIK ||
		PoseNode.bUseLegIK != bNewUseLegIK ||
		PoseNode.bUseFkRootGrounding != bNewUseFkRootGrounding ||
		PoseNode.bDriveHandRotation != bNewDriveHandRotation ||
		PoseNode.bUseQuestHandTracking != bNewUseQuestHandTracking ||
		PoseNode.bDriveQuestFingerBones != bNewDriveQuestFingerBones ||
		!FMath::IsNearlyEqual(PoseNode.QuestHandRotationBlend, NewQuestHandRotationBlend, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.QuestFingerRotationHalfLifeSeconds, NewQuestFingerRotationHalfLifeSeconds, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.ArmIKTargetHalfLifeSeconds, NewArmTargetHalfLifeSeconds, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.ArmIKRotationHalfLifeSeconds, NewArmRotationHalfLifeSeconds, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.SpineRotationHalfLifeSeconds, NewSpineRotationHalfLifeSeconds, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.HeadRotationHalfLifeSeconds, NewHeadRotationHalfLifeSeconds, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.HeadTwistWeight, NewHeadTwistWeight, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.HeadFaceBlend, NewHeadFaceBlend, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.HeadRotationMaxStepDegrees, NewHeadRotationMaxStepDegrees, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.HeadRotationMaxSpeedDegreesPerSecond, NewHeadRotationMaxSpeedDegreesPerSecond, 0.001f);

	PoseNode.bDriveClavicles = bNewDriveClavicles;
	PoseNode.bDriveSpine = bNewDriveSpine;
	PoseNode.bDrivePelvisTranslation = bNewDrivePelvisTranslation;
	PoseNode.bDriveLegs = bNewDriveLegs;
	PoseNode.bUseArmIK = bNewUseArmIK;
	PoseNode.bUseLegIK = bNewUseLegIK;
	PoseNode.bUseFkRootGrounding = bNewUseFkRootGrounding;
	PoseNode.bDriveHandRotation = bNewDriveHandRotation;
	PoseNode.bUseQuestHandTracking = bNewUseQuestHandTracking;
	PoseNode.bDriveQuestFingerBones = bNewDriveQuestFingerBones;
	PoseNode.QuestHandRotationBlend = NewQuestHandRotationBlend;
	PoseNode.QuestFingerRotationHalfLifeSeconds = NewQuestFingerRotationHalfLifeSeconds;
	PoseNode.ArmIKTargetHalfLifeSeconds = NewArmTargetHalfLifeSeconds;
	PoseNode.ArmIKRotationHalfLifeSeconds = NewArmRotationHalfLifeSeconds;
	PoseNode.SpineRotationHalfLifeSeconds = NewSpineRotationHalfLifeSeconds;
	PoseNode.HeadRotationHalfLifeSeconds = NewHeadRotationHalfLifeSeconds;
	PoseNode.HeadTwistWeight = NewHeadTwistWeight;
	PoseNode.HeadFaceBlend = NewHeadFaceBlend;
	PoseNode.HeadRotationMaxStepDegrees = NewHeadRotationMaxStepDegrees;
	PoseNode.HeadRotationMaxSpeedDegreesPerSecond = NewHeadRotationMaxSpeedDegreesPerSecond;

	if (bChanged)
	{
		PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetDriveClavicles(bool bInDriveClavicles)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bDriveClavicles != bInDriveClavicles)
	{
		Proxy.PoseNode.bDriveClavicles = bInDriveClavicles;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetDriveSpine(bool bInDriveSpine)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bDriveSpine != bInDriveSpine)
	{
		Proxy.PoseNode.bDriveSpine = bInDriveSpine;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetDrivePelvisTranslation(bool bInDrivePelvisTranslation)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bDrivePelvisTranslation != bInDrivePelvisTranslation)
	{
		Proxy.PoseNode.bDrivePelvisTranslation = bInDrivePelvisTranslation;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetDriveLegs(bool bInDriveLegs)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bDriveLegs != bInDriveLegs)
	{
		Proxy.PoseNode.bDriveLegs = bInDriveLegs;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetUseArmIK(bool bInUseArmIK)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bUseArmIK != bInUseArmIK)
	{
		Proxy.PoseNode.bUseArmIK = bInUseArmIK;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetUseLegIK(bool bInUseLegIK)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bUseLegIK != bInUseLegIK)
	{
		Proxy.PoseNode.bUseLegIK = bInUseLegIK;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetUseFkRootGrounding(bool bInUseFkRootGrounding)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bUseFkRootGrounding != bInUseFkRootGrounding)
	{
		Proxy.PoseNode.bUseFkRootGrounding = bInUseFkRootGrounding;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetDriveHandRotation(bool bInDriveHandRotation)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bDriveHandRotation != bInDriveHandRotation)
	{
		Proxy.PoseNode.bDriveHandRotation = bInDriveHandRotation;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetUseQuestHandTracking(bool bInUseQuestHandTracking)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bUseQuestHandTracking != bInUseQuestHandTracking)
	{
		Proxy.PoseNode.bUseQuestHandTracking = bInUseQuestHandTracking;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetDriveQuestFingerBones(bool bInDriveQuestFingerBones)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bDriveQuestFingerBones != bInDriveQuestFingerBones)
	{
		Proxy.PoseNode.bDriveQuestFingerBones = bInDriveQuestFingerBones;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}
