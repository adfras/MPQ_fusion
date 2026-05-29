void FAnimNode_MediaPipePoseDriven::DriveArmCS(FCSPose<FCompactPose>& CSPose, bool bIsLeft, float DeltaSeconds)
{
	if (!bDriveArms)
	{
		return;
	}

	const bool bHasRef = bIsLeft ? bHasRefArmL : bHasRefArmR;
	if (!bHasRef)
	{
		return;
	}

	const int32 ShoulderLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftShoulder : (int32)EMediaPipePoseLandmark::RightShoulder;
	const int32 ElbowLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftElbow : (int32)EMediaPipePoseLandmark::RightElbow;
	const int32 WristLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftWrist : (int32)EMediaPipePoseLandmark::RightWrist;
	const int32 IndexLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftIndex : (int32)EMediaPipePoseLandmark::RightIndex;
	const int32 PinkyLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftPinky : (int32)EMediaPipePoseLandmark::RightPinky;

	const FBoneReference& UpperBone = bIsLeft ? UpperArmL : UpperArmR;
	const FBoneReference& LowerBone = bIsLeft ? LowerArmL : LowerArmR;
	const FBoneReference& HandBone = bIsLeft ? HandL : HandR;

	const bool bQuestHandTrackingEnabled =
		bUseQuestHandTracking &&
		CVarQuestHandTracking.GetValueOnAnyThread() != 0;
	FQuestWristRuntimeState& QuestWristRuntimeState = GetQuestWristRuntimeState(RuntimeStateKey);
	FQuestWristSideRuntimeState& QuestWristSideState = bIsLeft ? QuestWristRuntimeState.Left : QuestWristRuntimeState.Right;
	const bool bMetaHumanFullArmChainRequested =
		TargetMetaHumanProfile.IsValidForPoseDriving() &&
		TargetMetaHumanProfile.bIsActiveProfile &&
		ResolveMediaPipeMetaHumanArmSourceMode(TargetMetaHumanProfile) == 1;
	const FMediaPipeFullArmChainSideSnapshot& FullArmChainSide = FullArmChain.GetSide(bIsLeft);
	const double FullArmChainNowSeconds = FPlatformTime::Seconds();
	const float FullArmChainMaxAgeSeconds = ResolveMediaPipeMetaHumanFullArmChainMaxAgeSeconds(TargetMetaHumanProfile);
	const float RefUpperLen = bIsLeft ? RefUpperLenCompL : RefUpperLenCompR;
	const float RefLowerLen = bIsLeft ? RefLowerLenCompL : RefLowerLenCompR;
	FMediaPipeMetaHumanFullArmChainRetargetInput FullArmChainInput;
	FullArmChainInput.Target = &TargetMetaHumanProfile;
	FullArmChainInput.Snapshot = &FullArmChain;
	FullArmChainInput.TargetComponentTransform = TargetCompTransform;
	FullArmChainInput.bIsLeft = bIsLeft;
	FullArmChainInput.NowSeconds = FullArmChainNowSeconds;
	FullArmChainInput.MaxAgeSeconds = FullArmChainMaxAgeSeconds;
	FullArmChainInput.FallbackUpperArmLengthCm = RefUpperLen;
	FullArmChainInput.FallbackLowerArmLengthCm = RefLowerLen;
	const FMediaPipeMetaHumanFullArmChainRetargetResult FullArmChainResult =
		FMediaPipeMetaHumanFullArmChainRetargeter::ResolveFullArmChainSource(FullArmChainInput);
	const float FullArmChainAgeSeconds = FullArmChainResult.ChainAgeSeconds;
	const bool bMetaHumanFullArmChainFresh =
		bMetaHumanFullArmChainRequested &&
		FullArmChainResult.bFresh;
	auto EmitMetaHumanFullArmChainLog = [&](
		const bool bChainActive,
		const bool bQuestHandUsed,
		const float TargetReachCm,
		const float ElbowBendDeg,
		const FVector& HandWorld)
	{
		if (!bMetaHumanFullArmChainRequested ||
			!ShouldTraceMediaPipeMetaHumanFullArmChain(TargetMetaHumanProfile))
		{
			return;
		}

		double& LastLogTimeSeconds = bIsLeft
			? DiagnosticsState.LastMetaHumanFullArmChainLogTimeSecondsL
			: DiagnosticsState.LastMetaHumanFullArmChainLogTimeSecondsR;
		const double NowSeconds = FPlatformTime::Seconds();
		const double RequiredLogIntervalSeconds = static_cast<double>(
			FMath::Clamp(ResolveMediaPipeMetaHumanFullArmChainTraceIntervalSeconds(TargetMetaHumanProfile), 0.05f, 5.0f));
		if (!FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, RequiredLogIntervalSeconds, LastLogTimeSeconds))
		{
			return;
		}

		FMediaPipeMetaHumanFullArmChainLogInput FullArmChainLogInput;
		FullArmChainLogInput.ProfileId = TargetMetaHumanProfile.ProfileId;
		FullArmChainLogInput.TargetActorName = TargetActorName;
		FullArmChainLogInput.bIsLeft = bIsLeft;
		FullArmChainLogInput.bChainActive = bChainActive;
		FullArmChainLogInput.bShoulderValid = FullArmChainSide.Shoulder.bPositionValid != 0;
		FullArmChainLogInput.bUpperArmValid = FullArmChainSide.UpperArm.bPositionValid != 0;
		FullArmChainLogInput.bLowerArmValid = FullArmChainSide.LowerArm.bPositionValid != 0;
		FullArmChainLogInput.bWristOrPalmValid = FullArmChainSide.WristOrPalm.bPositionValid != 0;
		FullArmChainLogInput.bMediaPipeArmUsed = false;
		FullArmChainLogInput.bQuestHandUsed = bQuestHandUsed;
		FullArmChainLogInput.bWristOrPalmUsesPalm = FullArmChainSide.bWristOrPalmUsesPalm != 0;
		FullArmChainLogInput.TargetReachCm = TargetReachCm;
		FullArmChainLogInput.ElbowBendDeg = ElbowBendDeg;
		FullArmChainLogInput.HandWorld = HandWorld;
		FullArmChainLogInput.Confidence = FullArmChainSide.Confidence;
		FullArmChainLogInput.ChainAgeSeconds = FullArmChainAgeSeconds;
		UE_LOG(LogMediaPipePose, Log, TEXT("%s"), *FormatMediaPipeMetaHumanFullArmChainLog(FullArmChainLogInput));
	};
	const int32 QuestArmMode = FMath::Clamp(CVarQuestArmMode.GetValueOnAnyThread(), 0, 3);
	const bool bQuestArmFallbackAllowed = !bMetaHumanFullArmChainFresh;
	const bool bQuestArmUsesWristEndpoint = QuestArmMode >= 1 && bQuestArmFallbackAllowed;
	const bool bQuestArmUsesConstrainedSolve = QuestArmMode >= 2 && bQuestArmFallbackAllowed;
	const bool bQuestArmUsesLegacyReachAssist = QuestArmMode == 1 && bQuestArmFallbackAllowed;
	const bool bUseHmdRelativeAvatarArmFrame = QuestArmMode >= 3 && bQuestArmFallbackAllowed;
	const bool bQuestSideTrackedForArm = bQuestHandTrackingEnabled && IsQuestHandSideTracked(QuestHands, bIsLeft);
	const bool bQuestSideUsableForArm = bQuestHandTrackingEnabled && IsQuestHandSideUsableForWrist(QuestHands, bIsLeft);

	auto TryGetBoneWorld = [&](const FBoneReference& Bone, FVector& OutWorld) -> bool
	{
		if (!Bone.IsValidToEvaluate())
		{
			return false;
		}

		const FVector BoneComp = CSPose.GetComponentSpaceTransform(Bone.CachedCompactPoseIndex).GetTranslation();
		OutWorld = TargetCompTransform.TransformPosition(BoneComp);
		return !OutWorld.ContainsNaN();
	};

	FVector ShoulderWorld = FVector::ZeroVector;
	FVector ElbowWorld = FVector::ZeroVector;
	FVector WristWorld = FVector::ZeroVector;
	const bool bHasMediaPipeArmWorld =
		IsMeasured(ShoulderLm) &&
		IsMeasured(ElbowLm) &&
		IsMeasured(WristLm) &&
		TryGetLmWorld(ShoulderLm, ShoulderWorld) &&
		TryGetLmWorld(ElbowLm, ElbowWorld) &&
		TryGetLmWorld(WristLm, WristWorld);
	const FVector MediaPipeSourceShoulderWorld = bHasMediaPipeArmWorld ? ShoulderWorld : FVector::ZeroVector;
	const FVector MediaPipeSourceElbowWorld = bHasMediaPipeArmWorld ? ElbowWorld : FVector::ZeroVector;
	const FVector MediaPipeSourceWristWorld = bHasMediaPipeArmWorld ? WristWorld : FVector::ZeroVector;

	if (bMetaHumanFullArmChainRequested && !bMetaHumanFullArmChainFresh)
	{
		EmitMetaHumanFullArmChainLog(false, false, 0.0f, 0.0f, FVector::ZeroVector);
	}

	if (bMetaHumanFullArmChainFresh)
	{
		ShoulderWorld = FullArmChainResult.ShoulderWorld;
		ElbowWorld = FullArmChainResult.ElbowWorld;
		WristWorld = FullArmChainResult.WristWorld;
	}
	else if (bUseHmdRelativeAvatarArmFrame)
	{
		FVector AvatarShoulderWorld = FVector::ZeroVector;
		FVector AvatarElbowWorld = FVector::ZeroVector;
		FVector AvatarWristWorld = FVector::ZeroVector;
		if (!TryGetBoneWorld(UpperBone, AvatarShoulderWorld) ||
			!TryGetBoneWorld(LowerBone, AvatarElbowWorld) ||
			!TryGetBoneWorld(HandBone, AvatarWristWorld))
		{
			return;
		}

		ShoulderWorld = AvatarShoulderWorld;
		ElbowWorld = AvatarElbowWorld;
		WristWorld = AvatarWristWorld;
	}
	else if (!bHasMediaPipeArmWorld)
	{
		return;
	}

	const FVector& RefUpperDir = bIsLeft ? RefUpperDirCompL : RefUpperDirCompR;
	const FVector& RefLowerDir = bIsLeft ? RefLowerDirCompL : RefLowerDirCompR;
	const FVector& RefPoleDir = bIsLeft ? RefPoleDirCompL : RefPoleDirCompR;
	const FQuat& RefUpperComp = bIsLeft ? RefUpperArmCompL : RefUpperArmCompR;
	const FQuat& RefLowerComp = bIsLeft ? RefLowerArmCompL : RefLowerArmCompR;
	const FQuat& RefUpperBasisComp = bIsLeft ? RefUpperArmBasisCompL : RefUpperArmBasisCompR;
	const FQuat& RefLowerBasisComp = bIsLeft ? RefLowerArmBasisCompL : RefLowerArmBasisCompR;
	const FQuat& RefUpperSurfaceBasisComp = bIsLeft ? RefUpperArmSurfaceBasisCompL : RefUpperArmSurfaceBasisCompR;
	const FQuat& RefLowerSurfaceBasisComp = bIsLeft ? RefLowerArmSurfaceBasisCompL : RefLowerArmSurfaceBasisCompR;
	const FQuat& RefHandComp = bIsLeft ? RefHandCompL : RefHandCompR;
	const FQuat& RefHandBasisComp = bIsLeft ? RefHandBasisCompL : RefHandBasisCompR;
	bool& bHasSmoothedUpperRotCS = bIsLeft ? LeftArmState.bHasSmoothedUpperArmRotCS : RightArmState.bHasSmoothedUpperArmRotCS;
	FQuat& SmoothedUpperRotCS = bIsLeft ? LeftArmState.SmoothedUpperArmRotCS : RightArmState.SmoothedUpperArmRotCS;
	bool& bHasSmoothedLowerRotCS = bIsLeft ? LeftArmState.bHasSmoothedLowerArmRotCS : RightArmState.bHasSmoothedLowerArmRotCS;
	FQuat& SmoothedLowerRotCS = bIsLeft ? LeftArmState.SmoothedLowerArmRotCS : RightArmState.SmoothedLowerArmRotCS;

	bool& bHasLastReliableArmSample = bIsLeft ? LeftArmState.bHasLastReliableArmSample : RightArmState.bHasLastReliableArmSample;
	FVector& LastReliableShoulderWorld = bIsLeft ? LeftArmState.LastReliableShoulderWorld : RightArmState.LastReliableShoulderWorld;
	FVector& LastReliableElbowWorld = bIsLeft ? LeftArmState.LastReliableElbowWorld : RightArmState.LastReliableElbowWorld;
	FVector& LastReliableWristWorld = bIsLeft ? LeftArmState.LastReliableWristWorld : RightArmState.LastReliableWristWorld;

	FMediaPipeQuestWristHeldTargetLossInput HeldTargetLossInput;
	const float HeldTargetGraceSeconds = FMath::Max(0.0f, CVarQuestWristLostTrackingGraceSeconds.GetValueOnAnyThread());
	const double HeldTargetNowSeconds = FPlatformTime::Seconds();
	HeldTargetLossInput.bHasHeldTarget = QuestWristSideState.bHasHeldTarget;
	HeldTargetLossInput.GraceSeconds = HeldTargetGraceSeconds;
	HeldTargetLossInput.LastTargetAgeSeconds = QuestWristSideState.LastTargetTimeSeconds >= 0.0
		? static_cast<float>(HeldTargetNowSeconds - QuestWristSideState.LastTargetTimeSeconds)
		: -1.0f;
	const bool bHasRecentQuestWristHeldTarget =
		FMediaPipeQuestWristApplyPolicy::HasFreshHeldTargetForPositionAttempt(HeldTargetLossInput);

	FMediaPipeQuestWristPositionAttemptInput WristAttemptInput;
	WristAttemptInput.bQuestArmUsesWristEndpoint = bQuestArmUsesWristEndpoint;
	WristAttemptInput.bQuestArmUsesConstrainedSolve = bQuestArmUsesConstrainedSolve;
	WristAttemptInput.bQuestSideUsable = bQuestSideUsableForArm;
	WristAttemptInput.bHasHeldTarget = bHasRecentQuestWristHeldTarget;
	WristAttemptInput.RequestedPositionBlend = CVarQuestWristPositionBlend.GetValueOnAnyThread();
	const bool bQuestWristPositionCandidate =
		FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve(WristAttemptInput);

	FMediaPipeQuestArmHoldOnLossInput ArmHoldInput;
	ArmHoldInput.bHoldOnQuestHandLossEnabled =
		!bMetaHumanFullArmChainFresh &&
		CVarMediaPipeArmHoldOnQuestHandLoss.GetValueOnAnyThread() != 0;
	ArmHoldInput.bQuestHandTrackingEnabled = bQuestHandTrackingEnabled;
	ArmHoldInput.bQuestSideTracked = bQuestSideTrackedForArm;
	ArmHoldInput.bHasLastReliableArmSample = bHasLastReliableArmSample;
	ArmHoldInput.bQuestWristPositionCandidate = bQuestWristPositionCandidate;
	const bool bHoldArmOnQuestHandLoss =
		FMediaPipeQuestWristApplyPolicy::ShouldHoldArmOnQuestHandLoss(ArmHoldInput);

	float ArmObservationAlphaScale = 1.0f;
	if (!bMetaHumanFullArmChainFresh && bHoldArmOnQuestHandLoss)
	{
		ShoulderWorld = LastReliableShoulderWorld;
		ElbowWorld = LastReliableElbowWorld;
		WristWorld = LastReliableWristWorld;
		ArmObservationAlphaScale = 0.0f;
	}

	if (!bMetaHumanFullArmChainFresh &&
		!bUseHmdRelativeAvatarArmFrame &&
		CVarMediaPipeArmReliabilityGate.GetValueOnAnyThread() != 0)
	{
		const float ArmMinReliability = FMath::Clamp(CVarMediaPipeArmMinReliability.GetValueOnAnyThread(), 0.0f, 1.0f);
		const float ShoulderReliability = GetLandmarkReliability(ShoulderLm);
		const float ElbowReliability = GetLandmarkReliability(ElbowLm);
		const float WristReliability = GetLandmarkReliability(WristLm);
		const bool bReliableEnough =
			!bHoldArmOnQuestHandLoss &&
			ShoulderReliability >= ArmMinReliability &&
			ElbowReliability >= ArmMinReliability &&
			(bQuestWristPositionCandidate || WristReliability >= ArmMinReliability);
		const float ArmReliability = bQuestWristPositionCandidate
			? FMath::Min(ShoulderReliability, ElbowReliability)
			: FMath::Min(ShoulderReliability, FMath::Min(ElbowReliability, WristReliability));
		if (ArmMinReliability > 0.0f)
		{
			const float ReliabilityWeight = RemapClamped(ArmReliability, ArmMinReliability, FMath::Min(1.0f, ArmMinReliability + 0.20f));
			ArmObservationAlphaScale = FMath::Lerp(0.35f, 1.0f, ReliabilityWeight);
		}

		bool bStepAcceptable = true;
		bool bSegmentLengthAcceptable = true;
		if (bHasLastReliableArmSample)
		{
			const float MaxElbowStepCm = FMath::Max(0.0f, CVarMediaPipeArmMaxElbowStepCm.GetValueOnAnyThread());
			const float MaxWristStepCm = FMath::Max(0.0f, CVarMediaPipeArmMaxWristStepCm.GetValueOnAnyThread());
			const float ElbowStepCm = FVector::Dist(ElbowWorld, LastReliableElbowWorld);
			const float WristStepCm = FVector::Dist(WristWorld, LastReliableWristWorld);
			bStepAcceptable =
				(MaxElbowStepCm <= 0.0f || ElbowStepCm <= MaxElbowStepCm) &&
				(bQuestWristPositionCandidate || MaxWristStepCm <= 0.0f || WristStepCm <= MaxWristStepCm);

			const float MaxSegmentDeltaFraction = FMath::Max(0.0f, CVarMediaPipeArmMaxSegmentLengthDeltaFraction.GetValueOnAnyThread());
			if (MaxSegmentDeltaFraction > 0.0f)
			{
				const float MaxSegmentDeltaCm = FMath::Max(0.0f, CVarMediaPipeArmMaxSegmentLengthDeltaCm.GetValueOnAnyThread());
				const float UpperLenCm = FVector::Dist(ShoulderWorld, ElbowWorld);
				const float LowerLenCm = FVector::Dist(ElbowWorld, WristWorld);
				const float LastUpperLenCm = FVector::Dist(LastReliableShoulderWorld, LastReliableElbowWorld);
				const float LastLowerLenCm = FVector::Dist(LastReliableElbowWorld, LastReliableWristWorld);
				const float UpperToleranceCm = FMath::Max(MaxSegmentDeltaCm, LastUpperLenCm * MaxSegmentDeltaFraction);
				const float LowerToleranceCm = FMath::Max(MaxSegmentDeltaCm, LastLowerLenCm * MaxSegmentDeltaFraction);
				bSegmentLengthAcceptable =
					LastUpperLenCm <= KINDA_SMALL_NUMBER || FMath::Abs(UpperLenCm - LastUpperLenCm) <= UpperToleranceCm;
				bSegmentLengthAcceptable = bSegmentLengthAcceptable &&
					(bQuestWristPositionCandidate ||
					 LastLowerLenCm <= KINDA_SMALL_NUMBER ||
					 FMath::Abs(LowerLenCm - LastLowerLenCm) <= LowerToleranceCm);
			}
		}

		if (!bStepAcceptable || !bSegmentLengthAcceptable)
		{
			ArmObservationAlphaScale = FMath::Min(ArmObservationAlphaScale, 0.35f);
		}

		if (bReliableEnough && bStepAcceptable && bSegmentLengthAcceptable)
		{
			LastReliableShoulderWorld = ShoulderWorld;
			LastReliableElbowWorld = ElbowWorld;
			LastReliableWristWorld = WristWorld;
			bHasLastReliableArmSample = true;
		}
		else if (bHasLastReliableArmSample)
		{
			const float RejectedSampleAlpha = FMath::Clamp(CVarMediaPipeArmRejectedSampleAlpha.GetValueOnAnyThread(), 0.0f, 1.0f);
			const float HoldAlpha = bReliableEnough ? FMath::Min(ArmObservationAlphaScale, RejectedSampleAlpha) : 0.0f;
			ShoulderWorld = FMath::Lerp(LastReliableShoulderWorld, ShoulderWorld, HoldAlpha);
			ElbowWorld = FMath::Lerp(LastReliableElbowWorld, ElbowWorld, HoldAlpha);
			WristWorld = FMath::Lerp(LastReliableWristWorld, WristWorld, HoldAlpha);
			ArmObservationAlphaScale = FMath::Min(ArmObservationAlphaScale, HoldAlpha);
		}
		else
		{
			return;
		}
	}

	FMediaPipeAvatarArmBasisInput AvatarArmBasisInput;
	AvatarArmBasisInput.TargetComponentTransform = TargetCompTransform;
	AvatarArmBasisInput.bUseTargetFaceForwardAxis = bUseTargetFaceForwardAxis;
	const FMediaPipeAvatarArmBasisResult AvatarArmBasis =
		MediaPipeBodySolverMath::BuildAvatarArmBasis(AvatarArmBasisInput);
	FVector HipRightWorld = AvatarArmBasis.RightWorld;
	FVector ShoulderRightWorld = AvatarArmBasis.RightWorld;
	FVector UpWorld = AvatarArmBasis.UpWorld;
	FVector ForwardWorld = AvatarArmBasis.ForwardWorld;
	const bool bHasTorsoBasis = TryGetTorsoBasisWorld(HipRightWorld, ShoulderRightWorld, UpWorld, ForwardWorld);
	const float QuestConstrainedArmMaxReachFraction = FMath::Clamp(
		CVarQuestConstrainedArmMaxReachFraction.GetValueOnAnyThread(),
		0.50f,
		0.999f);

	FQuestWristMappingTrace QuestWristTrace;
	bool bHasRecentHmdRelativeReachContinuity = false;
	float PreviousHmdRelativeReachContinuityCm = 0.0f;
	auto StoreHmdRelativeReachContinuity = [&](const FVector& FinalArmWristWorld)
	{
		if (!bUseHmdRelativeAvatarArmFrame ||
			!bQuestArmUsesConstrainedSolve ||
			QuestWristTrace.bMapped == 0 ||
			QuestWristTrace.bConstrainedArmBodyFallbackApplied != 0)
		{
			return;
		}

		const float FinalReachCm = FVector::Dist(ShoulderWorld, FinalArmWristWorld);
		if (FinalReachCm <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		QuestWristSideState.bHasHmdRelativeReachContinuity = true;
		QuestWristSideState.HmdRelativeReachContinuityCm = FinalReachCm;
		QuestWristSideState.HmdRelativeReachContinuityTimeSeconds = FPlatformTime::Seconds();
	};
	bool bQuestWristPositionApplied =
		bQuestArmUsesWristEndpoint &&
		TryApplyQuestWristPositionWorld(CSPose, bIsLeft, WristWorld, WristWorld, &QuestWristTrace);
	if (!bQuestWristPositionApplied &&
		bUseHmdRelativeAvatarArmFrame &&
		bQuestArmUsesConstrainedSolve &&
		CVarQuestConstrainedArmBodyFallback.GetValueOnAnyThread() != 0 &&
		bHasMediaPipeArmWorld &&
		RefUpperLen > KINDA_SMALL_NUMBER &&
		RefLowerLen > KINDA_SMALL_NUMBER)
	{
		FMediaPipeConstrainedArmFallbackInput FallbackInput;
		FallbackInput.SourceShoulderWorld = MediaPipeSourceShoulderWorld;
		FallbackInput.SourceElbowWorld = MediaPipeSourceElbowWorld;
		FallbackInput.SourceWristWorld = MediaPipeSourceWristWorld;
		FallbackInput.TargetShoulderWorld = ShoulderWorld;
		FallbackInput.UpWorld = UpWorld;
		FallbackInput.ShoulderRightWorld = ShoulderRightWorld;
		FallbackInput.bHasTorsoBasis = bHasTorsoBasis;
		FallbackInput.bIsLeft = bIsLeft;
		FallbackInput.TargetUpperLenCm = RefUpperLen;
		FallbackInput.TargetLowerLenCm = RefLowerLen;
		FallbackInput.MaxReachFraction = QuestConstrainedArmMaxReachFraction;
		FallbackInput.bEnableDownStraighten =
			CVarQuestConstrainedArmDownStraighten.GetValueOnAnyThread() != 0;
		FallbackInput.DownStraightenThresholdCm = CVarQuestConstrainedArmDownStraightenThresholdCm.GetValueOnAnyThread();
		FallbackInput.DownStraightenMaxCm = CVarQuestConstrainedArmDownStraightenMaxCm.GetValueOnAnyThread();
		FallbackInput.DownStraightenMinBelowShoulderRatio = CVarQuestConstrainedArmDownStraightenMinBelowShoulderRatio.GetValueOnAnyThread();
		FallbackInput.DownStraightenReachFloorFraction = CVarQuestConstrainedArmDownStraightenReachFloorFraction.GetValueOnAnyThread();
		FallbackInput.DownStraightenMaxReachFraction = CVarQuestConstrainedArmDownStraightenMaxReachFraction.GetValueOnAnyThread();

		FMediaPipeConstrainedArmFallbackResult FallbackResult;
		if (FMediaPipeQuestConstrainedArmSolver::BuildBodyFallbackEndpoint(FallbackInput, FallbackResult))
		{
			FVector FallbackElbowWorld = FallbackResult.TargetElbowWorld;
			FVector FallbackWristWorld = FallbackResult.TargetWristWorld;
			FMediaPipeArmSolverState& ActiveArmState = bIsLeft ? LeftArmState : RightArmState;
			const double NowSeconds = FPlatformTime::Seconds();
			const float LastSolveMaxAgeSeconds = FMath::Max(
				0.50f,
				CVarQuestWristLostTrackingGraceSeconds.GetValueOnAnyThread() + 0.50f);
			FMediaPipeConstrainedArmFallbackContinuityInput ContinuityInput;
			ContinuityInput.FallbackElbowWorld = FallbackResult.TargetElbowWorld;
			ContinuityInput.FallbackWristWorld = FallbackResult.TargetWristWorld;
			ContinuityInput.bConstrainElbowToCurrentSide = true;
			ContinuityInput.bIsLeft = bIsLeft;
			ContinuityInput.TargetShoulderWorld = FallbackInput.TargetShoulderWorld;
			ContinuityInput.ShoulderRightWorld = FallbackInput.ShoulderRightWorld;
			ContinuityInput.bHasLastConstrainedArmSolve = ActiveArmState.bHasLastConstrainedArmSolve;
			ContinuityInput.LastConstrainedArmElbowWorld = ActiveArmState.LastConstrainedArmElbowWorld;
			ContinuityInput.LastConstrainedArmWristWorld = ActiveArmState.LastConstrainedArmWristWorld;
			ContinuityInput.LastSolveAgeSeconds = ActiveArmState.LastConstrainedArmSolveTimeSeconds >= 0.0
				? static_cast<float>(NowSeconds - ActiveArmState.LastConstrainedArmSolveTimeSeconds)
				: -1.0f;
			ContinuityInput.MaxLastSolveAgeSeconds = LastSolveMaxAgeSeconds;
			ContinuityInput.DeltaSeconds = FMath::Max(DeltaSeconds, CachedDeltaTimeSeconds);
			ContinuityInput.WristHalfLifeSeconds = FMath::Max(
				0.0f,
				CVarQuestConstrainedArmBodyFallbackWristHalfLife.GetValueOnAnyThread());
			ContinuityInput.MaxWristStepCm = FMath::Max(
				0.0f,
				CVarQuestConstrainedArmBodyFallbackMaxWristStepCm.GetValueOnAnyThread());
			ContinuityInput.MaxElbowStepCm = ContinuityInput.MaxWristStepCm;

			FMediaPipeConstrainedArmFallbackContinuityResult ContinuityResult;
			if (FMediaPipeQuestConstrainedArmSolver::ApplyBodyFallbackContinuity(ContinuityInput, ContinuityResult))
			{
				FallbackElbowWorld = ContinuityResult.TargetElbowWorld;
				FallbackWristWorld = ContinuityResult.TargetWristWorld;
			}
			if (ContinuityResult.bUsedContinuity)
			{
				QuestWristTrace.bPositionFilterApplied = 1;
				QuestWristTrace.PositionFilterAlpha = ContinuityResult.BlendAlpha;
				QuestWristTrace.PositionFilterTargetDeltaCm = ContinuityResult.RawWristStepCm;
				QuestWristTrace.PositionFilterFilteredDeltaCm = ContinuityResult.FilteredWristStepCm;
				QuestWristTrace.PositionFilterSpeedCmSec = ContinuityResult.FilteredWristSpeedCmSec;
			}

			ElbowWorld = FallbackElbowWorld;
			WristWorld = FallbackWristWorld;
			QuestWristTrace.MediaPipeWristWorld = MediaPipeSourceWristWorld;
			QuestWristTrace.MappedQuestWristWorld = FallbackResult.TargetWristWorld;
			QuestWristTrace.FinalWristWorld = FallbackWristWorld;
			QuestWristTrace.MappedOffsetFromMediaPipeCm = FVector::Dist(FallbackResult.TargetWristWorld, MediaPipeSourceWristWorld);
			QuestWristTrace.CalibrationMode = EQuestMediaSpaceCalibrationMode::HmdRelativeAvatar;
			QuestWristTrace.bMapped = 1;
			QuestWristTrace.bPositionApplied = 1;
			if (FallbackResult.bReachClamped || FallbackResult.bDownStraightened)
			{
				QuestWristTrace.bReachClamped = 1;
			}
			QuestWristTrace.bUsedHeldQuestWrist = 1;
			QuestWristTrace.bConstrainedArmBodyFallbackApplied = 1;
			QuestWristTrace.bConstrainedArmBodyFallbackDownStraightened = FallbackResult.bDownStraightened ? 1 : 0;
			QuestWristTrace.ConstrainedArmBodyFallbackReachFraction = FallbackResult.SourceReachFraction;
			QuestWristTrace.ConstrainedArmBodyFallbackTargetReachCm = FallbackResult.TargetReachCm;
			QuestWristTrace.ConstrainedArmBodyFallbackTargetReachFraction = FallbackResult.TargetReachCm / FMath::Max(RefUpperLen + RefLowerLen, UE_SMALL_NUMBER);
			QuestWristTrace.ConstrainedArmBodyFallbackDownStraightenAlpha = FallbackResult.DownStraightenAdaptiveAlpha;
			QuestWristTrace.bQuestTracked = bQuestSideTrackedForArm ? 1 : 0;
			bQuestWristPositionApplied = true;
		}
	}
	const bool bWasDropoutDownFallbackActive = QuestWristSideState.bDropoutDownFallbackActive;
	if (bQuestSideTrackedForArm)
	{
		if (bWasDropoutDownFallbackActive)
		{
			const double ReacquireNowSeconds = FPlatformTime::Seconds();
			const float ReacquireSuppressSeconds = FMath::Max(
				0.10f,
				CVarQuestArmDropoutDownFallbackBlendHalfLife.GetValueOnAnyThread() * 3.0f);
			QuestWristSideState.DropoutReacquireReachScaleSuppressUntilTimeSeconds = FMath::Max(
				QuestWristSideState.DropoutReacquireReachScaleSuppressUntilTimeSeconds,
				ReacquireNowSeconds + ReacquireSuppressSeconds);
		}
		QuestWristSideState.bDropoutDownFallbackActive = false;
		QuestWristSideState.DropoutDownFallbackLastUpdateTimeSeconds = -1.0;
	}
	const bool bQuestArmCanUseDropoutDownFallback =
		CVarQuestArmDropoutDownFallback.GetValueOnAnyThread() != 0 &&
		bUseHmdRelativeAvatarArmFrame &&
		bQuestArmUsesConstrainedSolve &&
		bQuestHandTrackingEnabled &&
		!bQuestSideTrackedForArm &&
		RefUpperLen > KINDA_SMALL_NUMBER &&
		RefLowerLen > KINDA_SMALL_NUMBER;
	const bool bQuestArmHasOnlyDropoutEndpoint =
		!bQuestWristPositionApplied ||
		QuestWristTrace.bUsedUntrackedJointData != 0 ||
		QuestWristTrace.bUsedHeldQuestWrist != 0 ||
		QuestWristTrace.bRawQuestRejected != 0;
	if (bQuestArmCanUseDropoutDownFallback && bQuestArmHasOnlyDropoutEndpoint)
	{
		FVector DropoutUpWorld = UpWorld.GetSafeNormal();
		if (DropoutUpWorld.IsNearlyZero())
		{
			DropoutUpWorld = TargetCompTransform.GetUnitAxis(EAxis::Z).GetSafeNormal();
		}
		if (DropoutUpWorld.IsNearlyZero())
		{
			DropoutUpWorld = FVector::UpVector;
		}

		const double NowSeconds = FPlatformTime::Seconds();
		const FMediaPipeArmSolverState& DropoutArmState = bIsLeft ? LeftArmState : RightArmState;
		const float RecentTrackedSeconds = FMath::Max(
			0.0f,
			CVarQuestArmDropoutDownFallbackRecentTrackedSeconds.GetValueOnAnyThread());
		const float LastTrackedAgeSeconds =
			QuestWristSideState.LastTrackedQuestArmTimeSeconds >= 0.0
				? static_cast<float>(NowSeconds - QuestWristSideState.LastTrackedQuestArmTimeSeconds)
				: -1.0f;
		const bool bHasRecentTrackedArmPose =
			QuestWristSideState.bHasLastTrackedQuestArmPose &&
			LastTrackedAgeSeconds >= 0.0f &&
			(RecentTrackedSeconds <= KINDA_SMALL_NUMBER || LastTrackedAgeSeconds <= RecentTrackedSeconds);
		const float MinDownDominance = FMath::Clamp(
			CVarQuestArmDropoutDownFallbackMinDownDominance.GetValueOnAnyThread(),
			0.0f,
			1.0f);
		const bool bLastTrackedPoseWasDown =
			bHasRecentTrackedArmPose &&
			QuestWristSideState.LastTrackedQuestArmDownDominance >= MinDownDominance;
		const float LastSolveMaxAgeSeconds = FMath::Max(
			0.50f,
			CVarQuestWristLostTrackingGraceSeconds.GetValueOnAnyThread() + 0.50f);
		const float LastConstrainedAgeSeconds =
			DropoutArmState.LastConstrainedArmSolveTimeSeconds >= 0.0
				? static_cast<float>(NowSeconds - DropoutArmState.LastConstrainedArmSolveTimeSeconds)
				: -1.0f;
		const bool bHasRecentConstrainedArmSolve =
			DropoutArmState.bHasLastConstrainedArmSolve &&
			LastConstrainedAgeSeconds >= 0.0f &&
			LastConstrainedAgeSeconds <= LastSolveMaxAgeSeconds &&
			!DropoutArmState.LastConstrainedArmShoulderWorld.ContainsNaN() &&
			!DropoutArmState.LastConstrainedArmElbowWorld.ContainsNaN() &&
			!DropoutArmState.LastConstrainedArmWristWorld.ContainsNaN();
		const bool bHasLastReliableArmPoseSeed =
			bHasLastReliableArmSample &&
			!LastReliableShoulderWorld.ContainsNaN() &&
			!LastReliableElbowWorld.ContainsNaN() &&
			!LastReliableWristWorld.ContainsNaN();
		const bool bHasAcceptedArmLengthCalibration =
			QuestWristRuntimeState.ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_Accepted &&
			QuestWristSideState.bHasArmLengthCalibrationForwardReach &&
			QuestWristSideState.ArmLengthCalibrationForwardReachCm > KINDA_SMALL_NUMBER &&
			QuestWristSideState.bHasArmLengthCalibrationDownSample &&
			QuestWristSideState.ArmLengthCalibrationDownDropCm > KINDA_SMALL_NUMBER;

		const float FullReachCm = RefUpperLen + RefLowerLen;
		const float MaxReachCm = FullReachCm * QuestConstrainedArmMaxReachFraction;
		const float MinReachCm = FMath::Abs(RefUpperLen - RefLowerLen) + 0.5f;
		float CalibratedDirectReachCm = MaxReachCm;
		if (QuestWristSideState.bHasArmLengthCalibrationForwardReach &&
			QuestWristSideState.ArmLengthCalibrationForwardReachCm > KINDA_SMALL_NUMBER)
		{
			CalibratedDirectReachCm = QuestWristSideState.ArmLengthCalibrationForwardReachCm;
		}
		CalibratedDirectReachCm = FMath::Clamp(
			CalibratedDirectReachCm,
			MinReachCm,
			FMath::Max(MinReachCm, MaxReachCm));
		const float CurrentDropoutEndpointReachCm = FVector::Dist(ShoulderWorld, WristWorld);
		const float InferDownPoseMaxReachFraction = FMath::Clamp(
			CVarQuestArmLengthCalibrationDownMinCorrectedReachFraction.GetValueOnAnyThread(),
			0.50f,
			1.0f);
		const bool bCanInferCalibratedDownPose =
			!bQuestWristPositionApplied ||
			CurrentDropoutEndpointReachCm <= CalibratedDirectReachCm * InferDownPoseMaxReachFraction;

		FVector MediaPipeHorizontalHintWorld = FVector::ZeroVector;
		float MediaPipeDownDominance = 0.0f;
		bool bMediaPipeDownHint = false;
		if (bHasMediaPipeArmWorld)
		{
			const FVector SourceShoulderToWristWorld = MediaPipeSourceWristWorld - MediaPipeSourceShoulderWorld;
			const float SourceReachCm = SourceShoulderToWristWorld.Size();
			const float SourceBelowShoulderCm = FVector::DotProduct(MediaPipeSourceShoulderWorld - MediaPipeSourceWristWorld, DropoutUpWorld);
			MediaPipeDownDominance = SourceReachCm > KINDA_SMALL_NUMBER
				? FMath::Max(0.0f, SourceBelowShoulderCm) / SourceReachCm
				: 0.0f;
			if (MediaPipeDownDominance >= MinDownDominance)
			{
				MediaPipeHorizontalHintWorld =
					SourceShoulderToWristWorld -
					FVector::DotProduct(SourceShoulderToWristWorld, DropoutUpWorld) * DropoutUpWorld;
				bMediaPipeDownHint = true;
			}
		}

		const bool bContinueActiveFallback =
			QuestWristSideState.bDropoutDownFallbackActive;

		FMediaPipeQuestArmDropoutDownFallbackPolicyInput DropoutPolicyInput;
		DropoutPolicyInput.bEnabled = CVarQuestArmDropoutDownFallback.GetValueOnAnyThread() != 0;
		DropoutPolicyInput.bUseHmdRelativeAvatarArmFrame = bUseHmdRelativeAvatarArmFrame;
		DropoutPolicyInput.bQuestArmUsesConstrainedSolve = bQuestArmUsesConstrainedSolve;
		DropoutPolicyInput.bQuestHandTrackingEnabled = bQuestHandTrackingEnabled;
		DropoutPolicyInput.bQuestSideTracked = bQuestSideTrackedForArm;
		DropoutPolicyInput.bHasOnlyDropoutEndpoint = bQuestArmHasOnlyDropoutEndpoint;
		DropoutPolicyInput.bHasRecentTrackedArmPose = bHasRecentTrackedArmPose;
		DropoutPolicyInput.bLastTrackedPoseWasDown = bLastTrackedPoseWasDown;
		DropoutPolicyInput.bHasMediaPipeDownHint = bMediaPipeDownHint;
		DropoutPolicyInput.bContinueActiveFallback = bContinueActiveFallback;
		DropoutPolicyInput.bHasAcceptedArmLengthCalibration = bHasAcceptedArmLengthCalibration;
		DropoutPolicyInput.bCanInferCalibratedDownPose = bCanInferCalibratedDownPose;
		DropoutPolicyInput.bHasRecentConstrainedArmSolve = bHasRecentConstrainedArmSolve;
		DropoutPolicyInput.bHasLastReliableArmSample = bHasLastReliableArmPoseSeed;
		const FMediaPipeQuestArmDropoutDownFallbackPolicyResult DropoutPolicy =
			FMediaPipeQuestWristApplyPolicy::ShouldUseDropoutDownFallback(DropoutPolicyInput);

		if (DropoutPolicy.bUseFallback)
		{
			FVector SeedShoulderToWristWorld = FVector::ZeroVector;
			FVector SeedShoulderToElbowWorld = FVector::ZeroVector;
			bool bHasSeedArmPose = false;
			if (bHasRecentTrackedArmPose)
			{
				SeedShoulderToWristWorld =
					QuestWristSideState.LastTrackedQuestArmWristWorld -
					QuestWristSideState.LastTrackedQuestArmShoulderWorld;
				SeedShoulderToElbowWorld =
					QuestWristSideState.LastTrackedQuestArmElbowWorld -
					QuestWristSideState.LastTrackedQuestArmShoulderWorld;
				bHasSeedArmPose = true;
			}
			else if (bHasRecentConstrainedArmSolve)
			{
				SeedShoulderToWristWorld =
					DropoutArmState.LastConstrainedArmWristWorld -
					DropoutArmState.LastConstrainedArmShoulderWorld;
				SeedShoulderToElbowWorld =
					DropoutArmState.LastConstrainedArmElbowWorld -
					DropoutArmState.LastConstrainedArmShoulderWorld;
				bHasSeedArmPose = true;
			}
			else if (bHasLastReliableArmPoseSeed)
			{
				SeedShoulderToWristWorld = LastReliableWristWorld - LastReliableShoulderWorld;
				SeedShoulderToElbowWorld = LastReliableElbowWorld - LastReliableShoulderWorld;
				bHasSeedArmPose = true;
			}

			FVector HorizontalHintWorld = FVector::ZeroVector;
			if (!DropoutPolicy.bInferCalibratedDownPose && bHasSeedArmPose)
			{
				HorizontalHintWorld =
					SeedShoulderToWristWorld -
					FVector::DotProduct(SeedShoulderToWristWorld, DropoutUpWorld) * DropoutUpWorld;
			}
			if (bMediaPipeDownHint && !MediaPipeHorizontalHintWorld.IsNearlyZero())
			{
				HorizontalHintWorld = HorizontalHintWorld.IsNearlyZero()
					? MediaPipeHorizontalHintWorld
					: FMath::Lerp(HorizontalHintWorld, MediaPipeHorizontalHintWorld, 0.35f);
			}
			if (HorizontalHintWorld.IsNearlyZero())
			{
				FVector OutwardWorld = bIsLeft ? -ShoulderRightWorld : ShoulderRightWorld;
				OutwardWorld = (OutwardWorld - FVector::DotProduct(OutwardWorld, DropoutUpWorld) * DropoutUpWorld).GetSafeNormal();
				if (OutwardWorld.IsNearlyZero())
				{
					OutwardWorld = (bIsLeft ? -TargetCompTransform.GetUnitAxis(EAxis::Y) : TargetCompTransform.GetUnitAxis(EAxis::Y)).GetSafeNormal();
				}
				HorizontalHintWorld = OutwardWorld * (CalibratedDirectReachCm * 0.20f);
			}
			const float MaxHorizontalCm = CalibratedDirectReachCm * 0.45f;
			if (HorizontalHintWorld.SizeSquared() > FMath::Square(MaxHorizontalCm))
			{
				HorizontalHintWorld = HorizontalHintWorld.GetSafeNormal() * MaxHorizontalCm;
			}

			const float HorizontalCm = HorizontalHintWorld.Size();
			const float TargetDownDropCm = FMath::Sqrt(FMath::Max(
				0.0f,
				CalibratedDirectReachCm * CalibratedDirectReachCm - HorizontalCm * HorizontalCm));
			const FVector RawFallbackWristWorld =
				ShoulderWorld +
				HorizontalHintWorld -
				DropoutUpWorld * TargetDownDropCm;
			const FVector SeedWristWorld =
				QuestWristSideState.bDropoutDownFallbackActive
					? QuestWristSideState.DropoutDownFallbackWristWorld
					: bHasSeedArmPose
						? ShoulderWorld + SeedShoulderToWristWorld
						: WristWorld;
			const FVector SeedElbowWorld =
				QuestWristSideState.bDropoutDownFallbackActive
					? QuestWristSideState.DropoutDownFallbackElbowWorld
					: bHasSeedArmPose
						? ShoulderWorld + SeedShoulderToElbowWorld
						: ElbowWorld;
			float FallbackDeltaSeconds = QuestWristSideState.DropoutDownFallbackLastUpdateTimeSeconds >= 0.0
				? static_cast<float>(NowSeconds - QuestWristSideState.DropoutDownFallbackLastUpdateTimeSeconds)
				: FMath::Max(DeltaSeconds, CachedDeltaTimeSeconds);
			if (!FMath::IsFinite(FallbackDeltaSeconds) || FallbackDeltaSeconds <= 0.0f || FallbackDeltaSeconds > 0.25f)
			{
				FallbackDeltaSeconds = FMath::Max(DeltaSeconds, CachedDeltaTimeSeconds);
			}
			const float FallbackAlpha = HalfLifeToAlpha(
				FMath::Max(0.0f, CVarQuestArmDropoutDownFallbackBlendHalfLife.GetValueOnAnyThread()),
				FallbackDeltaSeconds);

			WristWorld = FMath::Lerp(SeedWristWorld, RawFallbackWristWorld, FallbackAlpha);
			ElbowWorld = SeedElbowWorld;
			QuestWristSideState.bDropoutDownFallbackActive = true;
			QuestWristSideState.DropoutDownFallbackWristWorld = WristWorld;
			QuestWristSideState.DropoutDownFallbackElbowWorld = ElbowWorld;
			QuestWristSideState.DropoutDownFallbackLastUpdateTimeSeconds = NowSeconds;

			QuestWristTrace.bMapped = 1;
			QuestWristTrace.bPositionApplied = 1;
			QuestWristTrace.bUsedHeldQuestWrist = 1;
			QuestWristTrace.bConstrainedArmDropoutDownFallbackApplied = 1;
			QuestWristTrace.bConstrainedArmDropoutMediaPipeHintUsed = bMediaPipeDownHint ? 1 : 0;
			QuestWristTrace.CalibrationMode = EQuestMediaSpaceCalibrationMode::HmdRelativeAvatar;
			const FVector DropoutReferenceWristWorld = bHasMediaPipeArmWorld
				? MediaPipeSourceWristWorld
				: QuestWristTrace.MediaPipeWristWorld;
			QuestWristTrace.MediaPipeWristWorld = DropoutReferenceWristWorld;
			QuestWristTrace.MappedQuestWristWorld = RawFallbackWristWorld;
			QuestWristTrace.FinalWristWorld = WristWorld;
			QuestWristTrace.MappedOffsetFromMediaPipeCm = FVector::Dist(RawFallbackWristWorld, DropoutReferenceWristWorld);
			QuestWristTrace.ConstrainedArmDropoutDownFallbackAlpha = FallbackAlpha;
			QuestWristTrace.ConstrainedArmDropoutDirectReachCm = FVector::Dist(ShoulderWorld, WristWorld);
			QuestWristTrace.ConstrainedArmDropoutTargetReachCm = CalibratedDirectReachCm;
			QuestWristTrace.ConstrainedArmDropoutDownDominance = FMath::Max(
				FMath::Max(
					bHasRecentTrackedArmPose ? QuestWristSideState.LastTrackedQuestArmDownDominance : 0.0f,
					MediaPipeDownDominance),
				DropoutPolicy.bInferCalibratedDownPose ? 1.0f : 0.0f);
			QuestWristTrace.ConstrainedArmDropoutLastTrackedAgeSeconds =
				bHasRecentTrackedArmPose
					? LastTrackedAgeSeconds
					: LastConstrainedAgeSeconds;
			bQuestWristPositionApplied = true;
		}
	}
	FMediaPipeQuestArmLengthCalibrationOwnerPolicyInput ArmLengthOwnerInput;
	ArmLengthOwnerInput.bTargetIsMetaHuman = TargetMetaHumanProfile.bIsMetaHuman;
	ArmLengthOwnerInput.bTargetProfileActive = TargetMetaHumanProfile.bIsActiveProfile;
	ArmLengthOwnerInput.bHasTargetEmbodimentProfile = bHasTargetEmbodimentProfile;
	ArmLengthOwnerInput.bTargetIsMannyLike =
		TargetEmbodimentProfile.SkeletonFamily == EMediaPipeAvatarSkeletonFamily::MannyLike;
	const bool bQuestArmLengthCalibrationOwner =
		FMediaPipeQuestWristApplyPolicy::ShouldOwnArmLengthCalibration(ArmLengthOwnerInput);
	const bool bQuestArmLengthCalibrationAccepted =
		QuestWristRuntimeState.ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_Accepted;

	if (bQuestWristPositionApplied)
	{
		const float MaxReachCm = (RefUpperLen + RefLowerLen) * QuestConstrainedArmMaxReachFraction;
		const float MinReachCm = FMath::Abs(RefUpperLen - RefLowerLen) + 0.5f;
		const float ConfiguredMaxReachStepCm = FMath::Max(0.0f, CVarQuestConstrainedArmMaxReachStepCm.GetValueOnAnyThread());
		const float MaxReachStepCm = ConfiguredMaxReachStepCm;
		FVector ShoulderToWristWorld = WristWorld - ShoulderWorld;
		float WristReachCm = ShoulderToWristWorld.Size();
		const double ReachScaleNowSeconds = FPlatformTime::Seconds();
		const bool bSuppressReachScaleAfterDropout =
			bQuestSideTrackedForArm &&
			QuestWristSideState.DropoutReacquireReachScaleSuppressUntilTimeSeconds >= ReachScaleNowSeconds &&
			QuestWristTrace.bMapped != 0 &&
			QuestWristTrace.bUsedHeldQuestWrist == 0 &&
			QuestWristTrace.bConstrainedArmBodyFallbackApplied == 0 &&
			QuestWristTrace.bConstrainedArmDropoutDownFallbackApplied == 0;
		if (bUseHmdRelativeAvatarArmFrame &&
			CVarQuestConstrainedArmReachScaleCalibration.GetValueOnAnyThread() != 0 &&
			QuestWristTrace.bMapped != 0 &&
			QuestWristTrace.bUsedHeldQuestWrist == 0 &&
			QuestWristTrace.bConstrainedArmBodyFallbackApplied == 0 &&
			bQuestSideTrackedForArm &&
			WristReachCm > KINDA_SMALL_NUMBER &&
			MaxReachCm > KINDA_SMALL_NUMBER)
		{
			const float MinObservedFraction = FMath::Clamp(
				CVarQuestConstrainedArmReachScaleMinObservedFraction.GetValueOnAnyThread(),
				0.0f,
				1.0f);
			const float ReachScaleMin = FMath::Max(
				0.01f,
				CVarQuestConstrainedArmReachScaleMin.GetValueOnAnyThread());
			const float ReachScaleMax = FMath::Max(
				ReachScaleMin,
				CVarQuestConstrainedArmReachScaleMax.GetValueOnAnyThread());
			const float MinObservedReachCm = MaxReachCm * MinObservedFraction;
			const float MaxObservedReachCm = MaxReachCm / ReachScaleMin;
			if (!bSuppressReachScaleAfterDropout &&
				WristReachCm >= MinObservedReachCm &&
				WristReachCm <= MaxObservedReachCm &&
				(!QuestWristSideState.bHasHmdRelativeReachObservedMax ||
				 WristReachCm > QuestWristSideState.HmdRelativeReachObservedMaxCm))
			{
				QuestWristSideState.bHasHmdRelativeReachObservedMax = true;
				QuestWristSideState.HmdRelativeReachObservedMaxCm = WristReachCm;
			}

			if (QuestWristSideState.bHasHmdRelativeReachObservedMax)
			{
				FMediaPipeQuestReachScaleCalibrationInput ReachScaleInput;
				ReachScaleInput.bEnabled = true;
				ReachScaleInput.bSuppressDuringDropoutReacquire =
					bSuppressReachScaleAfterDropout ||
					QuestWristTrace.bUsedHeldQuestWrist != 0 ||
					QuestWristTrace.bUsedUntrackedJointData != 0;
				ReachScaleInput.bAllowScaleBelowOne =
					!bQuestArmLengthCalibrationOwner || bQuestArmLengthCalibrationAccepted;
				ReachScaleInput.CurrentReachCm = WristReachCm;
				ReachScaleInput.ObservedMaxReachCm = QuestWristSideState.HmdRelativeReachObservedMaxCm;
				ReachScaleInput.TargetMinReachCm = MinReachCm;
				ReachScaleInput.TargetMaxReachCm = MaxReachCm;
				ReachScaleInput.MinObservedTargetFraction = MinObservedFraction;
				ReachScaleInput.bApplyUniformScale =
					CVarQuestConstrainedArmReachScaleUniform.GetValueOnAnyThread() != 0;
				ReachScaleInput.ApplyStartObservedFraction =
					CVarQuestConstrainedArmReachScaleApplyStartFraction.GetValueOnAnyThread();
				ReachScaleInput.ApplyFullObservedFraction =
					CVarQuestConstrainedArmReachScaleApplyFullFraction.GetValueOnAnyThread();
				ReachScaleInput.MinScale = ReachScaleMin;
				ReachScaleInput.MaxScale = ReachScaleMax;
				const FMediaPipeQuestReachScaleCalibrationResult ReachScaleResult =
					FMediaPipeQuestWristApplyPolicy::ComputeReachScaleCalibration(ReachScaleInput);
				QuestWristTrace.ConstrainedArmReachScale = ReachScaleResult.Scale;
				QuestWristTrace.ConstrainedArmReachScaleAlpha = ReachScaleResult.ApplyAlpha;
				QuestWristTrace.ConstrainedArmReachScaleObservedMaxCm = QuestWristSideState.HmdRelativeReachObservedMaxCm;
				QuestWristTrace.ConstrainedArmReachScaleTargetReachCm = ReachScaleResult.TargetReachCm;
				if (ReachScaleResult.bApplied)
				{
					WristWorld = ShoulderWorld + ShoulderToWristWorld.GetSafeNormal() * ReachScaleResult.TargetReachCm;
					ShoulderToWristWorld = WristWorld - ShoulderWorld;
					WristReachCm = ReachScaleResult.TargetReachCm;
					QuestWristTrace.FinalWristWorld = WristWorld;
					QuestWristTrace.bConstrainedArmReachScaleApplied = 1;
				}
			}
		}
		if (bQuestArmLengthCalibrationOwner &&
			bUseHmdRelativeAvatarArmFrame &&
			bQuestArmUsesConstrainedSolve &&
			CVarQuestArmLengthCalibrationStartup.GetValueOnAnyThread() != 0 &&
			QuestWristTrace.bMapped != 0 &&
			QuestWristTrace.bUsedHeldQuestWrist == 0 &&
			QuestWristTrace.bConstrainedArmBodyFallbackApplied == 0 &&
			bQuestSideTrackedForArm &&
			WristReachCm > KINDA_SMALL_NUMBER &&
			MaxReachCm > KINDA_SMALL_NUMBER)
		{
			const FVector UpAxisWorld = UpWorld.IsNearlyZero()
				? TargetCompTransform.GetUnitAxis(EAxis::Z).GetSafeNormal()
				: UpWorld.GetSafeNormal();
			const double NowSeconds = FPlatformTime::Seconds();
			const float BelowShoulderCm = FVector::DotProduct(ShoulderWorld - WristWorld, UpAxisWorld);
			const float DownDropCm = FMath::Max(0.0f, BelowShoulderCm);
			const float DownDominance = WristReachCm > KINDA_SMALL_NUMBER ? DownDropCm / WristReachCm : 0.0f;

			if (QuestWristSideState.bHasArmLengthCalibrationLastSample &&
				QuestWristSideState.ArmLengthCalibrationLastSampleTimeSeconds >= 0.0 &&
				NowSeconds > QuestWristSideState.ArmLengthCalibrationLastSampleTimeSeconds)
			{
				const float SampleDeltaSeconds = static_cast<float>(NowSeconds - QuestWristSideState.ArmLengthCalibrationLastSampleTimeSeconds);
				QuestWristSideState.ArmLengthCalibrationLastVelocityCmSec =
					FVector::Dist(WristWorld, QuestWristSideState.ArmLengthCalibrationLastWristWorld) /
					FMath::Max(SampleDeltaSeconds, UE_SMALL_NUMBER);
			}
			else
			{
				QuestWristSideState.ArmLengthCalibrationLastVelocityCmSec = 0.0f;
			}
			QuestWristSideState.bHasArmLengthCalibrationLastSample = true;
			QuestWristSideState.ArmLengthCalibrationLastWristWorld = WristWorld;
			QuestWristSideState.ArmLengthCalibrationLastSampleTimeSeconds = NowSeconds;

			QuestWristSideState.bHasArmLengthCalibrationCandidate = true;
			QuestWristSideState.bArmLengthCalibrationCandidateTracked = true;
			QuestWristSideState.ArmLengthCalibrationCandidateWristWorld = WristWorld;
			QuestWristSideState.ArmLengthCalibrationCandidateShoulderWorld = ShoulderWorld;
			QuestWristSideState.ArmLengthCalibrationCandidateReachCm = WristReachCm;
			QuestWristSideState.ArmLengthCalibrationCandidateBelowShoulderCm = DownDropCm;
			QuestWristSideState.ArmLengthCalibrationCandidateVerticalDominance = DownDominance;
			QuestWristSideState.ArmLengthCalibrationCandidateTimeSeconds = NowSeconds;

			auto ResetArmLengthCalibrationProgress = [&]()
			{
				QuestWristRuntimeState.ArmLengthCalibrationStableFrameCount = 0;
				QuestWristRuntimeState.ArmLengthCalibrationStableSeconds = 0.0f;
				QuestWristRuntimeState.ArmLengthCalibrationLastUpdateTimeSeconds = -1.0;
			};

			auto IsRecentCandidate = [&](const FQuestWristSideRuntimeState& SideState) -> bool
			{
				return SideState.bHasArmLengthCalibrationCandidate &&
					SideState.bArmLengthCalibrationCandidateTracked &&
					SideState.ArmLengthCalibrationCandidateTimeSeconds >= 0.0 &&
					NowSeconds - SideState.ArmLengthCalibrationCandidateTimeSeconds <= 0.25;
			};

			FQuestWristSideRuntimeState& LeftSideState = QuestWristRuntimeState.Left;
			FQuestWristSideRuntimeState& RightSideState = QuestWristRuntimeState.Right;
			if (IsRecentCandidate(LeftSideState) && IsRecentCandidate(RightSideState))
			{
				if (QuestWristRuntimeState.ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_WaitingForHands)
				{
					QuestWristRuntimeState.ArmLengthCalibrationStage = QuestArmLengthCalibrationStage_ForwardReach;
					ResetArmLengthCalibrationProgress();
				}

				const float MaxHandVelocityCmSec = FMath::Max(
					0.0f,
					CVarQuestArmLengthCalibrationMaxHandVelocityCmSec.GetValueOnAnyThread());
				const bool bHandsStable =
					(MaxHandVelocityCmSec <= KINDA_SMALL_NUMBER ||
					 (LeftSideState.ArmLengthCalibrationLastVelocityCmSec <= MaxHandVelocityCmSec &&
					  RightSideState.ArmLengthCalibrationLastVelocityCmSec <= MaxHandVelocityCmSec));

				bool bPoseValid = false;
				float LeftCorrectedDownReachCm = 0.0f;
				float RightCorrectedDownReachCm = 0.0f;
				float LeftCorrectedDownDropCm = 0.0f;
				float RightCorrectedDownDropCm = 0.0f;
				if (QuestWristRuntimeState.ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_ForwardReach)
				{
					const float ForwardMinReachFraction = FMath::Clamp(
						CVarQuestArmLengthCalibrationForwardMinReachFraction.GetValueOnAnyThread(),
						0.0f,
						1.0f);
					const float MinForwardReachCm = MaxReachCm * ForwardMinReachFraction;
					bPoseValid =
						LeftSideState.ArmLengthCalibrationCandidateReachCm >= MinForwardReachCm &&
						RightSideState.ArmLengthCalibrationCandidateReachCm >= MinForwardReachCm;
				}
				else if (QuestWristRuntimeState.ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_DownReach)
				{
					const float DownMinBelowShoulderFraction = FMath::Clamp(
						CVarQuestArmLengthCalibrationDownMinBelowShoulderFraction.GetValueOnAnyThread(),
						0.0f,
						1.0f);
					const float DownMinVerticalDominance = FMath::Clamp(
						CVarQuestArmLengthCalibrationDownMinVerticalDominance.GetValueOnAnyThread(),
						0.0f,
						1.0f);
					const float DownMinCorrectedReachFraction = FMath::Clamp(
						CVarQuestArmLengthCalibrationDownMinCorrectedReachFraction.GetValueOnAnyThread(),
						0.0f,
						1.0f);
					const float MinDownDropCm = MaxReachCm * DownMinBelowShoulderFraction;
					const float MinCorrectedDownReachCm = MaxReachCm * DownMinCorrectedReachFraction;
					auto ComputeCorrectedDownReachCm = [&](
						const FQuestWristSideRuntimeState& SideState,
						float& OutCorrectedDownDropCm) -> float
					{
						const float CandidateReachCm = FMath::Max(0.0f, SideState.ArmLengthCalibrationCandidateReachCm);
						const float CandidateDownDropCm = FMath::Max(0.0f, SideState.ArmLengthCalibrationCandidateBelowShoulderCm);
						if (CandidateReachCm <= KINDA_SMALL_NUMBER || CandidateDownDropCm <= KINDA_SMALL_NUMBER)
						{
							OutCorrectedDownDropCm = 0.0f;
							return 0.0f;
						}

						const float HorizontalCmSq = FMath::Max(
							0.0f,
							CandidateReachCm * CandidateReachCm - CandidateDownDropCm * CandidateDownDropCm);
						const float TargetDownDropCm = FMath::Sqrt(FMath::Max(0.0f, MaxReachCm * MaxReachCm - HorizontalCmSq));
						const float MaxScale = FMath::Max(1.0f, CVarQuestArmDownFrameCorrectionMaxScale.GetValueOnAnyThread());
						const float DownScale = FMath::Clamp(TargetDownDropCm / CandidateDownDropCm, 1.0f, MaxScale);
						const float DirectionAlpha = RemapClamped(
							SideState.ArmLengthCalibrationCandidateVerticalDominance,
							DownMinVerticalDominance,
							1.0f);
						OutCorrectedDownDropCm = CandidateDownDropCm * FMath::Lerp(1.0f, DownScale, DirectionAlpha);
						return FMath::Sqrt(FMath::Max(0.0f, HorizontalCmSq + OutCorrectedDownDropCm * OutCorrectedDownDropCm));
					};
					LeftCorrectedDownReachCm = ComputeCorrectedDownReachCm(LeftSideState, LeftCorrectedDownDropCm);
					RightCorrectedDownReachCm = ComputeCorrectedDownReachCm(RightSideState, RightCorrectedDownDropCm);
					bPoseValid =
						LeftSideState.ArmLengthCalibrationCandidateBelowShoulderCm >= MinDownDropCm &&
						RightSideState.ArmLengthCalibrationCandidateBelowShoulderCm >= MinDownDropCm &&
						LeftSideState.ArmLengthCalibrationCandidateVerticalDominance >= DownMinVerticalDominance &&
						RightSideState.ArmLengthCalibrationCandidateVerticalDominance >= DownMinVerticalDominance &&
						LeftCorrectedDownReachCm >= MinCorrectedDownReachCm &&
						RightCorrectedDownReachCm >= MinCorrectedDownReachCm;
				}

				if (bPoseValid && bHandsStable)
				{
					float StableDeltaSeconds = QuestWristRuntimeState.ArmLengthCalibrationLastUpdateTimeSeconds >= 0.0
						? static_cast<float>(NowSeconds - QuestWristRuntimeState.ArmLengthCalibrationLastUpdateTimeSeconds)
						: FMath::Max(DeltaSeconds, CachedDeltaTimeSeconds);
					if (!FMath::IsFinite(StableDeltaSeconds) || StableDeltaSeconds <= 0.0f || StableDeltaSeconds > 0.25f)
					{
						StableDeltaSeconds = FMath::Max(DeltaSeconds, CachedDeltaTimeSeconds);
					}
					QuestWristRuntimeState.ArmLengthCalibrationStableSeconds += FMath::Clamp(StableDeltaSeconds, 1.0f / 240.0f, 0.10f);
					QuestWristRuntimeState.ArmLengthCalibrationStableFrameCount += 1;
					QuestWristRuntimeState.ArmLengthCalibrationLastUpdateTimeSeconds = NowSeconds;

					const float RequiredHoldSeconds = FMath::Max(0.0f, CVarQuestArmLengthCalibrationHoldSeconds.GetValueOnAnyThread());
					const int32 RequiredStableFrames = FMath::Max(1, CVarQuestArmLengthCalibrationStableFrames.GetValueOnAnyThread());
					if (QuestWristRuntimeState.ArmLengthCalibrationStableSeconds >= RequiredHoldSeconds &&
						QuestWristRuntimeState.ArmLengthCalibrationStableFrameCount >= RequiredStableFrames)
					{
						if (QuestWristRuntimeState.ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_ForwardReach)
						{
							LeftSideState.bHasArmLengthCalibrationForwardReach = true;
							RightSideState.bHasArmLengthCalibrationForwardReach = true;
							LeftSideState.ArmLengthCalibrationForwardReachCm = LeftSideState.ArmLengthCalibrationCandidateReachCm;
							RightSideState.ArmLengthCalibrationForwardReachCm = RightSideState.ArmLengthCalibrationCandidateReachCm;
							QuestWristRuntimeState.ArmLengthCalibrationStage = QuestArmLengthCalibrationStage_DownReach;
							ResetArmLengthCalibrationProgress();
							UE_LOG(
								LogMediaPipePose,
								Log,
								TEXT("mp.QuestArmLengthCalibration: stage=ForwardReach accepted=1 observedForwardReachCmL=%.1f observedForwardReachCmR=%.1f targetMetaHumanReachCm=%.1f next=DownReach"),
								LeftSideState.ArmLengthCalibrationForwardReachCm,
								RightSideState.ArmLengthCalibrationForwardReachCm,
								MaxReachCm);
						}
						else if (QuestWristRuntimeState.ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_DownReach)
						{
							LeftSideState.bHasArmLengthCalibrationDownSample = true;
							RightSideState.bHasArmLengthCalibrationDownSample = true;
							LeftSideState.ArmLengthCalibrationDownDropCm = LeftSideState.ArmLengthCalibrationCandidateBelowShoulderCm;
							RightSideState.ArmLengthCalibrationDownDropCm = RightSideState.ArmLengthCalibrationCandidateBelowShoulderCm;
							LeftSideState.ArmLengthCalibrationDownReachCm = LeftSideState.ArmLengthCalibrationCandidateReachCm;
							RightSideState.ArmLengthCalibrationDownReachCm = RightSideState.ArmLengthCalibrationCandidateReachCm;
							QuestWristRuntimeState.ArmLengthCalibrationStage = QuestArmLengthCalibrationStage_Accepted;
							QuestWristRuntimeState.ArmLengthCalibrationAcceptedTimeSeconds = NowSeconds;
							ResetArmLengthCalibrationProgress();
							UE_LOG(
								LogMediaPipePose,
								Log,
								TEXT("mp.QuestArmLengthCalibration: stage=DownReach accepted=1 observedForwardReachCmL=%.1f observedForwardReachCmR=%.1f observedDownDropCmL=%.1f observedDownDropCmR=%.1f observedDownReachCmL=%.1f observedDownReachCmR=%.1f correctedDownDropCmL=%.1f correctedDownDropCmR=%.1f correctedDownReachCmL=%.1f correctedDownReachCmR=%.1f targetMetaHumanReachCm=%.1f calibrationAccepted=1"),
								LeftSideState.ArmLengthCalibrationForwardReachCm,
								RightSideState.ArmLengthCalibrationForwardReachCm,
								LeftSideState.ArmLengthCalibrationDownDropCm,
								RightSideState.ArmLengthCalibrationDownDropCm,
								LeftSideState.ArmLengthCalibrationDownReachCm,
								RightSideState.ArmLengthCalibrationDownReachCm,
								LeftCorrectedDownDropCm,
								RightCorrectedDownDropCm,
								LeftCorrectedDownReachCm,
								RightCorrectedDownReachCm,
								MaxReachCm);
						}
					}
				}
				else if (QuestWristRuntimeState.ArmLengthCalibrationStage != QuestArmLengthCalibrationStage_Accepted)
				{
					ResetArmLengthCalibrationProgress();
				}
			}
			else if (QuestWristRuntimeState.ArmLengthCalibrationStage != QuestArmLengthCalibrationStage_Accepted)
			{
				QuestWristRuntimeState.ArmLengthCalibrationStage = QuestArmLengthCalibrationStage_WaitingForHands;
				ResetArmLengthCalibrationProgress();
			}

			QuestWristTrace.ArmLengthCalibrationStage = QuestWristRuntimeState.ArmLengthCalibrationStage;
			QuestWristTrace.ArmLengthCalibrationStableSeconds = QuestWristRuntimeState.ArmLengthCalibrationStableSeconds;
			QuestWristTrace.ArmLengthCalibrationForwardReachCm = QuestWristSideState.ArmLengthCalibrationForwardReachCm;
			QuestWristTrace.ArmLengthCalibrationDownDropCm = QuestWristSideState.ArmLengthCalibrationDownDropCm;
			QuestWristTrace.ArmLengthCalibrationTargetReachCm = MaxReachCm;

			if (QuestWristRuntimeState.ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_Accepted &&
				CVarQuestArmDownFrameCorrection.GetValueOnAnyThread() != 0 &&
				QuestWristSideState.bHasArmLengthCalibrationDownSample &&
				QuestWristSideState.ArmLengthCalibrationDownDropCm > KINDA_SMALL_NUMBER &&
				DownDropCm > KINDA_SMALL_NUMBER &&
				DownDominance >= FMath::Clamp(CVarQuestArmLengthCalibrationDownMinVerticalDominance.GetValueOnAnyThread(), 0.0f, 1.0f))
			{
				const float HorizontalCmSq = FMath::Max(0.0f, WristReachCm * WristReachCm - DownDropCm * DownDropCm);
				const float TargetDownDropCm = FMath::Sqrt(FMath::Max(0.0f, MaxReachCm * MaxReachCm - HorizontalCmSq));
				const float MaxScale = FMath::Max(1.0f, CVarQuestArmDownFrameCorrectionMaxScale.GetValueOnAnyThread());
				const float DownScale = FMath::Clamp(
					TargetDownDropCm / QuestWristSideState.ArmLengthCalibrationDownDropCm,
					1.0f,
					MaxScale);
				const float DirectionAlpha = RemapClamped(
					DownDominance,
					FMath::Clamp(CVarQuestArmLengthCalibrationDownMinVerticalDominance.GetValueOnAnyThread(), 0.0f, 1.0f),
					1.0f);
				const float CorrectedDownDropCm = DownDropCm * FMath::Lerp(1.0f, DownScale, DirectionAlpha);
				if (CorrectedDownDropCm > DownDropCm + 0.1f)
				{
					const FVector VerticalWorld = -UpAxisWorld * CorrectedDownDropCm;
					const FVector HorizontalWorld = ShoulderToWristWorld + UpAxisWorld * DownDropCm;
					WristWorld = ShoulderWorld + HorizontalWorld + VerticalWorld;
					ShoulderToWristWorld = WristWorld - ShoulderWorld;
					WristReachCm = ShoulderToWristWorld.Size();
					QuestWristTrace.FinalWristWorld = WristWorld;
					QuestWristTrace.MappedQuestWristWorld = WristWorld;
					QuestWristTrace.bConstrainedArmDownFrameCorrectionApplied = 1;
					QuestWristTrace.ConstrainedArmDownFrameScale = DownScale;
					QuestWristTrace.ConstrainedArmDownFrameAlpha = DirectionAlpha;
					QuestWristTrace.ConstrainedArmDownFrameObservedDropCm = QuestWristSideState.ArmLengthCalibrationDownDropCm;
					QuestWristTrace.ConstrainedArmDownFrameTargetDropCm = TargetDownDropCm;
				}
			}
		}
		if (WristReachCm > KINDA_SMALL_NUMBER)
		{
			const float ClampedReachCm = FMath::Clamp(WristReachCm, MinReachCm, FMath::Max(MinReachCm, MaxReachCm));
			if (!FMath::IsNearlyEqual(ClampedReachCm, WristReachCm, 0.1f))
			{
				WristWorld = ShoulderWorld + ShoulderToWristWorld.GetSafeNormal() * ClampedReachCm;
				ShoulderToWristWorld = WristWorld - ShoulderWorld;
				WristReachCm = ClampedReachCm;
				QuestWristTrace.FinalWristWorld = WristWorld;
				QuestWristTrace.bReachClamped = 1;
			}
		}
		if (bUseHmdRelativeAvatarArmFrame &&
			bQuestArmUsesConstrainedSolve &&
			QuestWristTrace.bMapped != 0 &&
			QuestWristTrace.bConstrainedArmBodyFallbackApplied == 0 &&
			WristReachCm > KINDA_SMALL_NUMBER &&
			MaxReachCm > KINDA_SMALL_NUMBER)
		{
			const double ReachContinuityNowSeconds = FPlatformTime::Seconds();
			const float LastReachContinuityMaxAgeSeconds = FMath::Max(
				0.50f,
				CVarQuestWristLostTrackingGraceSeconds.GetValueOnAnyThread() + 0.50f);
			const float LastReachContinuityAgeSeconds =
				QuestWristSideState.HmdRelativeReachContinuityTimeSeconds >= 0.0
					? static_cast<float>(ReachContinuityNowSeconds - QuestWristSideState.HmdRelativeReachContinuityTimeSeconds)
					: -1.0f;
			bHasRecentHmdRelativeReachContinuity =
				QuestWristSideState.bHasHmdRelativeReachContinuity &&
				LastReachContinuityAgeSeconds >= 0.0f &&
				LastReachContinuityAgeSeconds <= LastReachContinuityMaxAgeSeconds;
			PreviousHmdRelativeReachContinuityCm = QuestWristSideState.HmdRelativeReachContinuityCm;
			FMediaPipeQuestReachStepContinuityInput ReachContinuityInput;
			ReachContinuityInput.bEnabled = MaxReachStepCm > KINDA_SMALL_NUMBER;
			ReachContinuityInput.CurrentReachCm = WristReachCm;
			ReachContinuityInput.bHasPreviousReach = bHasRecentHmdRelativeReachContinuity;
			ReachContinuityInput.PreviousReachCm = PreviousHmdRelativeReachContinuityCm;
			ReachContinuityInput.MaxStepCm = MaxReachStepCm;
			ReachContinuityInput.MinReachCm = MinReachCm;
			ReachContinuityInput.MaxReachCm = FMath::Max(MinReachCm, MaxReachCm);
			const FMediaPipeQuestReachStepContinuityResult ReachContinuityResult =
				FMediaPipeQuestWristApplyPolicy::ApplyReachStepContinuity(ReachContinuityInput);
			QuestWristTrace.ConstrainedArmReachContinuityMaxStepCm = ReachContinuityResult.MaxStepCm;
			if (ReachContinuityResult.bApplied)
			{
				WristWorld = ShoulderWorld + ShoulderToWristWorld.GetSafeNormal() * ReachContinuityResult.TargetReachCm;
				ShoulderToWristWorld = WristWorld - ShoulderWorld;
				WristReachCm = ReachContinuityResult.TargetReachCm;
				QuestWristTrace.FinalWristWorld = WristWorld;
				QuestWristTrace.bConstrainedArmReachContinuityApplied = 1;
				QuestWristTrace.ConstrainedArmReachContinuityRawReachCm = ReachContinuityResult.RawReachCm;
				QuestWristTrace.ConstrainedArmReachContinuityPreviousReachCm = ReachContinuityResult.PreviousReachCm;
				QuestWristTrace.ConstrainedArmReachContinuityMaxStepCm = ReachContinuityResult.MaxStepCm;
			}
		}
		ArmObservationAlphaScale = 1.0f;
	}
	if (!bMetaHumanFullArmChainFresh &&
		CVarMediaPipeArmHoldOnQuestHandLoss.GetValueOnAnyThread() != 0 &&
		bQuestHandTrackingEnabled &&
		bQuestSideTrackedForArm)
	{
		LastReliableShoulderWorld = ShoulderWorld;
		LastReliableElbowWorld = ElbowWorld;
		LastReliableWristWorld = WristWorld;
		bHasLastReliableArmSample = true;
	}

	float QuestWristDriftGuardAlpha = 0.0f;
	float QuestWristDriftOffsetCm = 0.0f;
	if (bQuestWristPositionApplied &&
		QuestWristTrace.bMapped != 0 &&
		(bQuestArmUsesLegacyReachAssist || bQuestArmUsesConstrainedSolve) &&
		CVarQuestWristDriftGuard.GetValueOnAnyThread() != 0)
	{
		const float DriftGuardStartCm = FMath::Max(0.0f, CVarQuestWristDriftGuardStartCm.GetValueOnAnyThread());
		const float DriftGuardFullCm = FMath::Max(DriftGuardStartCm + 1.0f, CVarQuestWristDriftGuardFullCm.GetValueOnAnyThread());
		QuestWristDriftOffsetCm = QuestWristTrace.MappedOffsetFromMediaPipeCm;
		if (QuestWristDriftOffsetCm <= UE_SMALL_NUMBER)
		{
			QuestWristDriftOffsetCm = FVector::Dist(QuestWristTrace.MappedQuestWristWorld, QuestWristTrace.MediaPipeWristWorld);
		}
		QuestWristDriftGuardAlpha = FMath::Clamp(
			(QuestWristDriftOffsetCm - DriftGuardStartCm) / FMath::Max(DriftGuardFullCm - DriftGuardStartCm, UE_SMALL_NUMBER),
			0.0f,
			1.0f);
		if (QuestWristDriftGuardAlpha > KINDA_SMALL_NUMBER)
		{
			QuestWristTrace.bDriftGuardApplied = 1;
			QuestWristTrace.DriftGuardAlpha = QuestWristDriftGuardAlpha;
			QuestWristTrace.DriftGuardOffsetCm = QuestWristDriftOffsetCm;
		}
	}

	bool bQuestConstrainedArmSolveApplied = false;
	if (bQuestWristPositionApplied &&
		QuestWristTrace.bMapped != 0 &&
		bQuestArmUsesConstrainedSolve &&
		CVarQuestConstrainedArmSolve.GetValueOnAnyThread() != 0 &&
		CVarQuestWristForceArmIK.GetValueOnAnyThread() == 0 &&
		!bUseArmIK &&
		RefUpperLen > KINDA_SMALL_NUMBER &&
		RefLowerLen > KINDA_SMALL_NUMBER)
	{
		const float ConstrainedSolveBlend = FMath::Clamp(CVarQuestConstrainedArmSolveBlend.GetValueOnAnyThread(), 0.0f, 1.0f);
		const float WristAuthority = FMath::Clamp(CVarQuestConstrainedArmWristAuthority.GetValueOnAnyThread(), 0.0f, 1.0f);
		float EffectiveWristAuthority = WristAuthority;
		const float AuthorityFadeStartCm = FMath::Max(0.0f, CVarQuestConstrainedArmWristAuthorityFadeStartCm.GetValueOnAnyThread());
		const float AuthorityFadeFullCm = FMath::Max(AuthorityFadeStartCm + 1.0f, CVarQuestConstrainedArmWristAuthorityFadeFullCm.GetValueOnAnyThread());
		const float MinWristAuthority = FMath::Clamp(CVarQuestConstrainedArmWristAuthorityMin.GetValueOnAnyThread(), 0.0f, WristAuthority);
		if (AuthorityFadeStartCm > KINDA_SMALL_NUMBER && MinWristAuthority < WristAuthority)
		{
			float QuestCorrectionCm = FVector::Dist(QuestWristTrace.FinalWristWorld, QuestWristTrace.MediaPipeWristWorld);
			if (QuestWristTrace.PositionFilterTargetDeltaCm > QuestCorrectionCm)
			{
				QuestCorrectionCm = QuestWristTrace.PositionFilterTargetDeltaCm;
			}
			if (QuestWristTrace.MappedOffsetFromMediaPipeCm > QuestCorrectionCm)
			{
				QuestCorrectionCm = QuestWristTrace.MappedOffsetFromMediaPipeCm;
			}
			const float AuthorityFadeAlpha = FMath::Clamp(
				(QuestCorrectionCm - AuthorityFadeStartCm) / FMath::Max(AuthorityFadeFullCm - AuthorityFadeStartCm, UE_SMALL_NUMBER),
				0.0f,
				1.0f);
			EffectiveWristAuthority = FMath::Lerp(WristAuthority, MinWristAuthority, AuthorityFadeAlpha);
		}
		if (ConstrainedSolveBlend > KINDA_SMALL_NUMBER && EffectiveWristAuthority > KINDA_SMALL_NUMBER)
		{
			const FVector OriginalElbowWorld = ElbowWorld;
			const FVector QuestEndpointWorld = FMath::Lerp(QuestWristTrace.MediaPipeWristWorld, QuestWristTrace.FinalWristWorld, EffectiveWristAuthority);
			FVector ConstrainedSolveElbowHintWorld = ElbowWorld;
			bool bConstrainedSolveSourceElbowHintApplied = false;
			if (bUseHmdRelativeAvatarArmFrame &&
				bHasMediaPipeArmWorld &&
				QuestWristTrace.bConstrainedArmBodyFallbackApplied == 0 &&
				CVarQuestConstrainedArmMediaPipeElbowHint.GetValueOnAnyThread() > KINDA_SMALL_NUMBER)
			{
				bool bMediaPipeSourceElbowHintReliable = true;
				if (CVarMediaPipeArmReliabilityGate.GetValueOnAnyThread() != 0)
				{
					const float ArmMinReliability = FMath::Clamp(CVarMediaPipeArmMinReliability.GetValueOnAnyThread(), 0.0f, 1.0f);
					bMediaPipeSourceElbowHintReliable =
						GetLandmarkReliability(ShoulderLm) >= ArmMinReliability &&
						GetLandmarkReliability(ElbowLm) >= ArmMinReliability &&
						GetLandmarkReliability(WristLm) >= ArmMinReliability;
				}

				if (bMediaPipeSourceElbowHintReliable)
				{
					FMediaPipeConstrainedArmSourceElbowHintInput SourceElbowHintInput;
					SourceElbowHintInput.bIsLeft = bIsLeft;
					SourceElbowHintInput.SourceShoulderWorld = MediaPipeSourceShoulderWorld;
					SourceElbowHintInput.SourceElbowWorld = MediaPipeSourceElbowWorld;
					SourceElbowHintInput.SourceWristWorld = MediaPipeSourceWristWorld;
					SourceElbowHintInput.TargetShoulderWorld = ShoulderWorld;
					SourceElbowHintInput.TargetEndpointWorld = QuestEndpointWorld;
					SourceElbowHintInput.UpWorld = UpWorld;
					SourceElbowHintInput.ShoulderRightWorld = ShoulderRightWorld;
					SourceElbowHintInput.TargetUpperLenCm = RefUpperLen;
					SourceElbowHintInput.TargetLowerLenCm = RefLowerLen;
					SourceElbowHintInput.MaxReachFraction = QuestConstrainedArmMaxReachFraction;

					FMediaPipeConstrainedArmSourceElbowHintResult SourceElbowHintResult;
					if (FMediaPipeQuestConstrainedArmSolver::BuildSourceElbowHint(SourceElbowHintInput, SourceElbowHintResult))
					{
						ConstrainedSolveElbowHintWorld = SourceElbowHintResult.TargetElbowWorld;
						bConstrainedSolveSourceElbowHintApplied = true;
					}
				}
			}

			FMediaPipeArmSolverState& ActiveArmState = bIsLeft ? LeftArmState : RightArmState;
			const double ConstrainedNowSeconds = FPlatformTime::Seconds();
			const float LastSolveMaxAgeSeconds = FMath::Max(
				0.50f,
				CVarQuestWristLostTrackingGraceSeconds.GetValueOnAnyThread() + 0.50f);
			const bool bHasRecentLastConstrainedArmSolve =
				ActiveArmState.bHasLastConstrainedArmSolve &&
				ActiveArmState.LastConstrainedArmSolveTimeSeconds >= 0.0 &&
				static_cast<float>(ConstrainedNowSeconds - ActiveArmState.LastConstrainedArmSolveTimeSeconds) <= LastSolveMaxAgeSeconds;
			if (!bHasRecentLastConstrainedArmSolve)
			{
				ActiveArmState.bHasSmoothedConstrainedArmElbowWorld = false;
			}
			FMediaPipeConstrainedArmSolveInput SolveInput;
			SolveInput.bIsLeft = bIsLeft;
			SolveInput.ShoulderWorld = ShoulderWorld;
			SolveInput.CurrentElbowWorld = ConstrainedSolveElbowHintWorld;
			SolveInput.QuestEndpointWorld = QuestEndpointWorld;
			SolveInput.UpWorld = UpWorld;
			SolveInput.ShoulderRightWorld = ShoulderRightWorld;
			SolveInput.bHasTorsoBasis = bHasTorsoBasis;
			SolveInput.TargetUpperLenCm = RefUpperLen;
			SolveInput.TargetLowerLenCm = RefLowerLen;
			SolveInput.MaxReachFraction = QuestConstrainedArmMaxReachFraction;
			SolveInput.bEnableDownStraighten =
				CVarQuestConstrainedArmDownStraighten.GetValueOnAnyThread() != 0;
			SolveInput.DownStraightenThresholdCm = CVarQuestConstrainedArmDownStraightenThresholdCm.GetValueOnAnyThread();
			SolveInput.DownStraightenMaxCm = CVarQuestConstrainedArmDownStraightenMaxCm.GetValueOnAnyThread();
			SolveInput.DownStraightenMinBelowShoulderRatio = CVarQuestConstrainedArmDownStraightenMinBelowShoulderRatio.GetValueOnAnyThread();
			SolveInput.DownStraightenReachFloorFraction = CVarQuestConstrainedArmDownStraightenReachFloorFraction.GetValueOnAnyThread();
			SolveInput.DownStraightenMaxReachFraction = CVarQuestConstrainedArmDownStraightenMaxReachFraction.GetValueOnAnyThread();
			SolveInput.QuestWristDriftGuardAlpha = QuestWristDriftGuardAlpha;
			SolveInput.MediaPipeElbowHint = CVarQuestConstrainedArmMediaPipeElbowHint.GetValueOnAnyThread();
			SolveInput.StablePoleDown = CVarQuestConstrainedArmStablePoleDown.GetValueOnAnyThread();
			SolveInput.CloseReachStartCm = CVarQuestConstrainedArmCloseReachStartCm.GetValueOnAnyThread();
			SolveInput.CloseReachFullCm = CVarQuestConstrainedArmCloseReachFullCm.GetValueOnAnyThread();
			SolveInput.CloseReachPoleBias = CVarQuestConstrainedArmCloseReachPoleBias.GetValueOnAnyThread();
			SolveInput.CloseReachStablePoleDown = CVarQuestConstrainedArmCloseReachStablePoleDown.GetValueOnAnyThread();
			SolveInput.MaxElbowMoveCm = CVarQuestConstrainedArmMaxElbowMoveCm.GetValueOnAnyThread();
			SolveInput.MaxReachStepCm = bUseHmdRelativeAvatarArmFrame
				? (CVarQuestConstrainedArmMaxReachStepCm.GetValueOnAnyThread() > KINDA_SMALL_NUMBER
					? CVarQuestConstrainedArmMaxReachStepCm.GetValueOnAnyThread()
					: 0.0f)
				: CVarQuestConstrainedArmMaxReachStepCm.GetValueOnAnyThread();
			SolveInput.bHasReachContinuityHistory = bHasRecentHmdRelativeReachContinuity;
			SolveInput.ReachContinuityPreviousReachCm = PreviousHmdRelativeReachContinuityCm;
			SolveInput.bEnableNearFullPoleContinuity = CVarQuestConstrainedArmNearFullPoleContinuity.GetValueOnAnyThread() != 0;
			SolveInput.NearFullPoleStartFraction = CVarQuestConstrainedArmNearFullPoleStartFraction.GetValueOnAnyThread();
			SolveInput.NearFullPoleFullFraction = CVarQuestConstrainedArmNearFullPoleFullFraction.GetValueOnAnyThread();
			SolveInput.bHasLastConstrainedArmSolve = bHasRecentLastConstrainedArmSolve;
			SolveInput.LastConstrainedArmShoulderWorld = ActiveArmState.LastConstrainedArmShoulderWorld;
			SolveInput.LastConstrainedArmElbowWorld = ActiveArmState.LastConstrainedArmElbowWorld;
			SolveInput.LastConstrainedArmWristWorld = ActiveArmState.LastConstrainedArmWristWorld;

			FMediaPipeConstrainedArmSolveResult SolveResult;
			if (FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(SolveInput, SolveResult))
			{
				WristWorld = SolveResult.TargetWristWorld;
				QuestWristTrace.FinalWristWorld = WristWorld;
				if (SolveResult.bReachClamped)
				{
					QuestWristTrace.bReachClamped = 1;
				}
				if (SolveResult.bDownStraightened)
				{
					QuestWristTrace.bConstrainedArmDownStraightened = 1;
					QuestWristTrace.ConstrainedArmDownStraightenAlpha = SolveResult.DownStraightenAdaptiveAlpha;
				}
				if (SolveResult.bPoleContinuityApplied)
				{
					QuestWristTrace.bConstrainedArmPoleContinuityApplied = 1;
				}
				if (SolveResult.bReachContinuityApplied)
				{
					QuestWristTrace.bConstrainedArmReachContinuityApplied = 1;
					QuestWristTrace.ConstrainedArmReachContinuityRawReachCm = SolveResult.ReachContinuityRawReachCm;
					QuestWristTrace.ConstrainedArmReachContinuityPreviousReachCm = SolveResult.ReachContinuityPreviousReachCm;
					QuestWristTrace.ConstrainedArmReachContinuityMaxStepCm = SolveResult.ReachContinuityMaxStepCm;
				}
				QuestWristTrace.ConstrainedArmNearFullPoleAlpha = SolveResult.NearFullPoleAlpha;
				if (QuestWristTrace.ConstrainedArmReachContinuityMaxStepCm <= KINDA_SMALL_NUMBER)
				{
					QuestWristTrace.ConstrainedArmReachContinuityMaxStepCm = SolveInput.MaxReachStepCm;
				}
				QuestWristTrace.ConstrainedArmWristStepCm = SolveResult.ContinuityWristStepCm;
				QuestWristTrace.ConstrainedArmCandidateElbowStepCm = SolveResult.CandidateElbowStepCm;
				QuestWristTrace.ConstrainedArmAllowedElbowStepCm = SolveResult.AllowedElbowStepCm;

				FVector SolvedElbowWorld = SolveResult.TargetElbowWorld;
				const float ConfiguredElbowHalfLifeSeconds = FMath::Max(0.0f, CVarQuestConstrainedArmElbowHalfLife.GetValueOnAnyThread());
				const float ElbowHalfLifeSeconds = ConfiguredElbowHalfLifeSeconds;
				const float ConfiguredMaxElbowStepCm = FMath::Max(0.0f, CVarQuestConstrainedArmMaxElbowStepCm.GetValueOnAnyThread());
				const float MaxElbowStepCm = ConfiguredMaxElbowStepCm;
				if (ElbowHalfLifeSeconds > KINDA_SMALL_NUMBER || MaxElbowStepCm > KINDA_SMALL_NUMBER)
				{
					bool& bHasSmoothedConstrainedElbowWorld = bIsLeft
						? LeftArmState.bHasSmoothedConstrainedArmElbowWorld
						: RightArmState.bHasSmoothedConstrainedArmElbowWorld;
					FVector& SmoothedConstrainedElbowWorld = bIsLeft
						? LeftArmState.SmoothedConstrainedArmElbowWorld
						: RightArmState.SmoothedConstrainedArmElbowWorld;
					if (!bHasSmoothedConstrainedElbowWorld || SmoothedConstrainedElbowWorld.ContainsNaN())
					{
						SmoothedConstrainedElbowWorld = OriginalElbowWorld;
						bHasSmoothedConstrainedElbowWorld = true;
					}

					FVector GuardedElbowWorld = ElbowHalfLifeSeconds > KINDA_SMALL_NUMBER
						? FMath::Lerp(SmoothedConstrainedElbowWorld, SolvedElbowWorld, HalfLifeToAlpha(ElbowHalfLifeSeconds, DeltaSeconds))
						: SolvedElbowWorld;
					FVector GuardedDeltaWorld = GuardedElbowWorld - SmoothedConstrainedElbowWorld;
					const float GuardedStepCm = GuardedDeltaWorld.Size();
					if (MaxElbowStepCm > KINDA_SMALL_NUMBER && GuardedStepCm > MaxElbowStepCm)
					{
						GuardedDeltaWorld = GuardedDeltaWorld.GetSafeNormal() * MaxElbowStepCm;
						GuardedElbowWorld = SmoothedConstrainedElbowWorld + GuardedDeltaWorld;
					}
					SmoothedConstrainedElbowWorld = GuardedElbowWorld;
					SolvedElbowWorld = GuardedElbowWorld;
				}

				ElbowWorld = FMath::Lerp(ElbowWorld, SolvedElbowWorld, ConstrainedSolveBlend);
				QuestWristTrace.bConstrainedArmSolveApplied = 1;
				QuestWristTrace.ConstrainedArmSolveBlend = ConstrainedSolveBlend;
				QuestWristTrace.ConstrainedArmWristAuthority = EffectiveWristAuthority;
				QuestWristTrace.ConstrainedArmMediaPipeElbowHint = FMath::Clamp(CVarQuestConstrainedArmMediaPipeElbowHint.GetValueOnAnyThread(), 0.0f, 1.0f);
				if (bConstrainedSolveSourceElbowHintApplied)
				{
					QuestWristTrace.bConstrainedArmSourceElbowHintApplied = 1;
					QuestWristTrace.ConstrainedArmSourceElbowHintWorld = ConstrainedSolveElbowHintWorld;
				}
				QuestWristTrace.ConstrainedArmCloseReachAlpha = SolveResult.CloseReachPoleAlpha;
				QuestWristTrace.ConstrainedArmStablePoleDown = SolveResult.StablePoleDown;
				QuestWristTrace.ConstrainedArmElbowMoveCm = FVector::Dist(OriginalElbowWorld, ElbowWorld);
				QuestWristTrace.ConstrainedArmElbowWorld = ElbowWorld;
				ActiveArmState.bHasLastConstrainedArmSolve = true;
				ActiveArmState.LastConstrainedArmShoulderWorld = ShoulderWorld;
				ActiveArmState.LastConstrainedArmElbowWorld = ElbowWorld;
				ActiveArmState.LastConstrainedArmWristWorld = WristWorld;
				ActiveArmState.LastConstrainedArmSolveTimeSeconds = FPlatformTime::Seconds();
				if (bQuestSideTrackedForArm &&
					QuestWristTrace.bUsedHeldQuestWrist == 0 &&
					QuestWristTrace.bUsedUntrackedJointData == 0 &&
					QuestWristTrace.bConstrainedArmBodyFallbackApplied == 0 &&
					QuestWristTrace.bConstrainedArmDropoutDownFallbackApplied == 0)
				{
					FVector TrackedUpWorld = UpWorld.GetSafeNormal();
					if (TrackedUpWorld.IsNearlyZero())
					{
						TrackedUpWorld = TargetCompTransform.GetUnitAxis(EAxis::Z).GetSafeNormal();
					}
					if (TrackedUpWorld.IsNearlyZero())
					{
						TrackedUpWorld = FVector::UpVector;
					}
					const float TrackedReachCm = FVector::Dist(ShoulderWorld, WristWorld);
					const float TrackedBelowShoulderCm = FMath::Max(
						0.0f,
						FVector::DotProduct(ShoulderWorld - WristWorld, TrackedUpWorld));
					QuestWristSideState.bHasLastTrackedQuestArmPose = true;
					QuestWristSideState.LastTrackedQuestArmShoulderWorld = ShoulderWorld;
					QuestWristSideState.LastTrackedQuestArmElbowWorld = ElbowWorld;
					QuestWristSideState.LastTrackedQuestArmWristWorld = WristWorld;
					QuestWristSideState.LastTrackedQuestArmReachCm = TrackedReachCm;
					QuestWristSideState.LastTrackedQuestArmBelowShoulderCm = TrackedBelowShoulderCm;
					QuestWristSideState.LastTrackedQuestArmDownDominance = TrackedReachCm > KINDA_SMALL_NUMBER
						? TrackedBelowShoulderCm / TrackedReachCm
						: 0.0f;
					QuestWristSideState.LastTrackedQuestArmTimeSeconds = ActiveArmState.LastConstrainedArmSolveTimeSeconds;
				}
				StoreHmdRelativeReachContinuity(WristWorld);
				bQuestConstrainedArmSolveApplied = true;
			}
		}
	}
	if (!bQuestConstrainedArmSolveApplied)
	{
		auto KeepRecentConstrainedSolveOrReset = [&]()
		{
			FMediaPipeArmSolverState& ActiveArmState = bIsLeft ? LeftArmState : RightArmState;
			const double NowSeconds = FPlatformTime::Seconds();
			const float LastSolveMaxAgeSeconds = FMath::Max(
				0.50f,
				CVarQuestWristLostTrackingGraceSeconds.GetValueOnAnyThread() + 0.50f);
			const bool bKeepRecentSolve =
				ActiveArmState.bHasLastConstrainedArmSolve &&
				ActiveArmState.LastConstrainedArmSolveTimeSeconds >= 0.0 &&
				static_cast<float>(NowSeconds - ActiveArmState.LastConstrainedArmSolveTimeSeconds) <= LastSolveMaxAgeSeconds;
			if (bKeepRecentSolve)
			{
				return;
			}

			ActiveArmState.bHasSmoothedConstrainedArmElbowWorld = false;
			ActiveArmState.bHasLastConstrainedArmSolve = false;
			ActiveArmState.LastConstrainedArmSolveTimeSeconds = -1.0;
		};

		KeepRecentConstrainedSolveOrReset();
	}

	if (bQuestWristPositionApplied &&
		!bQuestConstrainedArmSolveApplied &&
		bQuestArmUsesLegacyReachAssist &&
		CVarQuestWristReachAssist.GetValueOnAnyThread() != 0 &&
		CVarQuestWristForceArmIK.GetValueOnAnyThread() == 0 &&
		!bUseArmIK &&
		RefUpperLen > KINDA_SMALL_NUMBER &&
		RefLowerLen > KINDA_SMALL_NUMBER)
	{
		const float BaseReachAssistBlend = FMath::Clamp(CVarQuestWristReachAssistBlend.GetValueOnAnyThread(), 0.0f, 1.0f);
		float ReachAssistBlend = BaseReachAssistBlend;
		if (QuestWristDriftGuardAlpha > KINDA_SMALL_NUMBER)
		{
			const float DriftReachBoost = FMath::Clamp(CVarQuestWristDriftGuardReachBlendBoost.GetValueOnAnyThread(), 0.0f, 1.0f);
			ReachAssistBlend = FMath::Clamp(BaseReachAssistBlend + QuestWristDriftGuardAlpha * DriftReachBoost, 0.0f, 1.0f);
			QuestWristTrace.DriftGuardReachAssistBlend = ReachAssistBlend;
		}
		if (ReachAssistBlend > KINDA_SMALL_NUMBER)
		{
			const FVector OriginalElbowWorld = ElbowWorld;
			FVector ShoulderToWristWorld = WristWorld - ShoulderWorld;
			float WristReachCm = ShoulderToWristWorld.Size();
			if (WristReachCm > KINDA_SMALL_NUMBER)
			{
				const float MinReachCm = FMath::Abs(RefUpperLen - RefLowerLen) + 0.5f;
				const float MaxReachCm = (RefUpperLen + RefLowerLen) * QuestConstrainedArmMaxReachFraction;
				const float ClampedReachCm = FMath::Clamp(WristReachCm, MinReachCm, FMath::Max(MinReachCm, MaxReachCm));
				const FVector ReachDirWorld = ShoulderToWristWorld / WristReachCm;
				if (!FMath::IsNearlyEqual(ClampedReachCm, WristReachCm, 0.1f))
				{
					WristWorld = ShoulderWorld + ReachDirWorld * ClampedReachCm;
					ShoulderToWristWorld = WristWorld - ShoulderWorld;
					WristReachCm = ClampedReachCm;
					QuestWristTrace.FinalWristWorld = WristWorld;
					QuestWristTrace.bReachClamped = 1;
				}

				const float AlongCm = FMath::Clamp(
					((RefUpperLen * RefUpperLen) + (WristReachCm * WristReachCm) - (RefLowerLen * RefLowerLen)) /
						FMath::Max(2.0f * WristReachCm, UE_SMALL_NUMBER),
					0.0f,
					RefUpperLen);
				const float ElbowOffsetCm = FMath::Sqrt(FMath::Max(0.0f, (RefUpperLen * RefUpperLen) - (AlongCm * AlongCm)));
				FVector ElbowPoleWorld = ElbowWorld - ShoulderWorld;
				ElbowPoleWorld -= FVector::DotProduct(ElbowPoleWorld, ReachDirWorld) * ReachDirWorld;
				FVector StablePoleSideWorld = bIsLeft ? -ShoulderRightWorld : ShoulderRightWorld;
				if (StablePoleSideWorld.IsNearlyZero())
				{
					StablePoleSideWorld = bIsLeft ? -FVector::RightVector : FVector::RightVector;
				}
				FVector StablePoleUpWorld = UpWorld.GetSafeNormal();
				if (StablePoleUpWorld.IsNearlyZero())
				{
					StablePoleUpWorld = FVector::UpVector;
				}
				FVector StablePoleWorld = StablePoleSideWorld - StablePoleUpWorld * 0.25f;
				StablePoleWorld -= FVector::DotProduct(StablePoleWorld, ReachDirWorld) * ReachDirWorld;
				if (ElbowPoleWorld.IsNearlyZero())
				{
					ElbowPoleWorld = StablePoleWorld;
				}
				else if (QuestWristDriftGuardAlpha > KINDA_SMALL_NUMBER && !StablePoleWorld.IsNearlyZero())
				{
					const float DriftPoleBlend = FMath::Clamp(
						QuestWristDriftGuardAlpha * FMath::Clamp(CVarQuestWristDriftGuardPoleBlend.GetValueOnAnyThread(), 0.0f, 1.0f),
						0.0f,
						1.0f);
					if (DriftPoleBlend > KINDA_SMALL_NUMBER)
					{
						const FVector MediaPipePoleDirWorld = ElbowPoleWorld.GetSafeNormal();
						const FVector StablePoleDirWorld = StablePoleWorld.GetSafeNormal();
						if (!MediaPipePoleDirWorld.IsNearlyZero() && !StablePoleDirWorld.IsNearlyZero())
						{
							ElbowPoleWorld = FMath::Lerp(MediaPipePoleDirWorld, StablePoleDirWorld, DriftPoleBlend).GetSafeNormal();
							QuestWristTrace.DriftGuardPoleBlend = DriftPoleBlend;
						}
					}
				}

				const FVector ElbowPoleDirWorld = ElbowPoleWorld.GetSafeNormal();
				if (!ElbowPoleDirWorld.IsNearlyZero())
				{
					FVector AssistedElbowWorld = ShoulderWorld + ReachDirWorld * AlongCm + ElbowPoleDirWorld * ElbowOffsetCm;
					float MaxElbowMoveCm = FMath::Max(0.0f, CVarQuestWristReachAssistMaxElbowMoveCm.GetValueOnAnyThread());
					if (QuestWristDriftGuardAlpha > KINDA_SMALL_NUMBER)
					{
						MaxElbowMoveCm += QuestWristDriftGuardAlpha * FMath::Max(0.0f, CVarQuestWristDriftGuardExtraElbowMoveCm.GetValueOnAnyThread());
					}
					FVector ElbowDeltaWorld = AssistedElbowWorld - ElbowWorld;
					const float ElbowMoveCm = ElbowDeltaWorld.Size();
					if (MaxElbowMoveCm > KINDA_SMALL_NUMBER && ElbowMoveCm > MaxElbowMoveCm)
					{
						ElbowDeltaWorld = ElbowDeltaWorld.GetSafeNormal() * MaxElbowMoveCm;
						AssistedElbowWorld = ElbowWorld + ElbowDeltaWorld;
					}

					ElbowWorld = FMath::Lerp(ElbowWorld, AssistedElbowWorld, ReachAssistBlend);
					QuestWristTrace.bReachAssistApplied = 1;
					QuestWristTrace.ReachAssistBlend = ReachAssistBlend;
					QuestWristTrace.ReachAssistElbowMoveCm = FVector::Dist(OriginalElbowWorld, ElbowWorld);
					QuestWristTrace.ReachAssistElbowWorld = ElbowWorld;
				}
			}
		}
	}

	const FVector PoseUpperWorld = (ElbowWorld - ShoulderWorld).GetSafeNormal();
	const FVector PoseLowerWorld = (WristWorld - ElbowWorld).GetSafeNormal();
	if (PoseUpperWorld.IsNearlyZero() || PoseLowerWorld.IsNearlyZero())
	{
		return;
	}

	const FVector PoseUpperComp = TargetCompTransform.InverseTransformVectorNoScale(PoseUpperWorld).GetSafeNormal();
	const FVector PoseLowerComp = TargetCompTransform.InverseTransformVectorNoScale(PoseLowerWorld).GetSafeNormal();
	if (PoseUpperComp.IsNearlyZero() || PoseLowerComp.IsNearlyZero())
	{
		return;
	}

	FVector AvatarRightComp = TargetCompTransform.InverseTransformVectorNoScale(AvatarArmBasis.RightWorld).GetSafeNormal();
	FVector AvatarUpComp = TargetCompTransform.InverseTransformVectorNoScale(AvatarArmBasis.UpWorld).GetSafeNormal();
	FVector AvatarForwardComp = TargetCompTransform.InverseTransformVectorNoScale(AvatarArmBasis.ForwardWorld).GetSafeNormal();
	if (AvatarRightComp.IsNearlyZero())
	{
		AvatarRightComp = bUseTargetFaceForwardAxis ? -FVector::ForwardVector : FVector::RightVector;
	}
	if (AvatarUpComp.IsNearlyZero())
	{
		AvatarUpComp = FVector::UpVector;
	}
	if (AvatarForwardComp.IsNearlyZero())
	{
		AvatarForwardComp = bUseTargetFaceForwardAxis ? FVector::RightVector : FVector::ForwardVector;
	}

	const FVector HipRightComp = bHasTorsoBasis ? TargetCompTransform.InverseTransformVectorNoScale(HipRightWorld).GetSafeNormal() : AvatarRightComp;
	const FVector ShoulderRightComp = bHasTorsoBasis ? TargetCompTransform.InverseTransformVectorNoScale(ShoulderRightWorld).GetSafeNormal() : AvatarRightComp;
	FVector UpComp = bHasTorsoBasis ? TargetCompTransform.InverseTransformVectorNoScale(UpWorld).GetSafeNormal() : AvatarUpComp;
	FVector ForwardComp = bHasTorsoBasis ? TargetCompTransform.InverseTransformVectorNoScale(ForwardWorld).GetSafeNormal() : AvatarForwardComp;
	if (UpComp.IsNearlyZero())
	{
		UpComp = AvatarUpComp.IsNearlyZero() ? FVector::UpVector : AvatarUpComp;
	}
	if (ForwardComp.IsNearlyZero())
	{
		ForwardComp = AvatarForwardComp.IsNearlyZero()
			? (bUseTargetFaceForwardAxis ? FVector::RightVector : FVector::ForwardVector)
			: AvatarForwardComp;
	}

	FMediaPipeQuestArmPoseWriteInput ArmPoseWriteInput;
	ArmPoseWriteInput.bUseHmdRelativeAvatarArmFrame = bUseHmdRelativeAvatarArmFrame;
	ArmPoseWriteInput.bQuestWristPositionApplied = bQuestWristPositionApplied;
	const bool bFrameCoherentQuestArmPoseWrite =
		bMetaHumanFullArmChainFresh ||
		FMediaPipeQuestWristApplyPolicy::ShouldWriteFrameCoherentQuestArmPose(ArmPoseWriteInput);
	const float EffectiveArmTargetHalfLifeSeconds = bFrameCoherentQuestArmPoseWrite
		? 0.0f
		: ArmIKTargetHalfLifeSeconds;
	const float EffectiveArmRotationHalfLifeSeconds = bFrameCoherentQuestArmPoseWrite
		? 0.0f
		: ArmIKRotationHalfLifeSeconds;

	const float ArmFixedMaxStepDegrees = FMath::Max(0.0f, CVarMediaPipeArmRotationMaxStepDegrees.GetValueOnAnyThread());
	const float ArmSpeedMaxStepDegrees = (DeltaSeconds > 0.0f)
		? FMath::Max(0.0f, CVarMediaPipeArmRotationMaxSpeedDegreesPerSecond.GetValueOnAnyThread()) * DeltaSeconds
		: 0.0f;
	float ArmMaxStepDegrees = bFrameCoherentQuestArmPoseWrite ? 0.0f : ArmFixedMaxStepDegrees;
	if (!bFrameCoherentQuestArmPoseWrite && ArmSpeedMaxStepDegrees > 0.0f)
	{
		ArmMaxStepDegrees = ArmMaxStepDegrees > 0.0f
			? FMath::Min(ArmMaxStepDegrees, ArmSpeedMaxStepDegrees)
			: ArmSpeedMaxStepDegrees;
	}

	const bool bHasRefClav = bIsLeft ? bHasRefClavL : bHasRefClavR;
	double& LastClavicleDiagnosticLogTimeSeconds = bIsLeft
		? DiagnosticsState.LastClavicleDiagnosticLogTimeSecondsL
		: DiagnosticsState.LastClavicleDiagnosticLogTimeSecondsR;
	const TCHAR* ClavicleSideLabel = bIsLeft ? TEXT("L") : TEXT("R");
	if (!bDriveClavicles)
	{
		if (bIsLeft)
		{
			LeftArmState.bHasSmoothedClavRotCS = false;
		}
		else
		{
			RightArmState.bHasSmoothedClavRotCS = false;
		}
		if (CVarMediaPipeTorsoDebug.GetValueOnAnyThread() != 0)
		{
			const double NowSeconds = FPlatformTime::Seconds();
			if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, LastClavicleDiagnosticLogTimeSeconds))
			{
				UE_LOG(LogMediaPipePose, Log,
					TEXT("mp.ClavicleDebug: actor=%s side=%s enabled=0 hasRef=%d reason=DriveClaviclesOff"),
					*TargetActorName.ToString(),
					ClavicleSideLabel,
					bHasRefClav ? 1 : 0);
			}
		}
	}
	else if (bHasRefClav)
	{
		const FBoneReference& ClavBone = bIsLeft ? ClavicleL : ClavicleR;
		const FVector& RefClavDir = bIsLeft ? RefClavDirCompL : RefClavDirCompR;
		const FQuat& RefClavComp = bIsLeft ? RefClavCompL : RefClavCompR;

		if (ClavBone.IsValidToEvaluate())
		{
			const FVector RightComp = !HipRightComp.IsNearlyZero() ? HipRightComp : ShoulderRightComp;
			FVector Outward = bIsLeft ? -RightComp : RightComp;
			if (Outward.IsNearlyZero())
			{
				Outward = RefClavDir;
			}
			Outward = (Outward - FVector::DotProduct(Outward, UpComp) * UpComp).GetSafeNormal();
			Outward = (Outward - FVector::DotProduct(Outward, ForwardComp) * ForwardComp).GetSafeNormal();
			if (Outward.IsNearlyZero())
			{
				Outward = RefClavDir;
			}

			const float ArmUp = FMath::Clamp(FVector::DotProduct(PoseUpperComp, UpComp), -1.0f, 1.0f);
			const float ArmForward = FMath::Clamp(FVector::DotProduct(PoseUpperComp, ForwardComp), -1.0f, 1.0f);
			float ShoulderShrugCm = 0.0f;
			float ShoulderShrugW = 0.0f;
			const float ClavicleShrugWeight = FMath::Clamp(CVarMediaPipeClavicleShrugWeight.GetValueOnAnyThread(), 0.0f, 2.0f);
			if (ClavicleShrugWeight > KINDA_SMALL_NUMBER && bHasTorsoBasis)
			{
				FVector LeftHipWorld = FVector::ZeroVector;
				FVector RightHipWorld = FVector::ZeroVector;
				const FVector UpWorldSafe = UpWorld.GetSafeNormal();
				if (!UpWorldSafe.IsNearlyZero() &&
					TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftHip, LeftHipWorld) &&
					TryGetLmWorld((int32)EMediaPipePoseLandmark::RightHip, RightHipWorld))
				{
					const FVector HipMidWorld = (LeftHipWorld + RightHipWorld) * 0.5f;
					const float ShoulderHeightCm = FVector::DotProduct(ShoulderWorld - HipMidWorld, UpWorldSafe);
					if (ShoulderHeightCm > KINDA_SMALL_NUMBER)
					{
						FMediaPipeArmSolverState& ClavicleArmState = bIsLeft ? LeftArmState : RightArmState;
						if (!ClavicleArmState.bHasShoulderHeightReference || ClavicleArmState.ShoulderHeightReferenceCm <= KINDA_SMALL_NUMBER)
						{
							ClavicleArmState.bHasShoulderHeightReference = true;
							ClavicleArmState.ShoulderHeightReferenceCm = ShoulderHeightCm;
						}

						const float ReferenceHalfLifeSeconds =
							ShoulderHeightCm < ClavicleArmState.ShoulderHeightReferenceCm ? 0.25f : 6.0f;
						const float ReferenceAlpha = HalfLifeToAlpha(ReferenceHalfLifeSeconds, DeltaSeconds);
						ClavicleArmState.ShoulderHeightReferenceCm = FMath::Lerp(
							ClavicleArmState.ShoulderHeightReferenceCm,
							ShoulderHeightCm,
							ReferenceAlpha);

						const float ShrugStartCm = FMath::Max(0.0f, CVarMediaPipeClavicleShrugMinCm.GetValueOnAnyThread());
						const float ShrugFullCm = FMath::Max(
							ShrugStartCm + 0.5f,
							CVarMediaPipeClavicleShrugFullCm.GetValueOnAnyThread());
						ShoulderShrugCm = FMath::Max(0.0f, ShoulderHeightCm - ClavicleArmState.ShoulderHeightReferenceCm);
						ShoulderShrugW = RemapClamped(ShoulderShrugCm, ShrugStartCm, ShrugFullCm) * ClavicleShrugWeight;
					}

					const int32 EarLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftEar : (int32)EMediaPipePoseLandmark::RightEar;
					const int32 EyeLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftEye : (int32)EMediaPipePoseLandmark::RightEye;
					FVector HeadSideWorld = FVector::ZeroVector;
					if (!TryGetLmWorld(EarLm, HeadSideWorld) &&
						!TryGetLmWorld(EyeLm, HeadSideWorld) &&
						!TryGetLmWorld((int32)EMediaPipePoseLandmark::Nose, HeadSideWorld))
					{
						HeadSideWorld = FVector::ZeroVector;
					}

					if (!HeadSideWorld.IsNearlyZero())
					{
						FMediaPipeArmSolverState& ClavicleArmState = bIsLeft ? LeftArmState : RightArmState;
						const float HeadClearanceCm = FVector::DotProduct(HeadSideWorld - ShoulderWorld, UpWorldSafe);
						if (HeadClearanceCm > KINDA_SMALL_NUMBER)
						{
							if (!ClavicleArmState.bHasShoulderHeadClearanceReference ||
								ClavicleArmState.ShoulderHeadClearanceReferenceCm <= KINDA_SMALL_NUMBER)
							{
								ClavicleArmState.bHasShoulderHeadClearanceReference = true;
								ClavicleArmState.ShoulderHeadClearanceReferenceCm = HeadClearanceCm;
							}

							const float ClearanceReferenceHalfLifeSeconds =
								HeadClearanceCm > ClavicleArmState.ShoulderHeadClearanceReferenceCm ? 0.25f : 8.0f;
							const float ClearanceReferenceAlpha = HalfLifeToAlpha(ClearanceReferenceHalfLifeSeconds, DeltaSeconds);
							ClavicleArmState.ShoulderHeadClearanceReferenceCm = FMath::Lerp(
								ClavicleArmState.ShoulderHeadClearanceReferenceCm,
								HeadClearanceCm,
								ClearanceReferenceAlpha);

							const float ShrugStartCm = FMath::Max(0.0f, CVarMediaPipeClavicleShrugMinCm.GetValueOnAnyThread());
							const float ShrugFullCm = FMath::Max(
								ShrugStartCm + 0.5f,
								CVarMediaPipeClavicleShrugFullCm.GetValueOnAnyThread());
							const float ClearanceShrugCm = FMath::Max(
								0.0f,
								ClavicleArmState.ShoulderHeadClearanceReferenceCm - HeadClearanceCm);
							if (ClearanceShrugCm > ShoulderShrugCm)
							{
								ShoulderShrugCm = ClearanceShrugCm;
								ShoulderShrugW = RemapClamped(ClearanceShrugCm, ShrugStartCm, ShrugFullCm) * ClavicleShrugWeight;
							}
						}
					}
				}
			}

			const float UpW = FMath::Clamp((FMath::Clamp(ArmUp, 0.0f, 1.0f) * 0.35f) + ShoulderShrugW, 0.0f, 1.25f);
			const float FwdW = FMath::Clamp(ArmForward, 0.0f, 1.0f) * 0.25f;

			FVector DesiredClavDir = (Outward + UpComp * UpW + ForwardComp * FwdW).GetSafeNormal();
			if (DesiredClavDir.IsNearlyZero())
			{
				DesiredClavDir = Outward;
			}

			const FQuat TargetClavRotCS = (FQuat::FindBetweenNormals(RefClavDir, DesiredClavDir) * RefClavComp).GetNormalized();
			const float ClavAlpha = HalfLifeToAlpha(EffectiveArmRotationHalfLifeSeconds, DeltaSeconds) * ArmObservationAlphaScale;
			bool& bHasSmoothedClavRot = bIsLeft ? LeftArmState.bHasSmoothedClavRotCS : RightArmState.bHasSmoothedClavRotCS;
			FQuat& SmoothedClavRotCS = bIsLeft ? LeftArmState.SmoothedClavRotCS : RightArmState.SmoothedClavRotCS;
			UpdateSmoothedRotation(bHasSmoothedClavRot, SmoothedClavRotCS, TargetClavRotCS, ClavAlpha, ArmMaxStepDegrees);
			ApplyRotationCS(CSPose, ClavBone, SmoothedClavRotCS);

			if (CVarMediaPipeTorsoDebug.GetValueOnAnyThread() != 0)
			{
				const double NowSeconds = FPlatformTime::Seconds();
				if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, LastClavicleDiagnosticLogTimeSeconds))
				{
					UE_LOG(LogMediaPipePose, Log,
						TEXT("mp.ClavicleDebug: actor=%s side=%s enabled=1 valid=1 armUp=%.2f armForward=%.2f shrugCm=%.1f shrugWeight=%.2f upWeight=%.2f forwardWeight=%.2f targetDeg=%.1f appliedDeg=%.1f alpha=%.2f maxStepDeg=%.1f"),
						*TargetActorName.ToString(),
						ClavicleSideLabel,
						ArmUp,
						ArmForward,
						ShoulderShrugCm,
						ShoulderShrugW,
						UpW,
						FwdW,
						QuatAngularDistanceDegrees(TargetClavRotCS, RefClavComp),
						QuatAngularDistanceDegrees(SmoothedClavRotCS, RefClavComp),
						ClavAlpha,
						ArmMaxStepDegrees);
				}
			}
		}
		else if (CVarMediaPipeTorsoDebug.GetValueOnAnyThread() != 0)
		{
			const double NowSeconds = FPlatformTime::Seconds();
			if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, LastClavicleDiagnosticLogTimeSeconds))
			{
				UE_LOG(LogMediaPipePose, Log,
					TEXT("mp.ClavicleDebug: actor=%s side=%s enabled=1 valid=0 hasRef=1 reason=BoneInvalid"),
					*TargetActorName.ToString(),
					ClavicleSideLabel);
			}
		}
	}
	else if (CVarMediaPipeTorsoDebug.GetValueOnAnyThread() != 0)
	{
		const double NowSeconds = FPlatformTime::Seconds();
		if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, LastClavicleDiagnosticLogTimeSeconds))
		{
			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.ClavicleDebug: actor=%s side=%s enabled=1 valid=0 hasRef=0 reason=NoReferenceClavicle"),
				*TargetActorName.ToString(),
				ClavicleSideLabel);
		}
	}

	const float EffectiveUpperArmTwistWeight = FMath::Clamp(CVarMediaPipeUpperArmTwistWeight.GetValueOnAnyThread(), 0.0f, 1.0f);
	const float EffectiveLowerArmTwistWeight = FMath::Clamp(CVarMediaPipeLowerArmTwistWeight.GetValueOnAnyThread(), 0.0f, 1.0f);
	const float EffectiveUpperArmTwistClampDegrees = FMath::Clamp(CVarMediaPipeUpperArmTwistClampDegrees.GetValueOnAnyThread(), 0.0f, 180.0f);
	const float EffectiveLowerArmTwistClampDegrees = FMath::Clamp(CVarMediaPipeLowerArmTwistClampDegrees.GetValueOnAnyThread(), 0.0f, 180.0f);
	const bool bShouldApplyShoulderRollbackGuard =
		!bMetaHumanFullArmChainFresh &&
		FMediaPipeArmGuardPolicy::ShouldApplyShoulderRollbackGuard(
			CVarMediaPipeShoulderRollbackGuard.GetValueOnAnyThread() != 0,
			bQuestConstrainedArmSolveApplied,
			bUseHmdRelativeAvatarArmFrame);

	auto BuildArmBasisRotation = [&](const FVector& RefDir, const FVector& TargetDir, const FVector& TargetPole, const FQuat& RefRot, const FQuat& RefBasis) -> FQuat
	{
		FMediaPipeArmBasisRotationInput BasisInput;
		BasisInput.RefDir = RefDir;
		BasisInput.TargetDir = TargetDir;
		BasisInput.TargetPole = TargetPole;
		BasisInput.RefRot = RefRot;
		BasisInput.RefBasis = RefBasis;
		BasisInput.bHasRefBasis = bHasRef;

		FQuat TargetRotCS = FQuat::Identity;
		if (!MediaPipeBodySolverMath::TryBuildArmBasisRotation(BasisInput, TargetRotCS))
		{
			return (FQuat::FindBetweenNormals(RefDir, TargetDir) * RefRot).GetNormalized();
		}

		return TargetRotCS;
	};

	if (ArmObservationAlphaScale <= KINDA_SMALL_NUMBER && (!bHasSmoothedUpperRotCS || !bHasSmoothedLowerRotCS))
	{
		return;
	}

	const bool bHadUpperRotBeforeUpdate = bHasSmoothedUpperRotCS;
	const bool bHadLowerRotBeforeUpdate = bHasSmoothedLowerRotCS;
	const FQuat UpperRotBeforeUpdateCS = SmoothedUpperRotCS;
	const FQuat LowerRotBeforeUpdateCS = SmoothedLowerRotCS;
	FQuat ShoulderRollbackTargetUpperCS = FQuat::Identity;
	FQuat ShoulderRollbackTargetLowerCS = FQuat::Identity;
	bool bHasShoulderRollbackTargets = false;
	bool bShoulderRollbackUsesStableRigPole = false;
	bool bShoulderRollbackUsesElbowPlaneRoll = false;
	bool bShoulderRollbackGuardApplied = false;
	float ShoulderRollbackGuardBlendUsed = 0.0f;
	const float ShoulderRollbackElbowReliability = GetLandmarkReliability(ElbowLm);
	const float ShoulderRollbackWristReliability = GetLandmarkReliability(WristLm);

	bool bArmIKBranchEntered = false;
	FVector LoggedArmIKTargetComp = FVector::ZeroVector;
	FVector LoggedArmIKWristComp = FVector::ZeroVector;
	const bool bQuestWristForcesArmIK =
		bQuestWristPositionApplied &&
		CVarQuestWristForceArmIK.GetValueOnAnyThread() != 0;
	if ((bUseArmIK || bQuestWristForcesArmIK) && RefUpperLen > KINDA_SMALL_NUMBER && RefLowerLen > KINDA_SMALL_NUMBER && UpperBone.IsValidToEvaluate() && LowerBone.IsValidToEvaluate())
	{
		bArmIKBranchEntered = true;
		const float TargetAlpha = HalfLifeToAlpha(EffectiveArmTargetHalfLifeSeconds, DeltaSeconds) * ArmObservationAlphaScale;
		const float RotAlpha = HalfLifeToAlpha(EffectiveArmRotationHalfLifeSeconds, DeltaSeconds) * ArmObservationAlphaScale;

		const FVector ShoulderPosComp = CSPose.GetComponentSpaceTransform(UpperBone.CachedCompactPoseIndex).GetTranslation();
		FVector PoseToWristVecComp = TargetCompTransform.InverseTransformVectorNoScale(WristWorld - ShoulderWorld);
		if (PoseToWristVecComp.IsNearlyZero())
		{
			PoseToWristVecComp = PoseUpperComp * (RefUpperLen + RefLowerLen);
		}

		FVector DirToWristComp = PoseToWristVecComp.GetSafeNormal();
		if (DirToWristComp.IsNearlyZero())
		{
			DirToWristComp = PoseUpperComp;
		}

		FVector ArmSolveRightComp = !ShoulderRightComp.IsNearlyZero() ? ShoulderRightComp : HipRightComp;
		if (ArmSolveRightComp.IsNearlyZero())
		{
			ArmSolveRightComp = FVector::RightVector;
		}
		const FVector ArmOutwardComp = (bIsLeft ? -ArmSolveRightComp : ArmSolveRightComp).GetSafeNormal();

		FVector LeftHipWorld = FVector::ZeroVector;
		FVector RightHipWorld = FVector::ZeroVector;
		const bool bHasHipMidWorld =
			TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftHip, LeftHipWorld) &&
			TryGetLmWorld((int32)EMediaPipePoseLandmark::RightHip, RightHipWorld);
		const FVector HipMidWorld = bHasHipMidWorld ? 0.5f * (LeftHipWorld + RightHipWorld) : FVector::ZeroVector;
		const float ArmReach = FMath::Max(RefUpperLen + RefLowerLen, 1.0f);
		const float UpperDownMetric = RemapClamped(FVector::DotProduct(PoseUpperComp, -UpComp), 0.30f, 0.85f);
		float WristBelowWaistMetric = 0.0f;
		if (bHasHipMidWorld)
		{
			const float WristHeightAlongUp = FVector::DotProduct(WristWorld - HipMidWorld, UpWorld);
			WristBelowWaistMetric = RemapClamped((-WristHeightAlongUp) / ArmReach, 0.00f, 0.30f);
		}
		const float DownPosePoleAlpha = UpperDownMetric * WristBelowWaistMetric;
		const FVector DownPosePoleHintComp = (-ForwardComp + 0.25f * ArmOutwardComp).GetSafeNormal();
		const FVector DownPosePoleReference = ProjectPoleReferenceToPlane(
			DownPosePoleHintComp,
			DirToWristComp,
			-ForwardComp,
			!ArmOutwardComp.IsNearlyZero() ? ArmOutwardComp : RefPoleDir);
		const FVector ActivePoleReference =
			(DownPosePoleAlpha > KINDA_SMALL_NUMBER && !DownPosePoleReference.IsNearlyZero())
				? LerpNormalized(RefPoleDir, DownPosePoleReference, DownPosePoleAlpha)
				: RefPoleDir;

		const FVector TargetWristComp = ShoulderPosComp + PoseToWristVecComp;
		LoggedArmIKTargetComp = TargetWristComp;

		const FVector PoseToWristDirWorld = (WristWorld - ShoulderWorld).GetSafeNormal();
		const FVector ToElbowWorld = (ElbowWorld - ShoulderWorld);
		FVector PoleDirWorld = ToElbowWorld - FVector::DotProduct(ToElbowWorld, PoseToWristDirWorld) * PoseToWristDirWorld;
		const float MeasuredPoleFraction = ArmReach > KINDA_SMALL_NUMBER ? (PoleDirWorld.Size() / ArmReach) : 0.0f;
		FVector PoleDirComp = TargetCompTransform.InverseTransformVectorNoScale(PoleDirWorld).GetSafeNormal();
		if (PoleDirComp.IsNearlyZero())
		{
			PoleDirComp = ActivePoleReference;
		}

		PoleDirComp = (PoleDirComp - FVector::DotProduct(PoleDirComp, DirToWristComp) * DirToWristComp).GetSafeNormal();
		if (PoleDirComp.IsNearlyZero())
		{
			PoleDirComp = ActivePoleReference;
		}
		PoleDirComp = LockVectorToHemisphere(PoleDirComp, ActivePoleReference);

		bool& bHasSmoothedIK = bIsLeft ? LeftArmState.bHasSmoothedArmIK : RightArmState.bHasSmoothedArmIK;
		FVector& SmoothedWristTargetComp = bIsLeft ? LeftArmState.SmoothedWristTargetComp : RightArmState.SmoothedWristTargetComp;
		FVector& SmoothedPoleDirComp = bIsLeft ? LeftArmState.SmoothedPoleDirComp : RightArmState.SmoothedPoleDirComp;

		if (!bHasSmoothedIK)
		{
			SmoothedWristTargetComp = TargetWristComp;
			SmoothedPoleDirComp = PoleDirComp;
			bHasSmoothedIK = true;
		}
		else
		{
			SmoothedWristTargetComp = FMath::Lerp(SmoothedWristTargetComp, TargetWristComp, TargetAlpha);
			SmoothedPoleDirComp = LockVectorToHemisphere(SmoothedPoleDirComp, ActivePoleReference);
			SmoothedPoleDirComp = LerpNormalized(SmoothedPoleDirComp, PoleDirComp, TargetAlpha);
			SmoothedPoleDirComp = LockVectorToHemisphere(SmoothedPoleDirComp, ActivePoleReference);
		}

		FVector TargetDelta = (SmoothedWristTargetComp - ShoulderPosComp);
		float Dist = TargetDelta.Size();
		if (Dist <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		Dist = FMath::Clamp(Dist, FMath::Abs(RefUpperLen - RefLowerLen) + 0.1f, (RefUpperLen + RefLowerLen) - 0.1f);

		const FVector DirToTarget = (TargetDelta / TargetDelta.Size());
		const FVector WristPosComp = ShoulderPosComp + DirToTarget * Dist;
		LoggedArmIKWristComp = WristPosComp;
		FVector Pole = (SmoothedPoleDirComp - FVector::DotProduct(SmoothedPoleDirComp, DirToTarget) * DirToTarget).GetSafeNormal();
		if (Pole.IsNearlyZero())
		{
			Pole = ActivePoleReference;
			Pole = (Pole - FVector::DotProduct(Pole, DirToTarget) * DirToTarget).GetSafeNormal();
		}
		if (Pole.IsNearlyZero())
		{
			Pole = FVector::UpVector;
			Pole = (Pole - FVector::DotProduct(Pole, DirToTarget) * DirToTarget).GetSafeNormal();
		}
		Pole = LockVectorToHemisphere(Pole, ActivePoleReference);

		FVector PlaneNormal = FVector::CrossProduct(DirToTarget, Pole).GetSafeNormal();
		if (PlaneNormal.IsNearlyZero())
		{
			PlaneNormal = FVector::CrossProduct(DirToTarget, FVector::UpVector).GetSafeNormal();
		}
		const FVector DirPerp = FVector::CrossProduct(PlaneNormal, DirToTarget).GetSafeNormal();
		if (DirPerp.IsNearlyZero())
		{
			return;
		}

		const float L1 = RefUpperLen;
		const float L2 = RefLowerLen;
		const float CosA = FMath::Clamp(((L1 * L1) + (Dist * Dist) - (L2 * L2)) / (2.0f * L1 * Dist), -1.0f, 1.0f);
		const float SinA = FMath::Sqrt(FMath::Max(0.0f, 1.0f - (CosA * CosA)));

		const FVector ElbowPosA = ShoulderPosComp + DirToTarget * (L1 * CosA) + DirPerp * (L1 * SinA);
		const FVector ElbowPosB = ShoulderPosComp + DirToTarget * (L1 * CosA) - DirPerp * (L1 * SinA);

		const FVector UpperDirA = (ElbowPosA - ShoulderPosComp).GetSafeNormal();
		const FVector LowerDirA = (WristPosComp - ElbowPosA).GetSafeNormal();
		const FVector PoleA = (UpperDirA - FVector::DotProduct(UpperDirA, DirToTarget) * DirToTarget).GetSafeNormal();
		FVector MeasuredPoleForBranch = TargetCompTransform.InverseTransformVectorNoScale(PoleDirWorld);
		MeasuredPoleForBranch = (MeasuredPoleForBranch - FVector::DotProduct(MeasuredPoleForBranch, DirToTarget) * DirToTarget).GetSafeNormal();
		const FVector BranchPoleReference = !MeasuredPoleForBranch.IsNearlyZero() ? MeasuredPoleForBranch : Pole;
		const float BranchPoleWeight = !MeasuredPoleForBranch.IsNearlyZero()
			? (1.10f * RemapClamped(MeasuredPoleFraction, 0.02f, 0.12f))
			: (0.35f * DownPosePoleAlpha);
		const float ScoreA =
			FVector::DotProduct(UpperDirA, PoseUpperComp) +
			FVector::DotProduct(LowerDirA, PoseLowerComp) +
			FVector::DotProduct(PoleA, BranchPoleReference) * BranchPoleWeight;

		const FVector UpperDirB = (ElbowPosB - ShoulderPosComp).GetSafeNormal();
		const FVector LowerDirB = (WristPosComp - ElbowPosB).GetSafeNormal();
		const FVector PoleB = (UpperDirB - FVector::DotProduct(UpperDirB, DirToTarget) * DirToTarget).GetSafeNormal();
		const float ScoreB =
			FVector::DotProduct(UpperDirB, PoseUpperComp) +
			FVector::DotProduct(LowerDirB, PoseLowerComp) +
			FVector::DotProduct(PoleB, BranchPoleReference) * BranchPoleWeight;

		double& LastArmDiagnosticLogTimeSeconds = bIsLeft ? DiagnosticsState.LastArmDiagnosticLogTimeSecondsL : DiagnosticsState.LastArmDiagnosticLogTimeSecondsR;
		const bool bUseA = (ScoreA >= ScoreB);
		if (TargetActorName == FName(TEXT("MP_MediaPipeMannyLike")) &&
			FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(PoseTimestampSeconds, 1.0, LastArmDiagnosticLogTimeSeconds))
		{
			const FVector PlaneWorld = FVector::CrossProduct(ElbowWorld - ShoulderWorld, WristWorld - ElbowWorld).GetSafeNormal();
			const FVector PlaneComp = FVector::CrossProduct(PoseUpperComp, PoseLowerComp).GetSafeNormal();
			FMediaPipeMannyLikeArmSolveLogInput ArmLogInput;
			ArmLogInput.TargetActorName = TargetActorName;
			ArmLogInput.bIsLeft = bIsLeft;
			ArmLogInput.ShoulderWorld = ShoulderWorld;
			ArmLogInput.ElbowWorld = ElbowWorld;
			ArmLogInput.WristWorld = WristWorld;
			ArmLogInput.PoseUpperComp = PoseUpperComp;
			ArmLogInput.PoseLowerComp = PoseLowerComp;
			ArmLogInput.PlaneWorld = PlaneWorld;
			ArmLogInput.ForwardWorld = ForwardWorld;
			ArmLogInput.PlaneComp = PlaneComp;
			ArmLogInput.ForwardComp = ForwardComp;
			ArmLogInput.UpComp = UpComp;
			ArmLogInput.MeasuredPoleFraction = MeasuredPoleFraction;
			ArmLogInput.UpperDownMetric = UpperDownMetric;
			ArmLogInput.WristBelowWaistMetric = WristBelowWaistMetric;
			ArmLogInput.DownPosePoleAlpha = DownPosePoleAlpha;
			ArmLogInput.Pole = Pole;
			ArmLogInput.BranchPoleReference = BranchPoleReference;
			ArmLogInput.ActivePoleReference = ActivePoleReference;
			ArmLogInput.BranchPoleWeight = BranchPoleWeight;
			ArmLogInput.ScoreA = ScoreA;
			ArmLogInput.ScoreB = ScoreB;
			ArmLogInput.bUseA = bUseA;
			FMediaPipeBodyDiagnostics::EmitMannyLikeArmSolveLog(ArmLogInput);
		}
		const FVector UpperDirComp = bUseA ? UpperDirA : UpperDirB;
		const FVector LowerDirComp = bUseA ? LowerDirA : LowerDirB;

		auto OrthoToForward = [](const FVector& V, const FVector& Forward) -> FVector
		{
			const FVector Fwd = Forward.GetSafeNormal();
			if (Fwd.IsNearlyZero())
			{
				return FVector::UpVector;
			}
			const FVector Ortho = V - FVector::DotProduct(V, Fwd) * Fwd;
			return Ortho.IsNearlyZero() ? FVector::UpVector : Ortho.GetSafeNormal();
		};

		auto TargetFromPole = [&](const FVector& RefDir, const FVector& TargetDir, const FVector& RefPole, const FVector& TargetPole, const FQuat& RefRot) -> FQuat
		{
			const FVector RefUp = OrthoToForward(RefPole, RefDir);
			const FVector TargetUp = OrthoToForward(TargetPole, TargetDir);
			if (!RefUp.IsNearlyZero() && !TargetUp.IsNearlyZero() &&
				!FVector::CrossProduct(RefDir, RefUp).IsNearlyZero() &&
				!FVector::CrossProduct(TargetDir, TargetUp).IsNearlyZero())
			{
				const FQuat RefBasis = MakeQuatFromForwardUp(RefDir, RefUp);
				const FQuat TargetBasis = MakeQuatFromForwardUp(TargetDir, TargetUp);
				if (!RefBasis.IsIdentity() && !TargetBasis.IsIdentity())
				{
					const FQuat Delta = TargetBasis * RefBasis.Inverse();
					return (Delta * RefRot).GetNormalized();
				}
			}

			const FQuat DeltaDir = FQuat::FindBetweenNormals(RefDir, TargetDir);
			return (DeltaDir * RefRot).GetNormalized();
		};

		const FQuat FallbackUpperComp = TargetFromPole(RefUpperDir, UpperDirComp, ActivePoleReference, Pole, RefUpperComp);
		const FQuat FallbackLowerComp = TargetFromPole(RefLowerDir, LowerDirComp, ActivePoleReference, Pole, RefLowerComp);
		FQuat FilteredUpperComp = FilterTargetRotationSwingTwist(
			RefUpperComp,
			RefUpperDir,
			FallbackUpperComp,
			EffectiveUpperArmTwistWeight,
			EffectiveUpperArmTwistClampDegrees);
		FQuat FilteredLowerComp = FilterTargetRotationSwingTwist(
			RefLowerComp,
			RefLowerDir,
			FallbackLowerComp,
			EffectiveLowerArmTwistWeight,
			EffectiveLowerArmTwistClampDegrees);

		ShoulderRollbackTargetUpperCS = FilteredUpperComp;
		ShoulderRollbackTargetLowerCS = FilteredLowerComp;
		bHasShoulderRollbackTargets = true;

		if (bShouldApplyShoulderRollbackGuard && bHadUpperRotBeforeUpdate)
		{
			FVector WristFromShoulderComp = TargetCompTransform.InverseTransformVectorNoScale(WristWorld - ShoulderWorld).GetSafeNormal();
			if (WristFromShoulderComp.IsNearlyZero())
			{
				WristFromShoulderComp = PoseUpperComp;
			}

			const float BackDotThreshold = FMath::Clamp(CVarMediaPipeShoulderRollbackBackDotThreshold.GetValueOnAnyThread(), -1.0f, 1.0f);
			const float UpperForwardDotForGuard = FVector::DotProduct(PoseUpperComp, ForwardComp);
			const float WristForwardDotForGuard = FVector::DotProduct(WristFromShoulderComp, ForwardComp);
			const float MaxTargetFromRefDegrees = FMath::Clamp(CVarMediaPipeShoulderRollbackGuardMaxTargetFromRefDegrees.GetValueOnAnyThread(), 1.0f, 180.0f);
			const float UpperTargetFromRefDegrees = FMath::RadiansToDegrees(RefUpperComp.AngularDistance(FilteredUpperComp));
			const float MinReliability = FMath::Clamp(CVarMediaPipeShoulderRollbackGuardMinReliability.GetValueOnAnyThread(), 0.0f, 1.0f);
			const bool bLowArmReliability = FMath::Min(ShoulderRollbackElbowReliability, ShoulderRollbackWristReliability) < MinReliability;
			const bool bGuardThisTarget =
				UpperForwardDotForGuard <= BackDotThreshold &&
				(WristForwardDotForGuard <= BackDotThreshold || bLowArmReliability || UpperTargetFromRefDegrees >= MaxTargetFromRefDegrees);
			if (bGuardThisTarget)
			{
				ShoulderRollbackGuardBlendUsed = FMath::Clamp(CVarMediaPipeShoulderRollbackGuardBlend.GetValueOnAnyThread(), 0.0f, 1.0f);
				FilteredUpperComp = FQuat::Slerp(UpperRotBeforeUpdateCS, FilteredUpperComp, ShoulderRollbackGuardBlendUsed).GetNormalized();
				if (bHadLowerRotBeforeUpdate)
				{
					FilteredLowerComp = FQuat::Slerp(LowerRotBeforeUpdateCS, FilteredLowerComp, ShoulderRollbackGuardBlendUsed).GetNormalized();
				}
				bShoulderRollbackGuardApplied = true;
			}
		}

		UpdateSmoothedRotation(bHasSmoothedUpperRotCS, SmoothedUpperRotCS, FilteredUpperComp, RotAlpha, ArmMaxStepDegrees);
		ApplyRotationCS(CSPose, UpperBone, SmoothedUpperRotCS);
		UpdateSmoothedRotation(bHasSmoothedLowerRotCS, SmoothedLowerRotCS, FilteredLowerComp, RotAlpha, ArmMaxStepDegrees);
		ApplyRotationCS(CSPose, LowerBone, SmoothedLowerRotCS);
	}
	else
	{
		const float RotAlpha = HalfLifeToAlpha(EffectiveArmRotationHalfLifeSeconds, DeltaSeconds) * ArmObservationAlphaScale;
		FQuat FallbackUpperComp = (FQuat::FindBetweenNormals(RefUpperDir, PoseUpperComp) * RefUpperComp).GetNormalized();
		FQuat FallbackLowerComp = (FQuat::FindBetweenNormals(RefLowerDir, PoseLowerComp) * RefLowerComp).GetNormalized();
		bool bUsesStableRigPole = false;
		bool bUsesElbowPlaneRoll = false;
		const float ElbowPlaneSin = FVector::CrossProduct(PoseUpperComp, PoseLowerComp).Size();
		FMediaPipeSolvedElbowPlaneArmInput SolvedPlaneInput;
		SolvedPlaneInput.RefUpperDir = RefUpperDir;
		SolvedPlaneInput.RefLowerDir = RefLowerDir;
		SolvedPlaneInput.TargetUpperDir = PoseUpperComp;
		SolvedPlaneInput.TargetLowerDir = PoseLowerComp;
		SolvedPlaneInput.RefUpperRot = RefUpperComp;
		SolvedPlaneInput.RefLowerRot = RefLowerComp;
		SolvedPlaneInput.RefUpperBasis = RefUpperBasisComp;
		SolvedPlaneInput.RefLowerBasis = RefLowerBasisComp;
		SolvedPlaneInput.bHasRefUpperBasis = bHasRef;
		SolvedPlaneInput.bHasRefLowerBasis = bHasRef;
		const float DirectArmPlaneMinSin = FMath::Clamp(CVarMediaPipeArmElbowPlaneMinSin.GetValueOnAnyThread(), 0.0f, 1.0f);
		SolvedPlaneInput.MinElbowPlaneSin = (bQuestConstrainedArmSolveApplied || bMetaHumanFullArmChainFresh)
			? FMath::Clamp(CVarQuestConstrainedArmSolvedPlaneMinSin.GetValueOnAnyThread(), 0.0f, 1.0f)
			: DirectArmPlaneMinSin;

		FMediaPipeSolvedElbowPlaneArmResult SolvedPlaneResult;
		const bool bShouldUseSolvedElbowPlane =
			bMetaHumanFullArmChainFresh ||
			bQuestConstrainedArmSolveApplied ||
			CVarMediaPipeArmUseElbowPlaneRoll.GetValueOnAnyThread() != 0;
		if (bShouldUseSolvedElbowPlane &&
			ElbowPlaneSin >= SolvedPlaneInput.MinElbowPlaneSin &&
			MediaPipeBodySolverMath::TryBuildSolvedElbowPlaneArmRotations(SolvedPlaneInput, SolvedPlaneResult))
		{
			bUsesElbowPlaneRoll = true;
			FallbackUpperComp = SolvedPlaneResult.UpperRotCS;
			FallbackLowerComp = SolvedPlaneResult.LowerRotCS;
		}
		else
		{
			const FVector UpperSurfaceHint = BuildArmSurfaceUpHint(UpComp, ForwardComp, PoseUpperComp);
			const FVector StableUpperSurfaceUp = ProjectPoleReferenceToPlane(UpperSurfaceHint, PoseUpperComp, ForwardComp, RefPoleDir);
			if (!StableUpperSurfaceUp.IsNearlyZero())
			{
				const FVector StableLowerSurfaceUp = ProjectPoleReferenceToPlane(StableUpperSurfaceUp, PoseLowerComp, ForwardComp, RefPoleDir);
				FallbackUpperComp = BuildArmBasisRotation(RefUpperDir, PoseUpperComp, StableUpperSurfaceUp, RefUpperComp, RefUpperSurfaceBasisComp);
				if (!StableLowerSurfaceUp.IsNearlyZero())
				{
					FallbackLowerComp = BuildArmBasisRotation(RefLowerDir, PoseLowerComp, StableLowerSurfaceUp, RefLowerComp, RefLowerSurfaceBasisComp);
				}
				bUsesStableRigPole = true;
			}
		}
		FQuat FilteredUpperComp = bUsesStableRigPole
			? FallbackUpperComp
			: FilterTargetRotationSwingTwist(
				RefUpperComp,
				RefUpperDir,
				FallbackUpperComp,
				EffectiveUpperArmTwistWeight,
				EffectiveUpperArmTwistClampDegrees);
		FQuat FilteredLowerComp = bUsesStableRigPole
			? FallbackLowerComp
			: FilterTargetRotationSwingTwist(
				RefLowerComp,
				RefLowerDir,
				FallbackLowerComp,
				EffectiveLowerArmTwistWeight,
				EffectiveLowerArmTwistClampDegrees);

		ShoulderRollbackTargetUpperCS = FilteredUpperComp;
		ShoulderRollbackTargetLowerCS = FilteredLowerComp;
		bHasShoulderRollbackTargets = true;
		bShoulderRollbackUsesStableRigPole = bUsesStableRigPole;
		bShoulderRollbackUsesElbowPlaneRoll = bUsesElbowPlaneRoll;

		if (bShouldApplyShoulderRollbackGuard && bHadUpperRotBeforeUpdate)
		{
			FVector WristFromShoulderComp = TargetCompTransform.InverseTransformVectorNoScale(WristWorld - ShoulderWorld).GetSafeNormal();
			if (WristFromShoulderComp.IsNearlyZero())
			{
				WristFromShoulderComp = PoseUpperComp;
			}

			const float BackDotThreshold = FMath::Clamp(CVarMediaPipeShoulderRollbackBackDotThreshold.GetValueOnAnyThread(), -1.0f, 1.0f);
			const float UpperForwardDotForGuard = FVector::DotProduct(PoseUpperComp, ForwardComp);
			const float WristForwardDotForGuard = FVector::DotProduct(WristFromShoulderComp, ForwardComp);
			const float MaxTargetFromRefDegrees = FMath::Clamp(CVarMediaPipeShoulderRollbackGuardMaxTargetFromRefDegrees.GetValueOnAnyThread(), 1.0f, 180.0f);
			const float UpperTargetFromRefDegrees = FMath::RadiansToDegrees(RefUpperComp.AngularDistance(FilteredUpperComp));
			const float MinReliability = FMath::Clamp(CVarMediaPipeShoulderRollbackGuardMinReliability.GetValueOnAnyThread(), 0.0f, 1.0f);
			const bool bLowArmReliability = FMath::Min(ShoulderRollbackElbowReliability, ShoulderRollbackWristReliability) < MinReliability;
			const bool bGuardThisTarget =
				UpperForwardDotForGuard <= BackDotThreshold &&
				(WristForwardDotForGuard <= BackDotThreshold || bLowArmReliability || UpperTargetFromRefDegrees >= MaxTargetFromRefDegrees);
			if (bGuardThisTarget)
			{
				ShoulderRollbackGuardBlendUsed = FMath::Clamp(CVarMediaPipeShoulderRollbackGuardBlend.GetValueOnAnyThread(), 0.0f, 1.0f);
				FilteredUpperComp = FQuat::Slerp(UpperRotBeforeUpdateCS, FilteredUpperComp, ShoulderRollbackGuardBlendUsed).GetNormalized();
				if (bHadLowerRotBeforeUpdate)
				{
					FilteredLowerComp = FQuat::Slerp(LowerRotBeforeUpdateCS, FilteredLowerComp, ShoulderRollbackGuardBlendUsed).GetNormalized();
				}
				bShoulderRollbackGuardApplied = true;
			}
		}

		UpdateSmoothedRotation(bHasSmoothedUpperRotCS, SmoothedUpperRotCS, FilteredUpperComp, RotAlpha, ArmMaxStepDegrees);
		ApplyRotationCS(CSPose, UpperBone, SmoothedUpperRotCS);
		UpdateSmoothedRotation(bHasSmoothedLowerRotCS, SmoothedLowerRotCS, FilteredLowerComp, RotAlpha, ArmMaxStepDegrees);
		ApplyRotationCS(CSPose, LowerBone, SmoothedLowerRotCS);
	}

	if (CVarMediaPipeShoulderRollbackTrace.GetValueOnAnyThread() != 0 && bHasShoulderRollbackTargets)
	{
		FVector WristFromShoulderComp = TargetCompTransform.InverseTransformVectorNoScale(WristWorld - ShoulderWorld).GetSafeNormal();
		if (WristFromShoulderComp.IsNearlyZero())
		{
			WristFromShoulderComp = PoseUpperComp;
		}

		const float BackDotThreshold = FMath::Clamp(CVarMediaPipeShoulderRollbackBackDotThreshold.GetValueOnAnyThread(), -1.0f, 1.0f);
		const float StepThresholdDegrees = FMath::Max(1.0f, CVarMediaPipeShoulderRollbackStepDegrees.GetValueOnAnyThread());
		const float UpperForwardDot = FVector::DotProduct(PoseUpperComp, ForwardComp);
		const float LowerForwardDot = FVector::DotProduct(PoseLowerComp, ForwardComp);
		const float WristForwardDot = FVector::DotProduct(WristFromShoulderComp, ForwardComp);
		const FVector ElbowPlaneNormalComp = FVector::CrossProduct(PoseUpperComp, PoseLowerComp).GetSafeNormal();
		const FVector OutwardComp = (bIsLeft ? -ShoulderRightComp : ShoulderRightComp).GetSafeNormal();
		const float ElbowPlaneOutwardDot = ElbowPlaneNormalComp.IsNearlyZero() || OutwardComp.IsNearlyZero()
			? 0.0f
			: FVector::DotProduct(ElbowPlaneNormalComp, OutwardComp);

		const float UpperTargetStepDegrees = bHadUpperRotBeforeUpdate
			? FMath::RadiansToDegrees(UpperRotBeforeUpdateCS.AngularDistance(ShoulderRollbackTargetUpperCS))
			: 0.0f;
		const float LowerTargetStepDegrees = bHadLowerRotBeforeUpdate
			? FMath::RadiansToDegrees(LowerRotBeforeUpdateCS.AngularDistance(ShoulderRollbackTargetLowerCS))
			: 0.0f;
		const float UpperAppliedStepDegrees = bHadUpperRotBeforeUpdate
			? FMath::RadiansToDegrees(UpperRotBeforeUpdateCS.AngularDistance(SmoothedUpperRotCS))
			: 0.0f;
		const float LowerAppliedStepDegrees = bHadLowerRotBeforeUpdate
			? FMath::RadiansToDegrees(LowerRotBeforeUpdateCS.AngularDistance(SmoothedLowerRotCS))
			: 0.0f;
		const float UpperTargetFromRefDegrees = FMath::RadiansToDegrees(RefUpperComp.AngularDistance(ShoulderRollbackTargetUpperCS));
		const float LowerTargetFromRefDegrees = FMath::RadiansToDegrees(RefLowerComp.AngularDistance(ShoulderRollbackTargetLowerCS));

		bool& bHasLastUpperForwardDot = bIsLeft ? DiagnosticsState.bHasLastShoulderRollbackUpperForwardDotL : DiagnosticsState.bHasLastShoulderRollbackUpperForwardDotR;
		float& LastUpperForwardDot = bIsLeft ? DiagnosticsState.LastShoulderRollbackUpperForwardDotL : DiagnosticsState.LastShoulderRollbackUpperForwardDotR;
		const bool bHadLastUpperForwardDot = bHasLastUpperForwardDot;
		const float PreviousUpperForwardDot = LastUpperForwardDot;
		const bool bCrossedBehind = bHadLastUpperForwardDot && PreviousUpperForwardDot > -0.05f && UpperForwardDot <= BackDotThreshold;
		bHasLastUpperForwardDot = true;
		LastUpperForwardDot = UpperForwardDot;

		const bool bUpperBehind = UpperForwardDot <= BackDotThreshold;
		const bool bWristBehind = WristForwardDot <= BackDotThreshold;
		const bool bTargetSnap = UpperTargetStepDegrees >= StepThresholdDegrees || LowerTargetStepDegrees >= StepThresholdDegrees;
		const bool bClampHit = ArmMaxStepDegrees > 0.0f &&
			((bHadUpperRotBeforeUpdate && UpperTargetStepDegrees > UpperAppliedStepDegrees + 5.0f && UpperAppliedStepDegrees >= ArmMaxStepDegrees - 0.25f) ||
			 (bHadLowerRotBeforeUpdate && LowerTargetStepDegrees > LowerAppliedStepDegrees + 5.0f && LowerAppliedStepDegrees >= ArmMaxStepDegrees - 0.25f));
		const bool bShouldLog = bCrossedBehind || bUpperBehind || bWristBehind || bTargetSnap || bClampHit;

		if (bShouldLog)
		{
			double& LastShoulderRollbackTraceLogTimeSeconds = bIsLeft ? DiagnosticsState.LastShoulderRollbackTraceLogTimeSecondsL : DiagnosticsState.LastShoulderRollbackTraceLogTimeSecondsR;
			const double NowSeconds = FPlatformTime::Seconds();
			const double RequiredLogIntervalSeconds = static_cast<double>(
				FMath::Clamp(CVarMediaPipeShoulderRollbackTraceLogIntervalSeconds.GetValueOnAnyThread(), 0.02f, 2.0f));
			if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, RequiredLogIntervalSeconds, LastShoulderRollbackTraceLogTimeSeconds))
			{
				FVector LeftHipWorld = FVector::ZeroVector;
				FVector RightHipWorld = FVector::ZeroVector;
				const bool bHasHipMidWorld =
					TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftHip, LeftHipWorld) &&
					TryGetLmWorld((int32)EMediaPipePoseLandmark::RightHip, RightHipWorld);
				const FVector HipMidWorld = bHasHipMidWorld ? (LeftHipWorld + RightHipWorld) * 0.5f : FVector::ZeroVector;
				const float ShoulderForwardCm = bHasHipMidWorld ? FVector::DotProduct(ShoulderWorld - HipMidWorld, ForwardWorld) : 0.0f;
				const float ElbowForwardCm = bHasHipMidWorld ? FVector::DotProduct(ElbowWorld - HipMidWorld, ForwardWorld) : 0.0f;
				const float WristForwardCm = bHasHipMidWorld ? FVector::DotProduct(WristWorld - HipMidWorld, ForwardWorld) : 0.0f;
				const float ShoulderReliability = GetLandmarkReliability(ShoulderLm);
				const float ElbowReliability = GetLandmarkReliability(ElbowLm);
				const float WristReliability = GetLandmarkReliability(WristLm);

				FMediaPipeShoulderRollbackTraceFormatInput TraceInput;
				TraceInput.TargetActorName = TargetActorName;
				TraceInput.bIsLeft = bIsLeft;
				TraceInput.bUpperBehind = bUpperBehind;
				TraceInput.bWristBehind = bWristBehind;
				TraceInput.bCrossedBehind = bCrossedBehind;
				TraceInput.bTargetSnap = bTargetSnap;
				TraceInput.bClampHit = bClampHit;
				TraceInput.bGuardApplied = bShoulderRollbackGuardApplied;
				TraceInput.GuardBlend = ShoulderRollbackGuardBlendUsed;
				TraceInput.UpperForwardDot = UpperForwardDot;
				TraceInput.LowerForwardDot = LowerForwardDot;
				TraceInput.WristForwardDot = WristForwardDot;
				TraceInput.PreviousUpperForwardDot = bHadLastUpperForwardDot ? PreviousUpperForwardDot : 0.0f;
				TraceInput.BackThreshold = BackDotThreshold;
				TraceInput.UpperTargetStepDeg = UpperTargetStepDegrees;
				TraceInput.LowerTargetStepDeg = LowerTargetStepDegrees;
				TraceInput.UpperAppliedStepDeg = UpperAppliedStepDegrees;
				TraceInput.LowerAppliedStepDeg = LowerAppliedStepDegrees;
				TraceInput.ArmMaxStepDeg = ArmMaxStepDegrees;
				TraceInput.UpperTargetFromRefDeg = UpperTargetFromRefDegrees;
				TraceInput.LowerTargetFromRefDeg = LowerTargetFromRefDegrees;
				TraceInput.ElbowPlaneOutwardDot = ElbowPlaneOutwardDot;
				TraceInput.bStablePole = bShoulderRollbackUsesStableRigPole;
				TraceInput.bElbowPlaneRoll = bShoulderRollbackUsesElbowPlaneRoll;
				TraceInput.bArmIK = bUseArmIK;
				TraceInput.bQuestForceArmIK = CVarQuestWristForceArmIK.GetValueOnAnyThread() != 0;
				TraceInput.bLegs = bDriveLegs;
				TraceInput.bLegIK = bUseLegIK;
				TraceInput.bPelvisTranslation = bDrivePelvisTranslation;
				TraceInput.bClavicles = bDriveClavicles;
				TraceInput.bTorsoBasis = bHasTorsoBasis;
				TraceInput.ShoulderReliability = ShoulderReliability;
				TraceInput.ElbowReliability = ElbowReliability;
				TraceInput.WristReliability = WristReliability;
				TraceInput.ShoulderForwardCm = ShoulderForwardCm;
				TraceInput.ElbowForwardCm = ElbowForwardCm;
				TraceInput.WristForwardCm = WristForwardCm;
				TraceInput.ShoulderWorld = ShoulderWorld;
				TraceInput.ElbowWorld = ElbowWorld;
				TraceInput.WristWorld = WristWorld;
				TraceInput.ForwardWorld = ForwardWorld;
				TraceInput.UpWorld = UpWorld;
				TraceInput.ShoulderRightWorld = ShoulderRightWorld;
				TraceInput.HipRightWorld = HipRightWorld;
				UE_LOG(LogMediaPipePose, Warning, TEXT("%s"), *FMediaPipeShoulderRollbackDiagnostics::FormatTraceLog(TraceInput));
			}
		}
	}

	const FVector QuestForearmAxisComp = (bHasSmoothedLowerRotCS ? SmoothedLowerRotCS.RotateVector(RefLowerDir) : PoseLowerComp).GetSafeNormal();
	const FQuat CurrentHandTargetCS = HandBone.IsValidToEvaluate()
		? CSPose.GetComponentSpaceTransform(HandBone.CachedCompactPoseIndex).GetRotation().GetNormalized()
		: RefHandComp;
	const FTransform HandBeforeCS = HandBone.IsValidToEvaluate()
		? CSPose.GetComponentSpaceTransform(HandBone.CachedCompactPoseIndex)
		: FTransform::Identity;
	FQuestHandRotationTrace QuestHandRotationTrace;
	const bool bQuestHandRotationApplied = DriveQuestHandCS(CSPose, bIsLeft, QuestForearmAxisComp, CurrentHandTargetCS, DeltaSeconds, &QuestHandRotationTrace, &QuestWristTrace);
	const FTransform HandAfterCS = HandBone.IsValidToEvaluate()
		? CSPose.GetComponentSpaceTransform(HandBone.CachedCompactPoseIndex)
		: FTransform::Identity;
	const float QuestHandRotationDeltaDeg = bQuestHandRotationApplied
		? FMath::RadiansToDegrees(HandBeforeCS.GetRotation().AngularDistance(HandAfterCS.GetRotation()))
		: 0.0f;
	const bool bWouldEnterArmIKIfApplied =
		QuestWristTrace.bMapped != 0 &&
		CVarQuestWristForceArmIK.GetValueOnAnyThread() != 0 &&
		RefUpperLen > KINDA_SMALL_NUMBER &&
		RefLowerLen > KINDA_SMALL_NUMBER &&
		UpperBone.IsValidToEvaluate() &&
		LowerBone.IsValidToEvaluate();
	if (CVarQuestWristDebug.GetValueOnAnyThread() != 0 ||
		CVarQuestWristTrace.GetValueOnAnyThread() != 0 ||
		CVarQuestHandDebug.GetValueOnAnyThread() != 0)
	{
		double& LastQuestWristSolveLogTimeSeconds = bIsLeft ? DiagnosticsState.LastQuestWristSolveLogTimeSecondsL : DiagnosticsState.LastQuestWristSolveLogTimeSecondsR;
		const double NowSeconds = FPlatformTime::Seconds();
		static double LastGlobalQuestWristSolveLogTimes[2] = {-1.0, -1.0};
		double& LastGlobalQuestWristSolveLogTimeSeconds = LastGlobalQuestWristSolveLogTimes[bIsLeft ? 0 : 1];
		const double RequiredLogIntervalSeconds = CVarQuestWristTrace.GetValueOnAnyThread() != 0
			? static_cast<double>(FMath::Clamp(CVarQuestWristTraceLogIntervalSeconds.GetValueOnAnyThread(), 0.05f, 5.0f))
			: 1.0;
		if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottledPair(
			NowSeconds,
			RequiredLogIntervalSeconds,
			LastQuestWristSolveLogTimeSeconds,
			LastGlobalQuestWristSolveLogTimeSeconds))
		{
			FMediaPipeQuestWristSolveLogFormatInput SolveLogInput;
			SolveLogInput.TargetActorName = TargetActorName;
			SolveLogInput.bIsLeft = bIsLeft;
			SolveLogInput.QuestArmMode = QuestArmMode;
			SolveLogInput.bRequireTrackedForApply = CVarQuestWristRequireTrackedForApply.GetValueOnAnyThread() != 0;
			SolveLogInput.bArmIKBranchEntered = bArmIKBranchEntered;
			SolveLogInput.bForceArmIK = CVarQuestWristForceArmIK.GetValueOnAnyThread() != 0;
			SolveLogInput.bWouldEnterArmIKIfApplied = bWouldEnterArmIKIfApplied;
			SolveLogInput.bUseArmIK = bUseArmIK;
			SolveLogInput.LoggedArmIKTargetComp = LoggedArmIKTargetComp;
			SolveLogInput.LoggedArmIKWristComp = LoggedArmIKWristComp;
			SolveLogInput.HandBoneName = HandBone.BoneName;
			SolveLogInput.QuestHandRotationBlend = QuestHandRotationBlend;
			SolveLogInput.bQuestHandRotationApplied = bQuestHandRotationApplied;
			SolveLogInput.QuestHandRotationDeltaDeg = QuestHandRotationDeltaDeg;
			SolveLogInput.ShoulderWorld = ShoulderWorld;
			SolveLogInput.ElbowWorld = ElbowWorld;
			SolveLogInput.HandBeforeLocationCS = HandBeforeCS.GetTranslation();
			SolveLogInput.HandAfterLocationCS = HandAfterCS.GetTranslation();
			SolveLogInput.WristTrace = &QuestWristTrace;
			SolveLogInput.HandTrace = &QuestHandRotationTrace;
			UE_LOG(LogMediaPipePose, Log, TEXT("%s"), *FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristSolveLog(SolveLogInput));
			FMediaPipeQuestWristPostSolveReportInput PostSolveReportInput;
			PostSolveReportInput.TargetActorName = TargetActorName;
			PostSolveReportInput.bIsLeft = bIsLeft;
			PostSolveReportInput.bEmitTraceLogs = CVarQuestWristTrace.GetValueOnAnyThread() != 0;
			PostSolveReportInput.bEmitHud =
				CVarQuestHandHud.GetValueOnAnyThread() != 0 ||
				CVarQuestWristCalibrationHud.GetValueOnAnyThread() != 0;
			PostSolveReportInput.bQuestHandRotationApplied = bQuestHandRotationApplied;
			PostSolveReportInput.bArmIKBranchEntered = bArmIKBranchEntered;
			PostSolveReportInput.bForceArmIK = CVarQuestWristForceArmIK.GetValueOnAnyThread() != 0;
			PostSolveReportInput.DeltaSeconds = DeltaSeconds;
			PostSolveReportInput.QuestHandRotationDeltaDeg = QuestHandRotationDeltaDeg;
			PostSolveReportInput.RequiredLogIntervalSeconds = RequiredLogIntervalSeconds;
			PostSolveReportInput.HandTrace = &QuestHandRotationTrace;
			FMediaPipeQuestWristDebugReporter::EmitPostSolveReports(PostSolveReportInput);
		}
	}
	if (bQuestHandRotationApplied)
	{
		DriveQuestFingerBonesCS(CSPose, bIsLeft, QuestHands, DeltaSeconds);
	}
	if (bMetaHumanFullArmChainFresh)
	{
		FVector PosedShoulderWorld = FVector::ZeroVector;
		FVector PosedElbowWorld = FVector::ZeroVector;
		FVector PosedHandWorld = WristWorld;
		FMediaPipeQuestWristDebugReporter::TryGetArmWorldAfterSolve(
			CSPose,
			UpperBone,
			LowerBone,
			HandBone,
			TargetCompTransform,
			PosedShoulderWorld,
			PosedElbowWorld,
			PosedHandWorld);
		const float TargetReachCm = FVector::Dist(ShoulderWorld, WristWorld);
		const float ElbowBendDeg = ComputeMediaPipeArmChainElbowBendDegrees(ShoulderWorld, ElbowWorld, WristWorld);
		EmitMetaHumanFullArmChainLog(true, bQuestHandRotationApplied, TargetReachCm, ElbowBendDeg, PosedHandWorld);
	}
	if (CVarMetaHumanArmSanity.GetValueOnAnyThread() != 0 ||
		CVarQuestWristTrace.GetValueOnAnyThread() != 0)
	{
		double& LastMetaHumanArmSanityLogTimeSeconds = bIsLeft ? DiagnosticsState.LastMetaHumanArmSanityLogTimeSecondsL : DiagnosticsState.LastMetaHumanArmSanityLogTimeSecondsR;
		const double NowSeconds = FPlatformTime::Seconds();
		const double SanityLogIntervalSeconds = static_cast<double>(FMath::Clamp(CVarMetaHumanArmSanityLogIntervalSeconds.GetValueOnAnyThread(), 0.05f, 5.0f));
		if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, SanityLogIntervalSeconds, LastMetaHumanArmSanityLogTimeSeconds))
		{
			FVector PosedShoulderWorld = FVector::ZeroVector;
			FVector PosedElbowWorld = FVector::ZeroVector;
			FVector PosedHandWorld = FVector::ZeroVector;
			const bool bHasPosedArm = FMediaPipeQuestWristDebugReporter::TryGetArmWorldAfterSolve(
				CSPose,
				UpperBone,
				LowerBone,
				HandBone,
				TargetCompTransform,
				PosedShoulderWorld,
				PosedElbowWorld,
				PosedHandWorld);

			const float MaxWristErrorCm = FMath::Max(0.0f, CVarMetaHumanArmSanityMaxWristErrorCm.GetValueOnAnyThread());
			const float MaxHandRotErrorDeg = FMath::Max(0.0f, CVarMetaHumanArmSanityMaxHandRotErrorDeg.GetValueOnAnyThread());
			const float MaxBasisErrorDeg = FMath::Max(0.0f, CVarMetaHumanArmSanityMaxBasisErrorDeg.GetValueOnAnyThread());
			const float MaxLengthErrorCm = FMath::Max(0.0f, CVarMetaHumanArmSanityMaxLengthErrorCm.GetValueOnAnyThread());
			const float MinElbowBendDeg = FMath::Max(0.0f, CVarMetaHumanArmSanityMinElbowBendDeg.GetValueOnAnyThread());
			const float MaxSwingDeg = FMath::Max(0.0f, CVarMetaHumanArmSanityMaxSwingDeg.GetValueOnAnyThread());

			const FMediaPipeMetaHumanArmSanityFormatInput SanityInput =
				FMediaPipeQuestWristDiagnosticFormatter::BuildMetaHumanArmSanityInput(
					TargetActorName,
					bIsLeft,
					bHasPosedArm,
					QuestArmMode,
					bQuestHandRotationApplied,
					bArmIKBranchEntered,
					MaxWristErrorCm,
					MaxHandRotErrorDeg,
					MaxBasisErrorDeg,
					MaxLengthErrorCm,
					MinElbowBendDeg,
					MaxSwingDeg,
					RefUpperLen,
					RefLowerLen,
					QuestWristTrace,
					QuestHandRotationTrace,
					PosedShoulderWorld,
					PosedElbowWorld,
					PosedHandWorld,
					ShoulderWorld,
					ElbowWorld);

			FMediaPipeQuestWristDebugReporter::EmitMetaHumanArmSanityReport(
				SanityInput,
				CVarQuestHandCompare.GetValueOnAnyThread() >= 2,
				bIsLeft,
				SanityLogIntervalSeconds);
		}
	}
	if (CVarQuestHandCompare.GetValueOnAnyThread() >= 1)
	{
		double& LastQuestHandCompareLogTimeSeconds = bIsLeft ? DiagnosticsState.LastQuestHandCompareLogTimeSecondsL : DiagnosticsState.LastQuestHandCompareLogTimeSecondsR;
		const double NowSeconds = FPlatformTime::Seconds();
		const double CompareLogIntervalSeconds = static_cast<double>(FMath::Clamp(CVarQuestWristTraceLogIntervalSeconds.GetValueOnAnyThread(), 0.05f, 5.0f));
		if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, CompareLogIntervalSeconds, LastQuestHandCompareLogTimeSeconds))
		{
			const FString TargetActorLabel = TargetActorName.ToString();
			FMediaPipeQuestHandCompareBuildInput CompareInput;
			CompareInput.TargetActorName = TargetActorName;
			CompareInput.bIsLeft = bIsLeft;
			CompareInput.CompareMode = CVarQuestHandCompare.GetValueOnAnyThread();
			CompareInput.bVisibleMetaHuman = TargetMetaHumanProfile.bIsMetaHuman;
			CompareInput.bQuestHandRotationApplied = bQuestHandRotationApplied;
			CompareInput.bArmIKBranchEntered = bArmIKBranchEntered;
			CompareInput.bForceArmIK = CVarQuestWristForceArmIK.GetValueOnAnyThread() != 0;
			CompareInput.QuestHands = &QuestHands;
			CompareInput.QuestWristTrace = QuestWristTrace;
			CompareInput.QuestHandRotationTrace = QuestHandRotationTrace;
			CompareInput.TargetCompTransform = TargetCompTransform;
			CompareInput.PoseToWorldTransform = PoseToWorldTransform;
			CompareInput.AvatarHandComp = HandAfterCS.GetTranslation();
			CompareInput.SolvedWristWorld = WristWorld;
			CompareInput.ShoulderWorld = ShoulderWorld;

			FMediaPipeQuestHandCompareDiagnostics::EmitReport(
				CompareInput,
				CSPose,
				HandBone,
				bIsLeft ? FingerBonesL : FingerBonesR,
				[this](const FVector& QuestDirectionWorld, FVector& OutMediaDirectionWorld)
				{
					return TryMapQuestDirectionToMediaWorld(QuestDirectionWorld, OutMediaDirectionWorld);
				});
		}
	}
	if (bQuestHandRotationApplied)
	{
		return;
	}

	if (bMetaHumanFullArmChainFresh)
	{
		DriveQuestFingerBonesCS(CSPose, bIsLeft, QuestHands, DeltaSeconds);
		return;
	}

	if (!bDriveHandRotation)
	{
		DriveQuestFingerBonesCS(CSPose, bIsLeft, QuestHands, DeltaSeconds);
		return;
	}

	FVector HandForwardWorld = FVector::ZeroVector;
	FVector HandUpWorld = FVector::ZeroVector;
	bool bHasHandBasis = false;
	float BasisSin = 1.0f;

	if (bHasPoseHands)
	{
		auto TryGetRawHandPointWorld = [&](const int32 HandIdx, FVector& OutWorld) -> bool
		{
			const bool bHasSide = bIsLeft ? (PoseHands.bHasLeft != 0) : (PoseHands.bHasRight != 0);
			if (!bHasSide)
			{
				return false;
			}

			const FMediaPipeRawHandLandmarks& HandWorld = bIsLeft ? PoseHands.LeftWorld : PoseHands.RightWorld;
			const FMediaPipeRawHandLandmark& WristRaw = HandWorld.Landmarks[HandLm_Wrist];
			const FMediaPipeRawHandLandmark& PointRaw = HandWorld.Landmarks[HandIdx];

			const FVector DeltaMp(
				PointRaw.X - WristRaw.X,
				PointRaw.Y - WristRaw.Y,
				PointRaw.Z - WristRaw.Z);
			FVector DeltaUe = MediaPipePoseCoordinate::MpWorldToUeLocalUnscaled(DeltaMp, bMirrorLandmarksLR) * WorldScale;
			DeltaUe = PoseToWorldTransform.TransformVectorNoScale(DeltaUe);
			OutWorld = WristWorld + DeltaUe;
			return true;
		};

		FVector IndexMcpWorld = FVector::ZeroVector;
		FVector PinkyMcpWorld = FVector::ZeroVector;
		FVector MiddleMcpWorld = FVector::ZeroVector;
		if (TryGetRawHandPointWorld(HandLm_IndexMcp, IndexMcpWorld) &&
			TryGetRawHandPointWorld(HandLm_PinkyMcp, PinkyMcpWorld))
		{
			const bool bHasMiddle = TryGetRawHandPointWorld(HandLm_MiddleMcp, MiddleMcpWorld);

			HandForwardWorld = ((IndexMcpWorld + PinkyMcpWorld) * 0.5f - WristWorld).GetSafeNormal();
			if (HandForwardWorld.IsNearlyZero() && bHasMiddle)
			{
				HandForwardWorld = (MiddleMcpWorld - WristWorld).GetSafeNormal();
			}

			const FVector AcrossWorld = (IndexMcpWorld - PinkyMcpWorld).GetSafeNormal();
			if (!HandForwardWorld.IsNearlyZero() && !AcrossWorld.IsNearlyZero())
			{
				BasisSin = FVector::CrossProduct(HandForwardWorld, AcrossWorld).Size();
				HandUpWorld = FVector::CrossProduct(HandForwardWorld, AcrossWorld).GetSafeNormal();
				if (HandUpWorld.IsNearlyZero())
				{
					HandUpWorld = FVector::CrossProduct(HandForwardWorld, PoseUpperWorld).GetSafeNormal();
				}
				bHasHandBasis = !HandUpWorld.IsNearlyZero();
			}
		}
	}

	if (!bHasHandBasis)
	{
		FVector IndexWorld = FVector::ZeroVector;
		FVector PinkyWorld = FVector::ZeroVector;
		if (!TryGetLmWorld(IndexLm, IndexWorld) || !TryGetLmWorld(PinkyLm, PinkyWorld))
		{
			return;
		}

		HandForwardWorld = ((IndexWorld + PinkyWorld) * 0.5f - WristWorld).GetSafeNormal();
		if (HandForwardWorld.IsNearlyZero())
		{
			HandForwardWorld = (IndexWorld - WristWorld).GetSafeNormal();
		}
		if (HandForwardWorld.IsNearlyZero())
		{
			return;
		}

		const FVector AcrossWorld = (IndexWorld - PinkyWorld).GetSafeNormal();
		if (AcrossWorld.IsNearlyZero())
		{
			return;
		}

		BasisSin = FVector::CrossProduct(HandForwardWorld, AcrossWorld).Size();
		HandUpWorld = FVector::CrossProduct(HandForwardWorld, AcrossWorld).GetSafeNormal();
		if (HandUpWorld.IsNearlyZero())
		{
			HandUpWorld = FVector::CrossProduct(HandForwardWorld, PoseUpperWorld).GetSafeNormal();
		}
		if (HandUpWorld.IsNearlyZero())
		{
			return;
		}
		bHasHandBasis = true;
	}

	const bool bTwistReliable = (BasisSin >= HandTwistMinBasisSin);
	const FVector HandForwardComp = TargetCompTransform.InverseTransformVectorNoScale(HandForwardWorld).GetSafeNormal();
	const FVector HandUpComp = TargetCompTransform.InverseTransformVectorNoScale(HandUpWorld).GetSafeNormal();
	if (HandForwardComp.IsNearlyZero() || HandUpComp.IsNearlyZero())
	{
		return;
	}

	const FVector ForearmAxisComp = (bHasSmoothedLowerRotCS ? SmoothedLowerRotCS.RotateVector(RefLowerDir) : PoseLowerComp).GetSafeNormal();
	if (ForearmAxisComp.IsNearlyZero())
	{
		return;
	}

	auto MakeTargetHandRot = [&](const FVector& ForwardC, const FVector& UpC) -> FQuat
	{
		const FQuat TargetBasisComp = MakeQuatFromForwardUp(ForwardC, UpC);
		const FQuat DeltaHand = TargetBasisComp * RefHandBasisComp.Inverse();
		return (DeltaHand * RefHandComp).GetNormalized();
	};

	bool& bHasLastGoodTarget = bIsLeft ? LeftArmState.bHasLastGoodHandTarget : RightArmState.bHasLastGoodHandTarget;
	FQuat& LastGoodTarget = bIsLeft ? LeftArmState.LastGoodHandTargetCS : RightArmState.LastGoodHandTargetCS;
	int32& ActiveBranch = bIsLeft ? LeftArmState.ActiveHandTargetBranch : RightArmState.ActiveHandTargetBranch;
	int32& PendingBranch = bIsLeft ? LeftArmState.PendingHandTargetBranch : RightArmState.PendingHandTargetBranch;
	int32& PendingFrames = bIsLeft ? LeftArmState.PendingHandTargetBranchFrames : RightArmState.PendingHandTargetBranchFrames;

	bool& bHasSmoothedSwing = bIsLeft ? LeftArmState.bHasSmoothedHandSwingCS : RightArmState.bHasSmoothedHandSwingCS;
	FQuat& SmoothedSwing = bIsLeft ? LeftArmState.SmoothedHandSwingCS : RightArmState.SmoothedHandSwingCS;
	bool& bHasSmoothedTwist = bIsLeft ? LeftArmState.bHasSmoothedHandTwist : RightArmState.bHasSmoothedHandTwist;
	float& SmoothedTwistDeg = bIsLeft ? LeftArmState.SmoothedHandTwistDeg : RightArmState.SmoothedHandTwistDeg;

	FQuat CurrentHandRotCS = RefHandComp;
	if (bHasSmoothedSwing && bHasSmoothedTwist)
	{
		const FQuat TwistQ(ForearmAxisComp, FMath::DegreesToRadians(SmoothedTwistDeg));
		CurrentHandRotCS = (SmoothedSwing * TwistQ).GetNormalized();
	}
	else if (bHasSmoothedSwing)
	{
		CurrentHandRotCS = SmoothedSwing;
	}

	const FQuat Candidates[4] = {
		MakeTargetHandRot(HandForwardComp, HandUpComp),
		MakeTargetHandRot(-HandForwardComp, HandUpComp),
		MakeTargetHandRot(HandForwardComp, -HandUpComp),
		MakeTargetHandRot(-HandForwardComp, -HandUpComp),
	};

	const FQuat BranchRef = bHasLastGoodTarget ? LastGoodTarget : CurrentHandRotCS;
	float BestScore = -1.0f;
	int32 BestIndex = 0;
	for (int32 i = 0; i < 4; ++i)
	{
		const float Score = FMath::Abs(Candidates[i] | BranchRef);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestIndex = i;
		}
	}

	if (!bHasLastGoodTarget)
	{
		ActiveBranch = BestIndex;
		PendingFrames = 0;
		PendingBranch = BestIndex;
	}
	else if (BestIndex != ActiveBranch)
	{
		if (bTwistReliable)
		{
			if (PendingBranch != BestIndex)
			{
				PendingBranch = BestIndex;
				PendingFrames = 1;
			}
			else
			{
				PendingFrames++;
			}

			if (PendingFrames >= FMath::Max(1, HandFlipHysteresisFrames))
			{
				ActiveBranch = BestIndex;
				PendingFrames = 0;
				PendingBranch = ActiveBranch;
			}
		}
	}
	else
	{
		PendingFrames = 0;
		PendingBranch = ActiveBranch;
	}

	const FQuat MeasuredTarget = Candidates[FMath::Clamp(ActiveBranch, 0, 3)];
	if (bTwistReliable && BestIndex == ActiveBranch)
	{
		LastGoodTarget = MeasuredTarget;
		bHasLastGoodTarget = true;
	}

	FQuat TargetSwing = FQuat::Identity;
	float TargetTwistDeg = 0.0f;
	if (!DecomposeSwingTwistDegAroundAxis(MeasuredTarget, ForearmAxisComp, TargetSwing, TargetTwistDeg))
	{
		return;
	}

	const float RotAlpha = HalfLifeToAlpha(ArmIKRotationHalfLifeSeconds, DeltaSeconds);
	if (!bHasSmoothedSwing)
	{
		SmoothedSwing = TargetSwing;
		bHasSmoothedSwing = true;
	}
	else
	{
		float AlphaSwing = RotAlpha;
		const float SwingAngleDeg = FMath::RadiansToDegrees(SmoothedSwing.AngularDistance(TargetSwing));
		const float MaxSwingStepDeg = HandSwingMaxDegreesPerSecond * DeltaSeconds;
		if (MaxSwingStepDeg > 0.0f && SwingAngleDeg > KINDA_SMALL_NUMBER)
		{
			AlphaSwing = FMath::Min(AlphaSwing, MaxSwingStepDeg / SwingAngleDeg);
		}
		SmoothedSwing = FQuat::Slerp(SmoothedSwing, TargetSwing, AlphaSwing).GetNormalized();
	}

	float DesiredTwistDeg = TargetTwistDeg;
	if (HandTwistClampDegrees > 0.0f)
	{
		DesiredTwistDeg = FMath::Clamp(DesiredTwistDeg, -HandTwistClampDegrees, HandTwistClampDegrees);
	}

	if (!bHasSmoothedTwist)
	{
		SmoothedTwistDeg = DesiredTwistDeg;
		bHasSmoothedTwist = true;
	}
	else if (bTwistReliable)
	{
		float DeltaTwistDeg = FMath::FindDeltaAngleDegrees(SmoothedTwistDeg, DesiredTwistDeg);
		const float MaxTwistStepDeg = HandTwistMaxDegreesPerSecond * DeltaSeconds;
		if (MaxTwistStepDeg > 0.0f)
		{
			DeltaTwistDeg = FMath::Clamp(DeltaTwistDeg, -MaxTwistStepDeg, MaxTwistStepDeg);
		}
		SmoothedTwistDeg = SmoothedTwistDeg + (DeltaTwistDeg * RotAlpha);
		if (HandTwistClampDegrees > 0.0f)
		{
			SmoothedTwistDeg = FMath::Clamp(SmoothedTwistDeg, -HandTwistClampDegrees, HandTwistClampDegrees);
		}
	}
	else if (HandTwistClampDegrees > 0.0f)
	{
		SmoothedTwistDeg = FMath::Clamp(SmoothedTwistDeg, -HandTwistClampDegrees, HandTwistClampDegrees);
	}

	const FQuat TwistQ(ForearmAxisComp, FMath::DegreesToRadians(SmoothedTwistDeg));
	const FQuat TargetHandRotCS = (SmoothedSwing * TwistQ).GetNormalized();
	ApplyRotationCS(CSPose, HandBone, TargetHandRotCS);
	DriveQuestFingerBonesCS(CSPose, bIsLeft, QuestHands, DeltaSeconds);
}
