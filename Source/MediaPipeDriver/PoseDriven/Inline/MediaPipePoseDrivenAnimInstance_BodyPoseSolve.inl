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
		if (CVarMediaPipeTorsoDebug.GetValueOnAnyThread() != 0)
		{
			const double NowSeconds = FPlatformTime::Seconds();
			if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, DiagnosticsState.LastHeadDiagnosticLogTimeSeconds))
			{
				UE_LOG(LogMediaPipePose, Log,
					TEXT("mp.HeadDebug: actor=%s enabled=0 faceBlend=%.2f twistWeight=%.2f reason=HeadFaceBlendAndHeadTwistWeightAreZero"),
					*TargetActorName.ToString(),
					HeadFaceBlend,
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
	FQuat HeadTargetBasis = FQuat::Slerp(ChestTargetBasis, FaceHeadTargetBasis, FMath::Clamp(HeadFaceBlend, 0.0f, 1.0f)).GetNormalized();
	float ScreenHeadYawDeg = 0.0f;
	float ScreenHeadPitchDeg = 0.0f;
	float ScreenHeadRollDeg = 0.0f;
	FVector2D ScreenHeadCenterDelta2D = FVector2D::ZeroVector;
	FVector2D ScreenHeadNoseDelta2D = FVector2D::ZeroVector;
	if (HeadFaceBlend > KINDA_SMALL_NUMBER)
	{
		auto TryGetNormalizedXY = [&](const int32 LmIdx, FVector2D& Out) -> bool
		{
			if (LmIdx < 0 || !PoseFrame.Normalized.IsValidIndex(LmIdx))
			{
				return false;
			}
			const FMediaPipePoseLandmark& Lm = PoseFrame.Normalized.Points[LmIdx];
			if (Lm.Presence <= KINDA_SMALL_NUMBER && Lm.Visibility <= KINDA_SMALL_NUMBER && Lm.Reliability <= KINDA_SMALL_NUMBER)
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
			float EyeRollDeg = 0.0f;
			if (bHasEyes2D)
			{
				const FVector2D EyeRight2D = REye2D - LEye2D;
				if (!EyeRight2D.IsNearlyZero())
				{
					EyeRollDeg = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(EyeRight2D.Y, EyeRight2D.X)));
				}
			}

			if (!BodyState.bHasHeadScreenReference)
			{
				BodyState.bHasHeadScreenReference = true;
				BodyState.HeadScreenCenterReference = HeadCenterOffset2D;
				BodyState.HeadScreenNoseReference = NoseOffset2D;
				BodyState.HeadScreenRollReferenceDeg = EyeRollDeg;
			}

			const FVector2D CenterDelta2D = HeadCenterOffset2D - BodyState.HeadScreenCenterReference;
			const FVector2D NoseDelta2D = NoseOffset2D - BodyState.HeadScreenNoseReference;
			ScreenHeadCenterDelta2D = CenterDelta2D;
			ScreenHeadNoseDelta2D = NoseDelta2D;
			const float RollDeltaDeg = FRotator::NormalizeAxis(EyeRollDeg - BodyState.HeadScreenRollReferenceDeg);
			const float ScreenWeight = FMath::Clamp(HeadFaceBlend, 0.0f, 1.0f);
			ScreenHeadYawDeg = FMath::Clamp((-NoseDelta2D.X * 85.0f - CenterDelta2D.X * 60.0f) * ScreenWeight, -60.0f, 60.0f);
			ScreenHeadPitchDeg = FMath::Clamp((NoseDelta2D.Y * 70.0f + CenterDelta2D.Y * 45.0f) * ScreenWeight, -45.0f, 45.0f);
			ScreenHeadRollDeg = FMath::Clamp(RollDeltaDeg * ScreenWeight, -45.0f, 45.0f);

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

			const FQuat ScreenHeadDelta =
				FQuat(ScreenUpComp, FMath::DegreesToRadians(ScreenHeadYawDeg)) *
				FQuat(ScreenRightComp, FMath::DegreesToRadians(ScreenHeadPitchDeg)) *
				FQuat(ScreenForwardComp, FMath::DegreesToRadians(ScreenHeadRollDeg));
			HeadTargetBasis = (ScreenHeadDelta * HeadTargetBasis).GetNormalized();

			const float ReferenceAlpha = HalfLifeToAlpha(12.0f, DeltaSeconds);
			BodyState.HeadScreenCenterReference = FMath::Lerp(BodyState.HeadScreenCenterReference, HeadCenterOffset2D, ReferenceAlpha);
			BodyState.HeadScreenNoseReference = FMath::Lerp(BodyState.HeadScreenNoseReference, NoseOffset2D, ReferenceAlpha);
			BodyState.HeadScreenRollReferenceDeg = FMath::Lerp(BodyState.HeadScreenRollReferenceDeg, EyeRollDeg, ReferenceAlpha);
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

		const FQuat SwingScaled = FQuat::Slerp(FQuat::Identity, Swing, FMath::Clamp(SwingWeight, 0.0f, 1.0f)).GetNormalized();
		const FQuat TwistScaled = FQuat::Slerp(FQuat::Identity, Twist, FMath::Clamp(TwistWeight, 0.0f, 1.0f)).GetNormalized();
		const FQuat TargetRotCS = (SwingScaled * TwistScaled * BaseTargetRotCS).GetNormalized();

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

	ApplyFaceDeltaFromChest(Neck, RefNeckComp, RefNeckBasisComp, NeckTargetBasis, 0.45f, 0.25f * FaceTwistWeight, BodyState.bHasSmoothedNeckRotCS, BodyState.SmoothedNeckRotCS);
	ApplyFaceDeltaFromChest(Neck02, RefNeck02Comp, RefNeck02BasisComp, Neck02TargetBasis, 0.75f, 0.50f * FaceTwistWeight, BodyState.bHasSmoothedNeck02RotCS, BodyState.SmoothedNeck02RotCS);
	ApplyFaceDeltaFromChest(Head, RefHeadComp, RefHeadBasisComp, HeadTargetBasis, 1.0f, FaceTwistWeight, BodyState.bHasSmoothedHeadRotCS, BodyState.SmoothedHeadRotCS);

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
				TEXT("mp.HeadDebug: actor=%s enabled=1 nose=%d ears=%d eyes=%d faceBlend=%.2f twistWeight=%.2f faceFromChestDeg=%.1f screenCenterDelta=(%.3f,%.3f) screenNoseDelta=(%.3f,%.3f) screenYaw=%.1f screenPitch=%.1f screenRoll=%.1f neckAppliedDeg=%.1f neck02AppliedDeg=%.1f headAppliedDeg=%.1f maxStepDeg=%.1f maxSpeedDegPerSec=%.1f"),
				*TargetActorName.ToString(),
				bHasNose ? 1 : 0,
				(bHasLeftEar && bHasRightEar) ? 1 : 0,
				(bHasLeftEye && bHasRightEye) ? 1 : 0,
				HeadFaceBlend,
				FaceTwistWeight,
				QuatAngularDistanceDegrees(FaceHeadTargetBasis, ChestTargetBasis),
				ScreenHeadCenterDelta2D.X,
				ScreenHeadCenterDelta2D.Y,
				ScreenHeadNoseDelta2D.X,
				ScreenHeadNoseDelta2D.Y,
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
