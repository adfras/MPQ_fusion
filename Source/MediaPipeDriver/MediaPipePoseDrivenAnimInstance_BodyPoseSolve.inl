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

			ConsiderFootFloor(bGotLeft, bGotLeftToe, LAnkleWorld, LToeWorld, LeftLegState.bHasObservedSourceFloor, LeftLegState.ObservedSourceFloorZ);
			ConsiderFootFloor(bGotRight, bGotRightToe, RAnkleWorld, RToeWorld, RightLegState.bHasObservedSourceFloor, RightLegState.ObservedSourceFloorZ);
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
					const float TargetRigHipHeightCm = BodyState.ReferenceRigHipHeightCm * CompressionAlpha;
					DeltaHeightCm = TargetRigHipHeightCm - BodyState.ReferenceRigHipHeightCm;
				}

				FVector CompUp = TargetCompTransform.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
				if (CompUp.IsNearlyZero())
				{
					CompUp = FVector::UpVector;
				}
				FVector TargetPelvisOffset = CompUp * DeltaHeightCm;

				const float PlanarWeight = FMath::Clamp(PelvisPlanarTranslationWeight, 0.0f, 1.0f);
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
					const bool bGotLeftSupport = TryGetSupportPointWorld(bGotLeft, bGotLeftHeel, bGotLeftToe, LAnkleWorld, LHeelWorld, LToeWorld, LeftLegState.bHasObservedSourceFloor, LeftLegState.ObservedSourceFloorZ, LSupportWorld);
					const bool bGotRightSupport = TryGetSupportPointWorld(bGotRight, bGotRightHeel, bGotRightToe, RAnkleWorld, RHeelWorld, RToeWorld, RightLegState.bHasObservedSourceFloor, RightLegState.ObservedSourceFloorZ, RSupportWorld);

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

	auto ConsiderFoot = [&](bool bSourceGrounded, const FBoneReference& BallBone)
	{
		if (!bSourceGrounded || !BallBone.IsValidToEvaluate())
		{
			return;
		}

		const float CurrentBallZ = CSPose.GetComponentSpaceTransform(BallBone.CachedCompactPoseIndex).GetTranslation().Z;
		const float HoverCm = CurrentBallZ - RefFootFloorZComp;
		if (HoverCm <= KINDA_SMALL_NUMBER || HoverCm > FkRootGroundingMaxCorrectionCm)
		{
			return;
		}

		const float CandidateOffsetZ = -HoverCm;
		if (!bHasGroundedFoot || CandidateOffsetZ < TargetOffsetZ)
		{
			TargetOffsetZ = CandidateOffsetZ;
			bHasGroundedFoot = true;
		}
	};

	ConsiderFoot(LeftLegState.bCurrentSourceFootGrounded, BallL);
	ConsiderFoot(RightLegState.bCurrentSourceFootGrounded, BallR);

	const FVector TargetOffsetComp(0.0f, 0.0f, bHasGroundedFoot ? TargetOffsetZ : 0.0f);
	const float Alpha = HalfLifeToAlpha(FkRootGroundingHalfLifeSeconds, DeltaSeconds);
	if (!BodyState.bHasSmoothedFkRootGroundOffset)
	{
		BodyState.SmoothedFkRootGroundOffsetComp = TargetOffsetComp;
		BodyState.bHasSmoothedFkRootGroundOffset = true;
	}
	else
	{
		BodyState.SmoothedFkRootGroundOffsetComp = FMath::Lerp(BodyState.SmoothedFkRootGroundOffsetComp, TargetOffsetComp, Alpha);
	}
}

void FAnimNode_MediaPipePoseDriven::DriveSpineCS(FCSPose<FCompactPose>& CSPose, float DeltaSeconds)
{
	if (!bDriveSpine || NumSpineBones <= 0)
	{
		return;
	}

	const float SpineRotAlpha = HalfLifeToAlpha(SpineRotationHalfLifeSeconds, FMath::Max(DeltaSeconds, 0.0f));
	const float HeadRotAlpha = HalfLifeToAlpha(HeadRotationHalfLifeSeconds, FMath::Max(DeltaSeconds, 0.0f));

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

	const float FaceTwistWeight = FMath::Clamp(HeadTwistWeight, 0.0f, 1.0f);

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
		float MaxHeadStepDegrees = HeadRotationMaxStepDegrees;
		if (HeadRotationMaxSpeedDegreesPerSecond > 0.0f && DeltaSeconds > 0.0f)
		{
			const float MaxSpeedStepDegrees = HeadRotationMaxSpeedDegreesPerSecond * DeltaSeconds;
			MaxHeadStepDegrees = MaxHeadStepDegrees > 0.0f
				? FMath::Min(MaxHeadStepDegrees, MaxSpeedStepDegrees)
				: MaxSpeedStepDegrees;
		}
		UpdateSmoothedRotation(bHasSmoothedRot, InOutSmoothedRotCS, TargetRotCS, HeadRotAlpha, MaxHeadStepDegrees);
		ApplyRotationCS(CSPose, Bone, InOutSmoothedRotCS);
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

	if (HeadFaceBlend <= KINDA_SMALL_NUMBER && FaceTwistWeight <= KINDA_SMALL_NUMBER)
	{
		BodyState.bHasSmoothedNeckRotCS = false;
		BodyState.bHasSmoothedNeck02RotCS = false;
		BodyState.bHasSmoothedHeadRotCS = false;
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
	const FQuat HeadTargetBasis = FQuat::Slerp(ChestTargetBasis, FaceHeadTargetBasis, FMath::Clamp(HeadFaceBlend, 0.0f, 1.0f)).GetNormalized();
	const FQuat NeckTargetBasis = FQuat::Slerp(ChestTargetBasis, HeadTargetBasis, 0.5f).GetNormalized();
	const FQuat Neck02TargetBasis = FQuat::Slerp(NeckTargetBasis, HeadTargetBasis, 0.5f).GetNormalized();

	ApplySemanticBasisSwingTwist(Neck, RefNeckComp, RefNeckBasisComp, NeckTargetBasis, CompUp, 0.65f, 0.35f * FaceTwistWeight, BodyState.bHasSmoothedNeckRotCS, BodyState.SmoothedNeckRotCS);
	ApplySemanticBasisSwingTwist(Neck02, RefNeck02Comp, RefNeck02BasisComp, Neck02TargetBasis, CompUp, 0.75f, 0.35f * FaceTwistWeight, BodyState.bHasSmoothedNeck02RotCS, BodyState.SmoothedNeck02RotCS);
	ApplySemanticBasisSwingTwist(Head, RefHeadComp, RefHeadBasisComp, HeadTargetBasis, CompUp, 0.85f, 0.35f * FaceTwistWeight, BodyState.bHasSmoothedHeadRotCS, BodyState.SmoothedHeadRotCS);
}
