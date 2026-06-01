#include "MediaPipeTrackedSkeletonActor.h"

#include "EmbodiedFusionComponent.h"
#include "MediaPipePoseDrivenAnimInstance.h"
#include "MediaPipePoseTrackerComponent.h"
#include "MediaPipePoseTypes.h"
#include "MediaPipeSolvedPose.h"

#include "Animation/AnimationAsset.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "ReferenceSkeleton.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	struct FTrackedRigBoneDef
	{
		const TCHAR* BoneName;
		const TCHAR* ParentName;
		const TCHAR* PrimaryChildName;
		const TCHAR* SecondaryChildName;
	};

	static const FTrackedRigBoneDef GSimplifiedTrackedRigBones[] = {
		{TEXT("pelvis"), nullptr, TEXT("spine"), TEXT("left_hip")},
		{TEXT("left_hip"), TEXT("pelvis"), TEXT("left_knee"), nullptr},
		{TEXT("left_knee"), TEXT("left_hip"), TEXT("left_ankle"), nullptr},
		{TEXT("left_ankle"), TEXT("left_knee"), TEXT("left_big_toe"), TEXT("left_heel")},
		{TEXT("left_big_toe"), TEXT("left_ankle"), nullptr, nullptr},
		{TEXT("left_heel"), TEXT("left_ankle"), nullptr, nullptr},
		{TEXT("right_hip"), TEXT("pelvis"), TEXT("right_knee"), nullptr},
		{TEXT("right_knee"), TEXT("right_hip"), TEXT("right_ankle"), nullptr},
		{TEXT("right_ankle"), TEXT("right_knee"), TEXT("right_big_toe"), TEXT("right_heel")},
		{TEXT("right_big_toe"), TEXT("right_ankle"), nullptr, nullptr},
		{TEXT("right_heel"), TEXT("right_ankle"), nullptr, nullptr},
		{TEXT("spine"), TEXT("pelvis"), TEXT("chest"), nullptr},
		{TEXT("chest"), TEXT("spine"), TEXT("neck"), TEXT("left_collar")},
		{TEXT("neck"), TEXT("chest"), TEXT("head"), nullptr},
		{TEXT("head"), TEXT("neck"), nullptr, nullptr},
		{TEXT("left_collar"), TEXT("chest"), TEXT("left_shoulder"), nullptr},
		{TEXT("left_shoulder"), TEXT("left_collar"), TEXT("left_elbow"), nullptr},
		{TEXT("left_elbow"), TEXT("left_shoulder"), TEXT("left_wrist"), nullptr},
		{TEXT("left_wrist"), TEXT("left_elbow"), TEXT("left_hand"), nullptr},
		{TEXT("left_hand"), TEXT("left_wrist"), nullptr, nullptr},
		{TEXT("right_collar"), TEXT("chest"), TEXT("right_shoulder"), nullptr},
		{TEXT("right_shoulder"), TEXT("right_collar"), TEXT("right_elbow"), nullptr},
		{TEXT("right_elbow"), TEXT("right_shoulder"), TEXT("right_wrist"), nullptr},
		{TEXT("right_wrist"), TEXT("right_elbow"), TEXT("right_hand"), nullptr},
		{TEXT("right_hand"), TEXT("right_wrist"), nullptr, nullptr},
	};

	static const FTrackedRigBoneDef GMannyLikeTrackedRigBones[] = {
		{TEXT("pelvis"), nullptr, TEXT("spine_01"), TEXT("thigh_l")},
		{TEXT("thigh_l"), TEXT("pelvis"), TEXT("calf_l"), nullptr},
		{TEXT("calf_l"), TEXT("thigh_l"), TEXT("foot_l"), nullptr},
		{TEXT("foot_l"), TEXT("calf_l"), TEXT("ball_l"), nullptr},
		{TEXT("ball_l"), TEXT("foot_l"), nullptr, nullptr},
		{TEXT("thigh_r"), TEXT("pelvis"), TEXT("calf_r"), nullptr},
		{TEXT("calf_r"), TEXT("thigh_r"), TEXT("foot_r"), nullptr},
		{TEXT("foot_r"), TEXT("calf_r"), TEXT("ball_r"), nullptr},
		{TEXT("ball_r"), TEXT("foot_r"), nullptr, nullptr},
		{TEXT("spine_01"), TEXT("pelvis"), TEXT("spine_02"), nullptr},
		{TEXT("spine_02"), TEXT("spine_01"), TEXT("spine_03"), nullptr},
		{TEXT("spine_03"), TEXT("spine_02"), TEXT("spine_04"), nullptr},
		{TEXT("spine_04"), TEXT("spine_03"), TEXT("spine_05"), nullptr},
		{TEXT("spine_05"), TEXT("spine_04"), TEXT("neck_01"), TEXT("clavicle_l")},
		{TEXT("neck_01"), TEXT("spine_05"), TEXT("neck_02"), nullptr},
		{TEXT("neck_02"), TEXT("neck_01"), TEXT("head"), nullptr},
		{TEXT("head"), TEXT("neck_02"), nullptr, nullptr},
		{TEXT("clavicle_l"), TEXT("spine_05"), TEXT("upperarm_l"), nullptr},
		{TEXT("upperarm_l"), TEXT("clavicle_l"), TEXT("lowerarm_l"), nullptr},
		{TEXT("lowerarm_l"), TEXT("upperarm_l"), TEXT("hand_l"), nullptr},
		{TEXT("hand_l"), TEXT("lowerarm_l"), nullptr, nullptr},
		{TEXT("clavicle_r"), TEXT("spine_05"), TEXT("upperarm_r"), nullptr},
		{TEXT("upperarm_r"), TEXT("clavicle_r"), TEXT("lowerarm_r"), nullptr},
		{TEXT("lowerarm_r"), TEXT("upperarm_r"), TEXT("hand_r"), nullptr},
		{TEXT("hand_r"), TEXT("lowerarm_r"), nullptr, nullptr},
	};

	const FTrackedRigBoneDef* GetActiveTrackedRigBones(const EMediaPipeTrackedSkeletonRig RigProfile, int32& OutBoneCount)
	{
		if (RigProfile == EMediaPipeTrackedSkeletonRig::MannyLikeHumanoid)
		{
			OutBoneCount = UE_ARRAY_COUNT(GMannyLikeTrackedRigBones);
			return GMannyLikeTrackedRigBones;
		}

		OutBoneCount = UE_ARRAY_COUNT(GSimplifiedTrackedRigBones);
		return GSimplifiedTrackedRigBones;
	}

	bool CanUseMannyLikeDebugBoneSet(const FReferenceSkeleton& RefSkeleton)
	{
		for (const FTrackedRigBoneDef& BoneDef : GMannyLikeTrackedRigBones)
		{
			if (RefSkeleton.FindBoneIndex(FName(BoneDef.BoneName)) == INDEX_NONE)
			{
				return false;
			}
		}

		return true;
	}

	bool IsBoxRenderRig(const EMediaPipeTrackedSkeletonRig RigProfile)
	{
		return RigProfile == EMediaPipeTrackedSkeletonRig::MannyLikeHumanoid ||
			RigProfile == EMediaPipeTrackedSkeletonRig::AssetSkeletonBoxes;
	}

	bool HemisphereLockVector(const FVector& Vector, const FVector& Reference, FVector& OutLocked)
	{
		const FVector Dir = Vector.GetSafeNormal();
		if (Dir.IsNearlyZero())
		{
			return false;
		}

		OutLocked = Dir;
		const FVector RefDir = Reference.GetSafeNormal();
		if (!RefDir.IsNearlyZero() && FVector::DotProduct(OutLocked, RefDir) < 0.0f)
		{
			OutLocked *= -1.0f;
		}
		return true;
	}

	bool TryGetPoint(const TMap<FName, FVector>& Points, const FName Name, FVector& OutPoint)
	{
		if (const FVector* Found = Points.Find(Name))
		{
			OutPoint = *Found;
			return true;
		}
		return false;
	}

	bool TryBuildTrackedBodyAxes(
		const TMap<FName, FVector>& JointPositions,
		FVector& OutForward,
		FVector& OutUp,
		FVector& OutRight)
	{
		FVector LeftShoulder = FVector::ZeroVector;
		FVector RightShoulder = FVector::ZeroVector;
		FVector LeftHip = FVector::ZeroVector;
		FVector RightHip = FVector::ZeroVector;
		if (!TryGetPoint(JointPositions, FName(TEXT("upperarm_l")), LeftShoulder) ||
			!TryGetPoint(JointPositions, FName(TEXT("upperarm_r")), RightShoulder) ||
			!TryGetPoint(JointPositions, FName(TEXT("thigh_l")), LeftHip) ||
			!TryGetPoint(JointPositions, FName(TEXT("thigh_r")), RightHip))
		{
			return false;
		}

		const FVector ShoulderCenter = 0.5f * (LeftShoulder + RightShoulder);
		const FVector HipCenter = 0.5f * (LeftHip + RightHip);
		FVector BodyRight = (RightShoulder - LeftShoulder).GetSafeNormal();
		FVector BodyUp = (ShoulderCenter - HipCenter).GetSafeNormal();
		if (BodyUp.IsNearlyZero() || BodyRight.IsNearlyZero())
		{
			return false;
		}

		BodyRight = (BodyRight - FVector::DotProduct(BodyRight, BodyUp) * BodyUp).GetSafeNormal();
		FVector BodyForward = FVector::CrossProduct(BodyRight, BodyUp).GetSafeNormal();
		if (BodyForward.IsNearlyZero() || BodyRight.IsNearlyZero())
		{
			return false;
		}

		FVector Nose = FVector::ZeroVector;
		if (TryGetPoint(JointPositions, FName(TEXT("nose")), Nose))
		{
			const FVector ToNose = (Nose - ShoulderCenter).GetSafeNormal();
			if (!ToNose.IsNearlyZero() && FVector::DotProduct(ToNose, BodyForward) < 0.0f)
			{
				BodyForward *= -1.0f;
			}
		}

		BodyUp = FVector::CrossProduct(BodyForward, BodyRight).GetSafeNormal();
		BodyRight = FVector::CrossProduct(BodyUp, BodyForward).GetSafeNormal();

		OutForward = BodyForward;
		OutUp = BodyUp;
		OutRight = BodyRight;
		return true;
	}

	bool TryBuildTrackedFacingDir(
		const FName BoneName,
		const TMap<FName, FVector>& JointPositions,
		FVector& OutFacingDir)
	{
		FVector BodyForward = FVector::ZeroVector;
		FVector BodyUp = FVector::ZeroVector;
		FVector BodyRight = FVector::ZeroVector;
		if (!TryBuildTrackedBodyAxes(JointPositions, BodyForward, BodyUp, BodyRight))
		{
			return false;
		}

		auto TryBuildPlaneFacing = [&](const FName A, const FName B, const FName C, const FVector& HemisphereRef) -> bool
		{
			FVector PA = FVector::ZeroVector;
			FVector PB = FVector::ZeroVector;
			FVector PC = FVector::ZeroVector;
			if (!TryGetPoint(JointPositions, A, PA) ||
				!TryGetPoint(JointPositions, B, PB) ||
				!TryGetPoint(JointPositions, C, PC))
			{
				return false;
			}

			return HemisphereLockVector(FVector::CrossProduct(PB - PA, PC - PB), HemisphereRef, OutFacingDir);
		};

		if (BoneName == FName(TEXT("pelvis")) ||
			BoneName == FName(TEXT("spine_01")) ||
			BoneName == FName(TEXT("spine_02")) ||
			BoneName == FName(TEXT("spine_03")) ||
			BoneName == FName(TEXT("spine_04")) ||
			BoneName == FName(TEXT("spine_05")))
		{
			OutFacingDir = BodyForward;
			return true;
		}

		if (BoneName == FName(TEXT("upperarm_l")) || BoneName == FName(TEXT("lowerarm_l")))
		{
			return TryBuildPlaneFacing(FName(TEXT("upperarm_l")), FName(TEXT("lowerarm_l")), FName(TEXT("hand_l")), BodyForward);
		}

		if (BoneName == FName(TEXT("upperarm_r")) || BoneName == FName(TEXT("lowerarm_r")))
		{
			return TryBuildPlaneFacing(FName(TEXT("upperarm_r")), FName(TEXT("lowerarm_r")), FName(TEXT("hand_r")), BodyForward);
		}

		if (BoneName == FName(TEXT("thigh_l")) || BoneName == FName(TEXT("calf_l")))
		{
			return TryBuildPlaneFacing(FName(TEXT("thigh_l")), FName(TEXT("calf_l")), FName(TEXT("foot_l")), BodyForward);
		}

		if (BoneName == FName(TEXT("thigh_r")) || BoneName == FName(TEXT("calf_r")))
		{
			return TryBuildPlaneFacing(FName(TEXT("thigh_r")), FName(TEXT("calf_r")), FName(TEXT("foot_r")), BodyForward);
		}

		if (BoneName == FName(TEXT("foot_l")) || BoneName == FName(TEXT("ball_l")))
		{
			FVector Foot = FVector::ZeroVector;
			FVector Ball = FVector::ZeroVector;
			if (!TryGetPoint(JointPositions, FName(TEXT("foot_l")), Foot) ||
				!TryGetPoint(JointPositions, FName(TEXT("ball_l")), Ball))
			{
				return false;
			}

			return HemisphereLockVector(FVector::CrossProduct(BodyRight, (Ball - Foot).GetSafeNormal()), BodyUp, OutFacingDir);
		}

		if (BoneName == FName(TEXT("foot_r")) || BoneName == FName(TEXT("ball_r")))
		{
			FVector Foot = FVector::ZeroVector;
			FVector Ball = FVector::ZeroVector;
			if (!TryGetPoint(JointPositions, FName(TEXT("foot_r")), Foot) ||
				!TryGetPoint(JointPositions, FName(TEXT("ball_r")), Ball))
			{
				return false;
			}

			return HemisphereLockVector(FVector::CrossProduct(BodyRight, (Ball - Foot).GetSafeNormal()), BodyUp, OutFacingDir);
		}

		if (BoneName == FName(TEXT("neck_01")) || BoneName == FName(TEXT("neck_02")) || BoneName == FName(TEXT("head")))
		{
			FVector Neck = FVector::ZeroVector;
			FVector Head = FVector::ZeroVector;
			if (!TryGetPoint(JointPositions, FName(TEXT("neck_02")), Neck))
			{
				TryGetPoint(JointPositions, FName(TEXT("neck_01")), Neck);
			}
			if (!TryGetPoint(JointPositions, FName(TEXT("head")), Head))
			{
				return false;
			}

			FVector HeadForward = FVector::ZeroVector;
			FVector Nose = FVector::ZeroVector;
			FVector LeftEar = FVector::ZeroVector;
			FVector RightEar = FVector::ZeroVector;
			if (TryGetPoint(JointPositions, FName(TEXT("nose")), Nose) &&
				TryGetPoint(JointPositions, FName(TEXT("left_ear")), LeftEar) &&
				TryGetPoint(JointPositions, FName(TEXT("right_ear")), RightEar))
			{
				const FVector EarMid = 0.5f * (LeftEar + RightEar);
				HeadForward = (Nose - EarMid).GetSafeNormal();
			}

			if (HeadForward.IsNearlyZero())
			{
				FVector HeadRight = FVector::ZeroVector;
				if (TryGetPoint(JointPositions, FName(TEXT("left_ear")), LeftEar) &&
					TryGetPoint(JointPositions, FName(TEXT("right_ear")), RightEar))
				{
					HeadRight = (RightEar - LeftEar).GetSafeNormal();
				}
				else
				{
					HeadRight = BodyRight;
				}

				const FVector HeadUp = (Head - Neck).GetSafeNormal();
				HeadForward = FVector::CrossProduct(HeadUp, HeadRight).GetSafeNormal();
			}

			return HemisphereLockVector(HeadForward, BodyForward, OutFacingDir);
		}

		return false;
	}

	bool TryBuildMannyLikeRefFacingAxesLocal(
		const FReferenceSkeleton& RefSkeleton,
		const TArray<FTransform>& RefComponentTransforms,
		TMap<FName, FVector>& OutLocalFacingAxes)
	{
		OutLocalFacingAxes.Reset();

		auto TryGetRefPos = [&](const FName BoneName, FVector& OutPos) -> bool
		{
			const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
			if (!RefComponentTransforms.IsValidIndex(BoneIndex))
			{
				return false;
			}
			OutPos = RefComponentTransforms[BoneIndex].GetLocation();
			return true;
		};

		FVector UpperArmL = FVector::ZeroVector;
		FVector UpperArmR = FVector::ZeroVector;
		FVector ThighL = FVector::ZeroVector;
		FVector ThighR = FVector::ZeroVector;
		FVector LowerArmL = FVector::ZeroVector;
		FVector HandL = FVector::ZeroVector;
		FVector LowerArmR = FVector::ZeroVector;
		FVector HandR = FVector::ZeroVector;
		FVector CalfL = FVector::ZeroVector;
		FVector FootL = FVector::ZeroVector;
		FVector CalfR = FVector::ZeroVector;
		FVector FootR = FVector::ZeroVector;
		FVector BallL = FVector::ZeroVector;
		FVector BallR = FVector::ZeroVector;
		FVector Neck2 = FVector::ZeroVector;
		FVector Head = FVector::ZeroVector;
		if (!TryGetRefPos(FName(TEXT("upperarm_l")), UpperArmL) ||
			!TryGetRefPos(FName(TEXT("upperarm_r")), UpperArmR) ||
			!TryGetRefPos(FName(TEXT("thigh_l")), ThighL) ||
			!TryGetRefPos(FName(TEXT("thigh_r")), ThighR) ||
			!TryGetRefPos(FName(TEXT("lowerarm_l")), LowerArmL) ||
			!TryGetRefPos(FName(TEXT("hand_l")), HandL) ||
			!TryGetRefPos(FName(TEXT("lowerarm_r")), LowerArmR) ||
			!TryGetRefPos(FName(TEXT("hand_r")), HandR) ||
			!TryGetRefPos(FName(TEXT("calf_l")), CalfL) ||
			!TryGetRefPos(FName(TEXT("foot_l")), FootL) ||
			!TryGetRefPos(FName(TEXT("calf_r")), CalfR) ||
			!TryGetRefPos(FName(TEXT("foot_r")), FootR) ||
			!TryGetRefPos(FName(TEXT("ball_l")), BallL) ||
			!TryGetRefPos(FName(TEXT("ball_r")), BallR) ||
			!TryGetRefPos(FName(TEXT("neck_02")), Neck2) ||
			!TryGetRefPos(FName(TEXT("head")), Head))
		{
			return false;
		}

		const FVector ShoulderCenter = 0.5f * (UpperArmL + UpperArmR);
		const FVector HipCenter = 0.5f * (ThighL + ThighR);
		FVector BodyRight = (UpperArmR - UpperArmL).GetSafeNormal();
		FVector BodyUp = (ShoulderCenter - HipCenter).GetSafeNormal();
		if (BodyUp.IsNearlyZero() || BodyRight.IsNearlyZero())
		{
			return false;
		}

		BodyRight = (BodyRight - FVector::DotProduct(BodyRight, BodyUp) * BodyUp).GetSafeNormal();
		FVector BodyForward = FVector::CrossProduct(BodyRight, BodyUp).GetSafeNormal();
		if (BodyForward.IsNearlyZero() || BodyRight.IsNearlyZero())
		{
			return false;
		}

		BodyUp = FVector::CrossProduct(BodyForward, BodyRight).GetSafeNormal();
		BodyRight = FVector::CrossProduct(BodyUp, BodyForward).GetSafeNormal();

		const FVector ArmPlaneL = FVector::CrossProduct(LowerArmL - UpperArmL, HandL - LowerArmL).GetSafeNormal();
		const FVector ArmPlaneR = FVector::CrossProduct(LowerArmR - UpperArmR, HandR - LowerArmR).GetSafeNormal();
		const FVector LegPlaneL = FVector::CrossProduct(CalfL - ThighL, FootL - CalfL).GetSafeNormal();
		const FVector LegPlaneR = FVector::CrossProduct(CalfR - ThighR, FootR - CalfR).GetSafeNormal();

		FVector ArmPlaneLLocked = FVector::ZeroVector;
		FVector ArmPlaneRLocked = FVector::ZeroVector;
		FVector LegPlaneLLocked = FVector::ZeroVector;
		FVector LegPlaneRLocked = FVector::ZeroVector;
		FVector FootUpL = FVector::ZeroVector;
		FVector FootUpR = FVector::ZeroVector;
		FVector HeadForward = FVector::ZeroVector;
		HemisphereLockVector(ArmPlaneL, BodyForward, ArmPlaneLLocked);
		HemisphereLockVector(ArmPlaneR, BodyForward, ArmPlaneRLocked);
		HemisphereLockVector(LegPlaneL, BodyForward, LegPlaneLLocked);
		HemisphereLockVector(LegPlaneR, BodyForward, LegPlaneRLocked);
		HemisphereLockVector(FVector::CrossProduct(BodyRight, (BallL - FootL).GetSafeNormal()), BodyUp, FootUpL);
		HemisphereLockVector(FVector::CrossProduct(BodyRight, (BallR - FootR).GetSafeNormal()), BodyUp, FootUpR);
		HemisphereLockVector(FVector::CrossProduct((Head - Neck2).GetSafeNormal(), BodyRight), BodyForward, HeadForward);

		struct FBoneFacingSeed
		{
			FName Bone;
			FVector Facing;
		};

		const TArray<FBoneFacingSeed> FacingSeeds = {
			{FName(TEXT("pelvis")), BodyForward},
			{FName(TEXT("spine_01")), BodyForward},
			{FName(TEXT("spine_02")), BodyForward},
			{FName(TEXT("spine_03")), BodyForward},
			{FName(TEXT("spine_04")), BodyForward},
			{FName(TEXT("spine_05")), BodyForward},
			{FName(TEXT("neck_01")), HeadForward.IsNearlyZero() ? BodyForward : HeadForward},
			{FName(TEXT("neck_02")), HeadForward.IsNearlyZero() ? BodyForward : HeadForward},
			{FName(TEXT("head")), HeadForward.IsNearlyZero() ? BodyForward : HeadForward},
			{FName(TEXT("upperarm_l")), ArmPlaneLLocked},
			{FName(TEXT("lowerarm_l")), ArmPlaneLLocked},
			{FName(TEXT("upperarm_r")), ArmPlaneRLocked},
			{FName(TEXT("lowerarm_r")), ArmPlaneRLocked},
			{FName(TEXT("thigh_l")), LegPlaneLLocked},
			{FName(TEXT("calf_l")), LegPlaneLLocked},
			{FName(TEXT("thigh_r")), LegPlaneRLocked},
			{FName(TEXT("calf_r")), LegPlaneRLocked},
			{FName(TEXT("foot_l")), FootUpL},
			{FName(TEXT("foot_r")), FootUpR},
			{FName(TEXT("ball_l")), FootUpL},
			{FName(TEXT("ball_r")), FootUpR},
		};

		for (const FBoneFacingSeed& Seed : FacingSeeds)
		{
			if (Seed.Facing.IsNearlyZero())
			{
				continue;
			}

			const int32 BoneIndex = RefSkeleton.FindBoneIndex(Seed.Bone);
			if (!RefComponentTransforms.IsValidIndex(BoneIndex))
			{
				continue;
			}

			const FVector LocalFacing = RefComponentTransforms[BoneIndex].GetRotation().Inverse().RotateVector(Seed.Facing).GetSafeNormal();
			if (!LocalFacing.IsNearlyZero())
			{
				OutLocalFacingAxes.Add(Seed.Bone, LocalFacing);
			}
		}

		return OutLocalFacingAxes.Num() > 0;
	}

	bool TryAveragePoints(const TArray<FVector>& Points, FVector& OutAverage)
	{
		if (Points.Num() <= 0)
		{
			return false;
		}

		FVector Sum = FVector::ZeroVector;
		for (const FVector& Point : Points)
		{
			Sum += Point;
		}

		OutAverage = Sum / static_cast<float>(Points.Num());
		return true;
	}

	FString MakeTrackedJointComponentName(const FName JointName)
	{
		return FString::Printf(TEXT("TrackedJoint_%s"), *JointName.ToString());
	}

	FString MakeTrackedBoneComponentName(const FName ParentName, const FName ChildName)
	{
		return FString::Printf(TEXT("TrackedBone_%s_%s"), *ParentName.ToString(), *ChildName.ToString());
	}

	void AddMannyLikeJointAliases(
		TMap<FName, FVector>& OutJointPositions,
		const FVector& Pelvis,
		const FVector& Chest,
		const FVector& HeadAnchor,
		const FVector& Head,
		const FVector& LeftCollar,
		const FVector& RightCollar,
		const FVector& LeftShoulder,
		const FVector& RightShoulder,
		const FVector& LeftElbow,
		const FVector& RightElbow,
		const FVector& LeftWrist,
		const FVector& RightWrist,
		const FVector& LeftHip,
		const FVector& RightHip,
		const FVector& LeftKnee,
		const FVector& RightKnee,
		const FVector& LeftAnkle,
		const FVector& RightAnkle,
		const FVector& LeftFootIndex,
		const FVector& RightFootIndex)
	{
		OutJointPositions.Add(FName(TEXT("center_of_mass")), Pelvis);
		OutJointPositions.Add(FName(TEXT("thigh_l")), LeftHip);
		OutJointPositions.Add(FName(TEXT("calf_l")), LeftKnee);
		OutJointPositions.Add(FName(TEXT("foot_l")), LeftAnkle);
		OutJointPositions.Add(FName(TEXT("ball_l")), LeftFootIndex);
		OutJointPositions.Add(FName(TEXT("thigh_r")), RightHip);
		OutJointPositions.Add(FName(TEXT("calf_r")), RightKnee);
		OutJointPositions.Add(FName(TEXT("foot_r")), RightAnkle);
		OutJointPositions.Add(FName(TEXT("ball_r")), RightFootIndex);
		OutJointPositions.Add(FName(TEXT("spine_01")), FMath::Lerp(Pelvis, Chest, 0.18f));
		OutJointPositions.Add(FName(TEXT("spine_02")), FMath::Lerp(Pelvis, Chest, 0.38f));
		OutJointPositions.Add(FName(TEXT("spine_03")), FMath::Lerp(Pelvis, Chest, 0.58f));
		OutJointPositions.Add(FName(TEXT("spine_04")), FMath::Lerp(Pelvis, Chest, 0.78f));
		OutJointPositions.Add(FName(TEXT("spine_05")), Chest);
		OutJointPositions.Add(FName(TEXT("neck_01")), FMath::Lerp(Chest, HeadAnchor, 0.18f));
		OutJointPositions.Add(FName(TEXT("neck_02")), FMath::Lerp(Chest, HeadAnchor, 0.42f));
		OutJointPositions.Add(FName(TEXT("head")), Head);
		OutJointPositions.Add(FName(TEXT("clavicle_l")), LeftCollar);
		OutJointPositions.Add(FName(TEXT("upperarm_l")), LeftShoulder);
		OutJointPositions.Add(FName(TEXT("lowerarm_l")), LeftElbow);
		OutJointPositions.Add(FName(TEXT("hand_l")), LeftWrist);
		OutJointPositions.Add(FName(TEXT("clavicle_r")), RightCollar);
		OutJointPositions.Add(FName(TEXT("upperarm_r")), RightShoulder);
		OutJointPositions.Add(FName(TEXT("lowerarm_r")), RightElbow);
		OutJointPositions.Add(FName(TEXT("hand_r")), RightWrist);
	}

	void LogTrackedMannyLikeElbowDiagnosticsIfNeeded(
		const AMediaPipeTrackedSkeletonActor* Actor,
		const TMap<FName, FVector>& JointPositions,
		double& InOutLastLogTimeSeconds)
	{
		if (!Actor || Actor->RigProfile != EMediaPipeTrackedSkeletonRig::MannyLikeHumanoid)
		{
			return;
		}

		if (Actor->GetActorNameOrLabel() != TEXT("MP_MediaPipeMannyLike"))
		{
			return;
		}

		const UWorld* World = Actor->GetWorld();
		const double NowSeconds = World ? static_cast<double>(World->GetTimeSeconds()) : 0.0;
		if (InOutLastLogTimeSeconds >= 0.0 && (NowSeconds - InOutLastLogTimeSeconds) < 1.0)
		{
			return;
		}

		FVector BodyForward = FVector::ZeroVector;
		FVector BodyUp = FVector::ZeroVector;
		FVector BodyRight = FVector::ZeroVector;
		if (!TryBuildTrackedBodyAxes(JointPositions, BodyForward, BodyUp, BodyRight))
		{
			return;
		}

		auto LogArm = [&](const TCHAR* SideLabel, const FName ShoulderName, const FName ElbowName, const FName WristName)
		{
			FVector Shoulder = FVector::ZeroVector;
			FVector Elbow = FVector::ZeroVector;
			FVector Wrist = FVector::ZeroVector;
			if (!TryGetPoint(JointPositions, ShoulderName, Shoulder) ||
				!TryGetPoint(JointPositions, ElbowName, Elbow) ||
				!TryGetPoint(JointPositions, WristName, Wrist))
			{
				return;
			}

			const FVector Upper = Elbow - Shoulder;
			const FVector Lower = Wrist - Elbow;
			const FVector Plane = FVector::CrossProduct(Upper, Lower);
			const FVector PlaneN = Plane.GetSafeNormal();

			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[MP MannyLike Elbow] actor=%s side=%s shoulder=%s elbow=%s wrist=%s upper=%s lower=%s plane=%s dotForward=%.4f dotUp=%.4f dotRight=%.4f"),
				*Actor->GetActorNameOrLabel(),
				SideLabel,
				*Shoulder.ToString(),
				*Elbow.ToString(),
				*Wrist.ToString(),
				*Upper.ToString(),
				*Lower.ToString(),
				*PlaneN.ToString(),
				FVector::DotProduct(PlaneN, BodyForward),
				FVector::DotProduct(PlaneN, BodyUp),
				FVector::DotProduct(PlaneN, BodyRight));
		};

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[MP MannyLike Body] actor=%s forward=%s up=%s right=%s"),
			*Actor->GetActorNameOrLabel(),
			*BodyForward.ToString(),
			*BodyUp.ToString(),
			*BodyRight.ToString());

		LogArm(TEXT("L"), FName(TEXT("upperarm_l")), FName(TEXT("lowerarm_l")), FName(TEXT("hand_l")));
		LogArm(TEXT("R"), FName(TEXT("upperarm_r")), FName(TEXT("lowerarm_r")), FName(TEXT("hand_r")));

		InOutLastLogTimeSeconds = NowSeconds;
	}
}

AMediaPipeTrackedSkeletonActor::AMediaPipeTrackedSkeletonActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCastShadow(bCastShadows);
	Body->SetMobility(EComponentMobility::Movable);
	Body->bSelectable = true;
	Body->bEnableUpdateRateOptimizations = false;
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Body->SetUpdateAnimationInEditor(true);
	Body->bTickInEditor = true;
	Body->PrimaryComponentTick.bStartWithTickEnabled = true;
	Body->SetComponentTickEnabled(true);

	PoseDriver = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("PoseDriver"));
	PoseDriver->SetupAttachment(Root);
	PoseDriver->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PoseDriver->SetGenerateOverlapEvents(false);
	PoseDriver->SetCastShadow(false);
	PoseDriver->SetMobility(EComponentMobility::Movable);
	PoseDriver->bSelectable = false;
	PoseDriver->SetVisibility(false, true);
	PoseDriver->SetHiddenInGame(true, true);
	PoseDriver->bTickInEditor = true;
	PoseDriver->PrimaryComponentTick.bStartWithTickEnabled = true;
	PoseDriver->SetComponentTickEnabled(true);

	EmbodiedFusionComponent = CreateDefaultSubobject<UEmbodiedFusionComponent>(TEXT("EmbodiedFusion"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		DebugPrimitiveMesh = CubeMeshFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicShapeMaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BasicShapeMaterialFinder.Succeeded())
	{
		DebugPrimitiveMaterial = BasicShapeMaterialFinder.Object;
	}

	OverrideMaterial = DebugPrimitiveMaterial;

	RefreshBodyMesh();
}

void AMediaPipeTrackedSkeletonActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

	if (Body)
	{
		Body->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
	}

	if (PoseDriver)
	{
		PoseDriver->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
	}
}

void AMediaPipeTrackedSkeletonActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshBodyMesh();
	RefreshFromSourcePose();
}

void AMediaPipeTrackedSkeletonActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (DeltaSeconds > KINDA_SMALL_NUMBER)
	{
		LastManualTickDeltaSeconds = DeltaSeconds;
	}
	RefreshFromSourcePose();
}

bool AMediaPipeTrackedSkeletonActor::RefreshFromSourcePose()
{
	EnsureSourceActor();
	UpdateActorTransformFromSource();
	RefreshBodyMesh();

	if (UsesStandardMannyTrackingPath())
	{
		const bool bHasPose = HasSelectedTrackingFrame();
		RefreshTrackingPoseBinding();

		if (Body)
		{
			float AnimationDeltaSeconds = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;
			if (AnimationDeltaSeconds <= KINDA_SMALL_NUMBER)
			{
				AnimationDeltaSeconds = LastManualTickDeltaSeconds;
			}
			if (AnimationDeltaSeconds <= KINDA_SMALL_NUMBER)
			{
				AnimationDeltaSeconds = 1.0f / 60.0f;
			}
			Body->TickAnimation(AnimationDeltaSeconds, false);
			Body->RefreshBoneTransforms();
			Body->UpdateComponentToWorld();
		}

		if (ShouldRenderBoxes())
		{
			if (Body)
			{
				UpdateDebugBoxRenderFromSkinnedMesh(Body);
				Body->SetVisibility(false, true);
			}
		}
		else
		{
			HideDebugBoxRender();
			if (Body)
			{
				Body->SetVisibility(true, true);
			}
		}
		if (PoseDriver)
		{
			PoseDriver->SetVisibility(false, true);
		}

		bHasValidPose = bHasPose;
		return bHasPose;
	}

	TMap<FName, FVector> JointPositions;
	const bool bBuiltPose = TryBuildMediaPipeLocalJointPositions(JointPositions);

	LogTrackedMannyLikeElbowDiagnosticsIfNeeded(this, JointPositions, LastElbowDiagnosticLogTimeSeconds);

	if (!bBuiltPose || !ApplyPoseLocalJointPositions(JointPositions))
	{
		ShowReferencePose();
		bHasValidPose = false;
		return false;
	}

	if (ShouldRenderBoxes())
	{
		UpdateDebugBoxRender(JointPositions, false);
		if (Body)
		{
			Body->SetVisibility(false, true);
		}
	}
	else
	{
		HideDebugBoxRender();
		if (Body)
		{
			Body->SetVisibility(true, true);
		}
	}
	if (PoseDriver)
	{
		PoseDriver->SetVisibility(false, true);
	}

	bHasValidPose = true;
	return true;
}

void AMediaPipeTrackedSkeletonActor::SetSkeletonVisible(const bool bVisible)
{
	if (Body)
	{
		Body->SetVisibility(bVisible && !ShouldRenderBoxes(), true);
	}

	if (!bVisible)
	{
		HideDebugBoxRender();
	}
}

bool AMediaPipeTrackedSkeletonActor::EnsureSourceActor()
{
	if (SourceActor || !bAutoFindSource)
	{
		return SourceActor != nullptr;
	}

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Candidate = *It;
			if (!Candidate || Candidate == this)
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

	return SourceActor != nullptr;
}

void AMediaPipeTrackedSkeletonActor::UpdateActorTransformFromSource()
{
	if (!bFollowSourceActorTransform || !SourceActor)
	{
		return;
	}

	const FTransform SourceTransform = SourceActor->GetActorTransform();
	const FVector OffsetWorld = SourceTransform.GetRotation().RotateVector(SourceRelativeOffset);
	SetActorLocationAndRotation(
		SourceTransform.GetLocation() + OffsetWorld,
		SourceTransform.GetRotation(),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
}

void AMediaPipeTrackedSkeletonActor::RefreshBodyMesh()
{
	if (!Body || !PoseDriver)
	{
		return;
	}

	Body->SetLeaderPoseComponent(nullptr, false, false);

	const bool bBodyMeshMismatch = BodySkeletalMesh && Body->GetSkeletalMeshAsset() != BodySkeletalMesh;
	const bool bDriverMeshMismatch = BodySkeletalMesh && PoseDriver->GetSkinnedAsset() != BodySkeletalMesh;
	if (bBodyMeshMismatch)
	{
		Body->SetSkeletalMesh(BodySkeletalMesh);
	}
	if (bDriverMeshMismatch)
	{
		PoseDriver->SetSkinnedAssetAndUpdate(BodySkeletalMesh);
		CachedReferenceMesh = nullptr;
	}

	Body->SetCollisionEnabled(bCollisionEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Body->SetCastShadow(bCastShadows);
	Body->bEnableUpdateRateOptimizations = false;
	Body->ClearRefPoseOverride();

	PoseDriver->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PoseDriver->SetCastShadow(false);
	PoseDriver->SetVisibility(false, true);
	PoseDriver->SetHiddenInGame(true, true);

	if (OverrideMaterial)
	{
		const int32 MaterialCount = FMath::Max(1, Body->GetNumMaterials());
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			Body->SetMaterial(MaterialIndex, OverrideMaterial);
		}
	}

	CacheReferencePoseData();
	RefreshTrackingPoseBinding();
	EnsureDebugBoxRenderComponents();
	if (!ShouldRenderBoxes())
	{
		HideDebugBoxRender();
	}
}

void AMediaPipeTrackedSkeletonActor::RefreshTrackingPoseBinding()
{
	if (!Body || !PoseDriver)
	{
		return;
	}

	if (!UsesStandardMannyTrackingPath())
	{
		Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		return;
	}

	const TSubclassOf<UAnimInstance> DesiredAnimClass = UMediaPipePoseDrivenAnimInstance::StaticClass();
	if (Body->GetAnimationMode() != EAnimationMode::AnimationBlueprint ||
		Body->GetAnimClass() != DesiredAnimClass)
	{
		Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Body->SetAnimInstanceClass(DesiredAnimClass);
		Body->InitializeAnimScriptInstance(true);
	}

	if (UMediaPipePoseDrivenAnimInstance* PoseDrivenAnim = Cast<UMediaPipePoseDrivenAnimInstance>(Body->GetAnimInstance()))
	{
		PoseDrivenAnim->SetSourceActor(SourceActor);
		PoseDrivenAnim->SetEmbodiedFusionComponent(EmbodiedFusionComponent);
		PoseDrivenAnim->ApplyRetargetQualitySettings();
	}
}

bool AMediaPipeTrackedSkeletonActor::UsesStandardMannyTrackingPath() const
{
	if (RigProfile != EMediaPipeTrackedSkeletonRig::MannyLikeHumanoid ||
		!Body)
	{
		return false;
	}

	const USkeletalMesh* Mesh = Body->GetSkeletalMeshAsset();
	return Mesh && CanUseMannyLikeDebugBoneSet(Mesh->GetRefSkeleton());
}

bool AMediaPipeTrackedSkeletonActor::HasSelectedTrackingFrame() const
{
	FMediaPipePoseFrame Frame;
	return TryGetMediaPipeFrame(Frame);
}

void AMediaPipeTrackedSkeletonActor::EnsureDebugBoxRenderComponents()
{
	if (!ShouldRenderBoxes() || !Root)
	{
		return;
	}

	UMaterialInterface* RenderMaterial = OverrideMaterial ? OverrideMaterial : DebugPrimitiveMaterial;
	const float JointScale = 0.10f;
	const float BoneThicknessScale = 0.08f;

	auto EnsureBoxComponents = [this, RenderMaterial](const FName BoneName, const FName ParentName)
	{
		if (!DebugJointComponents.Contains(BoneName))
		{
			UStaticMeshComponent* JointComponent = NewObject<UStaticMeshComponent>(this, *MakeTrackedJointComponentName(BoneName));
			JointComponent->SetupAttachment(Root);
			JointComponent->bVisualizeComponent = true;
			JointComponent->SetMobility(EComponentMobility::Movable);
			JointComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			JointComponent->SetGenerateOverlapEvents(false);
			JointComponent->SetCastShadow(bCastShadows);
			JointComponent->SetVisibility(false);
			JointComponent->SetHiddenInGame(false);
			if (DebugPrimitiveMesh)
			{
				JointComponent->SetStaticMesh(DebugPrimitiveMesh);
			}
			if (RenderMaterial)
			{
				JointComponent->SetMaterial(0, RenderMaterial);
			}
			JointComponent->RegisterComponent();
			AddInstanceComponent(JointComponent);
			DebugJointComponents.Add(BoneName, JointComponent);
		}

		if (!ParentName.IsNone())
		{
			const FName BoneKey(*MakeTrackedBoneComponentName(ParentName, BoneName));
			if (!DebugBoneComponents.Contains(BoneKey))
			{
			UStaticMeshComponent* BoneComponent = NewObject<UStaticMeshComponent>(this, *BoneKey.ToString());
			BoneComponent->SetupAttachment(Root);
			BoneComponent->bVisualizeComponent = true;
			BoneComponent->SetMobility(EComponentMobility::Movable);
				BoneComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				BoneComponent->SetGenerateOverlapEvents(false);
				BoneComponent->SetCastShadow(bCastShadows);
				BoneComponent->SetVisibility(false);
				BoneComponent->SetHiddenInGame(false);
				if (DebugPrimitiveMesh)
				{
					BoneComponent->SetStaticMesh(DebugPrimitiveMesh);
				}
				if (RenderMaterial)
				{
					BoneComponent->SetMaterial(0, RenderMaterial);
				}
				BoneComponent->RegisterComponent();
				AddInstanceComponent(BoneComponent);
				DebugBoneComponents.Add(BoneKey, BoneComponent);
			}
		}
	};

	if (RigProfile == EMediaPipeTrackedSkeletonRig::AssetSkeletonBoxes)
	{
		for (const TPair<FName, FBoneReferenceData>& Pair : BoneReferenceDataByName)
		{
			EnsureBoxComponents(Pair.Key, Pair.Value.ParentName);
		}
	}
	else
	{
		int32 ActiveBoneCount = 0;
		const FTrackedRigBoneDef* ActiveBoneDefs = GetActiveTrackedRigBones(RigProfile, ActiveBoneCount);
		for (int32 BoneDefIndex = 0; BoneDefIndex < ActiveBoneCount; ++BoneDefIndex)
		{
			const FTrackedRigBoneDef& BoneDef = ActiveBoneDefs[BoneDefIndex];
			EnsureBoxComponents(
				FName(BoneDef.BoneName),
				BoneDef.ParentName ? FName(BoneDef.ParentName) : NAME_None);
		}
	}

	for (TPair<FName, TObjectPtr<UStaticMeshComponent>>& Pair : DebugJointComponents)
	{
		if (Pair.Value)
		{
			Pair.Value->SetCastShadow(bCastShadows);
			Pair.Value->SetRelativeScale3D(FVector(JointScale));
			if (RenderMaterial)
			{
				Pair.Value->SetMaterial(0, RenderMaterial);
			}
		}
	}

	for (TPair<FName, TObjectPtr<UStaticMeshComponent>>& Pair : DebugBoneComponents)
	{
		if (Pair.Value)
		{
			Pair.Value->SetCastShadow(bCastShadows);
			Pair.Value->SetRelativeScale3D(FVector(1.0f, BoneThicknessScale, BoneThicknessScale));
			if (RenderMaterial)
			{
				Pair.Value->SetMaterial(0, RenderMaterial);
			}
		}
	}
}

void AMediaPipeTrackedSkeletonActor::UpdateDebugBoxRender(const TMap<FName, FVector>& JointPositions, const bool bUseReferencePose)
{
	if (!ShouldRenderBoxes())
	{
		HideDebugBoxRender();
		return;
	}

	EnsureDebugBoxRenderComponents();

	const float JointScale = 0.10f;
	const float BoneThicknessScale = 0.08f;
	if (RigProfile == EMediaPipeTrackedSkeletonRig::AssetSkeletonBoxes)
	{
		for (const TPair<FName, FBoneReferenceData>& Pair : BoneReferenceDataByName)
		{
			const FName BoneName = Pair.Key;
			const FBoneReferenceData& RefData = Pair.Value;
			UStaticMeshComponent* JointComponent = DebugJointComponents.FindRef(BoneName);
			if (!JointComponent)
			{
				continue;
			}

			FVector BonePos = FVector::ZeroVector;
			if (const FVector* FoundPos = JointPositions.Find(BoneName))
			{
				BonePos = *FoundPos;
			}
			else if (bUseReferencePose)
			{
				BonePos = RefData.RefComponentTransform.GetLocation();
			}
			else
			{
				JointComponent->SetVisibility(false);
				continue;
			}

			JointComponent->SetRelativeLocation(BonePos);
			JointComponent->SetRelativeRotation(FRotator::ZeroRotator);
			JointComponent->SetRelativeScale3D(FVector(JointScale));
			JointComponent->SetVisibility(true);

			if (!RefData.ParentName.IsNone())
			{
				UStaticMeshComponent* BoneComponent = DebugBoneComponents.FindRef(FName(*MakeTrackedBoneComponentName(RefData.ParentName, BoneName)));
				if (!BoneComponent)
				{
					continue;
				}

				FVector ParentPos = FVector::ZeroVector;
				if (const FVector* FoundParentPos = JointPositions.Find(RefData.ParentName))
				{
					ParentPos = *FoundParentPos;
				}
				else if (bUseReferencePose)
				{
					if (const FBoneReferenceData* ParentRef = BoneReferenceDataByName.Find(RefData.ParentName))
					{
						ParentPos = ParentRef->RefComponentTransform.GetLocation();
					}
					else
					{
						BoneComponent->SetVisibility(false);
						continue;
					}
				}
				else
				{
					BoneComponent->SetVisibility(false);
					continue;
				}

				const FVector Delta = BonePos - ParentPos;
				const float Length = Delta.Size();
				if (Length <= KINDA_SMALL_NUMBER)
				{
					BoneComponent->SetVisibility(false);
					continue;
				}

				BoneComponent->SetRelativeLocation((BonePos + ParentPos) * 0.5f);
				BoneComponent->SetRelativeRotation(FRotationMatrix::MakeFromX(Delta.GetSafeNormal()).Rotator());
				BoneComponent->SetRelativeScale3D(FVector(Length / 100.0f, BoneThicknessScale, BoneThicknessScale));
				BoneComponent->SetVisibility(true);
			}
		}
		return;
	}

	int32 ActiveBoneCount = 0;
	const FTrackedRigBoneDef* ActiveBoneDefs = GetActiveTrackedRigBones(RigProfile, ActiveBoneCount);

	for (int32 BoneDefIndex = 0; BoneDefIndex < ActiveBoneCount; ++BoneDefIndex)
	{
		const FTrackedRigBoneDef& BoneDef = ActiveBoneDefs[BoneDefIndex];
		const FName BoneName(BoneDef.BoneName);
		UStaticMeshComponent* JointComponent = DebugJointComponents.FindRef(BoneName);
		if (!JointComponent)
		{
			continue;
		}

		FVector BonePos = FVector::ZeroVector;
		if (const FVector* FoundPos = JointPositions.Find(BoneName))
		{
			BonePos = *FoundPos;
		}
		else if (bUseReferencePose)
		{
			if (const FBoneReferenceData* RefData = BoneReferenceDataByName.Find(BoneName))
			{
				BonePos = RefData->RefComponentTransform.GetLocation();
			}
			else
			{
				JointComponent->SetVisibility(false);
				continue;
			}
		}
		else
		{
			JointComponent->SetVisibility(false);
			continue;
		}

		JointComponent->SetRelativeLocation(BonePos);
		JointComponent->SetRelativeRotation(FRotator::ZeroRotator);
		JointComponent->SetRelativeScale3D(FVector(JointScale));
		JointComponent->SetVisibility(true);

		if (BoneDef.ParentName)
		{
			const FName ParentName(BoneDef.ParentName);
			const FName BoneKey(*MakeTrackedBoneComponentName(ParentName, BoneName));
			UStaticMeshComponent* BoneComponent = DebugBoneComponents.FindRef(BoneKey);
			if (!BoneComponent)
			{
				continue;
			}

			FVector ParentPos = FVector::ZeroVector;
			if (const FVector* FoundParentPos = JointPositions.Find(ParentName))
			{
				ParentPos = *FoundParentPos;
			}
			else if (bUseReferencePose)
			{
				if (const FBoneReferenceData* ParentRef = BoneReferenceDataByName.Find(ParentName))
				{
					ParentPos = ParentRef->RefComponentTransform.GetLocation();
				}
				else
				{
					BoneComponent->SetVisibility(false);
					continue;
				}
			}
			else
			{
				BoneComponent->SetVisibility(false);
				continue;
			}

			const FVector Delta = BonePos - ParentPos;
			const float Length = Delta.Size();
			if (Length <= KINDA_SMALL_NUMBER)
			{
				BoneComponent->SetVisibility(false);
				continue;
			}

			BoneComponent->SetRelativeLocation((BonePos + ParentPos) * 0.5f);
			BoneComponent->SetRelativeRotation(FRotationMatrix::MakeFromX(Delta.GetSafeNormal()).Rotator());
			BoneComponent->SetRelativeScale3D(FVector(Length / 100.0f, BoneThicknessScale, BoneThicknessScale));
			BoneComponent->SetVisibility(true);
		}
	}
}

void AMediaPipeTrackedSkeletonActor::UpdateDebugBoxRenderFromSkinnedMesh(USkinnedMeshComponent* SkinnedMeshComp)
{
	if (!SkinnedMeshComp || !ShouldRenderBoxes())
	{
		HideDebugBoxRender();
		return;
	}

	EnsureDebugBoxRenderComponents();

	const float JointScale = 0.10f;
	const float BoneThicknessScale = 0.08f;
	for (const TPair<FName, FBoneReferenceData>& Pair : BoneReferenceDataByName)
	{
		const FName BoneName = Pair.Key;
		const FBoneReferenceData& RefData = Pair.Value;
		UStaticMeshComponent* JointComponent = DebugJointComponents.FindRef(BoneName);
		if (!JointComponent)
		{
			continue;
		}

		const FVector BonePos = SkinnedMeshComp->GetBoneLocation(BoneName, EBoneSpaces::ComponentSpace);
		JointComponent->SetRelativeLocation(BonePos);
		JointComponent->SetRelativeRotation(FRotator::ZeroRotator);
		JointComponent->SetRelativeScale3D(FVector(JointScale));
		JointComponent->SetVisibility(true);

		if (!RefData.ParentName.IsNone())
		{
			UStaticMeshComponent* BoneComponent = DebugBoneComponents.FindRef(FName(*MakeTrackedBoneComponentName(RefData.ParentName, BoneName)));
			if (!BoneComponent)
			{
				continue;
			}

			const FVector ParentPos = SkinnedMeshComp->GetBoneLocation(RefData.ParentName, EBoneSpaces::ComponentSpace);
			const FVector Delta = BonePos - ParentPos;
			const float Length = Delta.Size();
			if (Length <= KINDA_SMALL_NUMBER)
			{
				BoneComponent->SetVisibility(false);
				continue;
			}

			BoneComponent->SetRelativeLocation((BonePos + ParentPos) * 0.5f);
			BoneComponent->SetRelativeRotation(FRotationMatrix::MakeFromX(Delta.GetSafeNormal()).Rotator());
			BoneComponent->SetRelativeScale3D(FVector(Length / 100.0f, BoneThicknessScale, BoneThicknessScale));
			BoneComponent->SetVisibility(true);
		}
	}
}

void AMediaPipeTrackedSkeletonActor::HideDebugBoxRender()
{
	for (const TPair<FName, TObjectPtr<UStaticMeshComponent>>& Pair : DebugJointComponents)
	{
		if (Pair.Value)
		{
			Pair.Value->SetVisibility(false);
		}
	}

	for (const TPair<FName, TObjectPtr<UStaticMeshComponent>>& Pair : DebugBoneComponents)
	{
		if (Pair.Value)
		{
			Pair.Value->SetVisibility(false);
		}
	}
}

void AMediaPipeTrackedSkeletonActor::CacheReferencePoseData()
{
	if (!PoseDriver)
	{
		return;
	}

	USkeletalMesh* MeshAsset = Cast<USkeletalMesh>(PoseDriver->GetSkinnedAsset());
	if (!MeshAsset)
	{
		BoneReferenceDataByName.Reset();
		CachedReferenceMesh = nullptr;
		return;
	}

	if (CachedReferenceMesh.Get() == MeshAsset && CachedRigProfile == RigProfile && BoneReferenceDataByName.Num() > 0)
	{
		return;
	}

	CachedReferenceMesh = MeshAsset;
	CachedRigProfile = RigProfile;
	BoneReferenceDataByName.Reset();

	const FReferenceSkeleton& RefSkeleton = MeshAsset->GetRefSkeleton();
	const TArray<FTransform>& RefLocalTransforms = RefSkeleton.GetRefBonePose();
	TArray<FTransform> RefComponentTransforms;
	RefComponentTransforms.SetNum(RefLocalTransforms.Num());
	TMap<FName, FVector> MannyLikeRefFacingAxesLocal;

	for (int32 BoneIndex = 0; BoneIndex < RefLocalTransforms.Num(); ++BoneIndex)
	{
		const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		RefComponentTransforms[BoneIndex] = (ParentIndex >= 0)
			? (RefLocalTransforms[BoneIndex] * RefComponentTransforms[ParentIndex])
			: RefLocalTransforms[BoneIndex];
	}

	const bool bHasMannyLikeRefFacingAxes = TryBuildMannyLikeRefFacingAxesLocal(
		RefSkeleton,
		RefComponentTransforms,
		MannyLikeRefFacingAxesLocal);

	if (RigProfile == EMediaPipeTrackedSkeletonRig::AssetSkeletonBoxes)
	{
		if (CanUseMannyLikeDebugBoneSet(RefSkeleton))
		{
			const int32 ActiveBoneCount = UE_ARRAY_COUNT(GMannyLikeTrackedRigBones);
			for (int32 BoneDefIndex = 0; BoneDefIndex < ActiveBoneCount; ++BoneDefIndex)
			{
				const FTrackedRigBoneDef& BoneDef = GMannyLikeTrackedRigBones[BoneDefIndex];
				const FName BoneName(BoneDef.BoneName);
				const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
				if (BoneIndex == INDEX_NONE)
				{
					continue;
				}

				FBoneReferenceData ReferenceData;
				ReferenceData.BoneName = BoneName;
				ReferenceData.ParentName = BoneDef.ParentName ? FName(BoneDef.ParentName) : NAME_None;
				ReferenceData.PrimaryChildName = BoneDef.PrimaryChildName ? FName(BoneDef.PrimaryChildName) : NAME_None;
				ReferenceData.SecondaryChildName = BoneDef.SecondaryChildName ? FName(BoneDef.SecondaryChildName) : NAME_None;
				ReferenceData.RefComponentTransform = RefComponentTransforms[BoneIndex];

				const FVector BoneRefLocation = ReferenceData.RefComponentTransform.GetLocation();

				if (BoneDef.PrimaryChildName)
				{
					const int32 PrimaryChildIndex = RefSkeleton.FindBoneIndex(ReferenceData.PrimaryChildName);
					if (PrimaryChildIndex != INDEX_NONE)
					{
						ReferenceData.RefPrimaryDir = (RefComponentTransforms[PrimaryChildIndex].GetLocation() - BoneRefLocation).GetSafeNormal();
						ReferenceData.bHasPrimaryDir = !ReferenceData.RefPrimaryDir.IsNearlyZero();
					}
				}

				if (BoneDef.SecondaryChildName)
				{
					const int32 SecondaryChildIndex = RefSkeleton.FindBoneIndex(ReferenceData.SecondaryChildName);
					if (SecondaryChildIndex != INDEX_NONE)
					{
						ReferenceData.RefSecondaryDir = (RefComponentTransforms[SecondaryChildIndex].GetLocation() - BoneRefLocation).GetSafeNormal();
						ReferenceData.bHasSecondaryDir = !ReferenceData.RefSecondaryDir.IsNearlyZero();
					}
				}

				if (!ReferenceData.ParentName.IsNone())
				{
					const int32 ParentIndex = RefSkeleton.FindBoneIndex(ReferenceData.ParentName);
					if (ParentIndex != INDEX_NONE)
					{
						ReferenceData.RefParentDir = (BoneRefLocation - RefComponentTransforms[ParentIndex].GetLocation()).GetSafeNormal();
						ReferenceData.bHasParentDir = !ReferenceData.RefParentDir.IsNearlyZero();
					}
				}

				if (bHasMannyLikeRefFacingAxes)
				{
					if (const FVector* FacingDir = MannyLikeRefFacingAxesLocal.Find(ReferenceData.BoneName))
					{
						ReferenceData.RefFacingDir = *FacingDir;
						ReferenceData.bHasFacingDir = !ReferenceData.RefFacingDir.IsNearlyZero();
					}
				}

				BoneReferenceDataByName.Add(ReferenceData.BoneName, ReferenceData);
			}
		}
		else
		{
			const int32 BoneCount = RefSkeleton.GetNum();
			for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
			{
				FBoneReferenceData ReferenceData;
				ReferenceData.BoneName = RefSkeleton.GetBoneName(BoneIndex);
				const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
				ReferenceData.ParentName = ParentIndex != INDEX_NONE ? RefSkeleton.GetBoneName(ParentIndex) : NAME_None;
				ReferenceData.RefComponentTransform = RefComponentTransforms[BoneIndex];

				const FVector BoneRefLocation = ReferenceData.RefComponentTransform.GetLocation();
				int32 ChildCount = 0;
				for (int32 ChildIndex = BoneIndex + 1; ChildIndex < BoneCount && ChildCount < 2; ++ChildIndex)
				{
					if (RefSkeleton.GetParentIndex(ChildIndex) != BoneIndex)
					{
						continue;
					}

					const FName ChildBoneName = RefSkeleton.GetBoneName(ChildIndex);
					if (ChildCount == 0)
					{
						ReferenceData.PrimaryChildName = ChildBoneName;
						ReferenceData.RefPrimaryDir = (RefComponentTransforms[ChildIndex].GetLocation() - BoneRefLocation).GetSafeNormal();
						ReferenceData.bHasPrimaryDir = !ReferenceData.RefPrimaryDir.IsNearlyZero();
					}
					else
					{
						ReferenceData.SecondaryChildName = ChildBoneName;
						ReferenceData.RefSecondaryDir = (RefComponentTransforms[ChildIndex].GetLocation() - BoneRefLocation).GetSafeNormal();
						ReferenceData.bHasSecondaryDir = !ReferenceData.RefSecondaryDir.IsNearlyZero();
					}

					++ChildCount;
				}

				if (ParentIndex != INDEX_NONE)
				{
					ReferenceData.RefParentDir = (BoneRefLocation - RefComponentTransforms[ParentIndex].GetLocation()).GetSafeNormal();
					ReferenceData.bHasParentDir = !ReferenceData.RefParentDir.IsNearlyZero();
				}

				BoneReferenceDataByName.Add(ReferenceData.BoneName, ReferenceData);
			}
		}
		return;
	}

	int32 ActiveBoneCount = 0;
	const FTrackedRigBoneDef* ActiveBoneDefs = GetActiveTrackedRigBones(RigProfile, ActiveBoneCount);
	for (int32 BoneDefIndex = 0; BoneDefIndex < ActiveBoneCount; ++BoneDefIndex)
	{
		const FTrackedRigBoneDef& BoneDef = ActiveBoneDefs[BoneDefIndex];
		const FName BoneName(BoneDef.BoneName);
		const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
		if (BoneIndex == INDEX_NONE)
		{
			continue;
		}

		FBoneReferenceData ReferenceData;
		ReferenceData.BoneName = BoneName;
		ReferenceData.ParentName = BoneDef.ParentName ? FName(BoneDef.ParentName) : NAME_None;
		ReferenceData.PrimaryChildName = BoneDef.PrimaryChildName ? FName(BoneDef.PrimaryChildName) : NAME_None;
		ReferenceData.SecondaryChildName = BoneDef.SecondaryChildName ? FName(BoneDef.SecondaryChildName) : NAME_None;
		ReferenceData.RefComponentTransform = RefComponentTransforms[BoneIndex];

		const FVector BoneRefLocation = ReferenceData.RefComponentTransform.GetLocation();

		if (BoneDef.PrimaryChildName)
		{
			const int32 PrimaryChildIndex = RefSkeleton.FindBoneIndex(ReferenceData.PrimaryChildName);
			if (PrimaryChildIndex != INDEX_NONE)
			{
				ReferenceData.RefPrimaryDir = (RefComponentTransforms[PrimaryChildIndex].GetLocation() - BoneRefLocation).GetSafeNormal();
				ReferenceData.bHasPrimaryDir = !ReferenceData.RefPrimaryDir.IsNearlyZero();
			}
		}

		if (BoneDef.SecondaryChildName)
		{
			const int32 SecondaryChildIndex = RefSkeleton.FindBoneIndex(ReferenceData.SecondaryChildName);
			if (SecondaryChildIndex != INDEX_NONE)
			{
				ReferenceData.RefSecondaryDir = (RefComponentTransforms[SecondaryChildIndex].GetLocation() - BoneRefLocation).GetSafeNormal();
				ReferenceData.bHasSecondaryDir = !ReferenceData.RefSecondaryDir.IsNearlyZero();
			}
		}

		const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		if (ParentIndex != INDEX_NONE)
		{
			ReferenceData.RefParentDir = (BoneRefLocation - RefComponentTransforms[ParentIndex].GetLocation()).GetSafeNormal();
			ReferenceData.bHasParentDir = !ReferenceData.RefParentDir.IsNearlyZero();
		}

		if (bHasMannyLikeRefFacingAxes)
		{
			if (const FVector* FacingDir = MannyLikeRefFacingAxesLocal.Find(ReferenceData.BoneName))
			{
				ReferenceData.RefFacingDir = *FacingDir;
				ReferenceData.bHasFacingDir = !ReferenceData.RefFacingDir.IsNearlyZero();
			}
		}

		BoneReferenceDataByName.Add(BoneName, ReferenceData);
	}
}

void AMediaPipeTrackedSkeletonActor::ShowReferencePose()
{
	RefreshBodyMesh();

	if (PoseDriver)
	{
		for (const TPair<FName, FBoneReferenceData>& Pair : BoneReferenceDataByName)
		{
			PoseDriver->ResetBoneTransformByName(Pair.Key);
		}
		PoseDriver->RefreshBoneTransforms();
		PoseDriver->UpdateComponentToWorld();
	}

	if (Body)
	{
		const bool bShowMesh = !ShouldRenderBoxes();
		Body->SetVisibility(bShowMesh, true);
		if (bShowMesh)
		{
			Body->TickAnimation(0.0f, false);
			Body->RefreshBoneTransforms();
			Body->UpdateComponentToWorld();
		}
	}
	if (PoseDriver)
	{
		PoseDriver->SetVisibility(false, true);
	}
	UpdateDebugBoxRender(TMap<FName, FVector>(), true);
}

UMediaPipePoseTrackerComponent* AMediaPipeTrackedSkeletonActor::ResolveMediaPipeTracker() const
{
	return SourceActor ? SourceActor->FindComponentByClass<UMediaPipePoseTrackerComponent>() : nullptr;
}

bool AMediaPipeTrackedSkeletonActor::TryGetMediaPipeFrame(FMediaPipePoseFrame& OutFrame) const
{
	if (UMediaPipePoseTrackerComponent* Tracker = ResolveMediaPipeTracker())
	{
		return Tracker->GetLatestFrame(OutFrame) && OutFrame.bValid;
	}

	return false;
}

bool AMediaPipeTrackedSkeletonActor::TryBuildMediaPipeLocalJointPositions(TMap<FName, FVector>& OutJointPositions) const
{
	OutJointPositions.Reset();

	FMediaPipePoseFrame Frame;
	if (!TryGetMediaPipeFrame(Frame))
	{
		return false;
	}

	UMediaPipePoseTrackerComponent* Tracker = ResolveMediaPipeTracker();
	const float WorldScale = Tracker ? Tracker->WorldScale : ManualWorldScale;
	const bool bMirrorLandmarksLR = Tracker ? Tracker->bMirrorLandmarksLR : bManualMirrorLandmarksLR;

	FMediaPipeSolvedPose SolvedPose;
	const FMediaPipeSolvedPoseOptions SolvedOptions = MediaPipeSolvedPose::MakeDefaultOptions(WorldScale, bMirrorLandmarksLR);
	if (!MediaPipeSolvedPose::BuildLocal(Frame, SolvedOptions, SolvedPose))
	{
		return false;
	}
	const TStaticArray<FVector, MediaPipePoseLandmarkCount>& LandmarksLocal = SolvedPose.LandmarksLocal;

	const FVector LeftShoulder = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::LeftShoulder)];
	const FVector RightShoulder = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::RightShoulder)];
	const FVector LeftElbow = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::LeftElbow)];
	const FVector RightElbow = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::RightElbow)];
	const FVector LeftWrist = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::LeftWrist)];
	const FVector RightWrist = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::RightWrist)];
	const FVector LeftHip = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::LeftHip)];
	const FVector RightHip = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::RightHip)];
	const FVector LeftKnee = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::LeftKnee)];
	const FVector RightKnee = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::RightKnee)];
	const FVector LeftAnkle = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::LeftAnkle)];
	const FVector RightAnkle = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::RightAnkle)];
	const FVector LeftHeel = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::LeftHeel)];
	const FVector RightHeel = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::RightHeel)];
	const FVector LeftFootIndex = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::LeftFootIndex)];
	const FVector RightFootIndex = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::RightFootIndex)];
	const FVector Nose = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::Nose)];
	const FVector LeftEar = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::LeftEar)];
	const FVector RightEar = LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::RightEar)];

	const FVector Pelvis = (LeftHip + RightHip) * 0.5f;
	const FVector Chest = (LeftShoulder + RightShoulder) * 0.5f;
	const FVector Spine = FMath::Lerp(Pelvis, Chest, 0.45f);
	const FVector EarMid = (LeftEar + RightEar) * 0.5f;
	const FVector HeadAnchor = (EarMid - Chest).SizeSquared() > KINDA_SMALL_NUMBER ? EarMid : Nose;
	FVector HeadUp = (HeadAnchor - Chest).GetSafeNormal();
	if (HeadUp.IsNearlyZero())
	{
		HeadUp = FVector::UpVector;
	}

	const float HeadExtension = FMath::Max((HeadAnchor - Chest).Size() * 0.18f, 6.0f);
	const FVector Head = HeadAnchor + (HeadUp * HeadExtension);
	const FVector Neck = FMath::Lerp(Chest, HeadAnchor, 0.35f);
	const FVector LeftCollar = FMath::Lerp(Chest, LeftShoulder, 0.35f);
	const FVector RightCollar = FMath::Lerp(Chest, RightShoulder, 0.35f);
	const auto BuildHandFromForearm = [](const FVector& Elbow, const FVector& Wrist, const float MinLengthCm)
	{
		const FVector Forearm = Wrist - Elbow;
		const float ForearmLength = Forearm.Size();
		const FVector ForearmDir = ForearmLength > KINDA_SMALL_NUMBER ? (Forearm / ForearmLength) : FVector::ZeroVector;
		if (ForearmDir.IsNearlyZero())
		{
			return Wrist;
		}

		const float HandLength = FMath::Max(ForearmLength * 0.35f, MinLengthCm);
		return Wrist + (ForearmDir * HandLength);
	};
	const FVector LeftHand = BuildHandFromForearm(LeftElbow, LeftWrist, 4.0f);
	const FVector RightHand = BuildHandFromForearm(RightElbow, RightWrist, 4.0f);

	OutJointPositions.Add(FName(TEXT("pelvis")), Pelvis);
	OutJointPositions.Add(FName(TEXT("left_hip")), LeftHip);
	OutJointPositions.Add(FName(TEXT("left_knee")), LeftKnee);
	OutJointPositions.Add(FName(TEXT("left_ankle")), LeftAnkle);
	OutJointPositions.Add(FName(TEXT("left_big_toe")), LeftFootIndex);
	OutJointPositions.Add(FName(TEXT("left_heel")), LeftHeel);
	OutJointPositions.Add(FName(TEXT("right_hip")), RightHip);
	OutJointPositions.Add(FName(TEXT("right_knee")), RightKnee);
	OutJointPositions.Add(FName(TEXT("right_ankle")), RightAnkle);
	OutJointPositions.Add(FName(TEXT("right_big_toe")), RightFootIndex);
	OutJointPositions.Add(FName(TEXT("right_heel")), RightHeel);
	OutJointPositions.Add(FName(TEXT("spine")), Spine);
	OutJointPositions.Add(FName(TEXT("chest")), Chest);
	OutJointPositions.Add(FName(TEXT("neck")), Neck);
	OutJointPositions.Add(FName(TEXT("head")), Head);
	OutJointPositions.Add(FName(TEXT("nose")), Nose);
	OutJointPositions.Add(FName(TEXT("left_ear")), LeftEar);
	OutJointPositions.Add(FName(TEXT("right_ear")), RightEar);
	OutJointPositions.Add(FName(TEXT("left_collar")), LeftCollar);
	OutJointPositions.Add(FName(TEXT("left_shoulder")), LeftShoulder);
	OutJointPositions.Add(FName(TEXT("left_elbow")), LeftElbow);
	OutJointPositions.Add(FName(TEXT("left_wrist")), LeftWrist);
	OutJointPositions.Add(FName(TEXT("left_hand")), LeftHand);
	OutJointPositions.Add(FName(TEXT("right_collar")), RightCollar);
	OutJointPositions.Add(FName(TEXT("right_shoulder")), RightShoulder);
	OutJointPositions.Add(FName(TEXT("right_elbow")), RightElbow);
	OutJointPositions.Add(FName(TEXT("right_wrist")), RightWrist);
	OutJointPositions.Add(FName(TEXT("right_hand")), RightHand);
	AddMannyLikeJointAliases(
		OutJointPositions,
		Pelvis,
		Chest,
		HeadAnchor,
		Head,
		LeftCollar,
		RightCollar,
		LeftShoulder,
		RightShoulder,
		LeftElbow,
		RightElbow,
		LeftWrist,
		RightWrist,
		LeftHip,
		RightHip,
		LeftKnee,
		RightKnee,
		LeftAnkle,
		RightAnkle,
		LeftFootIndex,
		RightFootIndex);

	FVector RootOffsetLocal = FVector::ZeroVector;
	if (bCenterPoseOnFeet)
	{
		TArray<FVector> SupportPoints;
		SupportPoints.Reserve(4);
		SupportPoints.Add(LeftFootIndex);
		SupportPoints.Add(RightFootIndex);
		SupportPoints.Add(LeftHeel);
		SupportPoints.Add(RightHeel);
		TryAveragePoints(SupportPoints, RootOffsetLocal);
	}

	for (TPair<FName, FVector>& Pair : OutJointPositions)
	{
		Pair.Value = (Pair.Value - RootOffsetLocal) + PoseLocalOffset;
	}
	OutJointPositions.Add(FName(TEXT("root")), PoseLocalOffset);
	OutJointPositions.Add(FName(TEXT("ik_foot_root")), PoseLocalOffset);
	OutJointPositions.Add(FName(TEXT("ik_foot_l")), (LeftAnkle - RootOffsetLocal) + PoseLocalOffset);
	OutJointPositions.Add(FName(TEXT("ik_foot_r")), (RightAnkle - RootOffsetLocal) + PoseLocalOffset);
	OutJointPositions.Add(FName(TEXT("ik_hand_root")), (Chest - RootOffsetLocal) + PoseLocalOffset);
	OutJointPositions.Add(FName(TEXT("ik_hand_gun")), (RightWrist - RootOffsetLocal) + PoseLocalOffset);
	OutJointPositions.Add(FName(TEXT("ik_hand_l")), (LeftWrist - RootOffsetLocal) + PoseLocalOffset);
	OutJointPositions.Add(FName(TEXT("ik_hand_r")), (RightWrist - RootOffsetLocal) + PoseLocalOffset);

	return OutJointPositions.Num() > 0;
}

bool AMediaPipeTrackedSkeletonActor::ApplyPoseLocalJointPositions(const TMap<FName, FVector>& JointPositions)
{
	RefreshBodyMesh();
	if (!PoseDriver || BoneReferenceDataByName.Num() <= 0)
	{
		return false;
	}

	bool bAppliedAny = false;
	int32 ActiveBoneCount = 0;
	const FTrackedRigBoneDef* ActiveBoneDefs = GetActiveTrackedRigBones(RigProfile, ActiveBoneCount);
	for (int32 BoneDefIndex = 0; BoneDefIndex < ActiveBoneCount; ++BoneDefIndex)
	{
		const FTrackedRigBoneDef& BoneDef = ActiveBoneDefs[BoneDefIndex];
		const FName BoneName(BoneDef.BoneName);
		const FBoneReferenceData* RefData = BoneReferenceDataByName.Find(BoneName);
		if (!RefData)
		{
			continue;
		}

		const FVector* BonePos = JointPositions.Find(BoneName);
		if (!BonePos)
		{
			PoseDriver->ResetBoneTransformByName(BoneName);
			continue;
		}

		FTransform BoneTransform = RefData->RefComponentTransform;
		BoneTransform.SetTranslation(*BonePos);

		bool bHasRotation = false;
		const FQuat BoneRotation = BuildBoneComponentRotation(*RefData, JointPositions, bHasRotation);
		if (bHasRotation)
		{
			BoneTransform.SetRotation(BoneRotation);
		}

		PoseDriver->SetBoneTransformByName(BoneName, BoneTransform, EBoneSpaces::ComponentSpace);
		bAppliedAny = true;
	}

	PoseDriver->RefreshBoneTransforms();
	PoseDriver->UpdateComponentToWorld();

	if (Body)
	{
		Body->TickAnimation(0.0f, false);
		Body->RefreshBoneTransforms();
		Body->UpdateComponentToWorld();
	}

	return bAppliedAny;
}

FQuat AMediaPipeTrackedSkeletonActor::BuildBoneComponentRotation(
	const FBoneReferenceData& RefData,
	const TMap<FName, FVector>& JointPositions,
	bool& bOutValid) const
{
	bOutValid = false;

	const FVector* BonePos = JointPositions.Find(RefData.BoneName);
	if (!BonePos)
	{
		return RefData.RefComponentTransform.GetRotation();
	}

	FQuat BoneRotation = RefData.RefComponentTransform.GetRotation();
	auto ApplyTwistAroundAxis = [&](const FVector& CurrentAxis) -> bool
	{
		if (!RefData.bHasFacingDir || CurrentAxis.IsNearlyZero())
		{
			return false;
		}

		FVector CurrentFacingDir = FVector::ZeroVector;
		if (!TryBuildTrackedFacingDir(RefData.BoneName, JointPositions, CurrentFacingDir))
		{
			return false;
		}

		const FVector RotatedRefFacingDir = BoneRotation.RotateVector(RefData.RefFacingDir);
		const FVector RefPlaneDir = FVector::VectorPlaneProject(RotatedRefFacingDir, CurrentAxis).GetSafeNormal();
		const FVector CurrentPlaneDir = FVector::VectorPlaneProject(CurrentFacingDir, CurrentAxis).GetSafeNormal();
		if (RefPlaneDir.IsNearlyZero() || CurrentPlaneDir.IsNearlyZero())
		{
			return false;
		}

		const float SignedAngleRadians = FMath::Atan2(
			FVector::DotProduct(CurrentAxis, FVector::CrossProduct(RefPlaneDir, CurrentPlaneDir)),
			FVector::DotProduct(RefPlaneDir, CurrentPlaneDir));
		BoneRotation = FQuat(CurrentAxis, SignedAngleRadians) * BoneRotation;
		return true;
	};

	if (RefData.bHasPrimaryDir)
	{
		if (const FVector* PrimaryChildPos = JointPositions.Find(RefData.PrimaryChildName))
		{
			const FVector CurrentPrimaryDir = (*PrimaryChildPos - *BonePos).GetSafeNormal();
			if (!CurrentPrimaryDir.IsNearlyZero())
			{
				const FQuat AlignQuat = FQuat::FindBetweenNormals(RefData.RefPrimaryDir, CurrentPrimaryDir);
				BoneRotation = AlignQuat * RefData.RefComponentTransform.GetRotation();

				bool bAppliedTwist = ApplyTwistAroundAxis(CurrentPrimaryDir);
				if (!bAppliedTwist && RefData.bHasSecondaryDir)
				{
					if (const FVector* SecondaryChildPos = JointPositions.Find(RefData.SecondaryChildName))
					{
						const FVector CurrentSecondaryDir = (*SecondaryChildPos - *BonePos).GetSafeNormal();
						const FVector RotatedRefSecondaryDir = AlignQuat.RotateVector(RefData.RefSecondaryDir);
						const FVector RefPlaneDir = FVector::VectorPlaneProject(RotatedRefSecondaryDir, CurrentPrimaryDir).GetSafeNormal();
						const FVector CurrentPlaneDir = FVector::VectorPlaneProject(CurrentSecondaryDir, CurrentPrimaryDir).GetSafeNormal();
						if (!RefPlaneDir.IsNearlyZero() && !CurrentPlaneDir.IsNearlyZero())
						{
							const float SignedAngleRadians = FMath::Atan2(
								FVector::DotProduct(CurrentPrimaryDir, FVector::CrossProduct(RefPlaneDir, CurrentPlaneDir)),
								FVector::DotProduct(RefPlaneDir, CurrentPlaneDir));
							BoneRotation = FQuat(CurrentPrimaryDir, SignedAngleRadians) * BoneRotation;
						}
					}
				}

				bOutValid = true;
			}
		}
	}

	if (!bOutValid && RefData.bHasParentDir)
	{
		if (const FVector* ParentPos = JointPositions.Find(RefData.ParentName))
		{
			const FVector CurrentParentDir = (*BonePos - *ParentPos).GetSafeNormal();
			if (!CurrentParentDir.IsNearlyZero())
			{
				const FQuat AlignQuat = FQuat::FindBetweenNormals(RefData.RefParentDir, CurrentParentDir);
				BoneRotation = AlignQuat * RefData.RefComponentTransform.GetRotation();
				ApplyTwistAroundAxis(CurrentParentDir);
				bOutValid = true;
			}
		}
	}

	return BoneRotation;
}

bool AMediaPipeTrackedSkeletonActor::ShouldRenderBoxes() const
{
	return IsBoxRenderRig(RigProfile);
}
