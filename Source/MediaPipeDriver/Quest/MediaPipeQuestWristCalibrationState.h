#pragma once

#include "CoreMinimal.h"
#include "MediaPipePoseDiagnostics.h"

struct FQuestHandRotationTrace;

enum EQuestArmLengthCalibrationStage : uint8
{
	QuestArmLengthCalibrationStage_WaitingForHands = 0,
	QuestArmLengthCalibrationStage_ForwardReach = 1,
	QuestArmLengthCalibrationStage_DownReach = 2,
	QuestArmLengthCalibrationStage_Accepted = 3,
};

struct FQuestWristSideRuntimeState
{
	bool bHasApplyCalibration = false;
	FVector ApplyQuestHmdToWristWorld = FVector::ZeroVector;
	FVector ApplyMediaPipeWristWorld = FVector::ZeroVector;
	double ApplyCalibrationTimeSeconds = -1.0;
	bool bHasTraceCalibration = false;
	FVector TraceQuestHmdToWristWorld = FVector::ZeroVector;
	double TraceCalibrationTimeSeconds = -1.0;
	bool bHasRotationCalibration = false;
	FQuat RotationCalibrationBasisComp = FQuat::Identity;
	uint8 RotationCalibrationSource = 0;
	// Source-flip dwell (2026-07-10 worn forensics): the per-frame solve source flips for a
	// few frames whenever a hand-tracking loss degrades the quest/forearm bases, and the old
	// immediate source-mismatch wipe destroyed an ACCEPTED calibration on every flicker - the
	// held-rotation bridge (which requires a calibration) died with it, the hand snapped limp,
	// and mid-motion re-acceptance took seconds (R hand: 11.5s of handRotApplied=0 after one
	// loss; 33% of the session's rows waiting) while each re-accept baked a NEW arbitrary
	// neutral basis (the shifting constant wrist offsets). A mismatch must now accumulate
	// this dwell before it may wipe; transient flips hold the last rotation instead.
	float RotationCalibrationSourceMismatchSeconds = 0.0f;
	double RotationCalibrationSourceMismatchLastTimeSeconds = -1.0;
	bool bHasRotationAnatomicalRollAxis = false;
	uint8 RotationAnatomicalRollAxisIndex = 0;
	float RotationAnatomicalRollAxisSign = 1.0f;
	bool bHasRotationProjectedAxis = false;
	uint8 RotationProjectedAxisIndex = 0;
	uint8 RotationProjectedCalibrationMask = 0;
	FVector RotationProjectedCalibrationRefsComp[6] = {};
	bool bHasRotationSemanticRollAxis = false;
	uint8 RotationSemanticRollAxisIndex = 0;
	float RotationSemanticRollCalibrationOffsetDeg = 0.0f;
	FVector RotationSemanticRollCalibrationForwardComp = FVector::ZeroVector;
	FVector RotationSemanticRollCalibrationUpComp = FVector::ZeroVector;
	bool bHasRotationSemanticRollLastTwist = false;
	float RotationSemanticRollLastTwistDeg = 0.0f;
	// Palm-roll measurement-source hysteresis + continuity (2026-07-09 worn forensics): the
	// projected-basis roll and the swing-corrected fallback disagree by 20-130deg during side
	// raises, and the projection score oscillates around the threshold, so per-frame source
	// switching snapped the right wrist 38+ times in one session (wristPalmFallback flapping,
	// wristPalmHeld never engaged). The active source may only change after a dwell (short
	// dips HOLD the last roll instead), and every committed switch rebases the new source
	// through a continuity bias so the output roll never steps. The bias decays to zero only
	// while the calibrated primary is measuring (the fallback's absolute value is untrusted -
	// it contributes relative motion only).
	uint8 PalmRollActiveSource = 0; // 0=none/held, 1=projected primary, 2=swing-corrected fallback
	float PalmRollPrimaryOkSeconds = 0.0f;
	float PalmRollPrimaryBadSeconds = 0.0f;
	float PalmRollContinuityBiasDeg = 0.0f;
	// Bounded bias lifetime (2026-07-10): wall-clock seconds the continuity bias has lived
	// without re-grounding (|bias| within a few degrees of zero). During raises the primary
	// drops out faster than its decay could run and the re-anchor-on-resume preserved a wrong
	// roll indefinitely ("only occasionally corrects itself"); past the age cap the bias
	// rate-limited-converges to the measuring source instead of holding.
	float PalmRollBiasAgeSeconds = 0.0f;
	double PalmRollSourceLastUpdateTimeSeconds = -1.0;
	uint8 RotationCalibrationState = QuestWristCalibrationState_WaitingForStablePose;
	uint8 RotationCalibrationRejectReason = QuestWristCalibrationReject_WaitingForStablePose;
	int32 RotationCalibrationStableFrameCount = 0;
	float RotationCalibrationStableSeconds = 0.0f;
	int32 RotationCalibrationFreshStableFrameCount = 0;
	float RotationCalibrationFreshStableSeconds = 0.0f;
	float RotationCalibrationLastBasisErrorDeg = 0.0f;
	float RotationCalibrationLastNeutralTwistDeg = 0.0f;
	double RotationCalibrationMeasureStartTimeSeconds = -1.0;
	double RotationCalibrationLastSampleTimeSeconds = -1.0;
	FVector RotationCalibrationLastWristWorld = FVector::ZeroVector;
	FQuat RotationCalibrationLastMeasuredComp = FQuat::Identity;
	float RotationCalibrationLastBodyYawDeg = 0.0f;
	float RotationCalibrationLastMannyYawDeg = 0.0f;
	uint8 RotationCalibrationLastSemanticAxisIndex = 0;
	bool bHasRotationCalibrationLastSample = false;
	bool bHasHeldTarget = false;
	FVector HeldTargetWorld = FVector::ZeroVector;
	FVector HeldRawQuestWristWorld = FVector::ZeroVector;
	FVector HeldMappedQuestWristWorld = FVector::ZeroVector;
	double LastTargetTimeSeconds = -1.0;
	bool bHasLastAcceptedLiveWristPosition = false;
	FVector LastAcceptedLiveWristWorld = FVector::ZeroVector;
	double LastAcceptedLiveWristTimeSeconds = -1.0;
	bool bHasPositionFilter = false;
	FVector PositionFilterDeltaComp = FVector::ZeroVector;
	FVector PositionFilterLastRawDeltaComp = FVector::ZeroVector;
	double PositionFilterLastTimeSeconds = -1.0;
	bool bHasPositionStartupLastSample = false;
	FVector PositionStartupLastRawQuestWristWorld = FVector::ZeroVector;
	FVector PositionStartupLastMediaPipeWristWorld = FVector::ZeroVector;
	double PositionStartupLastSampleTimeSeconds = -1.0;
	int32 PositionStartupStableFrameCount = 0;
	float PositionStartupStableSeconds = 0.0f;
	bool bHasHmdRelativeReachObservedMax = false;
	float HmdRelativeReachObservedMaxCm = 0.0f;
	bool bHasArmLengthCalibrationCandidate = false;
	bool bArmLengthCalibrationCandidateTracked = false;
	FVector ArmLengthCalibrationCandidateWristWorld = FVector::ZeroVector;
	FVector ArmLengthCalibrationCandidateShoulderWorld = FVector::ZeroVector;
	float ArmLengthCalibrationCandidateReachCm = 0.0f;
	float ArmLengthCalibrationCandidateBelowShoulderCm = 0.0f;
	float ArmLengthCalibrationCandidateVerticalDominance = 0.0f;
	double ArmLengthCalibrationCandidateTimeSeconds = -1.0;
	bool bHasArmLengthCalibrationForwardReach = false;
	float ArmLengthCalibrationForwardReachCm = 0.0f;
	bool bHasArmLengthCalibrationDownSample = false;
	float ArmLengthCalibrationDownDropCm = 0.0f;
	float ArmLengthCalibrationDownReachCm = 0.0f;
	bool bHasArmLengthCalibrationLastSample = false;
	FVector ArmLengthCalibrationLastWristWorld = FVector::ZeroVector;
	double ArmLengthCalibrationLastSampleTimeSeconds = -1.0;
	float ArmLengthCalibrationLastVelocityCmSec = 0.0f;
	bool bHasHmdRelativeReachContinuity = false;
	float HmdRelativeReachContinuityCm = 0.0f;
	double HmdRelativeReachContinuityTimeSeconds = -1.0;
	bool bHasLastTrackedQuestArmPose = false;
	FVector LastTrackedQuestArmShoulderWorld = FVector::ZeroVector;
	FVector LastTrackedQuestArmElbowWorld = FVector::ZeroVector;
	FVector LastTrackedQuestArmWristWorld = FVector::ZeroVector;
	float LastTrackedQuestArmReachCm = 0.0f;
	float LastTrackedQuestArmBelowShoulderCm = 0.0f;
	float LastTrackedQuestArmDownDominance = 0.0f;
	double LastTrackedQuestArmTimeSeconds = -1.0;
	bool bDropoutDownFallbackActive = false;
	FVector DropoutDownFallbackWristWorld = FVector::ZeroVector;
	FVector DropoutDownFallbackElbowWorld = FVector::ZeroVector;
	double DropoutDownFallbackLastUpdateTimeSeconds = -1.0;
	// Overhead MediaPipe arm rescue (mp.MediaPipeArmOverheadRescue) dwell/latch. Lives HERE in
	// the keyed runtime state, not in anim-node members: measured 2026-07-02/03, the node-member
	// dwell was reset before ever reaching the 0.15 s latch threshold (enterS pinned at 0.00
	// across 6,600+ diagnostic rows over two sessions), so the rescue never activated once.
	// Accumulation is wall-clock (LastUpdate delta) so zero-delta evaluations cannot stall it.
	// Deliberately NOT cleared by Reset(): pose-state resets must not un-latch an arm the camera
	// is actively driving; the exit dwell self-clears within 0.3 s when conditions end.
	bool bArmRescueActive = false;
	float ArmRescueEnterSeconds = 0.0f;
	float ArmRescueExitSeconds = 0.0f;
	double ArmRescueLastUpdateTimeSeconds = -1.0;
	// Diagnostic throttles for this side's mp.ArmOverheadRescue / mp.QuestWristSolve rows
	// (2026-07-10): node-member timers are wiped by CacheBones every frame (the rescue row
	// emitted at frame rate, 3,522 rows/side), and the wrist-solve row's function-static
	// global pair rationed rows across ALL actors (the acceptance mirror Kellan starved to
	// 4 rows while Manny consumed the budget - third session of starved evidence). Keyed
	// per-actor-side timers survive resets and give every actor its own cadence.
	double ArmRescueLastLogTimeSeconds = -1.0;
	double QuestWristSolveLastLogTimeSeconds = -1.0;
	// Quest-reach chain extension (mp.MediaPipeChainReachFromQuestHand, 2026-07-10): smoothed
	// radial extension (cm) of the chain-direct wrist target toward the REAL hand-tracking
	// wrist's reach fraction. Smoothed so tracked-flag flickers cannot pop the wrist; decays
	// to zero when the real wrist is unavailable. Keyed for the same CacheBones reason as
	// everything else here.
	float ChainReachExtensionCm = 0.0f;
	// Seconds the real-wrist straightness fraction has been sustained above the apply-start
	// threshold (2026-07-10 late-day fix): the extension chased instantaneous fraction/chain
	// mismatches - the low-latency hand wrist leads the laggy body chain during fast moves
	// (desired spiked 0->18.8->1.4cm across three 1Hz rows) and a fist-close re-fit steps
	// the wrist estimate a few cm - rendering as cm-scale arm pumps. Extension now needs the
	// fraction HIGH and SUSTAINED before it may apply.
	float ChainReachHighFracSeconds = 0.0f;
	double ChainReachExtensionLastUpdateTimeSeconds = -1.0;
	double ChainReachExtendLastLogTimeSeconds = -1.0;
	// Wall-clock time this side's Quest hand tracked flag was last TRUE (2026-07-09): the
	// tracked flag flickers sub-second at the FOV edge, and a single dropped frame let the
	// rescue's untracked clause and the camera-hand latch seize a hand mid side-raise
	// (armOwner=cameraRescue + handRotApplied=0 at 07:30:39, wrist snap). Quest stays the
	// hand authority through flickers shorter than mp.QuestWristLostTrackingGraceSeconds;
	// the divergence trigger (direct camera evidence the Quest is WRONG) bypasses the grace.
	// Like the rescue latch, deliberately NOT cleared by Reset(): recency self-heals.
	double LastQuestSideTrackedTimeSeconds = -1.0;
	// Camera elbow-swivel correction (mp.MediaPipeArmElbowSwivelFromCamera): smoothed azimuth
	// delta between the Quest chain's elbow and the camera's elbow about the shoulder->wrist
	// chord. The Quest chain synthesizes elbows flared OUTWARD (+14cm off-chord measured vs
	// the camera's 5.3cm and Epic's 8.3cm, take-3 referee 2026-07-05); the swivel is the one
	// arm degree of freedom the camera measures better. Wall-clock smoothed, keyed state.
	// Camera arm-direction transplant blend (eases in/out with camera reliability).
	float ArmDirectionBlendAlpha = 0.0f;
	double ArmDirectionLastUpdateTimeSeconds = -1.0;
	// Hysteresis latch for the camera direction vote (2026-07-10): the old binary rel-0.5
	// gate toggled the ENTIRE learned correction on sub-second reliability dips (fist closes
	// move the wrist landmark's confidence) - both arms jumped together. Engage needs rel
	// >= 0.6 sustained; release needs rel < 0.4 (or no camera arm) sustained.
	bool bArmDirCameraVoteEngaged = false;
	float ArmDirCameraVoteEnterSeconds = 0.0f;
	float ArmDirCameraVoteExitSeconds = 0.0f;
	double ArmDirCorrectionLogTimeSeconds = -1.0;
	// Quiet-gate baseline for correction LEARNING (2026-07-10 round 5): the corrector may
	// only learn while the raw chain wrist is slow - a standing-bias eraser has no business
	// learning during raises, where the 0.8s tau integrated transient camera-vs-chain
	// latency disagreements into 69-99deg corrections (10s wander 63-93deg, wrist displaced
	// up to 65cm; 27 of 34 traced arm jumps named this stage). Applied corrections are also
	// magnitude-clamped via mp.MediaPipeArmDirectionFromCameraMaxDeg.
	FVector ArmDirLearnLastChainWristWorld = FVector::ZeroVector;
	double ArmDirLearnLastTimeSeconds = -1.0;
	// Jump-attribution tracer baselines (mp.ArmJumpTrace, 2026-07-10): previous frame's wrist
	// target at each solve stage (raw chain / after direction correction / after reach
	// extension / final). Residual motion of the final target vs the raw chain isolates
	// solver-injected jumps from real body movement.
	bool bHasArmJumpBaseline = false;
	FVector ArmJumpLastChainWristWorld = FVector::ZeroVector;
	FVector ArmJumpLastDirWristWorld = FVector::ZeroVector;
	FVector ArmJumpLastExtWristWorld = FVector::ZeroVector;
	FVector ArmJumpLastFinalWristWorld = FVector::ZeroVector;
	double ArmJumpLastTimeSeconds = -1.0;
	double ArmJumpLastLogTimeSeconds = -1.0;
	// Complementary-fusion direction correction (2026-07-05 redesign after "arms too
	// erratic" worn/viewer verdict): the QUEST chain owns high-frequency arm motion
	// (smooth, fast); the CAMERA contributes a slow-filtered rotation correction that
	// erases the chain's constant biases (wide hanging arms) without passing through
	// camera landmark jitter. Stored as smoothed per-segment rotation deltas.
	bool bHasArmDirCorrection = false;
	FQuat ArmDirCorrectionElbow = FQuat::Identity;
	FQuat ArmDirCorrectionWrist = FQuat::Identity;
	float SmoothedElbowSwivelDeltaDeg = 0.0f;
	bool bHasSmoothedElbowSwivel = false;
	double ElbowSwivelLastUpdateTimeSeconds = -1.0;
	double DropoutReacquireReachScaleSuppressUntilTimeSeconds = -1.0;
	float PositionAuthorityAlpha = 0.0f;
	double PositionAuthorityLastTimeSeconds = -1.0;
	double LastHandRotationApplyTimeSeconds = -1.0;
	// Last time the MediaPipe quest-loss hand-rotation path applied for this side; compared with
	// LastHandRotationApplyTimeSeconds to detect the first camera frame after Quest ownership so
	// the swing/twist filter can be seeded from the current pose (smooth handover, no snap).
	double LastMediaPipeHandRotationApplyTimeSeconds = -1.0;
	// Sticky image-space hand match for this arm (0=MediaPipe left array, 1=right array,
	// 255=none): the proximity match must not alternate between detected hands at frame rate
	// when both hover near each other overhead (2026-07-03 worn verdict: twist/snap).
	uint8 MediaPipeHandStickyArraySide = 255;
	// Camera-owned hand rotation continuity (mp.MediaPipeHandRotationOnQuestLoss). Lives HERE
	// in the keyed runtime state, not in FMediaPipeArmSolverState node members: measured
	// 2026-07-03 (live session log), CacheBones_AnyThread -> BuildReferencePoseCache ->
	// ResetRotationSmoothing runs EVERY FRAME in live VR (the 1 Hz node-member takeover-log
	// throttle emitted at frame rate with a stable node serial and resets=1), so the smoothing
	// floor, swing/twist speed caps, and branch dwell added for the overhead takeover never
	// engaged - the branch chooser re-anchored to the ref pose each frame and flipped freely.
	// Smoothing fields ARE cleared by Reset() (they re-seed from the current pose next frame).
	bool bHasCameraHandSmoothedSwing = false;
	FQuat CameraHandSmoothedSwingCS = FQuat::Identity;
	bool bHasCameraHandSmoothedTwist = false;
	float CameraHandSmoothedTwistDeg = 0.0f;
	bool bHasCameraHandLastGoodTarget = false;
	FQuat CameraHandLastGoodTargetCS = FQuat::Identity;
	int32 CameraHandActiveBranch = 0;
	int32 CameraHandPendingBranch = 0;
	int32 CameraHandPendingBranchFrames = 0;
	double CameraHandSmoothLastStepTimeSeconds = -1.0;
	// Last rotation the camera path applied: re-applied on degenerate/missing-basis frames so
	// "hold last rotation" actually holds (2026-07-03: the hold path returned without applying,
	// snapping the hand to the pass-through pose for one frame on every handSel=none frame).
	bool bHasCameraHandLastApplied = false;
	FQuat CameraHandLastAppliedRotCS = FQuat::Identity;
	// Resolved palm chirality for the camera hand (mirror parity of the whole capture chain):
	// +1 = measured up sign agrees with the ref thumb cue, -1 = inverted, 0 = undecided. The
	// parity is a PHYSICAL CONSTANT of the capture chain, so it locks after 0.25 s of consistent
	// confident thumb frames and only re-flips after 1.0 s of sustained contrary evidence -
	// per-frame re-resolution flipped the palm 34/26 times (L/R) across the 2026-07-03 round-two
	// session whenever the thumb crossed the palm plane during palm rotations.
	int8 CameraHandChiralitySign = 0;
	int8 CameraHandChiralityPendingSign = 0;
	float CameraHandChiralityPendingSeconds = 0.0f;
	// Camera hand-pose ownership LATCH. Recomputing ownership per frame from the raw Quest
	// tracked flag alternated camera/Quest hand owners at tracked-flag flicker rate (2026-07-03
	// log: camera owned only ~68%/47% of frames L/R across the measured overhead window; 21 of
	// 30 sampled mp.QuestWristSolve rows showed the Quest path applying mid-takeover). Latched
	// like bArmRescueActive: deliberately NOT cleared by Reset(); releases only after the Quest
	// side stays solidly tracked for the handback dwell
	// (mp.MediaPipeHandOwnershipHandbackSeconds) or the camera-hand features turn off.
	bool bCameraHandOwnershipLatched = false;
	float CameraHandQuestSolidSeconds = 0.0f;
	double CameraHandOwnershipLastUpdateTimeSeconds = -1.0;

	void ResetRotationCalibration();
	void ResetPositionContinuity(bool bResetArmLengthCalibration = true);
	void Reset();
	void ResetCalibration();
};

struct FQuestWristRuntimeState
{
	EQuestMediaSpaceCalibrationMode CalibrationMode = EQuestMediaSpaceCalibrationMode::None;
	bool bHasHmdRelativeAvatarCalibration = false;
	FVector HmdRelativeQuestAnchorWorld = FVector::ZeroVector;
	FQuat HmdRelativeQuestAnchorYawWorld = FQuat::Identity;
	bool bHasHmdRelativeQuestTranslationFilter = false;
	FVector HmdRelativeQuestFilteredAnchorWorld = FVector::ZeroVector;
	FVector HmdRelativeQuestLastRawAnchorWorld = FVector::ZeroVector;
	double HmdRelativeQuestAnchorLastTimeSeconds = -1.0;
	uint8 ArmLengthCalibrationStage = QuestArmLengthCalibrationStage_WaitingForHands;
	int32 ArmLengthCalibrationStableFrameCount = 0;
	float ArmLengthCalibrationStableSeconds = 0.0f;
	double ArmLengthCalibrationLastUpdateTimeSeconds = -1.0;
	double ArmLengthCalibrationLastLogTimeSeconds = -1.0;
	double ArmLengthCalibrationAcceptedTimeSeconds = -1.0;
	FQuestWristSideRuntimeState Left;
	FQuestWristSideRuntimeState Right;

	void Reset();
	void ResetCalibration();
};

struct FQuestWristCalibrationSoftRejectSettings
{
	bool bEnableSoftGate = false;
	float HandLossPauseSeconds = 0.0f;
	float SoftRejectDecayRate = 0.0f;
	float RequiredStableSeconds = 0.0f;
	int32 RequiredStableFrames = 1;
};

struct MEDIAPIPEDRIVER_API FMediaPipeQuestWristCalibrationState
{
	static void WriteTrace(const FQuestWristSideRuntimeState& State, FQuestHandRotationTrace& Trace);
	static void ResetMeasurement(FQuestWristSideRuntimeState& State, uint8 Reason, FQuestHandRotationTrace* Trace = nullptr);
	static bool IsSoftRejectReason(uint8 Reason);
	static void SoftRejectMeasurement(
		FQuestWristSideRuntimeState& State,
		uint8 Reason,
		const FQuestWristCalibrationSoftRejectSettings& Settings,
		float DeltaSeconds,
		double NowSeconds,
		FQuestHandRotationTrace* Trace = nullptr);
};
