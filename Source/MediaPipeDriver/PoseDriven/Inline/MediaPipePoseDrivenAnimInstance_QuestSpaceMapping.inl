bool FAnimNode_MediaPipePoseDriven::TryGetQuestHmdWorldPose(FVector& OutLocationWorld, FQuat& OutRotationWorld) const
{
	OutLocationWorld = CachedQuestHmdWorld;
	OutRotationWorld = CachedQuestHmdRotWorld;
	return bHasCachedQuestHmdPose;
}

FVector FAnimNode_MediaPipePoseDriven::GetCachedQuestTrackingUpWorld() const
{
	return CachedQuestTrackingUpWorld.IsNearlyZero() ? FVector::UpVector : CachedQuestTrackingUpWorld;
}

bool FAnimNode_MediaPipePoseDriven::TryGetMediaPipeHeadFrameWorld(FVector& OutHeadWorld, FQuat& OutBodyBasisWorld)
{
	OutHeadWorld = FVector::ZeroVector;
	OutBodyBasisWorld = FQuat::Identity;

	FVector HipRightWorld = FVector::ZeroVector;
	FVector ShoulderRightWorld = FVector::ZeroVector;
	FVector UpWorld = FVector::ZeroVector;
	FVector ForwardWorld = FVector::ZeroVector;
	if (!TryGetTorsoBasisWorld(HipRightWorld, ShoulderRightWorld, UpWorld, ForwardWorld))
	{
		return false;
	}

	const FVector RightWorld = !ShoulderRightWorld.IsNearlyZero() ? ShoulderRightWorld : HipRightWorld;
	if (RightWorld.IsNearlyZero() || UpWorld.IsNearlyZero() || ForwardWorld.IsNearlyZero())
	{
		return false;
	}

	FVector LeftEyeWorld = FVector::ZeroVector;
	FVector RightEyeWorld = FVector::ZeroVector;
	FVector LeftEarWorld = FVector::ZeroVector;
	FVector RightEarWorld = FVector::ZeroVector;
	FVector NoseWorld = FVector::ZeroVector;
	const bool bHasEyes =
		TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftEye, LeftEyeWorld) &&
		TryGetLmWorld((int32)EMediaPipePoseLandmark::RightEye, RightEyeWorld);
	const bool bHasEars =
		TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftEar, LeftEarWorld) &&
		TryGetLmWorld((int32)EMediaPipePoseLandmark::RightEar, RightEarWorld);
	const bool bHasNose = TryGetLmWorld((int32)EMediaPipePoseLandmark::Nose, NoseWorld);

	if (bHasEyes)
	{
		OutHeadWorld = (LeftEyeWorld + RightEyeWorld) * 0.5f;
	}
	else if (bHasEars)
	{
		OutHeadWorld = (LeftEarWorld + RightEarWorld) * 0.5f;
	}
	else if (bHasNose)
	{
		OutHeadWorld = NoseWorld;
	}
	else
	{
		FVector LeftShoulderWorld = FVector::ZeroVector;
		FVector RightShoulderWorld = FVector::ZeroVector;
		if (!TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftShoulder, LeftShoulderWorld) ||
			!TryGetLmWorld((int32)EMediaPipePoseLandmark::RightShoulder, RightShoulderWorld))
		{
			return false;
		}
		OutHeadWorld = ((LeftShoulderWorld + RightShoulderWorld) * 0.5f) + UpWorld.GetSafeNormal() * 24.0f;
	}

	const FQuat BodyBasisWorld = MakeQuatFromForwardUp(ForwardWorld, UpWorld);
	if (FVector::CrossProduct(ForwardWorld, UpWorld).IsNearlyZero())
	{
		return false;
	}

	OutBodyBasisWorld = BodyBasisWorld;
	return true;
}

bool FAnimNode_MediaPipePoseDriven::TryBuildQuestMediaSpaceCalibration()
{
	FVector MediaHeadWorld = FVector::ZeroVector;
	FQuat MediaBodyBasisWorld = FQuat::Identity;
	if (!TryGetMediaPipeHeadFrameWorld(MediaHeadWorld, MediaBodyBasisWorld))
	{
		return false;
	}

	const FVector QuestUpWorld = GetCachedQuestTrackingUpWorld();

	FVector QuestHmdWorld = FVector::ZeroVector;
	FQuat QuestHmdRotWorld = FQuat::Identity;
	if (TryGetQuestHmdWorldPose(QuestHmdWorld, QuestHmdRotWorld))
	{
		FVector QuestForwardWorld = QuestHmdRotWorld.RotateVector(FVector::ForwardVector);
		QuestForwardWorld = (QuestForwardWorld - FVector::DotProduct(QuestForwardWorld, QuestUpWorld) * QuestUpWorld).GetSafeNormal();
		if (QuestForwardWorld.IsNearlyZero())
		{
			QuestForwardWorld = FVector::ForwardVector;
		}

		if (IsQuestHandSideTracked(QuestHands, true) && IsQuestHandSideTracked(QuestHands, false))
		{
			const FVector LeftWristWorld = QuestHands.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::Wrist)];
			const FVector RightWristWorld = QuestHands.RightPositionsWorld[static_cast<int32>(EHandKeypoint::Wrist)];
			FVector QuestRightWorld = (RightWristWorld - LeftWristWorld).GetSafeNormal();
			QuestRightWorld = (QuestRightWorld - FVector::DotProduct(QuestRightWorld, QuestUpWorld) * QuestUpWorld).GetSafeNormal();
			if (!QuestRightWorld.IsNearlyZero())
			{
				FVector HandsForwardWorld = FVector::CrossProduct(QuestRightWorld, QuestUpWorld).GetSafeNormal();
				if (!HandsForwardWorld.IsNearlyZero())
				{
					QuestForwardWorld = LockVectorToHemisphere(HandsForwardWorld, QuestForwardWorld);
				}
			}
		}

		const FQuat QuestBasisWorld = MakeQuatFromForwardUp(QuestForwardWorld, QuestUpWorld);
		QuestWristState.QuestToMediaSpaceWorld = (MediaBodyBasisWorld * QuestBasisWorld.Inverse()).GetNormalized();
		QuestWristState.QuestCalibrationBasisWorld = QuestBasisWorld;
		QuestWristState.MediaPipeCalibrationBodyBasisWorld = MediaBodyBasisWorld;
		QuestWristState.QuestCalibrationHmdWorld = QuestHmdWorld;
		QuestWristState.MediaPipeCalibrationHeadWorld = MediaHeadWorld;
		QuestWristState.MediaPipeCalibrationShoulderMidWorld = FVector::ZeroVector;
		QuestWristState.MediaPipeCalibrationAnchorBodyLocal = FVector::ZeroVector;
		QuestWristState.QuestToMediaSpaceScale = 1.0f;
		QuestWristState.QuestMediaSpaceCalibrationMode = EQuestMediaSpaceCalibrationMode::HmdHead;
		QuestWristState.bHasQuestMediaSpaceCalibration = true;
		return true;
	}

	if (!IsQuestHandSideTracked(QuestHands, true) || !IsQuestHandSideTracked(QuestHands, false))
	{
		return false;
	}

	const FVector QuestLeftWristWorld = QuestHands.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::Wrist)];
	const FVector QuestRightWristWorld = QuestHands.RightPositionsWorld[static_cast<int32>(EHandKeypoint::Wrist)];
	const FVector QuestWristMidWorld = 0.5f * (QuestLeftWristWorld + QuestRightWristWorld);
	const float QuestWristSpan = FVector::Dist(QuestLeftWristWorld, QuestRightWristWorld);
	if (QuestLeftWristWorld.IsNearlyZero() || QuestRightWristWorld.IsNearlyZero() || QuestWristSpan <= 8.0f)
	{
		return false;
	}

	FVector QuestRightWorld = (QuestRightWristWorld - QuestLeftWristWorld).GetSafeNormal();
	QuestRightWorld = (QuestRightWorld - FVector::DotProduct(QuestRightWorld, QuestUpWorld) * QuestUpWorld).GetSafeNormal();
	if (QuestRightWorld.IsNearlyZero())
	{
		return false;
	}

	const FVector QuestForwardWorld = FVector::CrossProduct(QuestRightWorld, QuestUpWorld).GetSafeNormal();
	const FQuat QuestBasisWorld = MakeQuatFromForwardUp(QuestForwardWorld, QuestUpWorld);

	FVector MediaLeftShoulderWorld = FVector::ZeroVector;
	FVector MediaRightShoulderWorld = FVector::ZeroVector;
	FVector MediaLeftWristWorld = FVector::ZeroVector;
	FVector MediaRightWristWorld = FVector::ZeroVector;
	if (!TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftShoulder, MediaLeftShoulderWorld) ||
		!TryGetLmWorld((int32)EMediaPipePoseLandmark::RightShoulder, MediaRightShoulderWorld) ||
		!TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftWrist, MediaLeftWristWorld) ||
		!TryGetLmWorld((int32)EMediaPipePoseLandmark::RightWrist, MediaRightWristWorld))
	{
		return false;
	}

	const FVector MediaShoulderMidWorld = 0.5f * (MediaLeftShoulderWorld + MediaRightShoulderWorld);
	const FVector MediaWristMidWorld = 0.5f * (MediaLeftWristWorld + MediaRightWristWorld);
	const float MediaWristSpan = FVector::Dist(MediaLeftWristWorld, MediaRightWristWorld);
	const float MediaShoulderSpan = FVector::Dist(MediaLeftShoulderWorld, MediaRightShoulderWorld);
	float PairScale = 1.0f;
	if (QuestWristSpan > 8.0f && MediaWristSpan > 8.0f)
	{
		PairScale = FMath::Clamp(MediaWristSpan / QuestWristSpan, 0.65f, 1.45f);
	}
	else if (QuestWristSpan > 8.0f && MediaShoulderSpan > 12.0f)
	{
		PairScale = FMath::Clamp(MediaShoulderSpan / QuestWristSpan, 0.65f, 1.45f);
	}

	QuestWristState.QuestToMediaSpaceWorld = (MediaBodyBasisWorld * QuestBasisWorld.Inverse()).GetNormalized();
	QuestWristState.QuestCalibrationBasisWorld = QuestBasisWorld;
	QuestWristState.MediaPipeCalibrationBodyBasisWorld = MediaBodyBasisWorld;
	QuestWristState.QuestCalibrationHmdWorld = QuestWristMidWorld;
	QuestWristState.MediaPipeCalibrationHeadWorld = MediaWristMidWorld;
	QuestWristState.MediaPipeCalibrationShoulderMidWorld = MediaShoulderMidWorld;
	QuestWristState.MediaPipeCalibrationAnchorBodyLocal = MediaBodyBasisWorld.Inverse().RotateVector(MediaWristMidWorld - MediaShoulderMidWorld);
	QuestWristState.QuestToMediaSpaceScale = PairScale;
	QuestWristState.QuestMediaSpaceCalibrationMode = EQuestMediaSpaceCalibrationMode::HandPairBody;
	QuestWristState.bHasQuestMediaSpaceCalibration = true;
	return true;
}

bool FAnimNode_MediaPipePoseDriven::TryGetCurrentQuestToMediaSpaceRotation(FQuat& OutQuestToMediaSpaceWorld)
{
	OutQuestToMediaSpaceWorld = FQuat::Identity;
	if (!QuestWristState.bHasQuestMediaSpaceCalibration && !TryBuildQuestMediaSpaceCalibration())
	{
		return false;
	}

	if (QuestWristState.QuestMediaSpaceCalibrationMode == EQuestMediaSpaceCalibrationMode::HandPairBody)
	{
		FVector MediaHeadWorld = FVector::ZeroVector;
		FQuat MediaBodyBasisWorld = FQuat::Identity;
		if (!TryGetMediaPipeHeadFrameWorld(MediaHeadWorld, MediaBodyBasisWorld))
		{
			return false;
		}

		OutQuestToMediaSpaceWorld = (MediaBodyBasisWorld * QuestWristState.QuestCalibrationBasisWorld.Inverse()).GetNormalized();
		return true;
	}

	OutQuestToMediaSpaceWorld = QuestWristState.QuestToMediaSpaceWorld.GetNormalized();
	return true;
}

bool FAnimNode_MediaPipePoseDriven::TryMapQuestDirectionToMediaWorld(const FVector& QuestDirectionWorld, FVector& OutMediaDirectionWorld)
{
	FQuat CurrentQuestToMediaSpaceWorld = FQuat::Identity;
	if (!TryGetCurrentQuestToMediaSpaceRotation(CurrentQuestToMediaSpaceWorld))
	{
		return false;
	}

	OutMediaDirectionWorld = CurrentQuestToMediaSpaceWorld.RotateVector(QuestDirectionWorld).GetSafeNormal();
	return !OutMediaDirectionWorld.IsNearlyZero();
}

bool FAnimNode_MediaPipePoseDriven::TryMapQuestHmdRelativeWristToAvatarWorld(
	FCSPose<FCompactPose>& CSPose,
	const bool bIsLeft,
	const FVector& QuestAnchorWorld,
	const FQuat& QuestAnchorYawWorld,
	const FVector& QuestWristWorld,
	FVector& OutAvatarEyeWorld,
	FVector& OutMappedWristWorld) const
{
	OutAvatarEyeWorld = FVector::ZeroVector;
	OutMappedWristWorld = FVector::ZeroVector;

	FMediaPipeAvatarEmbodimentProfile MappingProfile = bHasTargetEmbodimentProfile
		? TargetEmbodimentProfile
		: FMediaPipeAvatarEmbodimentProfile();
	MappingProfile.bUseTargetFaceForwardAxis = bUseTargetFaceForwardAxis;
	MappingProfile.DefaultEyeLocalOffset = bHasTargetEyeLocalOffset ? TargetEyeLocalOffset : FVector::ZeroVector;
	MappingProfile.EmbodiedCameraForwardOffsetCm = TargetEmbodiedCameraForwardOffsetCm;

	FVector FallbackEyeWorld = FVector::ZeroVector;
	if (Head.IsValidToEvaluate())
	{
		const FTransform HeadCS = CSPose.GetComponentSpaceTransform(Head.CachedCompactPoseIndex);
		FallbackEyeWorld = TargetCompTransform.TransformPosition(HeadCS.GetTranslation());
	}
	else if (!RefHeadPosComp.IsNearlyZero())
	{
		FallbackEyeWorld = TargetCompTransform.TransformPosition(RefHeadPosComp);
	}
	else
	{
		const FVector AvatarForwardWorld = FMediaPipeAvatarEmbodimentSolver::GetAvatarForwardWorld(TargetCompTransform, MappingProfile);
		const FVector AvatarUpWorld = FMediaPipeAvatarEmbodimentSolver::GetAvatarUpWorld(TargetCompTransform, AvatarForwardWorld);
		FallbackEyeWorld = TargetCompTransform.GetLocation() + AvatarUpWorld * 160.0f;
	}

	auto ReadConsoleFloat = [](const TCHAR* Name, const float DefaultValue) -> float
	{
		if (IConsoleVariable* ConsoleVariable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			return ConsoleVariable->GetFloat();
		}
		return DefaultValue;
	};
	FMediaPipeAvatarHmdWristMapInput MapInput;
	MapInput.QuestAnchorWorld = QuestAnchorWorld;
	MapInput.QuestAnchorYawWorld = QuestAnchorYawWorld;
	MapInput.QuestTrackingUpWorld = GetCachedQuestTrackingUpWorld();
	MapInput.QuestWristWorld = QuestWristWorld;
	MapInput.TargetCompTransform = TargetCompTransform;
	MapInput.Profile = MappingProfile;
	MapInput.bHasProfileEyeLocalOffset = bHasTargetEyeLocalOffset;
	MapInput.FallbackEyeWorld = FallbackEyeWorld;
	MapInput.UserCameraForwardOffsetCm = ReadConsoleFloat(TEXT("mp.AutoQuestEmbodiedCameraForwardOffsetCm"), 0.0f);
	MapInput.PositionScale = CVarQuestWristPositionScale.GetValueOnAnyThread();
	MapInput.MaxOffsetCm = CVarQuestWristMaxOffsetCm.GetValueOnAnyThread();
	const FString SideKey = bIsLeft ? FString(TEXT("left")) : FString(TEXT("right"));
	if (const FVector* WristOffset = MappingProfile.AvatarLockedWristArmChainOffsetsCm.Find(SideKey))
	{
		MapInput.WristArmChainOffsetCm = *WristOffset;
	}
	else if (const FVector* HandOffset = MappingProfile.AvatarLockedWristArmChainOffsetsCm.Find(
		bIsLeft ? FString(TEXT("hand_l")) : FString(TEXT("hand_r"))))
	{
		MapInput.WristArmChainOffsetCm = *HandOffset;
	}

	FMediaPipeAvatarHmdWristMapResult MapResult;
	if (!FMediaPipeAvatarEmbodimentSolver::MapQuestHmdRelativeWristToAvatarWorld(MapInput, MapResult))
	{
		return false;
	}

	OutAvatarEyeWorld = MapResult.AvatarCameraWorld;
	OutMappedWristWorld = MapResult.MappedWristWorld;
	return !OutMappedWristWorld.ContainsNaN();
}

bool FAnimNode_MediaPipePoseDriven::TryApplyQuestWristPositionWorld(FCSPose<FCompactPose>& CSPose, bool bIsLeft, const FVector& MediaPipeWristWorld, FVector& InOutWristWorld, FQuestWristMappingTrace* OutTrace)
{
	FQuestWristMappingTrace LocalTrace;
	FQuestWristMappingTrace& Trace = OutTrace ? *OutTrace : LocalTrace;
	Trace = FQuestWristMappingTrace{};
	Trace.MediaPipeWristWorld = MediaPipeWristWorld;
	Trace.FinalWristWorld = MediaPipeWristWorld;

	if (!bUseQuestHandTracking ||
		CVarQuestHandTracking.GetValueOnAnyThread() == 0)
	{
		return false;
	}

	const float RequestedBlend = FMath::Clamp(CVarQuestWristPositionBlend.GetValueOnAnyThread(), 0.0f, 1.0f);
	const bool bTraceOnly = RequestedBlend <= KINDA_SMALL_NUMBER && CVarQuestWristTrace.GetValueOnAnyThread() != 0;
	if (RequestedBlend <= KINDA_SMALL_NUMBER && !bTraceOnly)
	{
		return false;
	}
	float Blend = bTraceOnly ? 1.0f : RequestedBlend;
	Trace.RequestedBlend = RequestedBlend;
	Trace.EffectiveBlend = Blend;
	Trace.bTraceOnly = bTraceOnly ? 1 : 0;
	Trace.RuntimeStateKey = RuntimeStateKey;

	FQuestWristRuntimeState& QuestWristRuntimeState = GetQuestWristRuntimeState(RuntimeStateKey);
	FQuestWristSideRuntimeState& QuestWristSideState = bIsLeft ? QuestWristRuntimeState.Left : QuestWristRuntimeState.Right;
	const int32 QuestArmMode = FMath::Clamp(CVarQuestArmMode.GetValueOnAnyThread(), 0, 3);
	const bool bConstrainedArmMode = QuestArmMode >= 2;
	const bool bUseHmdRelativeAvatarEndpoint = QuestArmMode >= 3;

	auto FadePositionAuthorityToZero = [&]()
	{
		QuestWristSideState.PositionAuthorityAlpha = 0.0f;
		QuestWristSideState.PositionAuthorityLastTimeSeconds = -1.0;
		QuestWristSideState.bHasPositionFilter = false;
		QuestWristSideState.PositionFilterDeltaComp = FVector::ZeroVector;
		QuestWristSideState.PositionFilterLastRawDeltaComp = FVector::ZeroVector;
		QuestWristSideState.PositionFilterLastTimeSeconds = -1.0;
	};

	auto BuildWristCandidateWorld = [&](const FVector& MappedQuestWristWorld) -> FVector
	{
		const FVector TargetDeltaWorld = (MappedQuestWristWorld - MediaPipeWristWorld) * Blend;
		FVector FinalDeltaWorld = TargetDeltaWorld;

		if (!bTraceOnly && CVarQuestWristPositionAdaptiveFilter.GetValueOnAnyThread() != 0)
		{
			const FVector TargetDeltaComp = TargetCompTransform.InverseTransformVectorNoScale(TargetDeltaWorld);
			const double NowSeconds = FPlatformTime::Seconds();
			float FilterDeltaSeconds = QuestWristSideState.PositionFilterLastTimeSeconds >= 0.0
				? static_cast<float>(NowSeconds - QuestWristSideState.PositionFilterLastTimeSeconds)
				: FMath::Max(CachedDeltaTimeSeconds, 1.0f / 90.0f);
			if (!FMath::IsFinite(FilterDeltaSeconds) || FilterDeltaSeconds <= 0.0f)
			{
				FilterDeltaSeconds = FMath::Max(CachedDeltaTimeSeconds, 1.0f / 90.0f);
			}
			FilterDeltaSeconds = FMath::Clamp(FilterDeltaSeconds, 1.0f / 240.0f, 0.10f);

			const float TargetDeltaCm = TargetDeltaComp.Size();
			const float RawStepCm = QuestWristSideState.bHasPositionFilter
				? FVector::Dist(TargetDeltaComp, QuestWristSideState.PositionFilterLastRawDeltaComp)
				: 0.0f;
			const float SpeedCmSec = FilterDeltaSeconds > KINDA_SMALL_NUMBER
				? RawStepCm / FilterDeltaSeconds
				: 0.0f;
			const float ResetDistanceCm = FMath::Max(0.0f, CVarQuestWristPositionFilterResetDistanceCm.GetValueOnAnyThread());
			const bool bResetFilter =
				!QuestWristSideState.bHasPositionFilter ||
				QuestWristSideState.PositionFilterLastTimeSeconds < 0.0 ||
				NowSeconds - QuestWristSideState.PositionFilterLastTimeSeconds > 0.50 ||
				(ResetDistanceCm > KINDA_SMALL_NUMBER &&
				 FVector::Dist(TargetDeltaComp, QuestWristSideState.PositionFilterDeltaComp) > ResetDistanceCm);

			float FilterAlpha = 1.0f;
			if (bResetFilter)
			{
				QuestWristSideState.PositionFilterDeltaComp = TargetDeltaComp;
				QuestWristSideState.bHasPositionFilter = true;
			}
			else
			{
				FVector FilterInputComp = TargetDeltaComp;
				const float DeadbandCm = FMath::Max(0.0f, CVarQuestWristPositionFilterDeadbandCm.GetValueOnAnyThread());
				const FVector ErrorComp = TargetDeltaComp - QuestWristSideState.PositionFilterDeltaComp;
				const float ErrorCm = ErrorComp.Size();
				if (DeadbandCm > KINDA_SMALL_NUMBER && ErrorCm <= DeadbandCm)
				{
					FilterInputComp = QuestWristSideState.PositionFilterDeltaComp;
				}
				else if (DeadbandCm > KINDA_SMALL_NUMBER && ErrorCm > KINDA_SMALL_NUMBER)
				{
					FilterInputComp = QuestWristSideState.PositionFilterDeltaComp + ErrorComp.GetSafeNormal() * (ErrorCm - DeadbandCm);
				}

				const float StillHalfLife = FMath::Max(0.0f, CVarQuestWristPositionFilterStillHalfLife.GetValueOnAnyThread());
				const float MovingHalfLife = FMath::Max(0.0f, CVarQuestWristPositionFilterMovingHalfLife.GetValueOnAnyThread());
				const float SpeedForMinLag = FMath::Max(1.0f, CVarQuestWristPositionFilterSpeedForMinLag.GetValueOnAnyThread());
				const float MotionAlpha = RemapClamped(SpeedCmSec, SpeedForMinLag * 0.12f, SpeedForMinLag);
				const float EffectiveHalfLife = FMath::Lerp(StillHalfLife, MovingHalfLife, MotionAlpha);
				FilterAlpha = HalfLifeToAlpha(EffectiveHalfLife, FilterDeltaSeconds);
				QuestWristSideState.PositionFilterDeltaComp = FMath::Lerp(QuestWristSideState.PositionFilterDeltaComp, FilterInputComp, FilterAlpha);
			}

			QuestWristSideState.PositionFilterLastRawDeltaComp = TargetDeltaComp;
			QuestWristSideState.PositionFilterLastTimeSeconds = NowSeconds;
			FinalDeltaWorld = TargetCompTransform.TransformVectorNoScale(QuestWristSideState.PositionFilterDeltaComp);

			Trace.bPositionFilterApplied = 1;
			Trace.PositionFilterAlpha = FilterAlpha;
			Trace.PositionFilterSpeedCmSec = SpeedCmSec;
			Trace.PositionFilterTargetDeltaCm = TargetDeltaCm;
			Trace.PositionFilterFilteredDeltaCm = QuestWristSideState.PositionFilterDeltaComp.Size();
		}

		return MediaPipeWristWorld + FinalDeltaWorld;
	};

	auto TryFadeQuestWristToMediaPipe = [&]() -> bool
	{
		if (!bConstrainedArmMode ||
			bTraceOnly ||
			!QuestWristSideState.bHasPositionFilter ||
			QuestWristSideState.LastTargetTimeSeconds < 0.0)
		{
			FadePositionAuthorityToZero();
			return false;
		}

		const float GraceSeconds = FMath::Max(0.0f, CVarQuestWristLostTrackingGraceSeconds.GetValueOnAnyThread());
		const double NowSeconds = FPlatformTime::Seconds();
		if (GraceSeconds <= KINDA_SMALL_NUMBER ||
			NowSeconds - QuestWristSideState.LastTargetTimeSeconds > GraceSeconds)
		{
			FadePositionAuthorityToZero();
			return false;
		}

		const float FadeAlpha = FMath::Clamp(
			static_cast<float>((NowSeconds - QuestWristSideState.LastTargetTimeSeconds) / FMath::Max(static_cast<double>(GraceSeconds), UE_SMALL_NUMBER)),
			0.0f,
			1.0f);
		const FVector FadedDeltaComp = QuestWristSideState.PositionFilterDeltaComp * (1.0f - FadeAlpha);
		const FVector CandidateWristWorld = MediaPipeWristWorld + TargetCompTransform.TransformVectorNoScale(FadedDeltaComp);
		InOutWristWorld = CandidateWristWorld;
		QuestWristSideState.PositionAuthorityAlpha = FMath::Clamp(1.0f - FadeAlpha, 0.0f, 1.0f);
		Trace.bPositionApplied = 1;
		Trace.bPositionFilterApplied = 1;
		Trace.PositionFilterAlpha = FadeAlpha;
		Trace.PositionFilterTargetDeltaCm = 0.0f;
		Trace.PositionFilterFilteredDeltaCm = FadedDeltaComp.Size();
		Trace.RawQuestWristWorld = QuestWristSideState.HeldRawQuestWristWorld;
		Trace.MappedQuestWristWorld = CandidateWristWorld;
		Trace.FinalWristWorld = InOutWristWorld;
		Trace.MappedOffsetFromMediaPipeCm = FVector::Dist(CandidateWristWorld, MediaPipeWristWorld);
		Trace.CalibrationMode = QuestWristState.QuestMediaSpaceCalibrationMode;
		Trace.bMapped = 1;
		Trace.bUsedHeldQuestWrist = 1;
		return true;
	};

	auto TryUseHeldQuestWristTarget = [&]() -> bool
	{
		const float GraceSeconds = FMath::Max(0.0f, CVarQuestWristLostTrackingGraceSeconds.GetValueOnAnyThread());
		const double NowSeconds = FPlatformTime::Seconds();
		FMediaPipeQuestWristHeldTargetLossInput HeldTargetLossInput;
		HeldTargetLossInput.bHasHeldTarget = QuestWristSideState.bHasHeldTarget;
		HeldTargetLossInput.GraceSeconds = GraceSeconds;
		HeldTargetLossInput.LastTargetAgeSeconds = QuestWristSideState.LastTargetTimeSeconds >= 0.0
			? static_cast<float>(NowSeconds - QuestWristSideState.LastTargetTimeSeconds)
			: -1.0f;
		if (FMediaPipeQuestWristApplyPolicy::ShouldClearPositionAuthorityForHeldTargetLoss(HeldTargetLossInput))
		{
			FadePositionAuthorityToZero();
			return false;
		}

		const FVector CandidateWristWorld = BuildWristCandidateWorld(QuestWristSideState.HeldTargetWorld);
		if (!bTraceOnly)
		{
			InOutWristWorld = CandidateWristWorld;
			Trace.bPositionApplied = 1;
		}
		if (!IsUsableQuestWristPosition(Trace.RawQuestWristWorld))
		{
			Trace.RawQuestWristWorld = QuestWristSideState.HeldRawQuestWristWorld;
		}
		Trace.MappedQuestWristWorld = QuestWristSideState.HeldMappedQuestWristWorld;
		Trace.FinalWristWorld = bTraceOnly ? MediaPipeWristWorld : InOutWristWorld;
		Trace.MappedOffsetFromMediaPipeCm = FVector::Dist(QuestWristSideState.HeldMappedQuestWristWorld, MediaPipeWristWorld);
		Trace.CalibrationMode = QuestWristState.QuestMediaSpaceCalibrationMode;
		Trace.bMapped = 1;
		Trace.bUsedHeldQuestWrist = 1;
		return !bTraceOnly;
	};

	const bool bQuestSideTracked = IsQuestHandSideTracked(QuestHands, bIsLeft);
	const TStaticArray<FVector, QuestHandKeypointCount>& Positions = GetQuestHandPositions(QuestHands, bIsLeft);
	const FVector QuestWristWorld = Positions[static_cast<int32>(EHandKeypoint::Wrist)];
	const bool bQuestSideUsable = IsQuestHandSideUsableForWrist(QuestHands, bIsLeft);
	const bool bRequireTrackedForApply = CVarQuestWristRequireTrackedForApply.GetValueOnAnyThread() != 0;
	const double PolicyNowSeconds = FPlatformTime::Seconds();
	const bool bHasLastAcceptedLiveWristPosition =
		QuestWristSideState.bHasLastAcceptedLiveWristPosition &&
		QuestWristSideState.LastAcceptedLiveWristTimeSeconds >= 0.0;
	const float LastAcceptedLiveWristAgeSeconds = bHasLastAcceptedLiveWristPosition
		? static_cast<float>(PolicyNowSeconds - QuestWristSideState.LastAcceptedLiveWristTimeSeconds)
		: 0.0f;
	const float UntrackedLiveWristStepFromLastAcceptedCm = bHasLastAcceptedLiveWristPosition
		? FVector::Dist(QuestWristWorld, QuestWristSideState.LastAcceptedLiveWristWorld)
		: 0.0f;
	Trace.bQuestTracked = bQuestSideTracked ? 1 : 0;
	if (bQuestSideUsable)
	{
		Trace.RawQuestWristWorld = QuestWristWorld;
	}
	FMediaPipeQuestWristApplyPolicyInput ApplyPolicyInput;
	ApplyPolicyInput.bRequireTrackedForApply = bRequireTrackedForApply;
	ApplyPolicyInput.bAllowUsableUntrackedForPositionApply = bConstrainedArmMode;
	ApplyPolicyInput.bQuestSideTracked = bQuestSideTracked;
	ApplyPolicyInput.bQuestSideUsable = bQuestSideUsable;
	ApplyPolicyInput.bHasRecentAcceptedLiveWristPosition = bHasLastAcceptedLiveWristPosition;
	ApplyPolicyInput.UntrackedLiveWristStepFromLastAcceptedCm = UntrackedLiveWristStepFromLastAcceptedCm;
	ApplyPolicyInput.LastAcceptedLiveWristAgeSeconds = LastAcceptedLiveWristAgeSeconds;
	ApplyPolicyInput.MaxUntrackedLiveWristStepCm = FMath::Max(0.0f, CVarQuestWristPositionFilterResetDistanceCm.GetValueOnAnyThread());
	ApplyPolicyInput.MaxUntrackedLiveWristAgeSeconds = FMath::Max(0.0f, CVarQuestWristLostTrackingGraceSeconds.GetValueOnAnyThread());
	if (!FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(ApplyPolicyInput))
	{
		if (bQuestSideUsable && !bQuestSideTracked)
		{
			Trace.bRawQuestRejected = 1;
		}
		return TryUseHeldQuestWristTarget();
	}

	if (!IsUsableQuestWristPosition(QuestWristWorld))
	{
		return TryUseHeldQuestWristTarget();
	}
	Trace.RawQuestWristWorld = QuestWristWorld;
	Trace.bUsedUntrackedJointData = !bQuestSideTracked ? 1 : 0;

	auto StoreAcceptedLiveWristPosition = [&]()
	{
		QuestWristSideState.bHasLastAcceptedLiveWristPosition = true;
		QuestWristSideState.LastAcceptedLiveWristWorld = QuestWristWorld;
		QuestWristSideState.LastAcceptedLiveWristTimeSeconds = FPlatformTime::Seconds();
	};
	auto StoreHeldQuestWristTarget = [&](const FVector& HeldTargetWorld, const FVector& HeldRawQuestWristWorld, const FVector& HeldMappedQuestWristWorld)
	{
		QuestWristSideState.bHasHeldTarget = true;
		QuestWristSideState.HeldTargetWorld = HeldTargetWorld;
		QuestWristSideState.HeldRawQuestWristWorld = HeldRawQuestWristWorld;
		QuestWristSideState.HeldMappedQuestWristWorld = HeldMappedQuestWristWorld;
		QuestWristSideState.LastTargetTimeSeconds = FPlatformTime::Seconds();
	};

	FVector QuestHmdWorld = FVector::ZeroVector;
	FQuat QuestHmdRotWorld = FQuat::Identity;
	const bool bHmdPoseValid = TryGetQuestHmdWorldPose(QuestHmdWorld, QuestHmdRotWorld);
	Trace.bHmdPoseValid = bHmdPoseValid ? 1 : 0;
	Trace.RawWristToHmdCm = bHmdPoseValid ? FVector::Dist(QuestWristWorld, QuestHmdWorld) : 0.0f;
	const float RawMaxDistanceCm = FMath::Max(0.0f, CVarQuestWristRawMaxDistanceCm.GetValueOnAnyThread());
	if (bHmdPoseValid &&
		RawMaxDistanceCm > KINDA_SMALL_NUMBER &&
		FVector::DistSquared(QuestWristWorld, QuestHmdWorld) > FMath::Square(RawMaxDistanceCm))
	{
		Trace.bRawQuestRejected = 1;
		return TryUseHeldQuestWristTarget();
	}

	if (bUseHmdRelativeAvatarEndpoint)
	{
		if (QuestWristRuntimeState.CalibrationMode != EQuestMediaSpaceCalibrationMode::None &&
			QuestWristRuntimeState.CalibrationMode != EQuestMediaSpaceCalibrationMode::HmdRelativeAvatar)
		{
			QuestWristRuntimeState.ResetCalibration();
		}
		if (!bHmdPoseValid)
		{
			return TryUseHeldQuestWristTarget();
		}

		QuestWristRuntimeState.CalibrationMode = EQuestMediaSpaceCalibrationMode::HmdRelativeAvatar;
		Trace.CalibrationMode = EQuestMediaSpaceCalibrationMode::HmdRelativeAvatar;
		if (!QuestWristRuntimeState.bHasHmdRelativeAvatarCalibration)
		{
			const FVector TrackingUpWorld = GetCachedQuestTrackingUpWorld().GetSafeNormal();
			FVector HmdForwardWorld = QuestHmdRotWorld.RotateVector(FVector::ForwardVector);
			HmdForwardWorld = TrackingUpWorld.IsNearlyZero()
				? HmdForwardWorld.GetSafeNormal()
				: (HmdForwardWorld - FVector::DotProduct(HmdForwardWorld, TrackingUpWorld) * TrackingUpWorld).GetSafeNormal();
			if (HmdForwardWorld.IsNearlyZero())
			{
				return TryUseHeldQuestWristTarget();
			}

			QuestWristRuntimeState.bHasHmdRelativeAvatarCalibration = true;
			QuestWristRuntimeState.HmdRelativeQuestAnchorWorld = QuestHmdWorld;
			QuestWristRuntimeState.HmdRelativeQuestAnchorYawWorld = MakeQuatFromForwardUp(
				HmdForwardWorld,
				TrackingUpWorld.IsNearlyZero() ? FVector::UpVector : TrackingUpWorld);
			QuestWristRuntimeState.bHasHmdRelativeQuestTranslationFilter = true;
			QuestWristRuntimeState.HmdRelativeQuestFilteredAnchorWorld = QuestHmdWorld;
			QuestWristRuntimeState.HmdRelativeQuestLastRawAnchorWorld = QuestHmdWorld;
			QuestWristRuntimeState.HmdRelativeQuestAnchorLastTimeSeconds = FPlatformTime::Seconds();
			QuestWristRuntimeState.Left.ResetPositionContinuity();
			QuestWristRuntimeState.Right.ResetPositionContinuity();
			QuestWristRuntimeState.ArmLengthCalibrationStage = QuestArmLengthCalibrationStage_WaitingForHands;
			QuestWristRuntimeState.ArmLengthCalibrationStableFrameCount = 0;
			QuestWristRuntimeState.ArmLengthCalibrationStableSeconds = 0.0f;
			QuestWristRuntimeState.ArmLengthCalibrationLastUpdateTimeSeconds = -1.0;
			QuestWristRuntimeState.ArmLengthCalibrationAcceptedTimeSeconds = -1.0;
		}

		FVector QuestTranslationAnchorWorld = QuestHmdWorld;
		{
			const double NowSeconds = FPlatformTime::Seconds();
			float AnchorDeltaSeconds = QuestWristRuntimeState.HmdRelativeQuestAnchorLastTimeSeconds >= 0.0
				? static_cast<float>(NowSeconds - QuestWristRuntimeState.HmdRelativeQuestAnchorLastTimeSeconds)
				: FMath::Max(CachedDeltaTimeSeconds, 1.0f / 90.0f);
			if (!FMath::IsFinite(AnchorDeltaSeconds) || AnchorDeltaSeconds <= 0.0f)
			{
				AnchorDeltaSeconds = FMath::Max(CachedDeltaTimeSeconds, 1.0f / 90.0f);
			}
			AnchorDeltaSeconds = FMath::Clamp(AnchorDeltaSeconds, 1.0f / 240.0f, 0.10f);

			const float RawAnchorStepCm = QuestWristRuntimeState.bHasHmdRelativeQuestTranslationFilter
				? FVector::Dist(QuestHmdWorld, QuestWristRuntimeState.HmdRelativeQuestLastRawAnchorWorld)
				: 0.0f;
			const float AnchorSpeedCmSec = AnchorDeltaSeconds > KINDA_SMALL_NUMBER
				? RawAnchorStepCm / AnchorDeltaSeconds
				: 0.0f;
			const float TranslationHalfLife = FMath::Max(0.0f, CVarQuestHmdAvatarTranslationHalfLife.GetValueOnAnyThread());
			const float ResetDistanceCm = FMath::Max(0.0f, CVarQuestHmdAvatarTranslationResetDistanceCm.GetValueOnAnyThread());
			const bool bResetTranslationFilter =
				!QuestWristRuntimeState.bHasHmdRelativeQuestTranslationFilter ||
				QuestWristRuntimeState.HmdRelativeQuestAnchorLastTimeSeconds < 0.0 ||
				NowSeconds - QuestWristRuntimeState.HmdRelativeQuestAnchorLastTimeSeconds > 0.50 ||
				(ResetDistanceCm > KINDA_SMALL_NUMBER &&
				 FVector::Dist(QuestHmdWorld, QuestWristRuntimeState.HmdRelativeQuestFilteredAnchorWorld) > ResetDistanceCm);

			float TranslationFilterAlpha = 1.0f;
			if (bResetTranslationFilter || TranslationHalfLife <= KINDA_SMALL_NUMBER)
			{
				QuestWristRuntimeState.HmdRelativeQuestFilteredAnchorWorld = QuestHmdWorld;
				QuestWristRuntimeState.bHasHmdRelativeQuestTranslationFilter = true;
				if (bResetTranslationFilter)
				{
					const bool bKeepAcceptedArmLengthCalibration =
						QuestWristRuntimeState.ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_Accepted;
					QuestWristRuntimeState.Left.ResetPositionContinuity(!bKeepAcceptedArmLengthCalibration);
					QuestWristRuntimeState.Right.ResetPositionContinuity(!bKeepAcceptedArmLengthCalibration);
					if (!bKeepAcceptedArmLengthCalibration)
					{
						QuestWristRuntimeState.ArmLengthCalibrationStage = QuestArmLengthCalibrationStage_WaitingForHands;
						QuestWristRuntimeState.ArmLengthCalibrationStableFrameCount = 0;
						QuestWristRuntimeState.ArmLengthCalibrationStableSeconds = 0.0f;
						QuestWristRuntimeState.ArmLengthCalibrationLastUpdateTimeSeconds = -1.0;
						QuestWristRuntimeState.ArmLengthCalibrationAcceptedTimeSeconds = -1.0;
					}
				}
			}
			else
			{
				TranslationFilterAlpha = HalfLifeToAlpha(TranslationHalfLife, AnchorDeltaSeconds);
				QuestWristRuntimeState.HmdRelativeQuestFilteredAnchorWorld = FMath::Lerp(
					QuestWristRuntimeState.HmdRelativeQuestFilteredAnchorWorld,
					QuestHmdWorld,
					TranslationFilterAlpha);
			}

			QuestWristRuntimeState.HmdRelativeQuestLastRawAnchorWorld = QuestHmdWorld;
			QuestWristRuntimeState.HmdRelativeQuestAnchorLastTimeSeconds = NowSeconds;
			QuestTranslationAnchorWorld = QuestWristRuntimeState.HmdRelativeQuestFilteredAnchorWorld;
			Trace.bUsedLiveHmdTranslationAnchor = 1;
			Trace.HmdAvatarTranslationFilterAlpha = TranslationFilterAlpha;
			Trace.HmdAvatarTranslationSpeedCmSec = AnchorSpeedCmSec;
			Trace.HmdAvatarTranslationLagCm = FVector::Dist(QuestHmdWorld, QuestTranslationAnchorWorld);
		}
		Trace.QuestAnchorWorld = QuestTranslationAnchorWorld;

		if (bConstrainedArmMode && !bTraceOnly)
		{
			const double NowSeconds = FPlatformTime::Seconds();
			float AuthorityDeltaSeconds = QuestWristSideState.PositionAuthorityLastTimeSeconds >= 0.0
				? static_cast<float>(NowSeconds - QuestWristSideState.PositionAuthorityLastTimeSeconds)
				: FMath::Max(CachedDeltaTimeSeconds, 1.0f / 90.0f);
			if (!FMath::IsFinite(AuthorityDeltaSeconds) || AuthorityDeltaSeconds <= 0.0f)
			{
				AuthorityDeltaSeconds = FMath::Max(CachedDeltaTimeSeconds, 1.0f / 90.0f);
			}
			AuthorityDeltaSeconds = FMath::Clamp(AuthorityDeltaSeconds, 1.0f / 240.0f, 0.10f);
			QuestWristSideState.PositionAuthorityLastTimeSeconds = NowSeconds;
			QuestWristSideState.PositionAuthorityAlpha = FMath::Min(
				1.0f,
				QuestWristSideState.PositionAuthorityAlpha + AuthorityDeltaSeconds / 0.20f);
			Blend *= QuestWristSideState.PositionAuthorityAlpha;
			Trace.EffectiveBlend = Blend;
		}

		FVector AvatarEyeWorld = FVector::ZeroVector;
		FVector QuestMappedWristWorld = FVector::ZeroVector;
		if (!TryMapQuestHmdRelativeWristToAvatarWorld(
			CSPose,
			bIsLeft,
			QuestTranslationAnchorWorld,
			QuestWristRuntimeState.HmdRelativeQuestAnchorYawWorld,
			QuestWristWorld,
			AvatarEyeWorld,
			QuestMappedWristWorld))
		{
			return TryUseHeldQuestWristTarget();
		}

		const FVector CandidateWristWorld = BuildWristCandidateWorld(QuestMappedWristWorld);
		StoreAcceptedLiveWristPosition();
		StoreHeldQuestWristTarget(QuestMappedWristWorld, QuestWristWorld, QuestMappedWristWorld);
		if (!bTraceOnly)
		{
			InOutWristWorld = CandidateWristWorld;
			Trace.bPositionApplied = 1;
		}
		Trace.MediaAnchorWorld = AvatarEyeWorld;
		Trace.MappedQuestWristWorld = QuestMappedWristWorld;
		Trace.FinalWristWorld = bTraceOnly ? MediaPipeWristWorld : InOutWristWorld;
		Trace.MappedOffsetFromMediaPipeCm = FVector::Dist(QuestMappedWristWorld, MediaPipeWristWorld);
		Trace.bMapped = 1;
		return !bTraceOnly;
	}

	FVector MediaHeadWorld = FVector::ZeroVector;
	FQuat MediaBodyBasisWorld = FQuat::Identity;
	const bool bMediaHeadValid = TryGetMediaPipeHeadFrameWorld(MediaHeadWorld, MediaBodyBasisWorld);
	Trace.bMediaHeadValid = bMediaHeadValid ? 1 : 0;
	Trace.MediaPipeHeadWorld = MediaHeadWorld;
	if (!bMediaHeadValid)
	{
		return TryUseHeldQuestWristTarget();
	}

	if ((bHmdPoseValid && QuestWristState.QuestMediaSpaceCalibrationMode != EQuestMediaSpaceCalibrationMode::HmdHead) ||
		(!bHmdPoseValid && QuestWristState.QuestMediaSpaceCalibrationMode == EQuestMediaSpaceCalibrationMode::HmdHead))
	{
		QuestWristState.bHasQuestMediaSpaceCalibration = false;
		QuestWristState.QuestMediaSpaceCalibrationMode = EQuestMediaSpaceCalibrationMode::None;
		QuestWristState.Left.bHasQuestWristPositionCalibration = false;
		QuestWristState.Right.bHasQuestWristPositionCalibration = false;
		QuestWristState.Left.bHasQuestWristTraceCalibration = false;
		QuestWristState.Right.bHasQuestWristTraceCalibration = false;
		QuestWristState.Left.QuestWristTraceCalibrationWorld = FVector::ZeroVector;
		QuestWristState.Right.QuestWristTraceCalibrationWorld = FVector::ZeroVector;
		QuestWristState.Left.QuestWristTraceCalibrationTimeSeconds = -1.0;
		QuestWristState.Right.QuestWristTraceCalibrationTimeSeconds = -1.0;
	}

	if (!QuestWristState.bHasQuestMediaSpaceCalibration && !TryBuildQuestMediaSpaceCalibration())
	{
		return TryUseHeldQuestWristTarget();
	}
	Trace.CalibrationMode = QuestWristState.QuestMediaSpaceCalibrationMode;
	if (QuestWristRuntimeState.CalibrationMode != EQuestMediaSpaceCalibrationMode::None &&
		QuestWristRuntimeState.CalibrationMode != QuestWristState.QuestMediaSpaceCalibrationMode)
	{
		QuestWristRuntimeState.ResetCalibration();
	}
	QuestWristRuntimeState.CalibrationMode = QuestWristState.QuestMediaSpaceCalibrationMode;

	FQuat CurrentQuestToMediaSpaceWorld = QuestWristState.QuestToMediaSpaceWorld;
	FVector QuestAnchorWorld = QuestWristState.QuestCalibrationHmdWorld;
	FVector MediaAnchorWorld = QuestWristState.MediaPipeCalibrationHeadWorld;
	float SpaceScale = QuestWristState.QuestToMediaSpaceScale;
	if (QuestWristState.QuestMediaSpaceCalibrationMode == EQuestMediaSpaceCalibrationMode::HmdHead)
	{
		if (!bHmdPoseValid)
		{
			return TryUseHeldQuestWristTarget();
		}

		QuestAnchorWorld = QuestHmdWorld;
		MediaAnchorWorld = MediaHeadWorld;
		SpaceScale = 1.0f;
		Trace.bUsedLiveHmdAnchor = 1;
	}
	else if (QuestWristState.QuestMediaSpaceCalibrationMode == EQuestMediaSpaceCalibrationMode::HandPairBody)
	{
		FVector MediaLeftShoulderWorld = FVector::ZeroVector;
		FVector MediaRightShoulderWorld = FVector::ZeroVector;
		if (!TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftShoulder, MediaLeftShoulderWorld) ||
			!TryGetLmWorld((int32)EMediaPipePoseLandmark::RightShoulder, MediaRightShoulderWorld))
		{
			return TryUseHeldQuestWristTarget();
		}

		const FVector CurrentShoulderMidWorld = 0.5f * (MediaLeftShoulderWorld + MediaRightShoulderWorld);
		CurrentQuestToMediaSpaceWorld = (MediaBodyBasisWorld * QuestWristState.QuestCalibrationBasisWorld.Inverse()).GetNormalized();
		MediaAnchorWorld = CurrentShoulderMidWorld + MediaBodyBasisWorld.RotateVector(QuestWristState.MediaPipeCalibrationAnchorBodyLocal);
	}
	else
	{
		return TryUseHeldQuestWristTarget();
	}

	const float PositionScale = FMath::Max(0.0f, CVarQuestWristPositionScale.GetValueOnAnyThread()) * FMath::Max(0.01f, SpaceScale);
	const FVector CurrentQuestHmdToWristWorld = QuestWristWorld - QuestAnchorWorld;
	const bool bUseRelativeWristPositionCalibration =
		QuestWristState.QuestMediaSpaceCalibrationMode == EQuestMediaSpaceCalibrationMode::HmdHead &&
		CVarQuestWristRelativeCalibration.GetValueOnAnyThread() != 0 &&
		!bConstrainedArmMode;
	auto TryAcceptConstrainedArmStartup = [&]() -> bool
	{
		if (!bConstrainedArmMode || bTraceOnly)
		{
			return true;
		}

		if (QuestWristSideState.bHasApplyCalibration)
		{
			Trace.bHadApplyCalibration = 1;
			if (QuestWristSideState.ApplyCalibrationTimeSeconds >= 0.0)
			{
				Trace.ApplyCalibrationAgeSeconds = static_cast<float>(FPlatformTime::Seconds() - QuestWristSideState.ApplyCalibrationTimeSeconds);
			}
			return true;
		}

		const double NowSeconds = FPlatformTime::Seconds();
		float StableDeltaSeconds = 0.0f;
		bool bStableStartupSample = true;
		if (QuestWristSideState.bHasPositionStartupLastSample)
		{
			StableDeltaSeconds = static_cast<float>(FMath::Clamp(
				NowSeconds - QuestWristSideState.PositionStartupLastSampleTimeSeconds,
				0.0,
				0.10));
			const float QuestStepCm = FVector::Dist(QuestWristWorld, QuestWristSideState.PositionStartupLastRawQuestWristWorld);
			const float MediaPipeStepCm = FVector::Dist(MediaPipeWristWorld, QuestWristSideState.PositionStartupLastMediaPipeWristWorld);
			bStableStartupSample =
				QuestStepCm <= 18.0f &&
				MediaPipeStepCm <= 22.0f &&
				NowSeconds - QuestWristSideState.PositionStartupLastSampleTimeSeconds <= 0.25;
		}

		if (bStableStartupSample)
		{
			QuestWristSideState.PositionStartupStableFrameCount++;
			QuestWristSideState.PositionStartupStableSeconds += StableDeltaSeconds;
		}
		else
		{
			QuestWristSideState.PositionStartupStableFrameCount = 1;
			QuestWristSideState.PositionStartupStableSeconds = 0.0f;
		}

		QuestWristSideState.PositionStartupLastRawQuestWristWorld = QuestWristWorld;
		QuestWristSideState.PositionStartupLastMediaPipeWristWorld = MediaPipeWristWorld;
		QuestWristSideState.PositionStartupLastSampleTimeSeconds = NowSeconds;
		QuestWristSideState.bHasPositionStartupLastSample = true;

		if (QuestWristSideState.PositionStartupStableFrameCount < 6 ||
			QuestWristSideState.PositionStartupStableSeconds < 0.16f)
		{
			FadePositionAuthorityToZero();
			return false;
		}

		Trace.bHadApplyCalibration = 0;
		QuestWristSideState.ApplyQuestHmdToWristWorld = CurrentQuestHmdToWristWorld;
		QuestWristSideState.ApplyMediaPipeWristWorld = MediaPipeWristWorld;
		QuestWristSideState.ApplyCalibrationTimeSeconds = FPlatformTime::Seconds();
		QuestWristSideState.bHasApplyCalibration = true;
		Trace.bSetApplyCalibration = 1;
		QuestWristSideState.PositionAuthorityAlpha = 0.0f;
		QuestWristSideState.PositionAuthorityLastTimeSeconds = FPlatformTime::Seconds();
		return true;
	};

	if (!TryAcceptConstrainedArmStartup())
	{
		return false;
	}

	FVector QuestMappedWristWorld = MediaPipeWristWorld;
	if (bUseRelativeWristPositionCalibration)
	{
		const bool bUseTraceCalibration = bTraceOnly && CVarQuestWristTraceStableBaseline.GetValueOnAnyThread() != 0;
		FVector CalibrationQuestHmdToWristWorld = QuestWristSideState.ApplyQuestHmdToWristWorld;
		if (bUseTraceCalibration)
		{
			Trace.bHadTraceCalibration = QuestWristSideState.bHasTraceCalibration ? 1 : 0;
			if (!QuestWristSideState.bHasTraceCalibration && bQuestSideTracked)
			{
				QuestWristSideState.TraceQuestHmdToWristWorld = CurrentQuestHmdToWristWorld;
				QuestWristSideState.TraceCalibrationTimeSeconds = FPlatformTime::Seconds();
				QuestWristSideState.bHasTraceCalibration = true;
				Trace.bSetTraceCalibration = 1;
			}
			if (QuestWristSideState.bHasTraceCalibration)
			{
				CalibrationQuestHmdToWristWorld = QuestWristSideState.TraceQuestHmdToWristWorld;
				if (QuestWristSideState.TraceCalibrationTimeSeconds >= 0.0)
				{
					Trace.TraceCalibrationAgeSeconds = static_cast<float>(FPlatformTime::Seconds() - QuestWristSideState.TraceCalibrationTimeSeconds);
				}
			}
			else
			{
				CalibrationQuestHmdToWristWorld = CurrentQuestHmdToWristWorld;
			}
		}
		else if (!QuestWristSideState.bHasApplyCalibration)
		{
			Trace.bHadApplyCalibration = 0;
			QuestWristSideState.ApplyQuestHmdToWristWorld = CurrentQuestHmdToWristWorld;
			QuestWristSideState.ApplyMediaPipeWristWorld = MediaPipeWristWorld;
			QuestWristSideState.ApplyCalibrationTimeSeconds = FPlatformTime::Seconds();
			QuestWristSideState.bHasApplyCalibration = true;
			Trace.bSetApplyCalibration = 1;
			CalibrationQuestHmdToWristWorld = QuestWristSideState.ApplyQuestHmdToWristWorld;
			QuestWristSideState.PositionAuthorityAlpha = bConstrainedArmMode ? 0.0f : 1.0f;
			QuestWristSideState.PositionAuthorityLastTimeSeconds = FPlatformTime::Seconds();
		}
		else
		{
			Trace.bHadApplyCalibration = 1;
			if (QuestWristSideState.ApplyCalibrationTimeSeconds >= 0.0)
			{
				Trace.ApplyCalibrationAgeSeconds = static_cast<float>(FPlatformTime::Seconds() - QuestWristSideState.ApplyCalibrationTimeSeconds);
			}
		}

		FVector QuestRelativeDeltaWorld = (CurrentQuestHmdToWristWorld - CalibrationQuestHmdToWristWorld) * PositionScale;
		Trace.RawCalibrationDeltaCm = QuestRelativeDeltaWorld.Size();
		const float MaxRelativeDeltaCm = FMath::Max(0.0f, CVarQuestWristMaxRelativeDeltaCm.GetValueOnAnyThread());
		if (MaxRelativeDeltaCm > KINDA_SMALL_NUMBER && QuestRelativeDeltaWorld.SizeSquared() > FMath::Square(MaxRelativeDeltaCm))
		{
			QuestRelativeDeltaWorld = QuestRelativeDeltaWorld.GetSafeNormal() * MaxRelativeDeltaCm;
			Trace.RawCalibrationDeltaCm = MaxRelativeDeltaCm;
		}

		MediaAnchorWorld = MediaPipeWristWorld;
		QuestMappedWristWorld = MediaPipeWristWorld + CurrentQuestToMediaSpaceWorld.RotateVector(QuestRelativeDeltaWorld);
		Trace.bUsedRelativeWristCalibration = 1;
	}
	else
	{
		if (QuestWristState.QuestMediaSpaceCalibrationMode != EQuestMediaSpaceCalibrationMode::HmdHead)
		{
			QuestWristSideState.ResetCalibration();
		}

		FVector QuestAnchorToWristWorld = (QuestWristWorld - QuestAnchorWorld) * PositionScale;
		const float MaxOffsetCm = FMath::Max(0.0f, CVarQuestWristMaxOffsetCm.GetValueOnAnyThread());
		if (MaxOffsetCm > KINDA_SMALL_NUMBER && QuestAnchorToWristWorld.SizeSquared() > FMath::Square(MaxOffsetCm))
		{
			QuestAnchorToWristWorld = QuestAnchorToWristWorld.GetSafeNormal() * MaxOffsetCm;
		}

		QuestMappedWristWorld = MediaAnchorWorld + CurrentQuestToMediaSpaceWorld.RotateVector(QuestAnchorToWristWorld);
	}

	if (bConstrainedArmMode && !bTraceOnly)
	{
		const double NowSeconds = FPlatformTime::Seconds();
		float AuthorityDeltaSeconds = QuestWristSideState.PositionAuthorityLastTimeSeconds >= 0.0
			? static_cast<float>(NowSeconds - QuestWristSideState.PositionAuthorityLastTimeSeconds)
			: FMath::Max(CachedDeltaTimeSeconds, 1.0f / 90.0f);
		if (!FMath::IsFinite(AuthorityDeltaSeconds) || AuthorityDeltaSeconds <= 0.0f)
		{
			AuthorityDeltaSeconds = FMath::Max(CachedDeltaTimeSeconds, 1.0f / 90.0f);
		}
		AuthorityDeltaSeconds = FMath::Clamp(AuthorityDeltaSeconds, 1.0f / 240.0f, 0.10f);
		QuestWristSideState.PositionAuthorityLastTimeSeconds = NowSeconds;
		QuestWristSideState.PositionAuthorityAlpha = FMath::Min(
			1.0f,
			QuestWristSideState.PositionAuthorityAlpha + AuthorityDeltaSeconds / 0.45f);
		Blend *= QuestWristSideState.PositionAuthorityAlpha;
		Trace.EffectiveBlend = Blend;
	}

	const FVector CandidateWristWorld = BuildWristCandidateWorld(QuestMappedWristWorld);
	StoreAcceptedLiveWristPosition();
	StoreHeldQuestWristTarget(QuestMappedWristWorld, QuestWristWorld, QuestMappedWristWorld);
	if (!bTraceOnly)
	{
		InOutWristWorld = CandidateWristWorld;
		Trace.bPositionApplied = 1;
	}
	Trace.QuestAnchorWorld = QuestAnchorWorld;
	Trace.MediaAnchorWorld = MediaAnchorWorld;
	Trace.MappedQuestWristWorld = QuestMappedWristWorld;
	Trace.FinalWristWorld = bTraceOnly ? MediaPipeWristWorld : InOutWristWorld;
	Trace.MappedOffsetFromMediaPipeCm = FVector::Dist(QuestMappedWristWorld, MediaPipeWristWorld);
	Trace.bMapped = 1;
	return !bTraceOnly;
}
