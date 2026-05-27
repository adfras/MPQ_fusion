#include "MediaPipeQuestWristApplyPolicy.h"

#include "Math/UnrealMathUtility.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestWristApplyPolicyAutomationTest,
	"TestingKit3.MediaPipe.QuestWrist.ApplyPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestWristApplyPolicyAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeQuestWristApplyPolicyInput Input;
	Input.bQuestSideUsable = true;
	Input.bQuestSideTracked = true;
	Input.bRequireTrackedForApply = true;
	TestTrue(TEXT("Tracked usable Quest wrist can be applied when tracked apply is required"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	Input.bQuestSideTracked = false;
	Input.bRequireTrackedForApply = true;
	TestFalse(TEXT("Untracked Quest wrist is rejected when tracked apply is required"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	Input.bAllowUsableUntrackedForPositionApply = true;
	TestFalse(TEXT("Constrained endpoint solve rejects untracked Quest wrist positions without continuity"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	Input.bHasRecentAcceptedLiveWristPosition = true;
	Input.UntrackedLiveWristStepFromLastAcceptedCm = 12.0f;
	Input.LastAcceptedLiveWristAgeSeconds = 0.05f;
	Input.MaxUntrackedLiveWristStepCm = 45.0f;
	Input.MaxUntrackedLiveWristAgeSeconds = 0.35f;
	TestTrue(TEXT("Constrained endpoint solve can consume continuous usable untracked Quest wrist positions"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	Input.UntrackedLiveWristStepFromLastAcceptedCm = 90.0f;
	TestFalse(TEXT("Constrained endpoint solve rejects discontinuous untracked Quest wrist positions"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	Input.UntrackedLiveWristStepFromLastAcceptedCm = 12.0f;
	Input.LastAcceptedLiveWristAgeSeconds = 0.50f;
	TestFalse(TEXT("Constrained endpoint solve rejects stale untracked Quest wrist positions"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	Input.LastAcceptedLiveWristAgeSeconds = 0.05f;
	Input.bQuestSideUsable = false;
	TestFalse(TEXT("Constrained endpoint solve still rejects unusable untracked Quest wrist positions"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	Input.bQuestSideUsable = true;
	Input.bAllowUsableUntrackedForPositionApply = false;
	Input.bHasRecentAcceptedLiveWristPosition = false;
	Input.UntrackedLiveWristStepFromLastAcceptedCm = 0.0f;
	Input.LastAcceptedLiveWristAgeSeconds = 0.0f;
	Input.MaxUntrackedLiveWristStepCm = 0.0f;
	Input.MaxUntrackedLiveWristAgeSeconds = 0.0f;
	Input.bQuestSideTracked = false;
	Input.bRequireTrackedForApply = false;
	TestTrue(TEXT("Untracked but usable Quest wrist remains allowed for legacy tolerant profiles"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	Input.bQuestSideUsable = false;
	Input.bQuestSideTracked = true;
	Input.bRequireTrackedForApply = false;
	TestFalse(TEXT("Tracked but unusable Quest wrist is rejected"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	FMediaPipeQuestHandRotationFramePolicyInput HandRotationInput;
	HandRotationInput.LiveWristPolicy.bQuestSideUsable = true;
	HandRotationInput.LiveWristPolicy.bRequireTrackedForApply = true;
	HandRotationInput.LiveWristPolicy.bAllowUsableUntrackedForPositionApply = true;
	HandRotationInput.LiveWristPolicy.bHasRecentAcceptedLiveWristPosition = true;
	HandRotationInput.LiveWristPolicy.UntrackedLiveWristStepFromLastAcceptedCm = 12.0f;
	HandRotationInput.LiveWristPolicy.LastAcceptedLiveWristAgeSeconds = 0.05f;
	HandRotationInput.LiveWristPolicy.MaxUntrackedLiveWristStepCm = 45.0f;
	HandRotationInput.LiveWristPolicy.MaxUntrackedLiveWristAgeSeconds = 0.35f;
	HandRotationInput.bRequireTrackedForHandRotation = true;
	HandRotationInput.bQuestSideTracked = true;
	TestTrue(TEXT("Tracked Quest hand rotation can consume the current frame"),
		FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame(HandRotationInput));

	HandRotationInput.bQuestSideTracked = false;
	HandRotationInput.bCurrentWristPositionApplied = true;
	HandRotationInput.bCurrentWristMapped = true;
	HandRotationInput.bCurrentWristUsedUntrackedJointData = true;
	TestTrue(TEXT("Untracked Quest hand rotation can follow a live continuous untracked wrist frame"),
		FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame(HandRotationInput));

	HandRotationInput.bCurrentWristUsedHeldTarget = true;
	TestFalse(TEXT("Untracked Quest hand rotation cannot follow a held wrist target"),
		FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame(HandRotationInput));

	HandRotationInput.bCurrentWristUsedHeldTarget = false;
	HandRotationInput.bCurrentWristBodyFallback = true;
	TestFalse(TEXT("Untracked Quest hand rotation cannot follow a body-fallback wrist target"),
		FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame(HandRotationInput));

	HandRotationInput.bCurrentWristBodyFallback = false;
	HandRotationInput.bCurrentWristRawRejected = true;
	TestFalse(TEXT("Untracked Quest hand rotation cannot follow a raw-rejected wrist target"),
		FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame(HandRotationInput));

	HandRotationInput.bCurrentWristRawRejected = false;
	HandRotationInput.LiveWristPolicy.UntrackedLiveWristStepFromLastAcceptedCm = 90.0f;
	TestFalse(TEXT("Untracked Quest hand rotation cannot bypass wrist continuity"),
		FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame(HandRotationInput));

	HandRotationInput.LiveWristPolicy.UntrackedLiveWristStepFromLastAcceptedCm = 12.0f;
	HandRotationInput.bCurrentWristPositionApplied = false;
	TestFalse(TEXT("Untracked Quest hand rotation requires the current wrist frame to be applied"),
		FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame(HandRotationInput));

	FMediaPipeQuestWristPositionAttemptInput AttemptInput;
	AttemptInput.bQuestArmUsesWristEndpoint = true;
	AttemptInput.bQuestArmUsesConstrainedSolve = true;
	AttemptInput.bQuestSideUsable = true;
	AttemptInput.RequestedPositionBlend = 1.0f;
	TestTrue(TEXT("Constrained arm attempts Quest wrist path for usable wrist data even before tracked gating"),
		FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve(AttemptInput));

	AttemptInput.bQuestSideUsable = false;
	AttemptInput.bHasHeldTarget = true;
	TestTrue(TEXT("Constrained arm attempts Quest wrist path for held target continuity during brief loss"),
		FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve(AttemptInput));

	AttemptInput.bHasHeldTarget = false;
	TestFalse(TEXT("Constrained arm does not enter Quest wrist path without usable data or held continuity"),
		FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve(AttemptInput));

	AttemptInput.bQuestSideUsable = true;
	AttemptInput.RequestedPositionBlend = 0.0f;
	TestFalse(TEXT("Quest wrist path stays disabled when position blend is zero"),
		FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve(AttemptInput));

	AttemptInput.RequestedPositionBlend = 1.0f;
	AttemptInput.bQuestArmUsesWristEndpoint = false;
	TestFalse(TEXT("Quest wrist path stays disabled when the arm profile does not use wrist endpoints"),
		FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve(AttemptInput));

	FMediaPipeQuestWristHeldTargetLossInput HeldLossInput;
	HeldLossInput.bHasHeldTarget = false;
	HeldLossInput.GraceSeconds = 0.35f;
	HeldLossInput.LastTargetAgeSeconds = 0.05f;
	TestTrue(TEXT("Missing held target clears stale wrist authority"),
		FMediaPipeQuestWristApplyPolicy::ShouldClearPositionAuthorityForHeldTargetLoss(HeldLossInput));

	HeldLossInput.bHasHeldTarget = true;
	HeldLossInput.GraceSeconds = 0.0f;
	TestTrue(TEXT("Zero grace clears stale wrist authority"),
		FMediaPipeQuestWristApplyPolicy::ShouldClearPositionAuthorityForHeldTargetLoss(HeldLossInput));

	HeldLossInput.GraceSeconds = 0.35f;
	HeldLossInput.LastTargetAgeSeconds = -1.0f;
	TestTrue(TEXT("Unknown held target age clears stale wrist authority"),
		FMediaPipeQuestWristApplyPolicy::ShouldClearPositionAuthorityForHeldTargetLoss(HeldLossInput));

	HeldLossInput.LastTargetAgeSeconds = 0.50f;
	TestTrue(TEXT("Expired held target clears stale wrist authority before reacquisition"),
		FMediaPipeQuestWristApplyPolicy::ShouldClearPositionAuthorityForHeldTargetLoss(HeldLossInput));
	TestFalse(TEXT("Expired held target cannot waive wrist reliability for position attempt"),
		FMediaPipeQuestWristApplyPolicy::HasFreshHeldTargetForPositionAttempt(HeldLossInput));

	HeldLossInput.LastTargetAgeSeconds = 0.10f;
	TestFalse(TEXT("Fresh held target keeps wrist authority available for continuity"),
		FMediaPipeQuestWristApplyPolicy::ShouldClearPositionAuthorityForHeldTargetLoss(HeldLossInput));
	TestTrue(TEXT("Fresh held target can waive wrist reliability for a brief continuity attempt"),
		FMediaPipeQuestWristApplyPolicy::HasFreshHeldTargetForPositionAttempt(HeldLossInput));

	FMediaPipeQuestArmHoldOnLossInput HoldInput;
	HoldInput.bHoldOnQuestHandLossEnabled = true;
	HoldInput.bQuestHandTrackingEnabled = true;
	HoldInput.bQuestSideTracked = false;
	HoldInput.bHasLastReliableArmSample = true;
	HoldInput.bQuestWristPositionCandidate = false;
	TestTrue(TEXT("Arm-loss hold can keep the last reliable arm only when no Quest wrist position candidate exists"),
		FMediaPipeQuestWristApplyPolicy::ShouldHoldArmOnQuestHandLoss(HoldInput));

	HoldInput.bQuestWristPositionCandidate = true;
	TestFalse(TEXT("Quest wrist position candidate suppresses arm-loss hold so endpoint solve owns continuity"),
		FMediaPipeQuestWristApplyPolicy::ShouldHoldArmOnQuestHandLoss(HoldInput));

	HoldInput.bQuestWristPositionCandidate = false;
	HoldInput.bQuestSideTracked = true;
	TestFalse(TEXT("Tracked Quest side does not enter arm-loss hold"),
		FMediaPipeQuestWristApplyPolicy::ShouldHoldArmOnQuestHandLoss(HoldInput));

	HoldInput.bQuestSideTracked = false;
	HoldInput.bHoldOnQuestHandLossEnabled = false;
	TestFalse(TEXT("Disabled arm-loss hold stays disabled"),
		FMediaPipeQuestWristApplyPolicy::ShouldHoldArmOnQuestHandLoss(HoldInput));

	FMediaPipeQuestArmPoseWriteInput PoseWriteInput;
	PoseWriteInput.bUseHmdRelativeAvatarArmFrame = true;
	PoseWriteInput.bQuestWristPositionApplied = true;
	TestTrue(TEXT("HMD-relative Quest wrist endpoint writes the arm pose as one coherent frame"),
		FMediaPipeQuestWristApplyPolicy::ShouldWriteFrameCoherentQuestArmPose(PoseWriteInput));

	PoseWriteInput.bQuestWristPositionApplied = false;
	TestFalse(TEXT("HMD-relative arm without an endpoint keeps the normal MediaPipe smoothing policy"),
		FMediaPipeQuestWristApplyPolicy::ShouldWriteFrameCoherentQuestArmPose(PoseWriteInput));

	PoseWriteInput.bUseHmdRelativeAvatarArmFrame = false;
	PoseWriteInput.bQuestWristPositionApplied = true;
	TestFalse(TEXT("Non-HMD-relative arm profiles keep the normal MediaPipe smoothing policy"),
		FMediaPipeQuestWristApplyPolicy::ShouldWriteFrameCoherentQuestArmPose(PoseWriteInput));

	FMediaPipeQuestArmLengthCalibrationOwnerPolicyInput ArmLengthOwnerInput;
	ArmLengthOwnerInput.bTargetIsMetaHuman = true;
	ArmLengthOwnerInput.bTargetProfileActive = true;
	TestTrue(TEXT("Active MetaHuman profiles still own Quest arm-length calibration"),
		FMediaPipeQuestWristApplyPolicy::ShouldOwnArmLengthCalibration(ArmLengthOwnerInput));
	ArmLengthOwnerInput.bTargetIsMetaHuman = false;
	ArmLengthOwnerInput.bTargetProfileActive = false;
	ArmLengthOwnerInput.bHasTargetEmbodimentProfile = true;
	ArmLengthOwnerInput.bTargetIsMannyLike = true;
	TestTrue(TEXT("Manny-like embodiment profiles own the same Quest arm-length calibration path"),
		FMediaPipeQuestWristApplyPolicy::ShouldOwnArmLengthCalibration(ArmLengthOwnerInput));
	ArmLengthOwnerInput.bTargetIsMannyLike = false;
	TestFalse(TEXT("Unprofiled custom avatars do not implicitly own Quest arm-length calibration"),
		FMediaPipeQuestWristApplyPolicy::ShouldOwnArmLengthCalibration(ArmLengthOwnerInput));

	auto MakeDropoutDownPolicyInput = []()
	{
		FMediaPipeQuestArmDropoutDownFallbackPolicyInput DropoutInput;
		DropoutInput.bEnabled = true;
		DropoutInput.bUseHmdRelativeAvatarArmFrame = true;
		DropoutInput.bQuestArmUsesConstrainedSolve = true;
		DropoutInput.bQuestHandTrackingEnabled = true;
		DropoutInput.bQuestSideTracked = false;
		DropoutInput.bHasOnlyDropoutEndpoint = true;
		DropoutInput.bHasRecentConstrainedArmSolve = true;
		return DropoutInput;
	};

	FMediaPipeQuestArmDropoutDownFallbackPolicyInput DropoutDownInput = MakeDropoutDownPolicyInput();
	DropoutDownInput.bHasRecentTrackedArmPose = true;
	DropoutDownInput.bLastTrackedPoseWasDown = true;
	const FMediaPipeQuestArmDropoutDownFallbackPolicyResult TrackedDownDropout =
		FMediaPipeQuestWristApplyPolicy::ShouldUseDropoutDownFallback(DropoutDownInput);
	TestTrue(TEXT("Dropout-down fallback preserves the existing tracked-down admission path"),
		TrackedDownDropout.bUseFallback && !TrackedDownDropout.bInferCalibratedDownPose);

	DropoutDownInput = MakeDropoutDownPolicyInput();
	DropoutDownInput.bHasAcceptedArmLengthCalibration = true;
	DropoutDownInput.bCanInferCalibratedDownPose = true;
	const FMediaPipeQuestArmDropoutDownFallbackPolicyResult CalibratedDropout =
		FMediaPipeQuestWristApplyPolicy::ShouldUseDropoutDownFallback(DropoutDownInput);
	TestTrue(TEXT("Accepted arm-length calibration can infer by-side full reach after Quest endpoint collapse"),
		CalibratedDropout.bUseFallback && CalibratedDropout.bInferCalibratedDownPose);

	DropoutDownInput = MakeDropoutDownPolicyInput();
	DropoutDownInput.bHasAcceptedArmLengthCalibration = true;
	DropoutDownInput.bCanInferCalibratedDownPose = false;
	const FMediaPipeQuestArmDropoutDownFallbackPolicyResult NonCollapsedDropout =
		FMediaPipeQuestWristApplyPolicy::ShouldUseDropoutDownFallback(DropoutDownInput);
	TestFalse(TEXT("Accepted calibration does not pull a non-collapsed dropout endpoint down by itself"),
		NonCollapsedDropout.bUseFallback);

	DropoutDownInput = MakeDropoutDownPolicyInput();
	DropoutDownInput.bHasAcceptedArmLengthCalibration = true;
	DropoutDownInput.bCanInferCalibratedDownPose = true;
	DropoutDownInput.bHasRecentConstrainedArmSolve = false;
	const FMediaPipeQuestArmDropoutDownFallbackPolicyResult NoSeedDropout =
		FMediaPipeQuestWristApplyPolicy::ShouldUseDropoutDownFallback(DropoutDownInput);
	TestFalse(TEXT("Calibrated dropout-down fallback still requires an arm pose seed"),
		NoSeedDropout.bUseFallback);

	DropoutDownInput = MakeDropoutDownPolicyInput();
	DropoutDownInput.bHasRecentConstrainedArmSolve = false;
	DropoutDownInput.bContinueActiveFallback = true;
	const FMediaPipeQuestArmDropoutDownFallbackPolicyResult ContinueActiveDropout =
		FMediaPipeQuestWristApplyPolicy::ShouldUseDropoutDownFallback(DropoutDownInput);
	TestTrue(TEXT("Active dropout-down fallback stays active while Quest tracking remains lost"),
		ContinueActiveDropout.bUseFallback && !ContinueActiveDropout.bInferCalibratedDownPose);

	DropoutDownInput = MakeDropoutDownPolicyInput();
	DropoutDownInput.bHasRecentConstrainedArmSolve = false;
	DropoutDownInput.bHasMediaPipeDownHint = true;
	const FMediaPipeQuestArmDropoutDownFallbackPolicyResult MediaPipeHintDropout =
		FMediaPipeQuestWristApplyPolicy::ShouldUseDropoutDownFallback(DropoutDownInput);
	TestTrue(TEXT("A down-facing MediaPipe arm hint can seed Manny dropout fallback when Quest hands disappear"),
		MediaPipeHintDropout.bUseFallback && !MediaPipeHintDropout.bInferCalibratedDownPose);

	DropoutDownInput = MakeDropoutDownPolicyInput();
	DropoutDownInput.bHasAcceptedArmLengthCalibration = true;
	DropoutDownInput.bCanInferCalibratedDownPose = true;
	DropoutDownInput.bQuestSideTracked = true;
	const FMediaPipeQuestArmDropoutDownFallbackPolicyResult TrackedSideDropout =
		FMediaPipeQuestWristApplyPolicy::ShouldUseDropoutDownFallback(DropoutDownInput);
	TestFalse(TEXT("Tracked Quest sides do not enter dropout-down fallback"),
		TrackedSideDropout.bUseFallback);

	FMediaPipeQuestReachScaleCalibrationInput ReachScaleInput;
	ReachScaleInput.bEnabled = true;
	ReachScaleInput.CurrentReachCm = 48.0f;
	ReachScaleInput.ObservedMaxReachCm = 48.0f;
	ReachScaleInput.TargetMinReachCm = 4.0f;
	ReachScaleInput.TargetMaxReachCm = 54.0f;
	ReachScaleInput.MinObservedTargetFraction = 0.88f;
	ReachScaleInput.ApplyStartObservedFraction = 0.70f;
	ReachScaleInput.ApplyFullObservedFraction = 0.95f;
	ReachScaleInput.MinScale = 0.82f;
	ReachScaleInput.MaxScale = 1.18f;
	const FMediaPipeQuestReachScaleCalibrationResult ShortReachScale =
		FMediaPipeQuestWristApplyPolicy::ComputeReachScaleCalibration(ReachScaleInput);
	TestTrue(TEXT("Reach-scale calibration extends a high observed wearer reach toward avatar full reach"),
		ShortReachScale.bApplied && ShortReachScale.TargetReachCm > ReachScaleInput.CurrentReachCm);
	TestTrue(TEXT("Reach-scale calibration clamps extension to target max reach"),
		ShortReachScale.TargetReachCm <= ReachScaleInput.TargetMaxReachCm + 0.01f);

	ReachScaleInput.CurrentReachCm = 62.0f;
	ReachScaleInput.ObservedMaxReachCm = 62.0f;
	const FMediaPipeQuestReachScaleCalibrationResult LongReachScale =
		FMediaPipeQuestWristApplyPolicy::ComputeReachScaleCalibration(ReachScaleInput);
	TestTrue(TEXT("Reach-scale calibration reduces over-long observed wearer reach"),
		LongReachScale.bApplied && LongReachScale.TargetReachCm < ReachScaleInput.CurrentReachCm);

	FMediaPipeQuestReachScaleCalibrationInput NoCompressionReachScaleInput = ReachScaleInput;
	NoCompressionReachScaleInput.bAllowScaleBelowOne = false;
	const FMediaPipeQuestReachScaleCalibrationResult NoCompressionReachScale =
		FMediaPipeQuestWristApplyPolicy::ComputeReachScaleCalibration(NoCompressionReachScaleInput);
	TestFalse(TEXT("Reach-scale calibration can defer compression before accepted arm-length calibration"),
		NoCompressionReachScale.bApplied);

	FMediaPipeQuestReachScaleCalibrationInput DropoutReacquireReachScaleInput = ReachScaleInput;
	DropoutReacquireReachScaleInput.bSuppressDuringDropoutReacquire = true;
	const FMediaPipeQuestReachScaleCalibrationResult DropoutReacquireReachScale =
		FMediaPipeQuestWristApplyPolicy::ComputeReachScaleCalibration(DropoutReacquireReachScaleInput);
	TestFalse(TEXT("Reach-scale calibration is suppressed during dropout reacquisition"),
		DropoutReacquireReachScale.bApplied);

	FMediaPipeQuestReachScaleCalibrationInput UniformReachScaleInput = ReachScaleInput;
	UniformReachScaleInput.bApplyUniformScale = true;
	UniformReachScaleInput.CurrentReachCm = 40.0f;
	UniformReachScaleInput.ObservedMaxReachCm = 67.0f;
	const FMediaPipeQuestReachScaleCalibrationResult UniformReachScale =
		FMediaPipeQuestWristApplyPolicy::ComputeReachScaleCalibration(UniformReachScaleInput);
	TestTrue(TEXT("Uniform reach-scale calibration normalizes bent/down reaches after full reach is learned"),
		UniformReachScale.bApplied && UniformReachScale.TargetReachCm < UniformReachScaleInput.CurrentReachCm);
	TestTrue(TEXT("Uniform reach-scale calibration applies the learned arm length scale across the range"),
		FMath::IsNearlyEqual(UniformReachScale.ApplyAlpha, 1.0f, 0.01f));

	ReachScaleInput.ObservedMaxReachCm = 36.0f;
	ReachScaleInput.CurrentReachCm = 36.0f;
	const FMediaPipeQuestReachScaleCalibrationResult LowObservedReachScale =
		FMediaPipeQuestWristApplyPolicy::ComputeReachScaleCalibration(ReachScaleInput);
	TestFalse(TEXT("Reach-scale calibration waits until observed reach is plausibly near full extension"),
		LowObservedReachScale.bApplied);

	FMediaPipeQuestReachStepContinuityInput ReachContinuityInput;
	ReachContinuityInput.bEnabled = true;
	ReachContinuityInput.CurrentReachCm = 27.0f;
	ReachContinuityInput.bHasPreviousReach = true;
	ReachContinuityInput.PreviousReachCm = 53.0f;
	ReachContinuityInput.MaxStepCm = 6.0f;
	ReachContinuityInput.MinReachCm = 4.0f;
	ReachContinuityInput.MaxReachCm = 54.0f;
	const FMediaPipeQuestReachStepContinuityResult ReachCollapseContinuity =
		FMediaPipeQuestWristApplyPolicy::ApplyReachStepContinuity(ReachContinuityInput);
	TestTrue(TEXT("Endpoint reach-step continuity catches a pre-solver full-to-bent reach collapse"),
		ReachCollapseContinuity.bApplied);
	TestTrue(TEXT("Endpoint reach-step continuity limits inward reach collapse to one configured step"),
		FMath::IsNearlyEqual(ReachCollapseContinuity.TargetReachCm, 47.0f, 0.01f));

	ReachContinuityInput.CurrentReachCm = 53.0f;
	ReachContinuityInput.PreviousReachCm = 27.0f;
	const FMediaPipeQuestReachStepContinuityResult ReachExtendContinuity =
		FMediaPipeQuestWristApplyPolicy::ApplyReachStepContinuity(ReachContinuityInput);
	TestTrue(TEXT("Endpoint reach-step continuity also limits outward snaps"),
		ReachExtendContinuity.bApplied);
	TestTrue(TEXT("Endpoint reach-step continuity moves outward by one configured step"),
		FMath::IsNearlyEqual(ReachExtendContinuity.TargetReachCm, 33.0f, 0.01f));

	ReachContinuityInput.bHasPreviousReach = false;
	const FMediaPipeQuestReachStepContinuityResult NoHistoryReachContinuity =
		FMediaPipeQuestWristApplyPolicy::ApplyReachStepContinuity(ReachContinuityInput);
	TestFalse(TEXT("Endpoint reach-step continuity does not invent history"),
		NoHistoryReachContinuity.bApplied);

	TestTrue(TEXT("Quest semantic wrist roll keeps positive continuity across the +/-180 wrap"),
		FMath::IsNearlyEqual(FMediaPipeQuestWristApplyPolicy::ContinueAngleDegrees(169.0f, -179.0f), 181.0f, 0.01f));
	TestTrue(TEXT("Quest semantic wrist roll keeps negative continuity across the +/-180 wrap"),
		FMath::IsNearlyEqual(FMediaPipeQuestWristApplyPolicy::ContinueAngleDegrees(-169.0f, 179.0f), -181.0f, 0.01f));
	TestTrue(TEXT("Quest semantic wrist roll can continue beyond one revolution without snapping backward"),
		FMath::IsNearlyEqual(FMediaPipeQuestWristApplyPolicy::ContinueAngleDegrees(350.0f, 10.0f), 370.0f, 0.01f));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
