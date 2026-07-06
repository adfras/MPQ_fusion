#include "MediaPipePoseDrivenAnimInstance.h"

#include "MediaPipeArmGuardPolicy.h"
#include "MediaPipeArmTwistSolver.h"
#include "MediaPipeBodyDiagnostics.h"
#include "MediaPipeBodyFusionDebugFormatter.h"
#include "MediaPipeBodyFusionPoseWriteContext.h"
#include "MediaPipeBodyFusionRuntime.h"
#include "MediaPipeTrackingSourceFrameBuilder.h"
#include "MediaPipeBodySolverMath.h"
#include "MediaPipeMetaHumanArmRetargeter.h"
#include "MediaPipeMetaHumanPoseAdapter.h"
#include "MediaPipePoseCoordinate.h"
#include "MediaPipePoseDiagnosticReporter.h"
#include "MediaPipePoseDiagnostics.h"
#include "MediaPipePoseFrameContinuity.h"
#include "MediaPipeRuntimeCVars.h"
#include "MediaPipeStage2ShoulderEvidence.h"
#include "MediaPipeTrackingFusionDatasetReplay.h"
#include "MediaPipeQuestHandDebugReporter.h"
#include "MediaPipeQuestHandCompareDiagnostics.h"
#include "MediaPipeQuestFingerSolver.h"
#include "MediaPipeQuestConstrainedArmSolver.h"
#include "MediaPipeQuestWristApplyPolicy.h"
#include "MediaPipeQuestWristDebugReporter.h"
#include "MediaPipeQuestWristCalibrationState.h"
#include "MediaPipeQuestWristDiagnosticFormatter.h"
#include "MediaPipeSkeletonPoseAdapter.h"
#include "MediaPipeShoulderRollbackDiagnostics.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeSolvedPose.h"

#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"

#include <atomic>
#include "HAL/IConsoleManager.h"
#include "HeadMountedDisplayTypes.h"
#include "Math/RotationMatrix.h"

#include "MediaPipePoseDrivenAnimInstanceShared.h"

void FAnimNode_MediaPipePoseDriven::DrivePelvisTranslationCS(FCSPose<FCompactPose>& CSPose, float DeltaSeconds)
{
	if (!bDriveLegs || !bDrivePelvisTranslation)
	{
		return;
	}

	if (!Pelvis.IsValidToEvaluate())
	{
		return;
	}

	// --- Quest/HMD metric height scaffold (lower body) ---
	// Updated every evaluation so the rolling standing baseline stays warm. Monocular MediaPipe
	// supplies squat/stand timing but not metric depth (the recorded landmark frame is
	// hip-centered); the HMD height against its standing baseline supplies the metric magnitude.
	// The fused result drives the pelvis offset below and the grounded-leg flexion correction in
	// DriveLegCS, keeping both consistent.
	const float ScaffoldHmdWeight = FMath::Clamp(CVarMediaPipeLegScaffoldHmdWeight.GetValueOnAnyThread(), 0.0f, 1.0f);
	BodyState.bHasLowerBodyScaffoldSample = false;
	BodyState.bScaffoldHmdPoseValid = bHasCachedQuestHmdPose;
	BodyState.ScaffoldHmdHeightZ = CachedQuestHmdWorld.Z;
	MediaPipeBodySolverMath::FMediaPipeHmdHeightScaffoldResult HmdScaffoldResult;
	if (ScaffoldHmdWeight > KINDA_SMALL_NUMBER)
	{
		// Raw torso pitch from the landmark midlines (not the constrained torso basis, whose
		// upright blend would hide the very lean this compensation needs to observe).
		float TorsoUprightDot = 1.0f;
		{
			FVector LShoulderWorld = FVector::ZeroVector;
			FVector RShoulderWorld = FVector::ZeroVector;
			FVector LHipWorldForLean = FVector::ZeroVector;
			FVector RHipWorldForLean = FVector::ZeroVector;
			if (TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftShoulder, LShoulderWorld) &&
				TryGetLmWorld((int32)EMediaPipePoseLandmark::RightShoulder, RShoulderWorld) &&
				TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftHip, LHipWorldForLean) &&
				TryGetLmWorld((int32)EMediaPipePoseLandmark::RightHip, RHipWorldForLean))
			{
				const FVector RawTorsoUp =
					((LShoulderWorld + RShoulderWorld) * 0.5f - (LHipWorldForLean + RHipWorldForLean) * 0.5f).GetSafeNormal();
				if (!RawTorsoUp.IsNearlyZero())
				{
					TorsoUprightDot = FMath::Clamp(FVector::DotProduct(RawTorsoUp, FVector::UpVector), 0.0f, 1.0f);
				}
			}
		}

		MediaPipeBodySolverMath::FMediaPipeHmdHeightScaffoldInput HmdScaffoldInput;
		HmdScaffoldInput.bHasHmdPose = bHasCachedQuestHmdPose;
		HmdScaffoldInput.HmdHeightZ = CachedQuestHmdWorld.Z;
		HmdScaffoldInput.DeltaSeconds = DeltaSeconds;
		HmdScaffoldInput.BaselineWindowSeconds =
			FMath::Max(CVarMediaPipeLegScaffoldBaselineWindowSeconds.GetValueOnAnyThread(), 1.0f);
		HmdScaffoldInput.TorsoUprightDot = TorsoUprightDot;
		HmdScaffoldInput.LeanCompensationCoefficient =
			FMath::Max(CVarMediaPipeLegScaffoldLeanCoefficient.GetValueOnAnyThread(), 0.0f);
		HmdScaffoldInput.HipFromHmdRatio = CVarMediaPipeLegScaffoldHipFromHmdRatio.GetValueOnAnyThread();
		HmdScaffoldResult = MediaPipeBodySolverMath::UpdateHmdHeightScaffold(BodyState.HmdHeightScaffold, HmdScaffoldInput);
	}
	BodyState.ScaffoldHmdBaselineZ = HmdScaffoldResult.BaselineHeadZ;
	BodyState.ScaffoldHmdHeadDropCm = HmdScaffoldResult.HeadDropCm;
	BodyState.ScaffoldHmdLeanCompCm = HmdScaffoldResult.LeanCompensationCm;
	BodyState.ScaffoldHmdAlpha01 = HmdScaffoldResult.CompressionAlpha01;
	BodyState.ScaffoldHmdConfidence = HmdScaffoldResult.Confidence;

	const int32 LHipLm = (int32)EMediaPipePoseLandmark::LeftHip;
	const int32 RHipLm = (int32)EMediaPipePoseLandmark::RightHip;
	const int32 LKneeLm = (int32)EMediaPipePoseLandmark::LeftKnee;
	const int32 RKneeLm = (int32)EMediaPipePoseLandmark::RightKnee;
	const int32 LAnkleLm = (int32)EMediaPipePoseLandmark::LeftAnkle;
	const int32 RAnkleLm = (int32)EMediaPipePoseLandmark::RightAnkle;
	const int32 LHeelLm = (int32)EMediaPipePoseLandmark::LeftHeel;
	const int32 RHeelLm = (int32)EMediaPipePoseLandmark::RightHeel;
	const int32 LToeLm = (int32)EMediaPipePoseLandmark::LeftFootIndex;
	const int32 RToeLm = (int32)EMediaPipePoseLandmark::RightFootIndex;

	// Derive pelvis height from hip-vs-floor height and smooth over time.
	// Use IsMeasured() so when the tracker temporarily holds joints (Reliability=0) we still have
	// stable positions to derive squat depth from, rather than snapping back to the ref pose.
	// Measure against the lowest observed foot contact, not the live ankle midpoint, otherwise ankle
	// jitter during a squat cancels the hip drop and the knees never get enough compression to bend.
	const bool bHasLeft = IsMeasured(LHipLm) && IsMeasured(LAnkleLm);
	const bool bHasRight = IsMeasured(RHipLm) && IsMeasured(RAnkleLm);

	if (bHasLeft || bHasRight)
	{
		FVector LHipWorld = FVector::ZeroVector;
		FVector RHipWorld = FVector::ZeroVector;
		FVector LKneeWorld = FVector::ZeroVector;
		FVector RKneeWorld = FVector::ZeroVector;
		FVector LAnkleWorld = FVector::ZeroVector;
		FVector RAnkleWorld = FVector::ZeroVector;
		FVector LHeelWorld = FVector::ZeroVector;
		FVector RHeelWorld = FVector::ZeroVector;
		FVector LToeWorld = FVector::ZeroVector;
		FVector RToeWorld = FVector::ZeroVector;
		const bool bGotLeft = bHasLeft && TryGetLmWorld(LHipLm, LHipWorld) && TryGetLmWorld(LAnkleLm, LAnkleWorld);
		const bool bGotRight = bHasRight && TryGetLmWorld(RHipLm, RHipWorld) && TryGetLmWorld(RAnkleLm, RAnkleWorld);
		const bool bGotLeftKnee = IsMeasured(LKneeLm) && TryGetLmWorld(LKneeLm, LKneeWorld);
		const bool bGotRightKnee = IsMeasured(RKneeLm) && TryGetLmWorld(RKneeLm, RKneeWorld);
		const bool bGotLeftHeel = IsMeasured(LHeelLm) && TryGetLmWorld(LHeelLm, LHeelWorld);
		const bool bGotRightHeel = IsMeasured(RHeelLm) && TryGetLmWorld(RHeelLm, RHeelWorld);
		const bool bGotLeftToe = IsMeasured(LToeLm) && TryGetLmWorld(LToeLm, LToeWorld);
		const bool bGotRightToe = IsMeasured(RToeLm) && TryGetLmWorld(RToeLm, RToeWorld);

		if (bGotLeft || bGotRight)
		{
			const FVector HipWorld = (bGotLeft && bGotRight) ? (LHipWorld + RHipWorld) * 0.5f : (bGotLeft ? LHipWorld : RHipWorld);
			float StableFootFloorZ = TNumericLimits<float>::Max();
			bool bHasStableFootFloor = false;

			auto ConsiderFootFloor = [&](bool bGotHipAnkle, bool bGotToe, const FVector& AnkleWorld, const FVector& ToeWorld, bool bHasObservedFloor, float ObservedFloorZ)
			{
				if (!bGotHipAnkle)
				{
					return;
				}

				float CandidateFloorZ = AnkleWorld.Z;
				if (bHasObservedFloor)
				{
					// Once we have a planted-foot floor estimate, keep using it instead of letting live toe/ankle
					// samples drift the floor downward during a squat and cancel the hip drop.
					CandidateFloorZ = ObservedFloorZ;
				}
				else if (bGotToe)
				{
					CandidateFloorZ = FMath::Min(CandidateFloorZ, ToeWorld.Z);
				}

				StableFootFloorZ = bHasStableFootFloor ? FMath::Min(StableFootFloorZ, CandidateFloorZ) : CandidateFloorZ;
				bHasStableFootFloor = true;
			};

			auto TryGetSupportPointWorld = [&](bool bGotHipAnkle, bool bGotHeel, bool bGotToe, const FVector& AnkleWorld, const FVector& HeelWorld, const FVector& ToeWorld, bool bHasObservedFloor, float ObservedFloorZ, FVector& OutSupportWorld)
			{
				if (!bGotHipAnkle)
				{
					return false;
				}

				FVector Accum = FVector::ZeroVector;
				int32 Samples = 0;
				auto AddSample = [&](const FVector& Sample)
				{
					Accum += Sample;
					++Samples;
				};

				if (bGotHeel && bGotToe)
				{
					AddSample(HeelWorld);
					AddSample(ToeWorld);
				}
				else
				{
					AddSample(AnkleWorld);
					if (bGotHeel)
					{
						AddSample(HeelWorld);
					}
					if (bGotToe)
					{
						AddSample(ToeWorld);
					}
				}

				OutSupportWorld = Accum / float(FMath::Max(Samples, 1));
				if (bHasObservedFloor)
				{
					OutSupportWorld.Z = ObservedFloorZ;
				}
				else
				{
					float FloorZ = AnkleWorld.Z;
					if (bGotHeel)
					{
						FloorZ = FMath::Min(FloorZ, HeelWorld.Z);
					}
					if (bGotToe)
					{
						FloorZ = FMath::Min(FloorZ, ToeWorld.Z);
					}
					OutSupportWorld.Z = FloorZ;
				}
				return true;
			};

			// Floor state must come from the SAME store the leg solve maintains: with the keyed
			// foot-contact state active (live trial + parity), the node-state copies read here
			// were never written - the pelvis stabilizer anchored to a stale/empty floor and the
			// root wandered (take-3 clean baseline 2026-07-05: fore-aft pelvis wander 5.1cm vs
			// the referee's 1.8; lateral had already recovered via the windowed floor).
			const bool bBodyUseKeyedFootFloor =
				CVarMediaPipeFootContactKeyedState.GetValueOnAnyThread() != 0 && RuntimeStateKey != 0;
			FMediaPipeFootContactRuntimeState& BodyFootContactState =
				GetFootContactRuntimeState(RuntimeStateKey);
			const bool bLeftFloorKnown = bBodyUseKeyedFootFloor
				? BodyFootContactState.Left.bHasObservedSourceFloor
				: LeftLegState.bHasObservedSourceFloor;
			const float LeftFloorZ = bBodyUseKeyedFootFloor
				? BodyFootContactState.Left.ObservedSourceFloorZ
				: LeftLegState.ObservedSourceFloorZ;
			const bool bRightFloorKnown = bBodyUseKeyedFootFloor
				? BodyFootContactState.Right.bHasObservedSourceFloor
				: RightLegState.bHasObservedSourceFloor;
			const float RightFloorZ = bBodyUseKeyedFootFloor
				? BodyFootContactState.Right.ObservedSourceFloorZ
				: RightLegState.ObservedSourceFloorZ;
			ConsiderFootFloor(bGotLeft, bGotLeftToe, LAnkleWorld, LToeWorld, bLeftFloorKnown, LeftFloorZ);
			ConsiderFootFloor(bGotRight, bGotRightToe, RAnkleWorld, RToeWorld, bRightFloorKnown, RightFloorZ);
			if (bHasStableFootFloor)
			{
				const float CurrentHipHeightCm = HipWorld.Z - StableFootFloorZ;
				float GeometryStandingHipHeightCm = 0.0f;
				int32 GeometryStandingSamples = 0;

				auto AccumulateStandingEstimate = [&](bool bGotSide, bool bGotKnee, const FVector& Hip, const FVector& Knee, const FVector& Ankle)
				{
					if (!bGotSide || !bGotKnee)
					{
						return;
					}

					const float ThighLenCm = (Knee - Hip).Size();
					const float CalfLenCm = (Ankle - Knee).Size();
					const float AnkleClearanceCm = FMath::Max(Ankle.Z - StableFootFloorZ, 0.0f);
					const float StandingEstimateCm = ThighLenCm + CalfLenCm + AnkleClearanceCm;
					if (StandingEstimateCm > KINDA_SMALL_NUMBER)
					{
						GeometryStandingHipHeightCm += StandingEstimateCm;
						++GeometryStandingSamples;
					}
				};

				AccumulateStandingEstimate(bGotLeft, bGotLeftKnee, LHipWorld, LKneeWorld, LAnkleWorld);
				AccumulateStandingEstimate(bGotRight, bGotRightKnee, RHipWorld, RKneeWorld, RAnkleWorld);
				if (GeometryStandingSamples > 0)
				{
					GeometryStandingHipHeightCm /= float(GeometryStandingSamples);
				}

				if (!BodyState.bHasReferenceHipHeight)
				{
					BodyState.ReferenceHipHeightCm = FMath::Max(CurrentHipHeightCm, GeometryStandingHipHeightCm);
					BodyState.bHasReferenceHipHeight = true;
				}

				// Conservative direct-drive root motion:
				// keep squat/compression (downward motion), but do not synthesize upward lift from monocular landmarks.
				// Normalize the source compression to the target rig's reference hip height so a shorter/taller MediaPipe
				// skeleton still produces the same relative squat depth on the target rig.
				BodyState.ReferenceHipHeightCm = FMath::Max(BodyState.ReferenceHipHeightCm, CurrentHipHeightCm);
				const float StandingSourceHipHeightCm = FMath::Max(BodyState.ReferenceHipHeightCm, GeometryStandingHipHeightCm);
				float DeltaHeightCm = FMath::Min(CurrentHipHeightCm - StandingSourceHipHeightCm, 0.0f);
				if (BodyState.ReferenceRigHipHeightCm > KINDA_SMALL_NUMBER && StandingSourceHipHeightCm > KINDA_SMALL_NUMBER)
				{
					const float CompressionAlpha = FMath::Clamp(CurrentHipHeightCm / StandingSourceHipHeightCm, 0.0f, 1.0f);

					// Fuse monocular squat/stand intent with the Quest/HMD metric height scaffold.
					MediaPipeBodySolverMath::FMediaPipeFusedPelvisCompressionInput FusionInput;
					FusionInput.bHasMonoAlpha = true;
					FusionInput.MonoAlpha01 = CompressionAlpha;
					FusionInput.bHasHmdAlpha = HmdScaffoldResult.bValid;
					FusionInput.HmdAlpha01 = HmdScaffoldResult.CompressionAlpha01;
					FusionInput.HmdConfidence01 = HmdScaffoldResult.Confidence;
					FusionInput.HmdWeight01 = ScaffoldHmdWeight;
					const MediaPipeBodySolverMath::FMediaPipeFusedPelvisCompressionResult Fusion =
						MediaPipeBodySolverMath::ComputeFusedPelvisCompression(FusionInput);

					const float TargetRigHipHeightCm = BodyState.ReferenceRigHipHeightCm * Fusion.FusedAlpha01;
					DeltaHeightCm = TargetRigHipHeightCm - BodyState.ReferenceRigHipHeightCm;

					BodyState.ScaffoldMonoAlpha01 = CompressionAlpha;
					BodyState.ScaffoldFusedAlpha01 = Fusion.FusedAlpha01;
					BodyState.ScaffoldHmdShare01 = Fusion.HmdShare01;
					BodyState.ScaffoldPelvisDropCm = BodyState.ReferenceRigHipHeightCm * (1.0f - Fusion.FusedAlpha01);
					BodyState.bHasLowerBodyScaffoldSample = true;
				}

				FVector CompUp = TargetCompTransform.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
				if (CompUp.IsNearlyZero())
				{
					CompUp = FVector::UpVector;
				}
				FVector TargetPelvisOffset = CompUp * DeltaHeightCm;

				// Lateral hip sway, metric: the Quest body-tracking hips joint POSITION against
				// its neutral (latched after the donning gate opens) translates the pelvis when
				// the wearer shifts their hips, which the camera's hip-vs-support estimate only
				// approximates. Live worn-headset sessions only - replay and fused evaluation
				// keep their existing pelvis ownership. While fresh it replaces the camera
				// planar term below (same physical quantity); on dropout the sway eases home.
				BodyState.bBodyTrackingSwayActive = false;
				const bool bLiveSwayEligible =
					BodyState.bLiveNeutralGateArmed &&
					BodyState.bLiveNeutralsReady &&
					(!FMediaPipeTrackingFusionDatasetReplayRuntime::Get().IsActive() ||
						FMediaPipeTrackingFusionDatasetReplayRuntime::IsLiveParityEnabled()) &&
					!ShouldUseBodyFusionPoseForEvaluation() &&
					BodyState.ReferenceRigHipHeightCm > KINDA_SMALL_NUMBER &&
					PelvisPlanarMaxOffsetRatio > KINDA_SMALL_NUMBER;
				if (bLiveSwayEligible)
				{
					const double NowSeconds = FPlatformTime::Seconds();
					const double ChainAgeSeconds = FullArmChain.TimestampSeconds >= 0.0
						? NowSeconds - FullArmChain.TimestampSeconds
						: -1.0;
					const bool bHipsPosFresh =
						(FullArmChain.Source == EMediaPipeFullArmChainSource::OpenXRBodyTracking ||
							FullArmChain.Source == EMediaPipeFullArmChainSource::TrackingFusionReplay) &&
						FullArmChain.bActive != 0 &&
						FullArmChain.Hips.bPositionValid != 0 &&
						ChainAgeSeconds >= 0.0 &&
						ChainAgeSeconds <= 0.3;
					if (bHipsPosFresh)
					{
						const FVector HipsPosWorld = FullArmChain.Hips.WorldTransform.GetLocation();
						const bool bGap =
							BodyState.LastBodyTrackingHipsPosSampleSeconds > 0.0 &&
							NowSeconds - BodyState.LastBodyTrackingHipsPosSampleSeconds > 1.0;
						if (!BodyState.bHasBodyTrackingHipsNeutralPos || bGap)
						{
							// (Re-)latch so the currently applied sway is preserved: tracking
							// gaps and re-acquisitions never step the pelvis.
							BodyState.BodyTrackingHipsNeutralPosWorld =
								HipsPosWorld - FVector(BodyState.BodyTrackingSwayWorldXY, 0.0f);
							BodyState.bHasBodyTrackingHipsNeutralPos = true;
						}
						BodyState.LastBodyTrackingHipsPosSampleSeconds = NowSeconds;

						const float MaxSwayCm = BodyState.ReferenceRigHipHeightCm * PelvisPlanarMaxOffsetRatio;
						FVector2D SwayXY(
							HipsPosWorld.X - BodyState.BodyTrackingHipsNeutralPosWorld.X,
							HipsPosWorld.Y - BodyState.BodyTrackingHipsNeutralPosWorld.Y);
						const float SwayCm = SwayXY.Size();
						if (SwayCm > MaxSwayCm)
						{
							SwayXY *= MaxSwayCm / SwayCm;
						}
						BodyState.BodyTrackingSwayWorldXY = SwayXY;
						BodyState.bBodyTrackingSwayActive = true;
					}
					else if (!BodyState.BodyTrackingSwayWorldXY.IsNearlyZero() && DeltaSeconds > 0.0f)
					{
						// Tracking lost: walk the held sway home instead of snapping or freezing.
						const float HomeAlpha = 1.0f - FMath::Pow(0.5f, DeltaSeconds / 1.5f);
						BodyState.BodyTrackingSwayWorldXY =
							FMath::Lerp(BodyState.BodyTrackingSwayWorldXY, FVector2D::ZeroVector, HomeAlpha);
					}
					if (!BodyState.BodyTrackingSwayWorldXY.IsNearlyZero())
					{
						const FVector SwayComp = TargetCompTransform.InverseTransformVectorNoScale(
							FVector(BodyState.BodyTrackingSwayWorldXY, 0.0f));
						TargetPelvisOffset += SwayComp - FVector::DotProduct(SwayComp, CompUp) * CompUp;
					}
				}

				const float PlanarWeight = BodyState.bBodyTrackingSwayActive
					? 0.0f
					: FMath::Clamp(PelvisPlanarTranslationWeight, 0.0f, 1.0f);
				if (PlanarWeight > KINDA_SMALL_NUMBER && BodyState.ReferenceRigHipHeightCm > KINDA_SMALL_NUMBER && PelvisPlanarMaxOffsetRatio > KINDA_SMALL_NUMBER)
				{
					FVector CompForward = TargetCompTransform.InverseTransformVectorNoScale(FVector::ForwardVector).GetSafeNormal();
					CompForward = (CompForward - FVector::DotProduct(CompForward, CompUp) * CompUp).GetSafeNormal();
					if (CompForward.IsNearlyZero())
					{
						CompForward = FVector::ForwardVector;
					}

					FVector CompRight = TargetCompTransform.InverseTransformVectorNoScale(FVector::RightVector).GetSafeNormal();
					CompRight = (CompRight - FVector::DotProduct(CompRight, CompUp) * CompUp).GetSafeNormal();
					if (CompRight.IsNearlyZero())
					{
						CompRight = FVector::CrossProduct(CompUp, CompForward).GetSafeNormal();
					}
					if (!CompRight.IsNearlyZero())
					{
						CompForward = FVector::CrossProduct(CompRight, CompUp).GetSafeNormal();
					}

					FVector SourceHipRightWorld = FVector::RightVector;
					FVector SourceShoulderRightWorld = FVector::RightVector;
					FVector SourceUpWorld = FVector::UpVector;
					FVector SourceForwardWorld = FVector::ForwardVector;
					const bool bHasTorsoBasis = TryGetTorsoBasisWorld(SourceHipRightWorld, SourceShoulderRightWorld, SourceUpWorld, SourceForwardWorld);

					FVector LSupportWorld = FVector::ZeroVector;
					FVector RSupportWorld = FVector::ZeroVector;
					// Same keyed-floor routing as above: node-state floor is stale when the keyed
					// store is active (the pelvis-wander source, 2026-07-05).
					const bool bGotLeftSupport = TryGetSupportPointWorld(bGotLeft, bGotLeftHeel, bGotLeftToe, LAnkleWorld, LHeelWorld, LToeWorld, bLeftFloorKnown, LeftFloorZ, LSupportWorld);
					const bool bGotRightSupport = TryGetSupportPointWorld(bGotRight, bGotRightHeel, bGotRightToe, RAnkleWorld, RHeelWorld, RToeWorld, bRightFloorKnown, RightFloorZ, RSupportWorld);

					if (bHasTorsoBasis && (bGotLeftSupport || bGotRightSupport))
					{
						FVector SupportCenterWorld = FVector::ZeroVector;
						int32 SupportSamples = 0;
						auto AccumulateSupportCenter = [&](bool bHasSupport, const FVector& SupportWorld)
						{
							if (!bHasSupport)
							{
								return;
							}
							SupportCenterWorld += SupportWorld;
							++SupportSamples;
						};
						AccumulateSupportCenter(bGotLeftSupport, LSupportWorld);
						AccumulateSupportCenter(bGotRightSupport, RSupportWorld);
						SupportCenterWorld /= float(FMath::Max(SupportSamples, 1));

						FVector RefSupportCenterComp = FVector::ZeroVector;
						int32 RefSupportSamples = 0;
						auto AccumulateRefSupportCenter = [&](bool bHasFootContact, const FVector& RefAnklePosComp, const FVector& RefBallPosComp)
						{
							if (!bHasFootContact)
							{
								return;
							}

							RefSupportCenterComp += (RefAnklePosComp + RefBallPosComp) * 0.5f;
							++RefSupportSamples;
						};
						AccumulateRefSupportCenter(bHasRefFootContactL, RefAnklePosCompL, RefBallPosCompL);
						AccumulateRefSupportCenter(bHasRefFootContactR, RefAnklePosCompR, RefBallPosCompR);
						if (RefSupportSamples > 0)
						{
							RefSupportCenterComp /= float(RefSupportSamples);

							FMediaPipePelvisPlanarOffsetInput PlanarOffsetInput;
							PlanarOffsetInput.SourceSupportToHipWorld = HipWorld - SupportCenterWorld;
							PlanarOffsetInput.SourceUpWorld = SourceUpWorld;
							PlanarOffsetInput.SourceHipRightWorld = SourceHipRightWorld;
							PlanarOffsetInput.SourceForwardWorld = SourceForwardWorld;
							PlanarOffsetInput.StandingSourceHipHeightCm = StandingSourceHipHeightCm;
							PlanarOffsetInput.ReferenceRigHipHeightCm = BodyState.ReferenceRigHipHeightCm;
							PlanarOffsetInput.PelvisPlanarMaxOffsetRatio = PelvisPlanarMaxOffsetRatio;
							PlanarOffsetInput.RefPelvisTranslationComp = RefPelvisTranslationComp;
							PlanarOffsetInput.RefSupportCenterComp = RefSupportCenterComp;
							PlanarOffsetInput.CompUp = CompUp;
							PlanarOffsetInput.CompRight = CompRight;
							PlanarOffsetInput.CompForward = CompForward;
							TargetPelvisOffset += ComputePelvisPlanarOffset(PlanarOffsetInput) * PlanarWeight;
						}
					}
				}

				// HMD pelvis anchor (2026-07-06): the camera's planar hip estimate drifts
				// (take-4 referee: +5cm lateral at settle growing to +7cm over 200s vs the
				// Epic solve's 0.8cm), and the closed-loop HMD lean then tilts the torso to
				// keep the head under the drift-free HMD - the wearer sees a leaning avatar.
				// Quest SLAM owns the low-frequency planar anchor: latch the pelvis<->HMD
				// planar offset once at live-neutral settle, then slow-correct the pelvis
				// target toward (HMD + offset). High-frequency camera motion passes through;
				// adaptation freezes while the wearer is not upright so genuine bends are
				// never corrected away.
				if (CVarMediaPipePelvisHmdAnchor.GetValueOnAnyThread() != 0 && bHasCachedQuestHmdPose)
				{
					const FVector PelvisWorldRaw =
						TargetCompTransform.TransformPosition(RefPelvisTranslationComp + TargetPelvisOffset);
					const FVector2D PelvisXY(PelvisWorldRaw.X, PelvisWorldRaw.Y);
					const FVector2D HmdXY(CachedQuestHmdWorld.X, CachedQuestHmdWorld.Y);
					const float UprightMaxCm =
						FMath::Max(CVarMediaPipePelvisHmdAnchorUprightMaxCm.GetValueOnAnyThread(), 1.0f);
					const bool bUpright = (HmdXY - PelvisXY).Size() < UprightMaxCm;
					if (!BodyState.bHasPelvisHmdAnchor)
					{
						if (BodyState.bLiveNeutralsReady && bUpright)
						{
							BodyState.PelvisHmdAnchorOffsetXY = PelvisXY - HmdXY;
							BodyState.PelvisHmdAnchorCorrectionXY = FVector2D::ZeroVector;
							BodyState.bHasPelvisHmdAnchor = true;
						}
					}
					else
					{
						if (bUpright && DeltaSeconds > 0.0f)
						{
							const FVector2D DriftErrXY =
								(HmdXY + BodyState.PelvisHmdAnchorOffsetXY) - PelvisXY;
							const float AnchorAlpha = HalfLifeToAlpha(
								FMath::Max(CVarMediaPipePelvisHmdAnchorHalfLifeSeconds.GetValueOnAnyThread(), 0.1f),
								DeltaSeconds);
							BodyState.PelvisHmdAnchorCorrectionXY =
								FMath::Lerp(BodyState.PelvisHmdAnchorCorrectionXY, DriftErrXY, AnchorAlpha);
							const float MaxCorrectionCm =
								FMath::Max(CVarMediaPipePelvisHmdAnchorMaxCm.GetValueOnAnyThread(), 0.0f);
							const float CorrectionSize = BodyState.PelvisHmdAnchorCorrectionXY.Size();
							if (CorrectionSize > MaxCorrectionCm && CorrectionSize > KINDA_SMALL_NUMBER)
							{
								BodyState.PelvisHmdAnchorCorrectionXY *= MaxCorrectionCm / CorrectionSize;
							}
						}
						const FVector CorrectionComp = TargetCompTransform.InverseTransformVectorNoScale(
							FVector(BodyState.PelvisHmdAnchorCorrectionXY, 0.0f));
						TargetPelvisOffset += CorrectionComp - FVector::DotProduct(CorrectionComp, CompUp) * CompUp;
					}
				}

				const float PelvisAlpha = HalfLifeToAlpha(PelvisTranslationHalfLifeSeconds, DeltaSeconds);
				if (!BodyState.bHasSmoothedPelvisOffset)
				{
					BodyState.SmoothedPelvisOffsetComp = TargetPelvisOffset;
					BodyState.bHasSmoothedPelvisOffset = true;
				}
				else
				{
					BodyState.SmoothedPelvisOffsetComp = FMath::Lerp(BodyState.SmoothedPelvisOffsetComp, TargetPelvisOffset, PelvisAlpha);
				}
			}
		}

	}

	const FCompactPoseBoneIndex PelvisIdx = Pelvis.CachedCompactPoseIndex;
	FTransform PelvisCS = CSPose.GetComponentSpaceTransform(PelvisIdx);
	PelvisCS.SetTranslation(RefPelvisTranslationComp + BodyState.SmoothedPelvisOffsetComp);
	const FBoneTransform BoneTransform(PelvisIdx, PelvisCS);
	CSPose.SafeSetCSBoneTransforms(MakeArrayView(&BoneTransform, 1));
}

void FAnimNode_MediaPipePoseDriven::UpdateFkRootGroundingCS(FCSPose<FCompactPose>& CSPose, float DeltaSeconds)
{
	if (!bDriveLegs || !bUseFkRootGrounding || bUseLegIK || !bHasRefFootFloorZ)
	{
		if (BodyState.bHasSmoothedFkRootGroundOffset)
		{
			const float Alpha = HalfLifeToAlpha(FkRootGroundingHalfLifeSeconds, DeltaSeconds);
			BodyState.SmoothedFkRootGroundOffsetComp = FMath::Lerp(BodyState.SmoothedFkRootGroundOffsetComp, FVector::ZeroVector, Alpha);
		}
		return;
	}

	float TargetOffsetZ = 0.0f;
	bool bHasGroundedFoot = false;
	float LowestBallDeltaZ = TNumericLimits<float>::Max();
	bool bHasBallDelta = false;

	auto ConsiderFoot = [&](bool bSourceGrounded, bool bSourceNearFloor, const FBoneReference& BallBone)
	{
		if (!BallBone.IsValidToEvaluate())
		{
			return;
		}

		const float CurrentBallZ = CSPose.GetComponentSpaceTransform(BallBone.CachedCompactPoseIndex).GetTranslation().Z;
		const float FloorDeltaCm = CurrentBallZ - RefFootFloorZComp;
		// Track the lowest foot independent of hover eligibility so the smoother's penetration
		// guard sees every foot, including ones that are descending too fast to count as grounded.
		LowestBallDeltaZ = bHasBallDelta ? FMath::Min(LowestBallDeltaZ, FloorDeltaCm) : FloorDeltaCm;
		bHasBallDelta = true;
		if (FMath::Abs(FloorDeltaCm) <= KINDA_SMALL_NUMBER ||
			FMath::Abs(FloorDeltaCm) > FkRootGroundingMaxCorrectionCm)
		{
			return;
		}

		const float CandidateOffsetZ = -FloorDeltaCm;
		const bool bCandidateFixesPenetration = CandidateOffsetZ > 0.0f;
		// Downward (hover) correction needs source evidence the foot is at or near its observed
		// floor. Requiring the full velocity-gated grounded state left the avatar hovering through
		// entire stepping/squat blocks: recorded soft knees shorten the leg chain while the pelvis
		// sits at reference height, and feet rarely settle long enough to count as grounded.
		if (!bSourceGrounded && !bSourceNearFloor && !bCandidateFixesPenetration)
		{
			return;
		}

		const bool bCurrentFixesPenetration = TargetOffsetZ > 0.0f;
		if (!bHasGroundedFoot ||
			(bCandidateFixesPenetration && (!bCurrentFixesPenetration || CandidateOffsetZ > TargetOffsetZ)) ||
			(!bCandidateFixesPenetration && !bCurrentFixesPenetration && CandidateOffsetZ > TargetOffsetZ))
		{
			// For hover, ground the LOWEST eligible foot (the least-negative correction) so a
			// lifted foot stays lifted and the planted foot is not pushed through the floor.
			TargetOffsetZ = CandidateOffsetZ;
			bHasGroundedFoot = true;
		}
	};

	ConsiderFoot(LeftLegState.bCurrentSourceFootGrounded, LeftLegState.bCurrentSourceFootNearFloor, BallL);
	ConsiderFoot(RightLegState.bCurrentSourceFootGrounded, RightLegState.bCurrentSourceFootNearFloor, BallR);

	MediaPipeBodySolverMath::FMediaPipeFkRootGroundingSmoothInput SmoothInput;
	SmoothInput.bHasSmoothedOffset = BodyState.bHasSmoothedFkRootGroundOffset;
	SmoothInput.SmoothedOffsetZ = BodyState.SmoothedFkRootGroundOffsetComp.Z;
	SmoothInput.TargetOffsetZ = bHasGroundedFoot ? TargetOffsetZ : 0.0f;
	SmoothInput.Alpha = HalfLifeToAlpha(FkRootGroundingHalfLifeSeconds, DeltaSeconds);
	SmoothInput.LowestBallDeltaZ = bHasBallDelta ? LowestBallDeltaZ : FkRootGroundingMaxCorrectionCm;
	SmoothInput.MaxCorrectionCm = FkRootGroundingMaxCorrectionCm;
	BodyState.SmoothedFkRootGroundOffsetComp =
		FVector(0.0f, 0.0f, MediaPipeBodySolverMath::SmoothFkRootGroundingOffsetZ(SmoothInput));
	BodyState.bHasSmoothedFkRootGroundOffset = true;
}

void FAnimNode_MediaPipePoseDriven::UpdateLiveNeutralGate(float DeltaSeconds)
{
	// Donning gate for the worn-headset drives. VR Preview is pressed bent over a desk holding
	// the headset, so the first seconds of HMD data describe the donning posture, not the
	// wearer's neutral - and several references (body-yaw zero, lean neutral, hips heading)
	// anchor to their first samples. Nothing may latch until the worn headset has been upright
	// and still for a moment; at that instant every live neutral re-zeros to the settled
	// standing pose. One-way per session so later bends and dropouts never re-gate.
	if (BodyState.bLiveNeutralsReady)
	{
		return;
	}
	if ((FMediaPipeTrackingFusionDatasetReplayRuntime::Get().IsActive() &&
			!FMediaPipeTrackingFusionDatasetReplayRuntime::IsLiveParityEnabled()) ||
		ShouldUseBodyFusionPoseForEvaluation())
	{
		// Byte-stable replay evaluation and fused-pose evaluation carry verified observations
		// from frame one. LIVE-PARITY replays fall through to the real donning gate instead:
		// frame-one latching anchored "forward" to the recording's unsettled first pose and
		// left the whole avatar rotated +36 deg vs the offline referee for the entire take
		// (take-3 forensics 2026-07-05). The recording carries the same HMD signals the gate
		// needs, so parity settles and re-zeros exactly like the live session it mirrors.
		BodyState.bLiveNeutralsReady = true;
		return;
	}
	if (!bHasCachedQuestHmdPose)
	{
		// No HMD this frame. The gate only ever arms when one appears, so camera-only desktop
		// sessions run ungated.
		return;
	}
	BodyState.bLiveNeutralGateArmed = true;

	const FRotator HmdRotation = CachedQuestHmdRotWorld.Rotator();
	bool bSettled = false;
	if (BodyState.bHasLiveNeutralHmdSample && DeltaSeconds > 0.0f)
	{
		const float PlanarSpeedCmSec =
			FVector2D(CachedQuestHmdWorld.X - BodyState.LastLiveNeutralHmdPos.X,
				CachedQuestHmdWorld.Y - BodyState.LastLiveNeutralHmdPos.Y).Size() / DeltaSeconds;
		const float YawRateDegSec =
			FMath::Abs(FMath::FindDeltaAngleDegrees(BodyState.LastLiveNeutralHmdYawDeg, HmdRotation.Yaw)) / DeltaSeconds;
		bSettled =
			bCachedQuestHmdWorn &&
			CachedQuestHmdWorld.Z > 120.0f &&
			FMath::Abs(HmdRotation.Pitch) < 35.0f &&
			PlanarSpeedCmSec < 40.0f &&
			YawRateDegSec < 90.0f;
	}
	BodyState.bHasLiveNeutralHmdSample = true;
	BodyState.LastLiveNeutralHmdPos = CachedQuestHmdWorld;
	BodyState.LastLiveNeutralHmdYawDeg = HmdRotation.Yaw;

	BodyState.LiveNeutralSettleSeconds = bSettled ? BodyState.LiveNeutralSettleSeconds + DeltaSeconds : 0.0f;
	if (BodyState.LiveNeutralSettleSeconds < 1.0f)
	{
		return;
	}

	// Settled: this standing pose is the wearer's neutral. Re-zero every live reference.
	BodyState.bLiveNeutralsReady = true;
	BodyState.HmdHeightScaffold.Reset();
	BodyState.HmdHeadYawNeutral.Reset();
	BodyState.HipTwistNeutral.Reset();
	BodyState.HipYawEstimator.Reset();
	BodyState.bHasHmdLeanNeutral = false;
	BodyState.HmdLeanNeutralXY = FVector2D::ZeroVector;
	BodyState.bHasInitialBodyYawNeutral = false;
	BodyState.InitialBodyYawNeutralDeg = 0.0f;
	BodyState.bBodyTrackingYawLatched = false;
	BodyState.bHasBodyTrackingCandidate = false;
	BodyState.BodyTrackingHipsStableSeconds = 0.0f;
	BodyState.LastBodyTrackingYawDeg = 0.0f;
	BodyState.bHasSmoothedBodyYaw = false;
	BodyState.SmoothedBodyYawDeg = 0.0f;
	BodyState.PrevBodyYawSource = 0;
}

void FAnimNode_MediaPipePoseDriven::DriveLivePelvisLeanTwistCS(FCSPose<FCompactPose>& CSPose, float DeltaSeconds)
{
	// Live pelvis lean + hip twist for worn-headset trials, used when spine retargeting is off
	// (the proven live profile) so the body still follows the wearer's torso:
	// - Lean pitch/roll comes from the HMD's planar displacement against a self-calibrating
	//   neutral - leaning back moves the head backward metrically and the body follows, so the
	//   first-person camera stays over the avatar's chest instead of drifting off it.
	// - Twist yaw comes from the MediaPipe hip-line direction against its own neutral.
	// Dataset replay, fused-pose evaluation, and spine-driven profiles keep pelvis ownership.
	const bool bDriveLean = CVarMediaPipeDriveHmdLean.GetValueOnAnyThread() != 0 && bHasCachedQuestHmdPose;
	const bool bDriveTwist = CVarMediaPipeDriveHipTwist.GetValueOnAnyThread() != 0 && bHasPoseFrame;
	if ((!bDriveLean && !bDriveTwist) ||
		bDriveSpine ||
		!bHasReferencePose ||
		!Pelvis.IsValidToEvaluate())
	{
		return;
	}
	// Parity-aware replay gate (2026-07-04): live-parity scoring replays run the live
	// neutral-settling/lean/twist/head paths (user-observed: hips never turned in parity
	// replays; MHA hip-yaw RMSE 95-170 deg in turn windows). The byte-stable evaluation
	// policy still suppresses them.
	if ((FMediaPipeTrackingFusionDatasetReplayRuntime::Get().IsActive() &&
			!FMediaPipeTrackingFusionDatasetReplayRuntime::IsLiveParityEnabled()) ||
		ShouldUseBodyFusionPoseForEvaluation())
	{
		return;
	}
	if (BodyState.bLiveNeutralGateArmed && !BodyState.bLiveNeutralsReady)
	{
		// Still donning the headset: nothing observed yet is a trustworthy neutral.
		return;
	}

	const float NeutralHalfLife = FMath::Max(CVarMediaPipeLivePoseNeutralHalfLife.GetValueOnAnyThread(), 0.05f);
	const bool bMirror = CVarMediaPipeLivePoseMirror.GetValueOnAnyThread() != 0;

	// Body yaw is resolved FIRST: the closed-loop lean below is solved around the twisted
	// torso, so a turned pelvis can never rotate the chest out from under the HMD (which is
	// what put the camera back inside the body when twist was composed after the lean).
	float TargetYawDeg = 0.0f;
	bool bBodyTrackingHipsUsed = false;
	if (bDriveTwist || bDriveLean)
	{
		// Primary pelvis yaw: the Quest runtime's own hips joint. Quest 3 inside-out body
		// tracking observes the torso directly, so isolated hip twists register even when the
		// head and the phone camera see nothing. The yaw is the horizontal heading drift of the
		// joint's most-horizontal local axis (convention-free, and immune to the pitch/roll of
		// bends); the neutral heading latches only after the heading has been stable so
		// headset-donning noise never becomes the zero point.
		const double NowSeconds = FPlatformTime::Seconds();
		const double FullArmChainAgeSeconds = FullArmChain.TimestampSeconds >= 0.0
			? NowSeconds - FullArmChain.TimestampSeconds
			: -1.0;
		// TrackingFusionReplay carries the RECORDED OpenXR hips (schema v3), so replayed
		// takes exercise the same yaw path the live wearer gets.
		const bool bBodyTrackingHipsFresh =
			(FullArmChain.Source == EMediaPipeFullArmChainSource::OpenXRBodyTracking ||
				FullArmChain.Source == EMediaPipeFullArmChainSource::TrackingFusionReplay) &&
			FullArmChain.bActive != 0 &&
			FullArmChain.Hips.bOrientationValid != 0 &&
			FullArmChainAgeSeconds >= 0.0 &&
			FullArmChainAgeSeconds <= 0.3;
		if (bBodyTrackingHipsFresh)
		{
			const FQuat HipsRotWorld = FullArmChain.Hips.WorldTransform.GetRotation();
			if (BodyState.LastBodyTrackingHipsSampleSeconds > 0.0 &&
				NowSeconds - BodyState.LastBodyTrackingHipsSampleSeconds > 1.0)
			{
				// Tracking gap: the runtime may have re-initialized the joint frame, so the
				// neutral has to be re-latched (relative to the applied yaw, so no step).
				BodyState.bBodyTrackingYawLatched = false;
				BodyState.bHasBodyTrackingCandidate = false;
				// Accumulation baseline must re-seed after the gap or the first post-gap
				// sample would register the whole gap as one giant yaw step.
				BodyState.bHasLastBodyTrackingHeading = false;
			}
			BodyState.LastBodyTrackingHipsSampleSeconds = NowSeconds;

			if (!BodyState.bBodyTrackingYawLatched)
			{
				const int32 AxisIndex = MediaPipeBodySolverMath::SelectMostHorizontalAxis(HipsRotWorld);
				float HeadingDeg = 0.0f;
				if (AxisIndex != INDEX_NONE &&
					MediaPipeBodySolverMath::TryGetAxisHeadingDeg(HipsRotWorld, AxisIndex, HeadingDeg))
				{
					const bool bCandidateHolds =
						BodyState.bHasBodyTrackingCandidate &&
						AxisIndex == BodyState.BodyTrackingHipsAxisIndex &&
						FMath::Abs(FMath::FindDeltaAngleDegrees(
							BodyState.BodyTrackingHipsCandidateHeadingDeg, HeadingDeg)) <= 8.0f;
					if (!bCandidateHolds)
					{
						BodyState.bHasBodyTrackingCandidate = true;
						BodyState.BodyTrackingHipsAxisIndex = AxisIndex;
						BodyState.BodyTrackingHipsCandidateHeadingDeg = HeadingDeg;
						BodyState.BodyTrackingHipsStableSeconds = 0.0f;
					}
					else
					{
						BodyState.BodyTrackingHipsStableSeconds += DeltaSeconds;
						if (BodyState.BodyTrackingHipsStableSeconds >= 0.75f)
						{
							BodyState.BodyTrackingHipsNeutralHeadingDeg = FRotator::NormalizeAxis(
								HeadingDeg -
								(BodyState.bHasSmoothedBodyYaw ? BodyState.SmoothedBodyYawDeg : 0.0f));
							BodyState.bBodyTrackingYawLatched = true;
							// Seed the accumulated yaw at the currently applied value so the
							// handover from the HMD-fallback source (or a re-latch) never steps.
							BodyState.LastBodyTrackingYawDeg =
								BodyState.bHasSmoothedBodyYaw ? BodyState.SmoothedBodyYawDeg : 0.0f;
							BodyState.bHasLastBodyTrackingHeading = false;
						}
					}
				}
			}
			if (BodyState.bBodyTrackingYawLatched)
			{
				float HeadingDeg = 0.0f;
				if (MediaPipeBodySolverMath::TryGetAxisHeadingDeg(
						HipsRotWorld, BodyState.BodyTrackingHipsAxisIndex, HeadingDeg))
				{
					// Delta accumulation (2026-07-04 full-turn fix): the historical form
					// clamped the ABSOLUTE offset from neutral to +/-100 and wrapped at
					// +/-180, so a full turn could never be represented - MHA take 2 showed
					// Epic's solve following complete turns while the avatar stopped at the
					// clamp (hip-yaw RMSE 17-27 deg in the turn windows). Accumulating
					// per-sample deltas keeps counting through the wrap; the range CVar
					// (default 100 = historical envelope) bounds the result, and the live
					// trial layer raises it for multi-turn freedom.
					const float BodyYawMaxDeg =
						FMath::Max(CVarMediaPipeBodyYawMaxDeg.GetValueOnAnyThread(), 10.0f);
					if (!BodyState.bHasLastBodyTrackingHeading)
					{
						BodyState.LastBodyTrackingHeadingDeg = HeadingDeg;
						BodyState.bHasLastBodyTrackingHeading = true;
					}
					const float StepDeg = FMath::FindDeltaAngleDegrees(
						BodyState.LastBodyTrackingHeadingDeg, HeadingDeg);
					BodyState.LastBodyTrackingHeadingDeg = HeadingDeg;
					BodyState.LastBodyTrackingYawDeg = FMath::Clamp(
						BodyState.LastBodyTrackingYawDeg + StepDeg,
						-BodyYawMaxDeg, BodyYawMaxDeg);

					// Slow drift recenter (same intent as before): sustained near-zero yaw
					// means the wearer is facing forward, so the accumulated yaw drains
					// toward zero (max 0.5 deg/s - imperceptible) to absorb IOBT
					// tracking-space drift. Held twists never qualify.
					if (FMath::Abs(BodyState.LastBodyTrackingYawDeg) < 10.0f)
					{
						BodyState.BodyYawRecenterStillSeconds += DeltaSeconds;
						if (BodyState.BodyYawRecenterStillSeconds > 5.0f)
						{
							BodyState.LastBodyTrackingYawDeg = MediaPipeBodySolverMath::ApproachAngleDeg(
								BodyState.LastBodyTrackingYawDeg,
								0.0f,
								DeltaSeconds,
								20.0f,
								0.5f);
						}
					}
					else
					{
						BodyState.BodyYawRecenterStillSeconds = 0.0f;
					}
				}
				// else: a deep bend turned the latched axis vertical - hold the last yaw.
				TargetYawDeg = BodyState.LastBodyTrackingYawDeg;
				bBodyTrackingHipsUsed = true;
			}
		}
	}
	if (!bBodyTrackingHipsUsed && bDriveLean)
	{
		// Fallback body yaw: the slow neutral component of the HMD yaw is the wearer's facing;
		// its drift from the session-start value turns the pelvis while the fast remainder
		// stays head glance (the HMD head drive).
		if (BodyState.HmdHeadYawNeutral.bHasNeutral)
		{
			if (!BodyState.bHasInitialBodyYawNeutral)
			{
				BodyState.InitialBodyYawNeutralDeg = BodyState.HmdHeadYawNeutral.NeutralYawDeg;
				BodyState.bHasInitialBodyYawNeutral = true;
			}
			else if (BodyState.PrevBodyYawSource == 2)
			{
				// Body tracking just dropped out: re-zero so this source's first target equals
				// the currently applied yaw instead of snapping to its own reference.
				BodyState.InitialBodyYawNeutralDeg = FRotator::NormalizeAxis(
					BodyState.HmdHeadYawNeutral.NeutralYawDeg -
					(BodyState.bHasSmoothedBodyYaw ? BodyState.SmoothedBodyYawDeg : 0.0f));
			}
			TargetYawDeg = FMath::Clamp(
				FMath::FindDeltaAngleDegrees(BodyState.InitialBodyYawNeutralDeg, BodyState.HmdHeadYawNeutral.NeutralYawDeg),
				-90.0f, 90.0f);
		}
	}
	if (!bBodyTrackingHipsUsed && bDriveTwist)
	{
		// Camera-observed hip twist rides on top of the fallback body yaw, estimated from
		// FORESHORTENING: a front camera observes the planar hip-line width directly (it
		// shrinks as cos(yaw) when the hips turn), which is far stronger than monocular hip
		// depth. The depth delta only supplies the rotation sign, gated by frame hysteresis.
		FVector LeftHipWorld = FVector::ZeroVector;
		FVector RightHipWorld = FVector::ZeroVector;
		if (IsMeasured((int32)EMediaPipePoseLandmark::LeftHip) &&
			IsMeasured((int32)EMediaPipePoseLandmark::RightHip) &&
			TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftHip, LeftHipWorld) &&
			TryGetLmWorld((int32)EMediaPipePoseLandmark::RightHip, RightHipWorld))
		{
			const FVector HipLine = RightHipWorld - LeftHipWorld;
			FVector SourceDepthAxis = PoseToWorldTransform.GetUnitAxis(EAxis::X);
			SourceDepthAxis.Z = 0.0f;
			SourceDepthAxis = SourceDepthAxis.GetSafeNormal();
			if (SourceDepthAxis.IsNearlyZero())
			{
				SourceDepthAxis = FVector::ForwardVector;
			}

			MediaPipeBodySolverMath::FMediaPipeHipYawEstimatorInput HipYawInput;
			HipYawInput.HipWidthCm = FVector2D(HipLine.X, HipLine.Y).Size();
			HipYawInput.HipDepthDeltaCm = FVector::DotProduct(HipLine, SourceDepthAxis);
			HipYawInput.DeltaSeconds = DeltaSeconds;
			HipYawInput.MaxYawDeg = FMath::Max(CVarMediaPipeHipTwistMaxDeg.GetValueOnAnyThread(), 0.0f);
			float HipResidualDeg = MediaPipeBodySolverMath::UpdateHipYawEstimator(
				BodyState.HipYawEstimator, HipYawInput);
			if (bMirror)
			{
				HipResidualDeg = -HipResidualDeg;
			}
			TargetYawDeg += HipResidualDeg;
		}
	}

	// Camera yaw anchor (2026-07-06): the Quest-derived body yaw carries a constant bias
	// plus slow drift (take-4 round-3 measured +6deg at start growing to +10deg vs the
	// Epic solve - the user sees the chest progressively "turning away"). The camera's
	// shoulder line observes the true torso heading every frame; a low-passed closed-loop
	// correction (updated after the pelvis write below) erases bias and drift while Quest
	// keeps owning fast turns - same complementary architecture as the arm-direction and
	// pelvis-anchor corrections.
	if (CVarMediaPipeBodyYawFromCamera.GetValueOnAnyThread() != 0)
	{
		TargetYawDeg += BodyState.BodyYawCameraCorrectionDeg;
	}

	// Every yaw source feeds ONE rate-limited state, so source switches and tracking dropouts
	// walk the pelvis over a fraction of a second instead of snapping it.
	BodyState.SmoothedBodyYawDeg = MediaPipeBodySolverMath::ApproachAngleDeg(
		BodyState.bHasSmoothedBodyYaw ? BodyState.SmoothedBodyYawDeg : TargetYawDeg,
		TargetYawDeg,
		DeltaSeconds,
		0.2f,
		120.0f);
	BodyState.bHasSmoothedBodyYaw = true;
	BodyState.PrevBodyYawSource = bBodyTrackingHipsUsed ? 2 : (bDriveLean || bDriveTwist ? 1 : 0);
	const float TwistYawDeg = BodyState.SmoothedBodyYawDeg;

	BodyState.LastBodyYawDeg = TwistYawDeg;
	BodyState.LastBodyYawSource = BodyState.PrevBodyYawSource;

	FVector CompUp = TargetCompTransform.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	if (CompUp.IsNearlyZero())
	{
		CompUp = FVector::UpVector;
	}
	const FQuat TwistComp(CompUp, FMath::DegreesToRadians(TwistYawDeg));

	const float MaxLeanDeg = FMath::Max(CVarMediaPipeHmdLeanMaxDeg.GetValueOnAnyThread(), 0.0f);
	const FVector CurrentPelvisComp = RefPelvisTranslationComp + BodyState.SmoothedPelvisOffsetComp;

	FQuat LeanSwingComp = FQuat::Identity;
	if (bDriveLean)
	{
		// Closed-loop lean: rotate the pelvis so the avatar's TWISTED reference pelvis->head
		// line points at the actual HMD position mapped into the avatar's component frame. The
		// camera stays 1:1 with the real head (never lag it); the body is solved to sit
		// underneath it, so bending forward/back can no longer leave the camera inside the
		// chest - at any body yaw, because the lean is solved after the twist. When the mapped
		// HMD is implausible for this skeleton (a non-embodied reference avatar elsewhere in the
		// room), fall back to the displacement-vs-neutral heuristic.
		// The camera sits at the EYES, in front of the head bone; target the head bone behind
		// the HMD along its view direction or a deep bend rotates the face/chest through the
		// camera (the inside-the-body view).
		const FQuat HmdRotComp = TargetCompTransform.InverseTransformRotation(CachedQuestHmdRotWorld);
		const FVector HmdComp =
			TargetCompTransform.InverseTransformPosition(CachedQuestHmdWorld) -
			HmdRotComp.GetForwardVector() * FMath::Max(CVarMediaPipeHmdLeanEyeBackCm.GetValueOnAnyThread(), 0.0f);
		const FVector RefTorso = (RefHeadPosComp - RefPelvisTranslationComp);
		const FVector TwistedRefTorso = TwistComp.RotateVector(RefTorso);
		const FVector DesiredTorso = HmdComp - CurrentPelvisComp;
		const float PlanarDistanceCm = FVector2D(HmdComp.X - CurrentPelvisComp.X, HmdComp.Y - CurrentPelvisComp.Y).Size();
		const bool bHmdMapsOntoAvatar =
			RefTorso.Size() > 10.0f &&
			DesiredTorso.Z > RefTorso.Size() * 0.25f &&
			DesiredTorso.Size() < RefTorso.Size() * 2.5f &&
			PlanarDistanceCm < 120.0f;

		if (bHmdMapsOntoAvatar)
		{
			LeanSwingComp = FQuat::FindBetweenVectors(TwistedRefTorso.GetSafeNormal(), DesiredTorso.GetSafeNormal());
		}
		else
		{
			// Heuristic fallback: HMD planar displacement against a slow neutral, expressed in
			// the wearer's average facing frame.
			const FVector2D HmdPlanarXY(CachedQuestHmdWorld.X, CachedQuestHmdWorld.Y);
			if (!BodyState.bHasHmdLeanNeutral)
			{
				BodyState.HmdLeanNeutralXY = HmdPlanarXY;
				BodyState.bHasHmdLeanNeutral = true;
			}
			else if (DeltaSeconds > 0.0f)
			{
				const float Alpha = 1.0f - FMath::Pow(0.5f, DeltaSeconds / NeutralHalfLife);
				BodyState.HmdLeanNeutralXY = FMath::Lerp(BodyState.HmdLeanNeutralXY, HmdPlanarXY, Alpha);
			}

			const FVector2D Displacement = HmdPlanarXY - BodyState.HmdLeanNeutralXY;
			const float LeverCm = FMath::Max(
				CachedQuestHmdWorld.Z * (1.0f - FMath::Clamp(CVarMediaPipeLegScaffoldHipFromHmdRatio.GetValueOnAnyThread(), 0.05f, 0.95f)),
				30.0f);
			const FVector LeanAxisWorld = FVector(-Displacement.Y, Displacement.X, 0.0f).GetSafeNormal();
			if (!LeanAxisWorld.IsNearlyZero())
			{
				const float LeanDeg = FMath::RadiansToDegrees(FMath::Atan2(
					FMath::Max(Displacement.Size() - 2.0f, 0.0f), LeverCm));
				const FVector LeanAxisComp = TargetCompTransform.InverseTransformVectorNoScale(LeanAxisWorld).GetSafeNormal();
				if (!LeanAxisComp.IsNearlyZero())
				{
					LeanSwingComp = FQuat(LeanAxisComp, FMath::DegreesToRadians(LeanDeg));
				}
			}
		}

		// Clamp the lean swing magnitude.
		FVector SwingAxis = FVector::ZeroVector;
		float SwingAngleRad = 0.0f;
		LeanSwingComp.ToAxisAndAngle(SwingAxis, SwingAngleRad);
		const float MaxLeanRad = FMath::DegreesToRadians(MaxLeanDeg);
		if (SwingAngleRad > MaxLeanRad)
		{
			LeanSwingComp = FQuat(SwingAxis, MaxLeanRad);
		}
	}

	// Twist first, then lean the twisted torso onto the HMD line: (Lean * Twist) * RefTorso ==
	// DesiredTorso exactly, so the chest stays under the camera at any body yaw.
	const FQuat TargetPelvisRotCS = ((LeanSwingComp * TwistComp) * RefPelvisComp).GetNormalized();
	UpdateSmoothedRotation(
		BodyState.bHasSmoothedPelvisRotCS,
		BodyState.SmoothedPelvisRotCS,
		TargetPelvisRotCS,
		HalfLifeToAlpha(SpineRotationHalfLifeSeconds, DeltaSeconds));
	ApplyRotationCS(CSPose, Pelvis, BodyState.SmoothedPelvisRotCS);

	// Camera yaw anchor error update (REDESIGNED 2026-07-06 night). The first attempt read
	// the converted landmark frame, which is HIP-YAW-NORMALIZED - absolute facing is removed
	// by construction and the loop measured a constant 0 (trace-proven). The RAW MediaPipe
	// world landmarks (PoseFrame.World: hip-origin camera-space meters, x image-right,
	// y vertical, z depth) DO carry the wearer's facing relative to the fixed room camera:
	// shoulder-line yaw in the camera's horizontal (x-z) plane. The camera-to-avatar frame
	// offset is latched ONCE at live-neutral settle (both systems define their zero there);
	// afterwards the closed loop erases accumulated yaw drift while Quest owns fast turns.
	// Adaptation freezes when the shoulder line is too foreshortened to condition the yaw
	// (profile views) or landmarks are unreliable.
	if (CVarMediaPipeBodyYawFromCamera.GetValueOnAnyThread() != 0 &&
		bHasPoseFrame && DeltaSeconds > 0.0f)
	{
		const int32 LsIdx = (int32)EMediaPipePoseLandmark::LeftShoulder;
		const int32 RsIdx = (int32)EMediaPipePoseLandmark::RightShoulder;
		if (PoseFrame.World.IsValidIndex(LsIdx) && PoseFrame.World.IsValidIndex(RsIdx) &&
			GetLandmarkReliability(LsIdx) >= 0.3f && GetLandmarkReliability(RsIdx) >= 0.3f)
		{
			const FMediaPipePoseLandmark& Ls = PoseFrame.World.Points[LsIdx];
			const FMediaPipePoseLandmark& Rs = PoseFrame.World.Points[RsIdx];
			const float Dx = Ls.X - Rs.X;
			const float Dz = Ls.Z - Rs.Z;
			const float PlanarLenM = FMath::Sqrt(Dx * Dx + Dz * Dz);
			// Below ~18cm of planar shoulder extent the yaw is ill-conditioned (turned away
			// or heavy occlusion): hold the correction, do not adapt.
			if (FMath::IsFinite(PlanarLenM) && PlanarLenM > 0.18f)
			{
				float CamYawDeg = FMath::RadiansToDegrees(FMath::Atan2(Dz, Dx));
				const bool bMirrorYaw = CVarMediaPipeLivePoseMirror.GetValueOnAnyThread() != 0;
				if (bMirrorYaw)
				{
					CamYawDeg = -CamYawDeg;
				}
				if (!BodyState.bHasBodyYawCamAnchor)
				{
					if (BodyState.bLiveNeutralsReady)
					{
						BodyState.BodyYawCamAnchorDeg =
							FMath::FindDeltaAngleDegrees(TwistYawDeg, CamYawDeg);
						BodyState.BodyYawCameraCorrectionDeg = 0.0f;
						BodyState.bHasBodyYawCamAnchor = true;
					}
				}
				else
				{
					const float ErrDeg = FMath::FindDeltaAngleDegrees(
						TwistYawDeg, CamYawDeg - BodyState.BodyYawCamAnchorDeg);
					const float YawAlpha = HalfLifeToAlpha(
						FMath::Max(CVarMediaPipeBodyYawFromCameraHalfLifeSeconds.GetValueOnAnyThread(), 0.1f),
						DeltaSeconds);
					const float MaxCorrDeg = FMath::Max(CVarMediaPipeBodyYawFromCameraMaxDeg.GetValueOnAnyThread(), 0.0f);
					BodyState.BodyYawCameraCorrectionDeg = FMath::Clamp(
						BodyState.BodyYawCameraCorrectionDeg + YawAlpha * ErrDeg,
						-MaxCorrDeg, MaxCorrDeg);
					static double LastYawAnchorLogSeconds = 0.0;
					const double NowSeconds = FPlatformTime::Seconds();
					if (NowSeconds - LastYawAnchorLogSeconds > 1.0)
					{
						LastYawAnchorLogSeconds = NowSeconds;
						UE_LOG(LogMediaPipePose, Log,
							TEXT("mp.BodyYawAnchor: camRaw=%.1f anchor=%.1f err=%.1f corr=%.1f twist=%.1f planarM=%.2f mirror=%d"),
							CamYawDeg, BodyState.BodyYawCamAnchorDeg, ErrDeg,
							BodyState.BodyYawCameraCorrectionDeg, TwistYawDeg, PlanarLenM, bMirrorYaw ? 1 : 0);
					}
				}
			}
		}
	}
}

void FAnimNode_MediaPipePoseDriven::DriveHmdHeadCS(FCSPose<FCompactPose>& CSPose, float DeltaSeconds)
{
	// Live head follow: the avatar head tracks the worn HMD rotation. Pitch and roll apply
	// directly; yaw is measured against a self-calibrating neutral so sustained body turns
	// recenter while quick glances read as head yaw. Runs after DriveSpineCS so it owns the
	// final head write when enabled. Dataset replay and fused-pose evaluation keep their own
	// head ownership.
	if (CVarMediaPipeDriveHmdHead.GetValueOnAnyThread() == 0 ||
		!bHasCachedQuestHmdPose ||
		!bHasReferencePose ||
		!Head.IsValidToEvaluate())
	{
		return;
	}
	// Parity-aware replay gate (2026-07-04): live-parity scoring replays run the live
	// neutral-settling/lean/twist/head paths (user-observed: hips never turned in parity
	// replays; MHA hip-yaw RMSE 95-170 deg in turn windows). The byte-stable evaluation
	// policy still suppresses them.
	if ((FMediaPipeTrackingFusionDatasetReplayRuntime::Get().IsActive() &&
			!FMediaPipeTrackingFusionDatasetReplayRuntime::IsLiveParityEnabled()) ||
		ShouldUseBodyFusionPoseForEvaluation())
	{
		return;
	}
	if (BodyState.bLiveNeutralGateArmed && !BodyState.bLiveNeutralsReady)
	{
		// Still donning the headset: the yaw neutral must seed from the settled standing pose.
		return;
	}

	const FRotator HmdRotation = CachedQuestHmdRotWorld.Rotator();
	const float YawDeltaDeg = MediaPipeBodySolverMath::UpdateHmdHeadNeutralYaw(
		BodyState.HmdHeadYawNeutral,
		HmdRotation.Yaw,
		DeltaSeconds,
		FMath::Max(CVarMediaPipeHmdHeadYawNeutralHalfLife.GetValueOnAnyThread(), 0.05f));

	const bool bMirror = CVarMediaPipeHmdHeadMirror.GetValueOnAnyThread() != 0;
	const FRotator HeadDeltaRotation(
		HmdRotation.Pitch,
		bMirror ? -YawDeltaDeg : YawDeltaDeg,
		bMirror ? -HmdRotation.Roll : HmdRotation.Roll);

	FVector AvatarForwardPlanar = GetTargetForwardWorld();
	AvatarForwardPlanar.Z = 0.0f;
	AvatarForwardPlanar = AvatarForwardPlanar.GetSafeNormal();
	if (AvatarForwardPlanar.IsNearlyZero())
	{
		return;
	}

	const FQuat AvatarYawQuat = MakeQuatFromForwardUp(AvatarForwardPlanar, FVector::UpVector);
	const FQuat HeadBasisWorld = (AvatarYawQuat * FQuat(HeadDeltaRotation)).GetNormalized();
	const FVector HeadForwardComp =
		TargetCompTransform.InverseTransformVectorNoScale(HeadBasisWorld.GetForwardVector()).GetSafeNormal();
	const FVector HeadUpComp =
		TargetCompTransform.InverseTransformVectorNoScale(HeadBasisWorld.GetUpVector()).GetSafeNormal();
	if (HeadForwardComp.IsNearlyZero() || HeadUpComp.IsNearlyZero() ||
		FVector::CrossProduct(HeadForwardComp, HeadUpComp).IsNearlyZero())
	{
		return;
	}

	const FQuat TargetHeadBasisComp = MakeQuatFromForwardUp(HeadForwardComp, HeadUpComp);
	const FQuat TargetHeadRotCS = ((TargetHeadBasisComp * RefHeadBasisComp.Inverse()) * RefHeadComp).GetNormalized();
	UpdateSmoothedRotation(
		BodyState.bHasSmoothedHeadRotCS,
		BodyState.SmoothedHeadRotCS,
		TargetHeadRotCS,
		HalfLifeToAlpha(HeadRotationHalfLifeSeconds, DeltaSeconds),
		HeadRotationMaxStepDegrees);
	ApplyRotationCS(CSPose, Head, BodyState.SmoothedHeadRotCS);
}

void FAnimNode_MediaPipePoseDriven::DriveSpineCS(FCSPose<FCompactPose>& CSPose, float DeltaSeconds)
{
	if (!bDriveSpine || NumSpineBones <= 0)
	{
		return;
	}

	const float SpineRotAlpha = HalfLifeToAlpha(SpineRotationHalfLifeSeconds, FMath::Max(DeltaSeconds, 0.0f));
	const bool bUseHolisticHeadSolve = CVarMediaPipeHolisticHeadSolve.GetValueOnAnyThread() != 0;

	const int32 LHipLm = (int32)EMediaPipePoseLandmark::LeftHip;
	const int32 RHipLm = (int32)EMediaPipePoseLandmark::RightHip;
	const int32 LShoulderLm = (int32)EMediaPipePoseLandmark::LeftShoulder;
	const int32 RShoulderLm = (int32)EMediaPipePoseLandmark::RightShoulder;
	if (!IsMeasured(LHipLm) || !IsMeasured(RHipLm) || !IsMeasured(LShoulderLm) || !IsMeasured(RShoulderLm))
	{
		return;
	}
	FVector LShoulderWorld = FVector::ZeroVector;
	FVector RShoulderWorld = FVector::ZeroVector;
	FVector ShoulderMidWorld = FVector::ZeroVector;
	if (!TryGetLmWorld(LShoulderLm, LShoulderWorld) || !TryGetLmWorld(RShoulderLm, RShoulderWorld))
	{
		return;
	}
	ShoulderMidWorld = (LShoulderWorld + RShoulderWorld) * 0.5f;

	FVector HipRightWorld = FVector::ZeroVector;
	FVector ShoulderRightWorld = FVector::ZeroVector;
	FVector UpWorld = FVector::ZeroVector;
	FVector ForwardWorld = FVector::ZeroVector;
	if (!TryGetTorsoBasisWorld(HipRightWorld, ShoulderRightWorld, UpWorld, ForwardWorld))
	{
		return;
	}

	const FVector HipRightComp = TargetCompTransform.InverseTransformVectorNoScale(HipRightWorld).GetSafeNormal();
	const FVector ShoulderRightComp = TargetCompTransform.InverseTransformVectorNoScale(ShoulderRightWorld).GetSafeNormal();
	const FVector UpComp = TargetCompTransform.InverseTransformVectorNoScale(UpWorld).GetSafeNormal();
	const FVector PoseFwdComp = TargetCompTransform.InverseTransformVectorNoScale(ForwardWorld).GetSafeNormal();

	FVector CompUp = TargetCompTransform.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	if (CompUp.IsNearlyZero())
	{
		CompUp = FVector::UpVector;
	}

	auto MakeBasis = [&](FVector Right, const FVector& Up, const FVector& ForwardHint) -> FQuat
	{
		FMediaPipeSemanticBodyBasisInput BasisInput;
		BasisInput.Right = Right;
		BasisInput.Up = Up;
		BasisInput.ForwardHint = ForwardHint;
		return MakeSemanticBodyBasis(BasisInput);
	};

	auto ApplySemanticBasisToBone = [&](const FBoneReference& Bone, const FQuat& RefBoneComp, const FQuat& RefBasisComp,
		const FQuat& TargetBasisComp, bool& bHasSmoothedRot, FQuat& InOutSmoothedRotCS)
	{
		if (!Bone.IsValidToEvaluate() || TargetBasisComp.IsIdentity() || RefBasisComp.IsIdentity())
		{
			return;
		}

		const FQuat TargetRotCS = ((TargetBasisComp * RefBasisComp.Inverse()) * RefBoneComp).GetNormalized();
		UpdateSmoothedRotation(bHasSmoothedRot, InOutSmoothedRotCS, TargetRotCS, SpineRotAlpha);
		ApplyRotationCS(CSPose, Bone, InOutSmoothedRotCS);
	};

	const float FaceBlendWeight = HeadFaceBlend;
	const float FacePitchScale = HeadPitchScale;
	const float FaceTwistWeight = HeadTwistWeight;

	auto ApplySemanticBasisSwingTwist = [&](const FBoneReference& Bone, const FQuat& RefBoneComp, const FQuat& RefBasisComp,
		const FQuat& TargetBasisComp, const FVector& TwistAxisComp, const float SwingWeight, const float TwistWeight,
		bool& bHasSmoothedRot, FQuat& InOutSmoothedRotCS)
	{
		if (!Bone.IsValidToEvaluate() || TargetBasisComp.IsIdentity() || RefBasisComp.IsIdentity())
		{
			return;
		}

		FVector TwistAxis = TwistAxisComp.GetSafeNormal();
		if (TwistAxis.IsNearlyZero())
		{
			TwistAxis = FVector::UpVector;
		}

		const FQuat Delta = (TargetBasisComp * RefBasisComp.Inverse()).GetNormalized();
		FQuat Swing = FQuat::Identity;
		FQuat Twist = FQuat::Identity;
		Delta.ToSwingTwist(TwistAxis, Swing, Twist);

		const FQuat SwingScaled = FQuat::Slerp(FQuat::Identity, Swing, SwingWeight).GetNormalized();
		const FQuat TwistScaled = FQuat::Slerp(FQuat::Identity, Twist, TwistWeight).GetNormalized();
		const FQuat RawTargetRotCS = (SwingScaled * TwistScaled * RefBoneComp).GetNormalized();
		FVector LocalTwistAxis = RefBasisComp.RotateVector(FVector::UpVector).GetSafeNormal();
		if (LocalTwistAxis.IsNearlyZero())
		{
			LocalTwistAxis = TwistAxis;
		}
		const FQuat TargetRotCS = FilterTargetRotationSwingTwist(RefBoneComp, LocalTwistAxis, RawTargetRotCS, FaceTwistWeight, 0.0f);
		bHasSmoothedRot = true;
		InOutSmoothedRotCS = TargetRotCS;
		ApplyRotationCS(CSPose, Bone, TargetRotCS);
	};

	const FQuat PelvisTargetBasis = MakeBasis(HipRightComp, UpComp, PoseFwdComp);
	ApplySemanticBasisToBone(Pelvis, RefPelvisComp, RefPelvisBasisComp, PelvisTargetBasis, BodyState.bHasSmoothedPelvisRotCS, BodyState.SmoothedPelvisRotCS);

	const FVector ChestRightComp = !ShoulderRightComp.IsNearlyZero() ? ShoulderRightComp : HipRightComp;
	const FQuat ChestTargetBasis = MakeBasis(ChestRightComp, UpComp, PoseFwdComp);

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

	const float Denom = FMath::Max(1.0f, static_cast<float>(NumSpineBones));
	for (int32 i = 0; i < NumSpineBones; ++i)
	{
		const uint8 Slot = SpineBoneSlots[i];
		if (Slot == 0)
		{
			continue;
		}

		const FBoneReference& SpineBone = GetSpineRefBySlot(Slot);
		if (!SpineBone.IsValidToEvaluate())
		{
			continue;
		}

		const float Weight = static_cast<float>(i + 1) / Denom;
		const FQuat TargetBasis = FQuat::Slerp(PelvisTargetBasis, ChestTargetBasis, Weight).GetNormalized();
		ApplySemanticBasisToBone(SpineBone, RefSpineComp[i], RefSpineBasisComp[i], TargetBasis, BodyState.bHasSmoothedSpineRotCS[i], BodyState.SmoothedSpineRotCS[i]);
	}

	if (FMediaPipeTrackingFusionDatasetReplayRuntime::Get().IsActive() && ShouldUseBodyFusionPoseForEvaluation())
	{
		BodyState.bHasSmoothedNeckRotCS = false;
		BodyState.bHasSmoothedNeck02RotCS = false;
		BodyState.bHasSmoothedHeadRotCS = false;
		return;
	}

	if (FaceBlendWeight <= KINDA_SMALL_NUMBER && FaceTwistWeight <= KINDA_SMALL_NUMBER)
	{
		BodyState.bHasSmoothedNeckRotCS = false;
		BodyState.bHasSmoothedNeck02RotCS = false;
		BodyState.bHasSmoothedHeadRotCS = false;
		if (CVarMediaPipeTorsoDebug.GetValueOnAnyThread() != 0)
		{
			const double NowSeconds = FPlatformTime::Seconds();
			if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, DiagnosticsState.LastHeadDiagnosticLogTimeSeconds))
			{
				UE_LOG(LogMediaPipePose, Log,
					TEXT("mp.HeadDebug: actor=%s enabled=0 faceBlend=%.2f twistWeight=%.2f reason=HeadFaceBlendAndHeadTwistWeightAreZero"),
					*TargetActorName.ToString(),
					FaceBlendWeight,
					FaceTwistWeight);
			}
		}
		return;
	}

	FVector HeadRightWorld = ShoulderRightWorld;
	FVector HeadUpWorld = UpWorld;
	FVector HeadForwardWorld = ForwardWorld;
	FVector NoseWorld = FVector::ZeroVector;
	const bool bHasNose = TryGetLmWorld((int32)EMediaPipePoseLandmark::Nose, NoseWorld);

	FVector LeftEarWorld = FVector::ZeroVector;
	FVector RightEarWorld = FVector::ZeroVector;
	const bool bHasLeftEar = TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftEar, LeftEarWorld);
	const bool bHasRightEar = TryGetLmWorld((int32)EMediaPipePoseLandmark::RightEar, RightEarWorld);

	FVector LeftEyeWorld = FVector::ZeroVector;
	FVector RightEyeWorld = FVector::ZeroVector;
	const bool bHasLeftEye = TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftEye, LeftEyeWorld);
	const bool bHasRightEye = TryGetLmWorld((int32)EMediaPipePoseLandmark::RightEye, RightEyeWorld);

	FVector LeftMouthWorld = FVector::ZeroVector;
	FVector RightMouthWorld = FVector::ZeroVector;
	const bool bHasLeftMouth = TryGetLmWorld((int32)EMediaPipePoseLandmark::MouthLeft, LeftMouthWorld);
	const bool bHasRightMouth = TryGetLmWorld((int32)EMediaPipePoseLandmark::MouthRight, RightMouthWorld);

	FVector HeadCenterWorld = ShoulderMidWorld + UpWorld * 20.0f;
	if (bHasLeftEar && bHasRightEar)
	{
		HeadRightWorld = (RightEarWorld - LeftEarWorld).GetSafeNormal();
		HeadCenterWorld = (LeftEarWorld + RightEarWorld) * 0.5f;
	}
	else if (bHasLeftEye && bHasRightEye)
	{
		HeadRightWorld = (RightEyeWorld - LeftEyeWorld).GetSafeNormal();
		HeadCenterWorld = (LeftEyeWorld + RightEyeWorld) * 0.5f;
	}
	else if (bHasNose)
	{
		HeadCenterWorld = NoseWorld;
	}

	const FVector HeadUpCandidate = (HeadCenterWorld - ShoulderMidWorld).GetSafeNormal();
	if (!HeadUpCandidate.IsNearlyZero())
	{
		HeadUpWorld = HeadUpCandidate;
	}

	if (bHasNose)
	{
		FVector ForwardCandidate = (NoseWorld - HeadCenterWorld).GetSafeNormal();
		if (ForwardCandidate.IsNearlyZero())
		{
			ForwardCandidate = (NoseWorld - ShoulderMidWorld).GetSafeNormal();
		}
		ForwardCandidate = (ForwardCandidate - FVector::DotProduct(ForwardCandidate, HeadUpWorld) * HeadUpWorld).GetSafeNormal();
		if (!ForwardCandidate.IsNearlyZero())
		{
			HeadForwardWorld = ForwardCandidate;
		}
	}

	const FVector HeadRightComp = TargetCompTransform.InverseTransformVectorNoScale(HeadRightWorld).GetSafeNormal();
	const FVector HeadUpComp = TargetCompTransform.InverseTransformVectorNoScale(HeadUpWorld).GetSafeNormal();
	const FVector HeadForwardComp = TargetCompTransform.InverseTransformVectorNoScale(HeadForwardWorld).GetSafeNormal();

	const FQuat FaceHeadTargetBasis = MakeBasis(
		!HeadRightComp.IsNearlyZero() ? HeadRightComp : ChestRightComp,
		!HeadUpComp.IsNearlyZero() ? HeadUpComp : UpComp,
		!HeadForwardComp.IsNearlyZero() ? HeadForwardComp : PoseFwdComp);
	// The Holistic path below applies dense face motion directly; keep the old sparse face basis from stacking on top of it.
	const float FaceBasisBlend = bUseHolisticHeadSolve ? 0.0f : FaceBlendWeight * FaceTwistWeight;
	FQuat HeadTargetBasis = FQuat::Slerp(ChestTargetBasis, FaceHeadTargetBasis, FaceBasisBlend).GetNormalized();
	float ScreenHeadYawDeg = 0.0f;
	float ScreenHeadPitchDeg = 0.0f;
	float ScreenHeadRollDeg = 0.0f;
	float ScreenHeadLateralAngleDeltaDeg = 0.0f;
	float ScreenHeadSideBendDeg = 0.0f;
	float ScreenHeadFacePitchInput = 0.0f;
	bool bDenseFaceSignalValid = false;
	float DenseFacePitchSignal = 0.0f;
	float DenseFaceYawSignal = 0.0f;
	float DenseFaceRollSignalDeg = 0.0f;
	float DenseFacePitchDeltaSignal = 0.0f;
	float DenseFaceYawDeltaSignal = 0.0f;
	float DenseFaceRollDeltaSignalDeg = 0.0f;
	float DenseHeadPitchAppliedDeg = 0.0f;
	float DenseHeadYawAppliedDeg = 0.0f;
	float DenseHeadRollAppliedDeg = 0.0f;
	float DenseHeadLocalPitchDeg = 0.0f;
	float DenseHeadLocalYawDeg = 0.0f;
	float DenseHeadLocalRollDeg = 0.0f;
	bool bDenseHeadLocalTargetValid = false;
	bool bHoldingDenseHeadLocalTarget = false;
	float ScreenHeadNoseEyePitchDelta = 0.0f;
	float ScreenHeadMouthEyePitchDelta = 0.0f;
	float ScreenHeadMouthEarPitchDelta = 0.0f;
	float ScreenHeadNoseEarPitchDelta = 0.0f;
	float ScreenHeadNoseEyePitchProxy = 0.0f;
	float ScreenHeadMouthEyePitchProxy = 0.0f;
	float ScreenHeadMouthEarPitchProxy = 0.0f;
	float ScreenHeadNoseEarPitchProxy = 0.0f;
	float HeadWorldMouthEyePitchDelta = 0.0f;
	float HeadWorldNoseEyePitchDelta = 0.0f;
	float HeadWorldMouthEarPitchDelta = 0.0f;
	float HeadWorldNoseEarPitchDelta = 0.0f;
	float HeadWorldForwardPitchDeltaDeg = 0.0f;
	float HeadWorldMouthEyePitchProxy = 0.0f;
	float HeadWorldNoseEyePitchProxy = 0.0f;
	float HeadWorldMouthEarPitchProxy = 0.0f;
	float HeadWorldNoseEarPitchProxy = 0.0f;
	float HeadWorldForwardPitchProxyDeg = 0.0f;
	int32 bScreenHasShoulders2D = 0;
	int32 bScreenHasNose2D = 0;
	int32 bScreenHasEyes2D = 0;
	int32 bScreenHasEars2D = 0;
	int32 bScreenHasMouth2D = 0;
	int32 ScreenHeadHadReference = 0;
	int32 ScreenHeadInitializedReference = 0;
	FVector2D ScreenHeadCenterDelta2D = FVector2D::ZeroVector;
	FVector2D ScreenHeadNoseDelta2D = FVector2D::ZeroVector;
	FVector2D ScreenHeadShoulderNoseDelta2D = FVector2D::ZeroVector;
	FVector2D ScreenHeadShoulderNoseAbs2D = FVector2D::ZeroVector;
	auto TryGetDenseFace2D = [&](const int32 Index, FVector2D& OutPoint) -> bool
	{
		if (!bUseHolisticHeadSolve ||
			Index < 0 ||
			!PoseFrame.bHasFace ||
			PoseFrame.Face.bHasFace == 0 ||
			Index >= PoseFrame.Face.Normalized.Count ||
			Index >= MediaPipeFaceLandmarkMaxCount)
		{
			return false;
		}

		const FMediaPipeRawHandLandmark& Landmark = PoseFrame.Face.Normalized.Landmarks[Index];
		if (!FMath::IsFinite(Landmark.X) || !FMath::IsFinite(Landmark.Y))
		{
			return false;
		}

		OutPoint = FVector2D(Landmark.X, Landmark.Y);
		return true;
	};
	auto TryGetDenseFaceMp = [&](const int32 Index, FVector& OutPoint) -> bool
	{
		if (!bUseHolisticHeadSolve ||
			Index < 0 ||
			!PoseFrame.bHasFace ||
			PoseFrame.Face.bHasFace == 0 ||
			Index >= PoseFrame.Face.Normalized.Count ||
			Index >= MediaPipeFaceLandmarkMaxCount)
		{
			return false;
		}

		const FMediaPipeRawHandLandmark& Landmark = PoseFrame.Face.Normalized.Landmarks[Index];
		const float AspectYOverX = FMath::Clamp(PoseFrame.ConditioningDiagnostics.InputAspectYOverX, 0.1f, 10.0f);
		OutPoint = FVector(Landmark.X, Landmark.Y * AspectYOverX, Landmark.Z);
		return !OutPoint.ContainsNaN();
	};
	FVector2D DenseFaceLeftEye = FVector2D::ZeroVector;
	FVector2D DenseFaceRightEye = FVector2D::ZeroVector;
	FVector2D DenseFaceNose = FVector2D::ZeroVector;
	FVector2D DenseFaceChin = FVector2D::ZeroVector;
	const bool bHasDenseFaceSolvePoints =
		FaceBlendWeight > KINDA_SMALL_NUMBER &&
		TryGetDenseFace2D(263, DenseFaceLeftEye) &&
		TryGetDenseFace2D(33, DenseFaceRightEye) &&
		TryGetDenseFace2D(1, DenseFaceNose) &&
		TryGetDenseFace2D(152, DenseFaceChin);
	if (FaceBlendWeight > KINDA_SMALL_NUMBER)
	{
		auto TryGetNormalizedXY = [&](const int32 LmIdx, FVector2D& Out) -> bool
		{
			if (LmIdx < 0 || !bHasPoseFrame || !IsMeasured(LmIdx) || !PoseFrame.Normalized.IsValidIndex(LmIdx))
			{
				return false;
			}
			const FMediaPipePoseLandmark& Lm = PoseFrame.Normalized.Points[LmIdx];
			if (!FMath::IsFinite(Lm.X) || !FMath::IsFinite(Lm.Y))
			{
				return false;
			}
			Out = FVector2D(Lm.X, Lm.Y);
			return true;
		};

		FVector2D LShoulder2D = FVector2D::ZeroVector;
		FVector2D RShoulder2D = FVector2D::ZeroVector;
		FVector2D Nose2D = FVector2D::ZeroVector;
		FVector2D LEye2D = FVector2D::ZeroVector;
		FVector2D REye2D = FVector2D::ZeroVector;
		FVector2D LEar2D = FVector2D::ZeroVector;
		FVector2D REar2D = FVector2D::ZeroVector;
		FVector2D LMouth2D = FVector2D::ZeroVector;
		FVector2D RMouth2D = FVector2D::ZeroVector;
		const bool bHasShoulders2D =
			TryGetNormalizedXY(LShoulderLm, LShoulder2D) &&
			TryGetNormalizedXY(RShoulderLm, RShoulder2D);
		const bool bHasNose2D = TryGetNormalizedXY((int32)EMediaPipePoseLandmark::Nose, Nose2D);
		const bool bHasEyes2D =
			TryGetNormalizedXY((int32)EMediaPipePoseLandmark::LeftEye, LEye2D) &&
			TryGetNormalizedXY((int32)EMediaPipePoseLandmark::RightEye, REye2D);
		const bool bHasEars2D =
			TryGetNormalizedXY((int32)EMediaPipePoseLandmark::LeftEar, LEar2D) &&
			TryGetNormalizedXY((int32)EMediaPipePoseLandmark::RightEar, REar2D);
		const bool bHasMouth2D =
			TryGetNormalizedXY((int32)EMediaPipePoseLandmark::MouthLeft, LMouth2D) &&
			TryGetNormalizedXY((int32)EMediaPipePoseLandmark::MouthRight, RMouth2D);
		bScreenHasShoulders2D = bHasShoulders2D ? 1 : 0;
		bScreenHasNose2D = bHasNose2D ? 1 : 0;
		bScreenHasEyes2D = bHasEyes2D ? 1 : 0;
		bScreenHasEars2D = bHasEars2D ? 1 : 0;
		bScreenHasMouth2D = bHasMouth2D ? 1 : 0;

		if (bHasShoulders2D && (bHasNose2D || bHasEyes2D || bHasEars2D))
		{
			const FVector2D ShoulderMid2D = (LShoulder2D + RShoulder2D) * 0.5f;
			const float ShoulderSpan2D = FMath::Max(FVector2D::Distance(LShoulder2D, RShoulder2D), 0.05f);
			FVector2D HeadCenter2D = bHasNose2D ? Nose2D : ShoulderMid2D;
			float FaceSpan2D = ShoulderSpan2D * 0.35f;
			if (bHasEars2D)
			{
				HeadCenter2D = (LEar2D + REar2D) * 0.5f;
				FaceSpan2D = FMath::Max(FVector2D::Distance(LEar2D, REar2D), FaceSpan2D);
			}
			else if (bHasEyes2D)
			{
				HeadCenter2D = (LEye2D + REye2D) * 0.5f;
				FaceSpan2D = FMath::Max(FVector2D::Distance(LEye2D, REye2D), FaceSpan2D);
			}

			const FVector2D HeadCenterOffset2D = (HeadCenter2D - ShoulderMid2D) / ShoulderSpan2D;
			const FVector2D NoseOffset2D = bHasNose2D
				? (Nose2D - HeadCenter2D) / FMath::Max(FaceSpan2D, 0.02f)
				: FVector2D::ZeroVector;
			const FVector2D NoseShoulderOffset2D = bHasNose2D
				? (Nose2D - ShoulderMid2D) / ShoulderSpan2D
				: HeadCenterOffset2D;
			const float HeadLateralAngleDeg = FRotator::NormalizeAxis(
				FMath::RadiansToDegrees(FMath::Atan2(
					NoseShoulderOffset2D.X,
					FMath::Max(-NoseShoulderOffset2D.Y, 0.05f))));
			float EyeRollDeg = 0.0f;
			if (bHasEyes2D)
			{
				const FVector2D EyeRight2D = REye2D - LEye2D;
				if (!EyeRight2D.IsNearlyZero())
				{
					EyeRollDeg = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(EyeRight2D.Y, EyeRight2D.X)));
				}
			}

			float CurrentNoseEyePitch = 0.0f;
			float CurrentMouthEyePitch = 0.0f;
			float CurrentMouthEarPitch = 0.0f;
			float CurrentNoseEarPitch = 0.0f;
			bool bHasNoseEyePitch = false;
			bool bHasMouthEyePitch = false;
			bool bHasMouthEarPitch = false;
			bool bHasNoseEarPitch = false;
			if (bHasEyes2D)
			{
				const FVector2D EyeMid2D = (LEye2D + REye2D) * 0.5f;
				const float EyeSpan2D = FMath::Max(FVector2D::Distance(LEye2D, REye2D), 0.02f);
				if (bHasNose2D)
				{
					CurrentNoseEyePitch = (Nose2D.Y - EyeMid2D.Y) / EyeSpan2D;
					bHasNoseEyePitch = true;
				}
				if (bHasMouth2D)
				{
					const FVector2D Mouth2D = (LMouth2D + RMouth2D) * 0.5f;
					CurrentMouthEyePitch = (Mouth2D.Y - EyeMid2D.Y) / EyeSpan2D;
					bHasMouthEyePitch = true;
				}
			}
			if (bHasEars2D && bHasMouth2D)
			{
				const FVector2D EarMid2D = (LEar2D + REar2D) * 0.5f;
				const float EarSpan2D = FMath::Max(FVector2D::Distance(LEar2D, REar2D), 0.02f);
				const FVector2D Mouth2D = (LMouth2D + RMouth2D) * 0.5f;
				CurrentMouthEarPitch = (Mouth2D.Y - EarMid2D.Y) / EarSpan2D;
				bHasMouthEarPitch = true;
			}
			if (bHasEars2D && bHasNose2D)
			{
				const FVector2D EarMid2D = (LEar2D + REar2D) * 0.5f;
				const float EarSpan2D = FMath::Max(FVector2D::Distance(LEar2D, REar2D), 0.02f);
				CurrentNoseEarPitch = (Nose2D.Y - EarMid2D.Y) / EarSpan2D;
				bHasNoseEarPitch = true;
			}
			ScreenHeadNoseEyePitchProxy = CurrentNoseEyePitch;
			ScreenHeadMouthEyePitchProxy = CurrentMouthEyePitch;
			ScreenHeadMouthEarPitchProxy = CurrentMouthEarPitch;
			ScreenHeadNoseEarPitchProxy = CurrentNoseEarPitch;

			float CurrentWorldMouthEyePitch = 0.0f;
			float CurrentWorldNoseEyePitch = 0.0f;
			float CurrentWorldMouthEarPitch = 0.0f;
			float CurrentWorldNoseEarPitch = 0.0f;
			bool bHasWorldMouthEyePitch = false;
			bool bHasWorldNoseEyePitch = false;
			bool bHasWorldMouthEarPitch = false;
			bool bHasWorldNoseEarPitch = false;
			const FVector FaceUpWorld = UpWorld.GetSafeNormal();
			if (!FaceUpWorld.IsNearlyZero())
			{
				if (bHasLeftEye && bHasRightEye)
				{
					const FVector EyeMidWorld = (LeftEyeWorld + RightEyeWorld) * 0.5f;
					const float EyeSpanWorld = FMath::Max(FVector::Distance(LeftEyeWorld, RightEyeWorld), 0.02f);
					if (bHasLeftMouth && bHasRightMouth)
					{
						const FVector MouthWorld = (LeftMouthWorld + RightMouthWorld) * 0.5f;
						CurrentWorldMouthEyePitch = FVector::DotProduct(MouthWorld - EyeMidWorld, FaceUpWorld) / EyeSpanWorld;
						bHasWorldMouthEyePitch = FMath::IsFinite(CurrentWorldMouthEyePitch);
					}
					if (bHasNose)
					{
						CurrentWorldNoseEyePitch = FVector::DotProduct(NoseWorld - EyeMidWorld, FaceUpWorld) / EyeSpanWorld;
						bHasWorldNoseEyePitch = FMath::IsFinite(CurrentWorldNoseEyePitch);
					}
				}
				if (bHasLeftEar && bHasRightEar)
				{
					const FVector EarMidWorld = (LeftEarWorld + RightEarWorld) * 0.5f;
					const float EarSpanWorld = FMath::Max(FVector::Distance(LeftEarWorld, RightEarWorld), 0.02f);
					if (bHasLeftMouth && bHasRightMouth)
					{
						const FVector MouthWorld = (LeftMouthWorld + RightMouthWorld) * 0.5f;
						CurrentWorldMouthEarPitch = FVector::DotProduct(MouthWorld - EarMidWorld, FaceUpWorld) / EarSpanWorld;
						bHasWorldMouthEarPitch = FMath::IsFinite(CurrentWorldMouthEarPitch);
					}
					if (bHasNose)
					{
						CurrentWorldNoseEarPitch = FVector::DotProduct(NoseWorld - EarMidWorld, FaceUpWorld) / EarSpanWorld;
						bHasWorldNoseEarPitch = FMath::IsFinite(CurrentWorldNoseEarPitch);
					}
				}
			}
			HeadWorldMouthEyePitchProxy = CurrentWorldMouthEyePitch;
			HeadWorldNoseEyePitchProxy = CurrentWorldNoseEyePitch;
			HeadWorldMouthEarPitchProxy = CurrentWorldMouthEarPitch;
			HeadWorldNoseEarPitchProxy = CurrentWorldNoseEarPitch;
			float CurrentWorldForwardPitchDeg = 0.0f;
			bool bHasWorldForwardPitch = false;
			const FVector ForwardPitchRightWorld = HeadRightWorld.GetSafeNormal();
			const FVector ForwardPitchUpWorld = HeadUpWorld.GetSafeNormal();
			const FVector ForwardPitchForwardWorld = HeadForwardWorld.GetSafeNormal();
			if (!ForwardPitchRightWorld.IsNearlyZero() && !ForwardPitchUpWorld.IsNearlyZero() && !ForwardPitchForwardWorld.IsNearlyZero())
			{
				FVector ForwardNoRight = ForwardPitchForwardWorld -
					FVector::DotProduct(ForwardPitchForwardWorld, ForwardPitchRightWorld) * ForwardPitchRightWorld;
				ForwardNoRight.Normalize();
				FVector BodyForwardWorld = FVector::CrossProduct(ForwardPitchRightWorld, ForwardPitchUpWorld).GetSafeNormal();
				if (!ForwardNoRight.IsNearlyZero() && !BodyForwardWorld.IsNearlyZero())
				{
					if (FVector::DotProduct(ForwardNoRight, BodyForwardWorld) < 0.0f)
					{
						BodyForwardWorld *= -1.0f;
					}
					CurrentWorldForwardPitchDeg = FMath::RadiansToDegrees(FMath::Atan2(
						FVector::DotProduct(ForwardNoRight, ForwardPitchUpWorld),
						FMath::Max(FMath::Abs(FVector::DotProduct(ForwardNoRight, BodyForwardWorld)), KINDA_SMALL_NUMBER)));
					bHasWorldForwardPitch = FMath::IsFinite(CurrentWorldForwardPitchDeg);
				}
			}
			HeadWorldForwardPitchProxyDeg = CurrentWorldForwardPitchDeg;

			FDerivedSignalRuntimeState& DerivedSignals = GetDerivedSignalRuntimeState(RuntimeStateKey);
			ScreenHeadHadReference = DerivedSignals.bHasHeadScreenReference ? 1 : 0;
			if (!DerivedSignals.bHasHeadScreenReference)
			{
				ScreenHeadInitializedReference = 1;
				DerivedSignals.bHasHeadScreenReference = true;
				DerivedSignals.HeadScreenCenterReference = HeadCenterOffset2D;
				DerivedSignals.HeadScreenNoseReference = NoseOffset2D;
				DerivedSignals.HeadScreenShoulderNoseReference = NoseShoulderOffset2D;
				DerivedSignals.HeadScreenNoseEyePitchReference = CurrentNoseEyePitch;
				DerivedSignals.HeadScreenMouthEyePitchReference = CurrentMouthEyePitch;
				DerivedSignals.HeadScreenMouthEarPitchReference = CurrentMouthEarPitch;
				DerivedSignals.HeadScreenNoseEarPitchReference = CurrentNoseEarPitch;
				DerivedSignals.HeadWorldMouthEyePitchReference = CurrentWorldMouthEyePitch;
				DerivedSignals.HeadWorldNoseEyePitchReference = CurrentWorldNoseEyePitch;
				DerivedSignals.HeadWorldMouthEarPitchReference = CurrentWorldMouthEarPitch;
				DerivedSignals.HeadWorldNoseEarPitchReference = CurrentWorldNoseEarPitch;
				DerivedSignals.HeadWorldForwardPitchReferenceDeg = CurrentWorldForwardPitchDeg;
				DerivedSignals.HeadScreenLateralAngleReferenceDeg = HeadLateralAngleDeg;
				DerivedSignals.HeadScreenRollReferenceDeg = EyeRollDeg;
			}

			const FVector2D CenterDelta2D = HeadCenterOffset2D - DerivedSignals.HeadScreenCenterReference;
			const FVector2D NoseDelta2D = NoseOffset2D - DerivedSignals.HeadScreenNoseReference;
			const FVector2D ShoulderNoseDelta2D = NoseShoulderOffset2D - DerivedSignals.HeadScreenShoulderNoseReference;
			const float NoseEyePitchDelta = bHasNoseEyePitch ? CurrentNoseEyePitch - DerivedSignals.HeadScreenNoseEyePitchReference : 0.0f;
			const float MouthEyePitchDelta = bHasMouthEyePitch ? CurrentMouthEyePitch - DerivedSignals.HeadScreenMouthEyePitchReference : 0.0f;
			const float MouthEarPitchDelta = bHasMouthEarPitch ? CurrentMouthEarPitch - DerivedSignals.HeadScreenMouthEarPitchReference : 0.0f;
			const float NoseEarPitchDelta = bHasNoseEarPitch ? CurrentNoseEarPitch - DerivedSignals.HeadScreenNoseEarPitchReference : 0.0f;
			const float WorldMouthEyePitchDelta = bHasWorldMouthEyePitch ? CurrentWorldMouthEyePitch - DerivedSignals.HeadWorldMouthEyePitchReference : 0.0f;
			const float WorldNoseEyePitchDelta = bHasWorldNoseEyePitch ? CurrentWorldNoseEyePitch - DerivedSignals.HeadWorldNoseEyePitchReference : 0.0f;
			const float WorldMouthEarPitchDelta = bHasWorldMouthEarPitch ? CurrentWorldMouthEarPitch - DerivedSignals.HeadWorldMouthEarPitchReference : 0.0f;
			const float WorldNoseEarPitchDelta = bHasWorldNoseEarPitch ? CurrentWorldNoseEarPitch - DerivedSignals.HeadWorldNoseEarPitchReference : 0.0f;
			const float WorldForwardPitchDeltaDeg = bHasWorldForwardPitch
				? FRotator::NormalizeAxis(CurrentWorldForwardPitchDeg - DerivedSignals.HeadWorldForwardPitchReferenceDeg)
				: 0.0f;
			ScreenHeadNoseEyePitchDelta = NoseEyePitchDelta;
			ScreenHeadMouthEyePitchDelta = MouthEyePitchDelta;
			ScreenHeadMouthEarPitchDelta = MouthEarPitchDelta;
			ScreenHeadNoseEarPitchDelta = NoseEarPitchDelta;
			HeadWorldMouthEyePitchDelta = WorldMouthEyePitchDelta;
			HeadWorldNoseEyePitchDelta = WorldNoseEyePitchDelta;
			HeadWorldMouthEarPitchDelta = WorldMouthEarPitchDelta;
			HeadWorldNoseEarPitchDelta = WorldNoseEarPitchDelta;
			HeadWorldForwardPitchDeltaDeg = WorldForwardPitchDeltaDeg;
			ScreenHeadCenterDelta2D = CenterDelta2D;
			ScreenHeadNoseDelta2D = NoseDelta2D;
			ScreenHeadShoulderNoseDelta2D = ShoulderNoseDelta2D;
			ScreenHeadShoulderNoseAbs2D = NoseShoulderOffset2D;
			const float LateralAngleDeltaDeg = FRotator::NormalizeAxis(HeadLateralAngleDeg - DerivedSignals.HeadScreenLateralAngleReferenceDeg);
			ScreenHeadLateralAngleDeltaDeg = LateralAngleDeltaDeg;
			const float RollDeltaDeg = FRotator::NormalizeAxis(EyeRollDeg - DerivedSignals.HeadScreenRollReferenceDeg);
			const float ScreenPitchWeight = bHasDenseFaceSolvePoints ? 0.0f : FaceBlendWeight;
			const float ScreenYawRollWeight = bHasDenseFaceSolvePoints ? 0.0f : FaceBlendWeight * FaceTwistWeight;
			const float ShoulderNoseYawInput = FMath::Abs(ShoulderNoseDelta2D.X) > 0.005f
				? ShoulderNoseDelta2D.X
				: NoseShoulderOffset2D.X;
			float FacePitchInput = 0.0f;
			const auto ResolveEarPitchInput = [&]() -> float
			{
				if (bHasWorldNoseEarPitch)
				{
					return WorldNoseEarPitchDelta;
				}
				if (bHasWorldMouthEarPitch)
				{
					return WorldMouthEarPitchDelta;
				}
				if (bHasNoseEarPitch)
				{
					return NoseEarPitchDelta;
				}
				if (bHasMouthEarPitch)
				{
					return MouthEarPitchDelta;
				}
				return 0.0f;
			};
			if (bHasWorldMouthEyePitch)
			{
				FacePitchInput = WorldMouthEyePitchDelta;
			}
			else if (bHasWorldNoseEyePitch)
			{
				FacePitchInput = WorldNoseEyePitchDelta;
			}
			else if (bHasNoseEarPitch)
			{
				FacePitchInput = NoseEarPitchDelta;
			}
			else if (bHasMouthEyePitch && bHasNoseEyePitch)
			{
				const bool bFacePitchDeltasAgree =
					MouthEyePitchDelta * NoseEyePitchDelta >= 0.0f;
				FacePitchInput = bFacePitchDeltasAgree
					? (MouthEyePitchDelta + NoseEyePitchDelta) * 0.5f
					: ResolveEarPitchInput();
			}
			else if (bHasMouthEyePitch)
			{
				FacePitchInput = MouthEyePitchDelta;
			}
			else if (bHasNoseEyePitch)
			{
				FacePitchInput = NoseEyePitchDelta;
			}
			else
			{
				FacePitchInput = MouthEarPitchDelta;
			}

			const float FacePitchOutputGainDegrees = 65.0f * FacePitchScale;
			ScreenHeadFacePitchInput = FacePitchInput;
			ScreenHeadYawDeg = (LateralAngleDeltaDeg * 1.2f + ShoulderNoseYawInput * 25.0f + NoseDelta2D.X * 18.0f + CenterDelta2D.X * 12.0f) * ScreenYawRollWeight;
			const float FacePitchDeg = FacePitchInput * FacePitchOutputGainDegrees;
			ScreenHeadPitchDeg = FacePitchDeg * ScreenPitchWeight;
			ScreenHeadSideBendDeg = (LateralAngleDeltaDeg * 0.95f + ShoulderNoseYawInput * 120.0f + CenterDelta2D.X * 35.0f) * ScreenYawRollWeight;
			ScreenHeadRollDeg = RollDeltaDeg * ScreenYawRollWeight + ScreenHeadSideBendDeg;

			FVector ScreenUpComp = CompUp.GetSafeNormal();
			if (ScreenUpComp.IsNearlyZero())
			{
				ScreenUpComp = FVector::UpVector;
			}
			FVector ScreenRightComp = ChestRightComp.GetSafeNormal();
			if (ScreenRightComp.IsNearlyZero())
			{
				ScreenRightComp = FVector::RightVector;
			}
			FVector ScreenForwardComp = PoseFwdComp.GetSafeNormal();
			if (ScreenForwardComp.IsNearlyZero())
			{
				ScreenForwardComp = FVector::ForwardVector;
			}

			const float ReferenceAlpha = HalfLifeToAlpha(12.0f, DeltaSeconds);
			DerivedSignals.HeadScreenCenterReference = FMath::Lerp(DerivedSignals.HeadScreenCenterReference, HeadCenterOffset2D, ReferenceAlpha);
			DerivedSignals.HeadScreenNoseReference = FMath::Lerp(DerivedSignals.HeadScreenNoseReference, NoseOffset2D, ReferenceAlpha);
			DerivedSignals.HeadScreenShoulderNoseReference = FMath::Lerp(DerivedSignals.HeadScreenShoulderNoseReference, NoseShoulderOffset2D, ReferenceAlpha);
			if (bHasNoseEyePitch)
			{
				DerivedSignals.HeadScreenNoseEyePitchReference += NoseEyePitchDelta * ReferenceAlpha;
			}
			if (bHasMouthEyePitch)
			{
				DerivedSignals.HeadScreenMouthEyePitchReference += MouthEyePitchDelta * ReferenceAlpha;
			}
			if (bHasMouthEarPitch)
			{
				DerivedSignals.HeadScreenMouthEarPitchReference += MouthEarPitchDelta * ReferenceAlpha;
			}
			if (bHasNoseEarPitch)
			{
				DerivedSignals.HeadScreenNoseEarPitchReference += NoseEarPitchDelta * ReferenceAlpha;
			}
			if (bHasWorldMouthEyePitch)
			{
				DerivedSignals.HeadWorldMouthEyePitchReference += WorldMouthEyePitchDelta * ReferenceAlpha;
			}
			if (bHasWorldNoseEyePitch)
			{
				DerivedSignals.HeadWorldNoseEyePitchReference += WorldNoseEyePitchDelta * ReferenceAlpha;
			}
			if (bHasWorldMouthEarPitch)
			{
				DerivedSignals.HeadWorldMouthEarPitchReference += WorldMouthEarPitchDelta * ReferenceAlpha;
			}
			if (bHasWorldNoseEarPitch)
			{
				DerivedSignals.HeadWorldNoseEarPitchReference += WorldNoseEarPitchDelta * ReferenceAlpha;
			}
			if (bHasWorldForwardPitch)
			{
				DerivedSignals.HeadWorldForwardPitchReferenceDeg = FRotator::NormalizeAxis(
					DerivedSignals.HeadWorldForwardPitchReferenceDeg + WorldForwardPitchDeltaDeg * ReferenceAlpha);
			}
			DerivedSignals.HeadScreenLateralAngleReferenceDeg = FRotator::NormalizeAxis(
				DerivedSignals.HeadScreenLateralAngleReferenceDeg +
				FRotator::NormalizeAxis(HeadLateralAngleDeg - DerivedSignals.HeadScreenLateralAngleReferenceDeg) * ReferenceAlpha);
			DerivedSignals.HeadScreenRollReferenceDeg = FMath::Lerp(DerivedSignals.HeadScreenRollReferenceDeg, EyeRollDeg, ReferenceAlpha);
		}
	}
	if (bHasDenseFaceSolvePoints)
	{
		const FVector2D EyeVec = DenseFaceRightEye - DenseFaceLeftEye;
		const float EyeSpan = EyeVec.Size();
		if (EyeSpan > KINDA_SMALL_NUMBER)
		{
			const FVector2D EyeMid = (DenseFaceLeftEye + DenseFaceRightEye) * 0.5f;
			const float DenseFacePitch = (DenseFaceChin.Y - EyeMid.Y) / EyeSpan;
			const float DenseFaceYaw = (DenseFaceNose.X - EyeMid.X) / EyeSpan;
			const float DenseFaceRollDeg = FMath::RadiansToDegrees(FMath::Atan2(EyeVec.Y, EyeVec.X));
			bDenseFaceSignalValid = true;
			DenseFacePitchSignal = DenseFacePitch;
			DenseFaceYawSignal = DenseFaceYaw;
			DenseFaceRollSignalDeg = DenseFaceRollDeg;

			FDerivedSignalRuntimeState& DerivedSignals = GetDerivedSignalRuntimeState(RuntimeStateKey);
			const bool bHadDenseFaceReference = DerivedSignals.bHasDenseFaceReference;
			const float DenseFacePitchReference = bHadDenseFaceReference ? DerivedSignals.DenseFacePitchReference : DenseFacePitch;
			const float DenseFaceYawReference = bHadDenseFaceReference ? DerivedSignals.DenseFaceYawReference : DenseFaceYaw;
			const float DenseFaceRollReferenceDeg = bHadDenseFaceReference ? DerivedSignals.DenseFaceRollReferenceDeg : DenseFaceRollDeg;
			bool bDenseFaceSampleAcceptedForReference = false;

			const float RawDensePitchDelta = DenseFacePitch - DenseFacePitchReference;
			const float DensePitchDelta = RawDensePitchDelta;
			const float DensePitchGainDegrees = 95.0f * FacePitchScale;

			const float DenseYawDelta = DenseFaceYaw - DenseFaceYawReference;
			const float DenseRollDeltaDeg = FRotator::NormalizeAxis(DenseFaceRollDeg - DenseFaceRollReferenceDeg);
			DenseFacePitchDeltaSignal = DensePitchDelta;
			DenseFaceYawDeltaSignal = DenseYawDelta;
			DenseFaceRollDeltaSignalDeg = DenseRollDeltaDeg;
			const float DensePitchDeg = DensePitchDelta * DensePitchGainDegrees;
			const float DenseYawDeg = DenseYawDelta * 70.0f;
			const float DenseRollDeg = DenseRollDeltaDeg * 1.10f;

			const float DensePitchBlend = FaceBlendWeight;
			const float DenseYawRollBlend = FaceBlendWeight * FaceTwistWeight;
			DenseHeadPitchAppliedDeg = DensePitchDeg * DensePitchBlend;
			DenseHeadYawAppliedDeg = DenseYawDeg * DenseYawRollBlend;
			DenseHeadRollAppliedDeg = DenseRollDeg * DenseYawRollBlend;
			DenseHeadLocalPitchDeg = -DenseHeadPitchAppliedDeg;
			DenseHeadLocalYawDeg = DenseHeadYawAppliedDeg;
			DenseHeadLocalRollDeg = DenseHeadRollAppliedDeg;

			FVector FaceRightEyeMp = FVector::ZeroVector;
			FVector FaceLeftEyeMp = FVector::ZeroVector;
			FVector FaceNoseMp = FVector::ZeroVector;
			FVector FaceChinMp = FVector::ZeroVector;
			FVector FaceForeheadMp = FVector::ZeroVector;
			const bool bHasDenseFaceBasisPoints =
				TryGetDenseFaceMp(33, FaceRightEyeMp) &&
				TryGetDenseFaceMp(263, FaceLeftEyeMp) &&
				TryGetDenseFaceMp(152, FaceChinMp) &&
				TryGetDenseFaceMp(10, FaceForeheadMp);
			const bool bHasDenseFaceNosePoint = TryGetDenseFaceMp(1, FaceNoseMp);
			if (bHasDenseFaceBasisPoints)
			{
				auto FaceMpDeltaToComponentRaw = [&](const FVector& MpDelta) -> FVector
				{
					const FVector SourceLocal =
						MediaPipePoseCoordinate::MpWorldToUeLocalUnscaled(MpDelta, bMirrorLandmarksLR);
					const FVector SourceWorld = PoseToWorldTransform.TransformVectorNoScale(SourceLocal);
					return TargetCompTransform.InverseTransformVectorNoScale(SourceWorld);
				};

				const FVector FaceRightRawComp = FaceMpDeltaToComponentRaw(FaceRightEyeMp - FaceLeftEyeMp);
				const FVector FaceUpRawComp = FaceMpDeltaToComponentRaw(FaceForeheadMp - FaceChinMp);
				const float FaceEyeSpanComp = FaceRightRawComp.Size();
				const float FaceVerticalSpanComp = FaceUpRawComp.Size();
				const bool bDenseFaceGeometryPlausible =
					FMath::IsFinite(FaceEyeSpanComp) &&
					FMath::IsFinite(FaceVerticalSpanComp) &&
					FaceEyeSpanComp > 1.0e-4f &&
					FaceVerticalSpanComp > FaceEyeSpanComp * 0.35f &&
					FaceVerticalSpanComp < FaceEyeSpanComp * 5.0f;
				const FVector FaceRightComp = FaceRightRawComp.GetSafeNormal();
				const FVector FaceUpComp = FaceUpRawComp.GetSafeNormal();
				const FVector FaceCenterMp = (FaceRightEyeMp + FaceLeftEyeMp + FaceForeheadMp + FaceChinMp) * 0.25f;
				FVector FaceForwardHintComp = bHasDenseFaceNosePoint
					? FaceMpDeltaToComponentRaw(FaceNoseMp - FaceCenterMp).GetSafeNormal()
					: FVector::ZeroVector;
				const FVector PreviousDenseForwardComp = DerivedSignals.bHasValidDenseHeadTargetBasis
					? DerivedSignals.LastDenseHeadTargetBasis.GetAxisX().GetSafeNormal()
					: FVector::ZeroVector;
				if (FaceForwardHintComp.IsNearlyZero() && !PreviousDenseForwardComp.IsNearlyZero())
				{
					FaceForwardHintComp = PreviousDenseForwardComp;
				}
				else if (FaceForwardHintComp.IsNearlyZero())
				{
					FaceForwardHintComp = ChestTargetBasis.GetAxisX().GetSafeNormal();
				}
				if (bDenseFaceGeometryPlausible && !FaceRightComp.IsNearlyZero() && !FaceUpComp.IsNearlyZero() && !FaceForwardHintComp.IsNearlyZero())
				{
					const FQuat DenseFaceBasisComp = MakeBasis(FaceRightComp, FaceUpComp, FaceForwardHintComp);
					const FQuat DenseFaceRelativeBasis = (DenseFaceBasisComp * ChestTargetBasis.Inverse()).GetNormalized();
					FQuat DenseFaceRelativeBasisReference = DerivedSignals.DenseFaceRelativeBasisReference;
					const bool bInitializeDenseFaceRelativeBasisReference = !DerivedSignals.bHasDenseFaceRelativeBasisReference;
					if (bInitializeDenseFaceRelativeBasisReference)
					{
						DenseFaceRelativeBasisReference = DenseFaceRelativeBasis;
					}

					FQuat DenseFaceRelativeDelta =
						(DenseFaceRelativeBasis * DenseFaceRelativeBasisReference.Inverse()).GetNormalized();
					const FVector DensePitchAxisComp = ChestTargetBasis.GetAxisY().GetSafeNormal();
					if (!DensePitchAxisComp.IsNearlyZero())
					{
						FQuat DenseNonPitchSwing = FQuat::Identity;
						FQuat DensePitchTwist = FQuat::Identity;
						DenseFaceRelativeDelta.ToSwingTwist(DensePitchAxisComp, DenseNonPitchSwing, DensePitchTwist);
						DenseFaceRelativeDelta = (DenseNonPitchSwing * DensePitchTwist.Inverse()).GetNormalized();
					}
					const FQuat DenseFaceRelativeDeltaScaled =
						FQuat::Slerp(FQuat::Identity, DenseFaceRelativeDelta, FaceBlendWeight).GetNormalized();
					const FQuat CandidateHeadTargetBasis = (DenseFaceRelativeDeltaScaled * ChestTargetBasis).GetNormalized();
					bool bAcceptDenseHeadTarget = true;
					if (DerivedSignals.bHasValidDenseHeadTargetBasis)
					{
						const float CandidateJumpDeg =
							QuatAngularDistanceDegrees(CandidateHeadTargetBasis, DerivedSignals.LastDenseHeadTargetBasis);
						bAcceptDenseHeadTarget = CandidateJumpDeg <= 75.0f;
					}
					if (bAcceptDenseHeadTarget)
					{
						if (bInitializeDenseFaceRelativeBasisReference)
						{
							DerivedSignals.bHasDenseFaceRelativeBasisReference = true;
							DerivedSignals.DenseFaceRelativeBasisReference = DenseFaceRelativeBasisReference;
						}
						HeadTargetBasis = CandidateHeadTargetBasis;
						bDenseHeadLocalTargetValid = true;
						bDenseFaceSampleAcceptedForReference = true;
						DerivedSignals.bHasValidDenseHeadLocalEuler = true;
						DerivedSignals.LastDenseHeadLocalPitchDeg = DenseHeadLocalPitchDeg;
						DerivedSignals.LastDenseHeadLocalYawDeg = DenseHeadLocalYawDeg;
						DerivedSignals.LastDenseHeadLocalRollDeg = DenseHeadLocalRollDeg;
						DerivedSignals.bHasValidDenseHeadTargetBasis = true;
						DerivedSignals.LastDenseHeadTargetBasis = HeadTargetBasis;
						DerivedSignals.LastDenseHeadTargetPoseTimestampUs = bHasPoseFrame ? PoseFrame.TimestampUs : -1;
					}
				}
			}

			if (bDenseFaceSampleAcceptedForReference)
			{
				if (!bHadDenseFaceReference)
				{
					DerivedSignals.bHasDenseFaceReference = true;
					DerivedSignals.DenseFacePitchReference = DenseFacePitch;
					DerivedSignals.DenseFaceYawReference = DenseFaceYaw;
					DerivedSignals.DenseFaceRollReferenceDeg = DenseFaceRollDeg;
				}
				else
				{
					const float DenseReferenceAlpha = HalfLifeToAlpha(14.0f, DeltaSeconds);
					DerivedSignals.DenseFacePitchReference += RawDensePitchDelta * DenseReferenceAlpha;
					DerivedSignals.DenseFaceYawReference += DenseYawDelta * DenseReferenceAlpha;
					DerivedSignals.DenseFaceRollReferenceDeg = FRotator::NormalizeAxis(
						DerivedSignals.DenseFaceRollReferenceDeg + DenseRollDeltaDeg * DenseReferenceAlpha);
				}
			}
		}
	}
	if (!bDenseHeadLocalTargetValid && bUseHolisticHeadSolve)
	{
		FDerivedSignalRuntimeState& DerivedSignals = GetDerivedSignalRuntimeState(RuntimeStateKey);
		constexpr int64 DenseHeadTargetHoldUs = 1200000;
		const int64 CurrentPoseTimestampUs = bHasPoseFrame ? PoseFrame.TimestampUs : -1;
		const bool bWithinDenseHeadHold =
			CurrentPoseTimestampUs > 0 &&
			DerivedSignals.LastDenseHeadTargetPoseTimestampUs > 0 &&
			CurrentPoseTimestampUs >= DerivedSignals.LastDenseHeadTargetPoseTimestampUs &&
			CurrentPoseTimestampUs - DerivedSignals.LastDenseHeadTargetPoseTimestampUs <= DenseHeadTargetHoldUs;
		if (DerivedSignals.bHasValidDenseHeadTargetBasis && bWithinDenseHeadHold)
		{
			HeadTargetBasis = DerivedSignals.LastDenseHeadTargetBasis;
			DenseHeadLocalPitchDeg = DerivedSignals.LastDenseHeadLocalPitchDeg;
			DenseHeadLocalYawDeg = DerivedSignals.LastDenseHeadLocalYawDeg;
			DenseHeadLocalRollDeg = DerivedSignals.LastDenseHeadLocalRollDeg;
			bDenseHeadLocalTargetValid = true;
			bHoldingDenseHeadLocalTarget = true;
		}
	}
	const FQuat NeckTargetBasis = FQuat::Slerp(ChestTargetBasis, HeadTargetBasis, 0.5f).GetNormalized();
	const FQuat Neck02TargetBasis = FQuat::Slerp(NeckTargetBasis, HeadTargetBasis, 0.5f).GetNormalized();

	auto ApplyFaceDeltaFromChest = [&](const FBoneReference& Bone, const FQuat& RefBoneComp, const FQuat& RefBasisComp,
		const FQuat& TargetBasisComp, const float SwingWeight, const float TwistWeight,
		bool& bHasSmoothedRot, FQuat& InOutSmoothedRotCS)
	{
		if (!Bone.IsValidToEvaluate() || ChestTargetBasis.IsIdentity() || TargetBasisComp.IsIdentity() || RefBasisComp.IsIdentity())
		{
			return;
		}

		FVector TwistAxis = CompUp.GetSafeNormal();
		if (TwistAxis.IsNearlyZero())
		{
			TwistAxis = FVector::UpVector;
		}

		const FQuat BaseTargetRotCS = ((ChestTargetBasis * RefBasisComp.Inverse()) * RefBoneComp).GetNormalized();
		const FQuat FaceDeltaCS = (TargetBasisComp * ChestTargetBasis.Inverse()).GetNormalized();
		FQuat Swing = FQuat::Identity;
		FQuat Twist = FQuat::Identity;
		FaceDeltaCS.ToSwingTwist(TwistAxis, Swing, Twist);

		const FQuat SwingScaled = FQuat::Slerp(FQuat::Identity, Swing, SwingWeight).GetNormalized();
		const FQuat TwistScaled = FQuat::Slerp(FQuat::Identity, Twist, TwistWeight).GetNormalized();
		const FQuat TargetRotCS = (SwingScaled * TwistScaled * BaseTargetRotCS).GetNormalized();

		bHasSmoothedRot = true;
		InOutSmoothedRotCS = TargetRotCS;
		ApplyRotationCS(CSPose, Bone, TargetRotCS);
	};
	if (bDenseHeadLocalTargetValid)
	{
		ApplyFaceDeltaFromChest(Neck, RefNeckComp, RefNeckBasisComp, NeckTargetBasis, 0.45f, 0.25f * FaceTwistWeight, BodyState.bHasSmoothedNeckRotCS, BodyState.SmoothedNeckRotCS);
		ApplyFaceDeltaFromChest(Neck02, RefNeck02Comp, RefNeck02BasisComp, Neck02TargetBasis, 0.75f, 0.50f * FaceTwistWeight, BodyState.bHasSmoothedNeck02RotCS, BodyState.SmoothedNeck02RotCS);
	}
	else
	{
		BodyState.bHasSmoothedNeckRotCS = false;
		BodyState.bHasSmoothedNeck02RotCS = false;
	}
	ApplyFaceDeltaFromChest(Head, RefHeadComp, RefHeadBasisComp, HeadTargetBasis, 1.0f, FaceTwistWeight, BodyState.bHasSmoothedHeadRotCS, BodyState.SmoothedHeadRotCS);
	LatestSignalSnapshot.bValid = true;
	LatestSignalSnapshot.RuntimeStateKey = RuntimeStateKey;
	LatestSignalSnapshot.PoseTimestampUs = bHasPoseFrame ? PoseFrame.TimestampUs : -1;
	LatestSignalSnapshot.Head.bHasDenseFace = bDenseFaceSignalValid;
	LatestSignalSnapshot.Head.bDenseHeadLocalTargetValid = bDenseHeadLocalTargetValid;
	LatestSignalSnapshot.Head.bHoldingDenseHeadLocalTarget = bHoldingDenseHeadLocalTarget;
	LatestSignalSnapshot.Head.DenseFacePitchRatio = DenseFacePitchSignal;
	LatestSignalSnapshot.Head.DenseFaceYawRatio = DenseFaceYawSignal;
	LatestSignalSnapshot.Head.DenseFaceRollDeg = DenseFaceRollSignalDeg;
	LatestSignalSnapshot.Head.DenseFacePitchDelta = DenseFacePitchDeltaSignal;
	LatestSignalSnapshot.Head.DenseFaceYawDelta = DenseFaceYawDeltaSignal;
	LatestSignalSnapshot.Head.DenseFaceRollDeltaDeg = DenseFaceRollDeltaSignalDeg;
	LatestSignalSnapshot.Head.DenseHeadPitchAppliedDeg = DenseHeadPitchAppliedDeg;
	LatestSignalSnapshot.Head.DenseHeadYawAppliedDeg = DenseHeadYawAppliedDeg;
	LatestSignalSnapshot.Head.DenseHeadRollAppliedDeg = DenseHeadRollAppliedDeg;
	LatestSignalSnapshot.Head.DenseHeadLocalPitchDeg = DenseHeadLocalPitchDeg;
	LatestSignalSnapshot.Head.DenseHeadLocalYawDeg = DenseHeadLocalYawDeg;
	LatestSignalSnapshot.Head.DenseHeadLocalRollDeg = DenseHeadLocalRollDeg;
	LatestSignalSnapshot.Head.ScreenPitchDeg = ScreenHeadPitchDeg;
	LatestSignalSnapshot.Head.ScreenYawDeg = ScreenHeadYawDeg;
	LatestSignalSnapshot.Head.ScreenRollDeg = ScreenHeadRollDeg;
	LatestSignalSnapshot.Head.ScreenLateralAngleDeltaDeg = ScreenHeadLateralAngleDeltaDeg;
	LatestSignalSnapshot.Head.ScreenSideBendDeg = ScreenHeadSideBendDeg;
	LatestSignalSnapshot.Head.ScreenFacePitchInput = ScreenHeadFacePitchInput;
	LatestSignalSnapshot.Head.ScreenCenterDeltaX = ScreenHeadCenterDelta2D.X;
	LatestSignalSnapshot.Head.ScreenCenterDeltaY = ScreenHeadCenterDelta2D.Y;
	LatestSignalSnapshot.Head.ScreenNoseDeltaX = ScreenHeadNoseDelta2D.X;
	LatestSignalSnapshot.Head.ScreenNoseDeltaY = ScreenHeadNoseDelta2D.Y;
	LatestSignalSnapshot.Head.ScreenShoulderNoseDeltaX = ScreenHeadShoulderNoseDelta2D.X;
	LatestSignalSnapshot.Head.ScreenShoulderNoseDeltaY = ScreenHeadShoulderNoseDelta2D.Y;
	LatestSignalSnapshot.Head.ScreenShoulderNoseAbsX = ScreenHeadShoulderNoseAbs2D.X;
	LatestSignalSnapshot.Head.ScreenShoulderNoseAbsY = ScreenHeadShoulderNoseAbs2D.Y;
	LatestSignalSnapshot.Head.NoseEyePitchDelta = ScreenHeadNoseEyePitchDelta;
	LatestSignalSnapshot.Head.MouthEyePitchDelta = ScreenHeadMouthEyePitchDelta;
	LatestSignalSnapshot.Head.MouthEarPitchDelta = ScreenHeadMouthEarPitchDelta;
	LatestSignalSnapshot.Head.NoseEarPitchDelta = ScreenHeadNoseEarPitchDelta;
	LatestSignalSnapshot.Head.WorldMouthEyePitchDelta = HeadWorldMouthEyePitchDelta;
	LatestSignalSnapshot.Head.WorldNoseEyePitchDelta = HeadWorldNoseEyePitchDelta;
	LatestSignalSnapshot.Head.WorldMouthEarPitchDelta = HeadWorldMouthEarPitchDelta;
	LatestSignalSnapshot.Head.WorldNoseEarPitchDelta = HeadWorldNoseEarPitchDelta;
	LatestSignalSnapshot.Head.WorldForwardPitchDeltaDeg = HeadWorldForwardPitchDeltaDeg;
	LatestSignalSnapshot.Head.HeadRotationMaxStepDegrees = HeadRotationMaxStepDegrees;
	LatestSignalSnapshot.Head.HeadRotationMaxSpeedDegreesPerSecond = HeadRotationMaxSpeedDegreesPerSecond;
	LatestSignalSnapshot.Head.ComputedPitchDeg = ScreenHeadPitchDeg + DenseHeadPitchAppliedDeg;
	LatestSignalSnapshot.Head.ComputedYawDeg = ScreenHeadYawDeg + DenseHeadYawAppliedDeg;
	LatestSignalSnapshot.Head.ComputedRollDeg = ScreenHeadRollDeg + DenseHeadRollAppliedDeg;

	if (CVarMediaPipeTorsoDebug.GetValueOnAnyThread() != 0)
	{
		const double NowSeconds = FPlatformTime::Seconds();
		if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, DiagnosticsState.LastHeadDiagnosticLogTimeSeconds))
		{
			float EffectiveMaxHeadStepDegrees = HeadRotationMaxStepDegrees;
			if (HeadRotationMaxSpeedDegreesPerSecond > 0.0f && DeltaSeconds > 0.0f)
			{
				const float MaxSpeedStepDegrees = HeadRotationMaxSpeedDegreesPerSecond * DeltaSeconds;
				EffectiveMaxHeadStepDegrees = EffectiveMaxHeadStepDegrees > 0.0f
					? FMath::Min(EffectiveMaxHeadStepDegrees, MaxSpeedStepDegrees)
					: MaxSpeedStepDegrees;
			}

			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.HeadDebug: actor=%s enabled=1 runtimeKey=%u refHad=%d refInit=%d nose=%d ears=%d eyes=%d screen2d=(shoulders=%d nose=%d eyes=%d ears=%d mouth=%d) faceBlend=%.2f twistWeight=%.2f faceFromChestDeg=%.1f screenCenterDelta=(%.3f,%.3f) screenNoseDelta=(%.3f,%.3f) screenShoulderNoseDelta=(%.3f,%.3f) screenShoulderNoseAbs=(%.3f,%.3f) facePitch=%.3f noseEyePitch=%.3f mouthEyePitch=%.3f mouthEarPitch=%.3f noseEarPitch=%.3f worldMouthEyePitch=%.3f worldNoseEyePitch=%.3f worldMouthEarPitch=%.3f worldNoseEarPitch=%.3f worldForwardPitch=%.1f noseEyeProxy=%.3f mouthEyeProxy=%.3f mouthEarProxy=%.3f noseEarProxy=%.3f worldMouthEyeProxy=%.3f worldNoseEyeProxy=%.3f worldMouthEarProxy=%.3f worldNoseEarProxy=%.3f worldForwardPitchProxy=%.1f screenAngleDelta=%.1f screenSideBend=%.1f screenYaw=%.1f screenPitch=%.1f screenRoll=%.1f neckAppliedDeg=%.1f neck02AppliedDeg=%.1f headAppliedDeg=%.1f maxStepDeg=%.1f maxSpeedDegPerSec=%.1f"),
				*TargetActorName.ToString(),
				RuntimeStateKey,
				ScreenHeadHadReference,
				ScreenHeadInitializedReference,
				bHasNose ? 1 : 0,
				(bHasLeftEar && bHasRightEar) ? 1 : 0,
				(bHasLeftEye && bHasRightEye) ? 1 : 0,
				bScreenHasShoulders2D,
				bScreenHasNose2D,
				bScreenHasEyes2D,
				bScreenHasEars2D,
				bScreenHasMouth2D,
				FaceBlendWeight,
				FaceTwistWeight,
				QuatAngularDistanceDegrees(FaceHeadTargetBasis, ChestTargetBasis),
				ScreenHeadCenterDelta2D.X,
				ScreenHeadCenterDelta2D.Y,
				ScreenHeadNoseDelta2D.X,
				ScreenHeadNoseDelta2D.Y,
				ScreenHeadShoulderNoseDelta2D.X,
				ScreenHeadShoulderNoseDelta2D.Y,
				ScreenHeadShoulderNoseAbs2D.X,
				ScreenHeadShoulderNoseAbs2D.Y,
				ScreenHeadFacePitchInput,
				ScreenHeadNoseEyePitchDelta,
				ScreenHeadMouthEyePitchDelta,
				ScreenHeadMouthEarPitchDelta,
				ScreenHeadNoseEarPitchDelta,
				HeadWorldMouthEyePitchDelta,
				HeadWorldNoseEyePitchDelta,
				HeadWorldMouthEarPitchDelta,
				HeadWorldNoseEarPitchDelta,
				HeadWorldForwardPitchDeltaDeg,
				ScreenHeadNoseEyePitchProxy,
				ScreenHeadMouthEyePitchProxy,
				ScreenHeadMouthEarPitchProxy,
				ScreenHeadNoseEarPitchProxy,
				HeadWorldMouthEyePitchProxy,
				HeadWorldNoseEyePitchProxy,
				HeadWorldMouthEarPitchProxy,
				HeadWorldNoseEarPitchProxy,
				HeadWorldForwardPitchProxyDeg,
				ScreenHeadLateralAngleDeltaDeg,
				ScreenHeadSideBendDeg,
				ScreenHeadYawDeg,
				ScreenHeadPitchDeg,
				ScreenHeadRollDeg,
				BodyState.bHasSmoothedNeckRotCS ? QuatAngularDistanceDegrees(BodyState.SmoothedNeckRotCS, RefNeckComp) : 0.0f,
				BodyState.bHasSmoothedNeck02RotCS ? QuatAngularDistanceDegrees(BodyState.SmoothedNeck02RotCS, RefNeck02Comp) : 0.0f,
				BodyState.bHasSmoothedHeadRotCS ? QuatAngularDistanceDegrees(BodyState.SmoothedHeadRotCS, RefHeadComp) : 0.0f,
				EffectiveMaxHeadStepDegrees,
				HeadRotationMaxSpeedDegreesPerSecond);
		}
	}
}
