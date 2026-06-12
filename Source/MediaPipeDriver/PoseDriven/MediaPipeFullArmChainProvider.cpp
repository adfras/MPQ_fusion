#include "MediaPipeFullArmChainProvider.h"

#include "MediaPipePoseLog.h"

#include "Engine/Engine.h"
#include "Features/IModularFeatures.h"
#include "HAL/PlatformTime.h"
#include "IOpenXRExtensionPlugin.h"
#include "IXRTrackingSystem.h"
#include "Misc/ScopeLock.h"
#include "OpenXRCore.h"

namespace
{
FCriticalSection GMediaPipeFullArmChainSnapshotLock;
FMediaPipeFullArmChainSnapshot GLatestMediaPipeFullArmChainSnapshot;
TUniquePtr<IOpenXRExtensionPlugin> GMediaPipeFullArmChainProvider;

FString CompactVectorString(const FVector& Vector)
{
	return FString::Printf(TEXT("(%.1f,%.1f,%.1f)"), Vector.X, Vector.Y, Vector.Z);
}

bool HasFlag(const XrSpaceLocationFlags Flags, const XrSpaceLocationFlags Flag)
{
	return (Flags & Flag) != 0;
}

FMediaPipeFullArmChainJointSnapshot BuildJointSnapshot(
	const XrBodyJointLocationFB& JointLocation,
	const FTransform& TrackingToWorldTransform,
	const float WorldToMetersScale)
{
	FMediaPipeFullArmChainJointSnapshot Joint;
	Joint.bPositionValid = HasFlag(JointLocation.locationFlags, XR_SPACE_LOCATION_POSITION_VALID_BIT) ? 1 : 0;
	Joint.bOrientationValid = HasFlag(JointLocation.locationFlags, XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) ? 1 : 0;
	Joint.bValid = (Joint.bPositionValid != 0 || Joint.bOrientationValid != 0) ? 1 : 0;
	if (Joint.bValid == 0)
	{
		return Joint;
	}

	FTransform TrackingTransform = FTransform::Identity;
	if (Joint.bOrientationValid != 0)
	{
		TrackingTransform.SetRotation(ToFQuat(JointLocation.pose.orientation));
	}
	if (Joint.bPositionValid != 0)
	{
		TrackingTransform.SetLocation(ToFVector(JointLocation.pose.position, WorldToMetersScale));
	}
	Joint.WorldTransform = TrackingTransform * TrackingToWorldTransform;
	return Joint;
}

void FillSideSnapshot(
	FMediaPipeFullArmChainSideSnapshot& OutSide,
	const XrBodyJointLocationFB* JointLocations,
	const bool bIsLeft,
	const bool bBodyActive,
	const float Confidence,
	const double TimestampSeconds,
	const FTransform& TrackingToWorldTransform,
	const float WorldToMetersScale)
{
	const XrBodyJointFB ShoulderJoint = bIsLeft ? XR_BODY_JOINT_LEFT_SHOULDER_FB : XR_BODY_JOINT_RIGHT_SHOULDER_FB;
	const XrBodyJointFB UpperArmJoint = bIsLeft ? XR_BODY_JOINT_LEFT_ARM_UPPER_FB : XR_BODY_JOINT_RIGHT_ARM_UPPER_FB;
	const XrBodyJointFB LowerArmJoint = bIsLeft ? XR_BODY_JOINT_LEFT_ARM_LOWER_FB : XR_BODY_JOINT_RIGHT_ARM_LOWER_FB;
	const XrBodyJointFB WristJoint = bIsLeft ? XR_BODY_JOINT_LEFT_HAND_WRIST_FB : XR_BODY_JOINT_RIGHT_HAND_WRIST_FB;
	const XrBodyJointFB PalmJoint = bIsLeft ? XR_BODY_JOINT_LEFT_HAND_PALM_FB : XR_BODY_JOINT_RIGHT_HAND_PALM_FB;

	OutSide.Shoulder = BuildJointSnapshot(
		JointLocations[static_cast<int32>(ShoulderJoint)],
		TrackingToWorldTransform,
		WorldToMetersScale);
	OutSide.UpperArm = BuildJointSnapshot(
		JointLocations[static_cast<int32>(UpperArmJoint)],
		TrackingToWorldTransform,
		WorldToMetersScale);
	OutSide.LowerArm = BuildJointSnapshot(
		JointLocations[static_cast<int32>(LowerArmJoint)],
		TrackingToWorldTransform,
		WorldToMetersScale);

	const FMediaPipeFullArmChainJointSnapshot Wrist = BuildJointSnapshot(
		JointLocations[static_cast<int32>(WristJoint)],
		TrackingToWorldTransform,
		WorldToMetersScale);
	const FMediaPipeFullArmChainJointSnapshot Palm = BuildJointSnapshot(
		JointLocations[static_cast<int32>(PalmJoint)],
		TrackingToWorldTransform,
		WorldToMetersScale);
	OutSide.WristOrPalm = Wrist.bPositionValid != 0 ? Wrist : Palm;
	OutSide.bWristOrPalmUsesPalm = Wrist.bPositionValid != 0 ? 0 : (Palm.bPositionValid != 0 ? 1 : 0);
	OutSide.Confidence = Confidence;
	OutSide.TimestampSeconds = TimestampSeconds;
	OutSide.bActive = bBodyActive && OutSide.HasRequiredPositionChain() ? 1 : 0;
}

void PublishInactiveSnapshot()
{
	FScopeLock Lock(&GMediaPipeFullArmChainSnapshotLock);
	++GLatestMediaPipeFullArmChainSnapshot.Sequence;
	const uint32 Sequence = GLatestMediaPipeFullArmChainSnapshot.Sequence;
	GLatestMediaPipeFullArmChainSnapshot.Reset();
	GLatestMediaPipeFullArmChainSnapshot.Sequence = Sequence;
}

class FMediaPipeOpenXRFullArmChainProvider : public IOpenXRExtensionPlugin
{
public:
	FMediaPipeOpenXRFullArmChainProvider()
	{
		RegisterOpenXRExtensionModularFeature();
	}

	virtual ~FMediaPipeOpenXRFullArmChainProvider() override
	{
		DestroyBodyTracker();
		UnregisterOpenXRExtensionModularFeature();
	}

	virtual FString GetDisplayName() override
	{
		return TEXT("TestingKit3FullArmChainOpenXRBodyTracking");
	}

	virtual bool GetOptionalExtensions(TArray<const ANSICHAR*>& OutExtensions) override
	{
		OutExtensions.Add(XR_FB_BODY_TRACKING_EXTENSION_NAME);
		return true;
	}

	virtual const void* OnGetSystem(XrInstance InInstance, const void* InNext) override
	{
		Instance = InInstance;
		bFunctionsResolved =
			XR_SUCCEEDED(xrGetInstanceProcAddr(Instance, "xrCreateBodyTrackerFB", reinterpret_cast<PFN_xrVoidFunction*>(&xrCreateBodyTrackerFB))) &&
			XR_SUCCEEDED(xrGetInstanceProcAddr(Instance, "xrDestroyBodyTrackerFB", reinterpret_cast<PFN_xrVoidFunction*>(&xrDestroyBodyTrackerFB))) &&
			XR_SUCCEEDED(xrGetInstanceProcAddr(Instance, "xrLocateBodyJointsFB", reinterpret_cast<PFN_xrVoidFunction*>(&xrLocateBodyJointsFB)));
		return InNext;
	}

	virtual const void* OnCreateSession(XrInstance InInstance, XrSystemId InSystem, const void* InNext) override
	{
		Instance = InInstance;
		System = InSystem;
		bBodyTrackingSupported = false;

		if (bFunctionsResolved)
		{
			XrSystemBodyTrackingPropertiesFB BodyTrackingProperties{XR_TYPE_SYSTEM_BODY_TRACKING_PROPERTIES_FB};
			XrSystemProperties SystemProperties{XR_TYPE_SYSTEM_PROPERTIES};
			SystemProperties.next = &BodyTrackingProperties;
			if (XR_SUCCEEDED(xrGetSystemProperties(Instance, System, &SystemProperties)))
			{
				bBodyTrackingSupported = BodyTrackingProperties.supportsBodyTracking == XR_TRUE;
			}
		}

		return InNext;
	}

	virtual const void* OnBeginSession(XrSession InSession, const void* InNext) override
	{
		Session = InSession;
		CreateBodyTracker();
		return InNext;
	}

	virtual void OnDestroySession(XrSession InSession) override
	{
		DestroyBodyTracker();
		Session = XR_NULL_HANDLE;
		PublishInactiveSnapshot();
	}

	virtual void UpdateDeviceLocations(XrSession InSession, XrTime DisplayTime, XrSpace TrackingSpace) override
	{
		if (!bBodyTrackingSupported ||
			BodyTracker == XR_NULL_HANDLE ||
			xrLocateBodyJointsFB == nullptr ||
			InSession == XR_NULL_HANDLE ||
			TrackingSpace == XR_NULL_HANDLE)
		{
			return;
		}

		IXRTrackingSystem* XRTrackingSystem = nullptr;
		if (GEngine && GEngine->XRSystem.IsValid())
		{
			XRTrackingSystem = GEngine->XRSystem.Get();
		}

		const float WorldToMetersScale = XRTrackingSystem ? XRTrackingSystem->GetWorldToMetersScale() : 100.0f;
		const FTransform TrackingToWorldTransform = XRTrackingSystem
			? XRTrackingSystem->GetTrackingToWorldTransform()
			: FTransform::Identity;

		XrBodyJointLocationFB JointLocations[XR_BODY_JOINT_COUNT_FB];
		FMemory::Memzero(JointLocations);

		XrBodyJointLocationsFB Locations{XR_TYPE_BODY_JOINT_LOCATIONS_FB};
		Locations.jointCount = XR_BODY_JOINT_COUNT_FB;
		Locations.jointLocations = JointLocations;

		XrBodyJointsLocateInfoFB LocateInfo{XR_TYPE_BODY_JOINTS_LOCATE_INFO_FB};
		LocateInfo.baseSpace = TrackingSpace;
		LocateInfo.time = DisplayTime;

		const XrResult Result = xrLocateBodyJointsFB(BodyTracker, &LocateInfo, &Locations);
		if (!XR_SUCCEEDED(Result))
		{
			PublishInactiveSnapshot();
			return;
		}

		FMediaPipeFullArmChainSnapshot Snapshot;
		Snapshot.Source = EMediaPipeFullArmChainSource::OpenXRBodyTracking;
		Snapshot.bActive = Locations.isActive == XR_TRUE ? 1 : 0;
		Snapshot.Confidence = Locations.confidence;
		Snapshot.TimestampSeconds = FPlatformTime::Seconds();

		const bool bBodyActive = Snapshot.bActive != 0;
		Snapshot.Hips = BuildJointSnapshot(
			JointLocations[static_cast<int32>(XR_BODY_JOINT_HIPS_FB)],
			TrackingToWorldTransform,
			WorldToMetersScale);
		FillSideSnapshot(
			Snapshot.Left,
			JointLocations,
			true,
			bBodyActive,
			Snapshot.Confidence,
			Snapshot.TimestampSeconds,
			TrackingToWorldTransform,
			WorldToMetersScale);
		FillSideSnapshot(
			Snapshot.Right,
			JointLocations,
			false,
			bBodyActive,
			Snapshot.Confidence,
			Snapshot.TimestampSeconds,
			TrackingToWorldTransform,
			WorldToMetersScale);

		FScopeLock Lock(&GMediaPipeFullArmChainSnapshotLock);
		Snapshot.Sequence = GLatestMediaPipeFullArmChainSnapshot.Sequence + 1;
		GLatestMediaPipeFullArmChainSnapshot = Snapshot;
	}

private:
	void CreateBodyTracker()
	{
		if (!bBodyTrackingSupported ||
			BodyTracker != XR_NULL_HANDLE ||
			Session == XR_NULL_HANDLE ||
			xrCreateBodyTrackerFB == nullptr)
		{
			return;
		}

		XrBodyTrackerCreateInfoFB CreateInfo{XR_TYPE_BODY_TRACKER_CREATE_INFO_FB};
		CreateInfo.bodyJointSet = XR_BODY_JOINT_SET_DEFAULT_FB;
		const XrResult Result = xrCreateBodyTrackerFB(Session, &CreateInfo, &BodyTracker);
		if (!XR_SUCCEEDED(Result))
		{
			BodyTracker = XR_NULL_HANDLE;
			UE_LOG(LogMediaPipePose, Warning, TEXT("mp.MetaHumanFullArmChain: failed to create XR_FB_body_tracking body tracker result=%d."), static_cast<int32>(Result));
		}
	}

	void DestroyBodyTracker()
	{
		if (BodyTracker != XR_NULL_HANDLE && xrDestroyBodyTrackerFB != nullptr)
		{
			xrDestroyBodyTrackerFB(BodyTracker);
		}
		BodyTracker = XR_NULL_HANDLE;
	}

	XrInstance Instance = XR_NULL_HANDLE;
	XrSystemId System = XR_NULL_SYSTEM_ID;
	XrSession Session = XR_NULL_HANDLE;
	XrBodyTrackerFB BodyTracker = XR_NULL_HANDLE;
	bool bFunctionsResolved = false;
	bool bBodyTrackingSupported = false;
	PFN_xrCreateBodyTrackerFB xrCreateBodyTrackerFB = nullptr;
	PFN_xrDestroyBodyTrackerFB xrDestroyBodyTrackerFB = nullptr;
	PFN_xrLocateBodyJointsFB xrLocateBodyJointsFB = nullptr;
};
} // namespace

void StartupMediaPipeFullArmChainProvider()
{
	if (!GMediaPipeFullArmChainProvider.IsValid())
	{
		GMediaPipeFullArmChainProvider = MakeUnique<FMediaPipeOpenXRFullArmChainProvider>();
	}
}

void ShutdownMediaPipeFullArmChainProvider()
{
	GMediaPipeFullArmChainProvider.Reset();
	PublishInactiveSnapshot();
}

bool ReadLatestMediaPipeFullArmChainSnapshot(FMediaPipeFullArmChainSnapshot& OutSnapshot)
{
	FScopeLock Lock(&GMediaPipeFullArmChainSnapshotLock);
	OutSnapshot = GLatestMediaPipeFullArmChainSnapshot;
	return OutSnapshot.Source != EMediaPipeFullArmChainSource::None && OutSnapshot.Sequence > 0;
}

float ComputeMediaPipeArmChainElbowBendDegrees(
	const FVector& ShoulderWorld,
	const FVector& ElbowWorld,
	const FVector& WristWorld)
{
	const FVector UpperDir = (ElbowWorld - ShoulderWorld).GetSafeNormal();
	const FVector LowerDir = (WristWorld - ElbowWorld).GetSafeNormal();
	if (UpperDir.IsNearlyZero() || LowerDir.IsNearlyZero())
	{
		return 0.0f;
	}

	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(UpperDir, LowerDir), -1.0f, 1.0f)));
}

FString FormatMediaPipeMetaHumanFullArmChainLog(const FMediaPipeMetaHumanFullArmChainLogInput& Input)
{
	return FString::Printf(
		TEXT("mp.MetaHumanFullArmChain: profile=%s actor=%s side=%s armSource=FullArmChain chainActive=%d shoulderValid=%d upperArmValid=%d lowerArmValid=%d wristOrPalmValid=%d mediaPipeArmUsed=%d questHandUsed=%d targetReachCm=%.1f elbowBendDeg=%.1f handWorld=%s confidence=%.2f chainAge=%.3f wristOrPalmSource=%s"),
		Input.ProfileId.IsNone() ? TEXT("Unknown") : *Input.ProfileId.ToString(),
		*Input.TargetActorName.ToString(),
		Input.bIsLeft ? TEXT("L") : TEXT("R"),
		Input.bChainActive ? 1 : 0,
		Input.bShoulderValid ? 1 : 0,
		Input.bUpperArmValid ? 1 : 0,
		Input.bLowerArmValid ? 1 : 0,
		Input.bWristOrPalmValid ? 1 : 0,
		Input.bMediaPipeArmUsed ? 1 : 0,
		Input.bQuestHandUsed ? 1 : 0,
		Input.TargetReachCm,
		Input.ElbowBendDeg,
		*CompactVectorString(Input.HandWorld),
		Input.Confidence,
		Input.ChainAgeSeconds,
		Input.bWristOrPalmUsesPalm ? TEXT("Palm") : TEXT("Wrist"));
}

FString FormatMediaPipeWallaceFullArmChainLog(const FMediaPipeWallaceFullArmChainLogInput& Input)
{
	FMediaPipeMetaHumanFullArmChainLogInput CompatibilityInput = Input;
	if (CompatibilityInput.ProfileId.IsNone())
	{
		CompatibilityInput.ProfileId = FName(TEXT("Wallace"));
	}
	return FString::Printf(
		TEXT("mp.WallaceFullArmChain: actor=%s side=%s armSource=FullArmChain chainActive=%d shoulderValid=%d upperArmValid=%d lowerArmValid=%d wristOrPalmValid=%d mediaPipeArmUsed=%d questHandUsed=%d targetReachCm=%.1f elbowBendDeg=%.1f handWorld=%s confidence=%.2f chainAge=%.3f wristOrPalmSource=%s"),
		*CompatibilityInput.TargetActorName.ToString(),
		CompatibilityInput.bIsLeft ? TEXT("L") : TEXT("R"),
		CompatibilityInput.bChainActive ? 1 : 0,
		CompatibilityInput.bShoulderValid ? 1 : 0,
		CompatibilityInput.bUpperArmValid ? 1 : 0,
		CompatibilityInput.bLowerArmValid ? 1 : 0,
		CompatibilityInput.bWristOrPalmValid ? 1 : 0,
		CompatibilityInput.bMediaPipeArmUsed ? 1 : 0,
		CompatibilityInput.bQuestHandUsed ? 1 : 0,
		CompatibilityInput.TargetReachCm,
		CompatibilityInput.ElbowBendDeg,
		*CompactVectorString(CompatibilityInput.HandWorld),
		CompatibilityInput.Confidence,
		CompatibilityInput.ChainAgeSeconds,
		CompatibilityInput.bWristOrPalmUsesPalm ? TEXT("Palm") : TEXT("Wrist"));
}
