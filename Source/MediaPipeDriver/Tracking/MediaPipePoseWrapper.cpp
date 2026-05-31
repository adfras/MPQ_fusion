#include "MediaPipePoseWrapper.h"
#include "MediaPipePoseLog.h"

#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/PlatformMisc.h"

namespace
{
	static const TStaticArray<FVector2f, MediaPipePoseLandmarkCount>& GetMockBasePositions()
	{
		static const TStaticArray<FVector2f, MediaPipePoseLandmarkCount> Base = {
			FVector2f(0.50f, 0.20f), // Nose
			FVector2f(0.47f, 0.18f), // LeftEyeInner
			FVector2f(0.46f, 0.18f), // LeftEye
			FVector2f(0.45f, 0.18f), // LeftEyeOuter
			FVector2f(0.53f, 0.18f), // RightEyeInner
			FVector2f(0.54f, 0.18f), // RightEye
			FVector2f(0.55f, 0.18f), // RightEyeOuter
			FVector2f(0.42f, 0.20f), // LeftEar
			FVector2f(0.58f, 0.20f), // RightEar
			FVector2f(0.47f, 0.23f), // MouthLeft
			FVector2f(0.53f, 0.23f), // MouthRight
			FVector2f(0.42f, 0.32f), // LeftShoulder
			FVector2f(0.58f, 0.32f), // RightShoulder
			FVector2f(0.36f, 0.45f), // LeftElbow
			FVector2f(0.64f, 0.45f), // RightElbow
			FVector2f(0.32f, 0.58f), // LeftWrist
			FVector2f(0.68f, 0.58f), // RightWrist
			FVector2f(0.30f, 0.60f), // LeftPinky
			FVector2f(0.70f, 0.60f), // RightPinky
			FVector2f(0.31f, 0.59f), // LeftIndex
			FVector2f(0.69f, 0.59f), // RightIndex
			FVector2f(0.33f, 0.57f), // LeftThumb
			FVector2f(0.67f, 0.57f), // RightThumb
			FVector2f(0.46f, 0.60f), // LeftHip
			FVector2f(0.54f, 0.60f), // RightHip
			FVector2f(0.45f, 0.78f), // LeftKnee
			FVector2f(0.55f, 0.78f), // RightKnee
			FVector2f(0.45f, 0.94f), // LeftAnkle
			FVector2f(0.55f, 0.94f), // RightAnkle
			FVector2f(0.44f, 0.97f), // LeftHeel
			FVector2f(0.56f, 0.97f), // RightHeel
			FVector2f(0.47f, 0.99f), // LeftFootIndex
			FVector2f(0.53f, 0.99f)  // RightFootIndex
		};

		return Base;
	}
}

FMediaPipePoseWrapper::FMediaPipePoseWrapper() = default;

FMediaPipePoseWrapper::~FMediaPipePoseWrapper()
{
	Shutdown();
	Unload();
}

bool FMediaPipePoseWrapper::Load(const FString& DllPath)
{
	if (IsLoaded())
	{
		return true;
	}

	FString ResolvedPath = DllPath;
	FPaths::NormalizeFilename(ResolvedPath);
	ResolvedPath = FPaths::ConvertRelativePathToFull(ResolvedPath);
	FPaths::CollapseRelativeDirectories(ResolvedPath);
	FPaths::NormalizeFilename(ResolvedPath);
	FPaths::MakePlatformFilename(ResolvedPath);

	if (!FPaths::FileExists(ResolvedPath))
	{
		UE_LOG(LogMediaPipePose, Error, TEXT("Wrapper DLL not found: %s"), *ResolvedPath);
		return false;
	}

	const FString DllDir = FPaths::GetPath(ResolvedPath);
	FPlatformProcess::PushDllDirectory(*DllDir);
	ON_SCOPE_EXIT
	{
		FPlatformProcess::PopDllDirectory(*DllDir);
	};

	LoadedPath = ResolvedPath;
	DllHandle = FPlatformProcess::GetDllHandle(*ResolvedPath);
	if (!DllHandle)
	{
		const uint32 ErrorCode = FPlatformMisc::GetLastError();
		TCHAR ErrorBuffer[1024];
		FPlatformMisc::GetSystemErrorMessage(ErrorBuffer, UE_ARRAY_COUNT(ErrorBuffer), ErrorCode);
		UE_LOG(LogMediaPipePose, Error, TEXT("Failed to load MediaPipe wrapper DLL: %s (0x%08x) %s"), *ResolvedPath, ErrorCode, ErrorBuffer);
		return false;
	}

	InitFn = reinterpret_cast<MP_InitFn>(FPlatformProcess::GetDllExport(DllHandle, TEXT("MP_Init")));
	Init2Fn = reinterpret_cast<MP_Init2Fn>(FPlatformProcess::GetDllExport(DllHandle, TEXT("MP_Init2")));
	Init3Fn = reinterpret_cast<MP_Init3Fn>(FPlatformProcess::GetDllExport(DllHandle, TEXT("MP_Init3")));
	Init4Fn = reinterpret_cast<MP_Init4Fn>(FPlatformProcess::GetDllExport(DllHandle, TEXT("MP_Init4")));
	ProcessFrameFn = reinterpret_cast<MP_ProcessFrameFn>(FPlatformProcess::GetDllExport(DllHandle, TEXT("MP_ProcessFrame")));
	GetLandmarksFn = reinterpret_cast<MP_GetLandmarksFn>(FPlatformProcess::GetDllExport(DllHandle, TEXT("MP_GetLandmarks")));
	GetHandsFn = reinterpret_cast<MP_GetHandLandmarksFn>(FPlatformProcess::GetDllExport(DllHandle, TEXT("MP_GetHandLandmarks")));
	GetFaceFn = reinterpret_cast<MP_GetFacePoseFn>(FPlatformProcess::GetDllExport(DllHandle, TEXT("MP_GetFacePose")));
	ShutdownFn = reinterpret_cast<MP_ShutdownFn>(FPlatformProcess::GetDllExport(DllHandle, TEXT("MP_Shutdown")));

	if (!InitFn || !ProcessFrameFn || !GetLandmarksFn || !ShutdownFn)
	{
		UE_LOG(LogMediaPipePose, Error, TEXT("Wrapper DLL is missing required exports (MP_Init/MP_ProcessFrame/MP_GetLandmarks/MP_Shutdown)."));
		Unload();
		return false;
	}

	return true;
}

void FMediaPipePoseWrapper::Unload()
{
	bReady = false;
	bMockMode = false;
	bHandsEnabled = false;
	bFaceEnabled = false;
	InitFn = nullptr;
	Init2Fn = nullptr;
	Init3Fn = nullptr;
	Init4Fn = nullptr;
	ProcessFrameFn = nullptr;
	GetLandmarksFn = nullptr;
	GetHandsFn = nullptr;
	GetFaceFn = nullptr;
	ShutdownFn = nullptr;

	if (DllHandle)
	{
#if WITH_EDITOR
		// The MediaPipe native runtime can crash Unreal on repeated FreeLibrary calls during editor
		// webcam restarts. Keep the DLL resident for the editor process; Windows releases it on exit.
		UE_LOG(LogMediaPipePose, Verbose, TEXT("Keeping MediaPipe wrapper DLL loaded for editor session: %s"), *LoadedPath);
#else
		FPlatformProcess::FreeDllHandle(DllHandle);
#endif
		DllHandle = nullptr;
	}
}

bool FMediaPipePoseWrapper::Init(const FString& PoseModelPath, const FString& HandModelPath, const FString& HolisticModelPath, const FMediaPipePoseNativeOptions& Options)
{
	if (!IsLoaded() || !InitFn)
	{
		UE_LOG(LogMediaPipePose, Error, TEXT("Wrapper DLL not loaded before Init."));
		return false;
	}

	bMockMode = false;
	bHandsEnabled = false;
	bFaceEnabled = false;

	const bool bWantsHands = Options.bEnableHands && !HandModelPath.IsEmpty() && FPaths::FileExists(HandModelPath);
	const bool bWantsHolistic = !HolisticModelPath.IsEmpty() && FPaths::FileExists(HolisticModelPath);
	if (bWantsHolistic && Init4Fn)
	{
		FMediaPipeNativeInitOptions NativeOptions;
		NativeOptions.EnableHands = bWantsHands ? 1 : 0;
		NativeOptions.NumPoses = FMath::Clamp(Options.NumPoses, 1, 4);
		NativeOptions.MinPoseDetectionConfidence = FMath::Clamp(Options.MinPoseDetectionConfidence, 0.0f, 1.0f);
		NativeOptions.MinPosePresenceConfidence = FMath::Clamp(Options.MinPosePresenceConfidence, 0.0f, 1.0f);
		NativeOptions.MinTrackingConfidence = FMath::Clamp(Options.MinTrackingConfidence, 0.0f, 1.0f);
		NativeOptions.OutputSegmentationMasks = Options.bOutputSegmentationMasks ? 1 : 0;
		NativeOptions.NumHands = FMath::Clamp(Options.NumHands, 1, 2);
		NativeOptions.MinHandDetectionConfidence = FMath::Clamp(Options.MinHandDetectionConfidence, 0.0f, 1.0f);
		NativeOptions.MinHandPresenceConfidence = FMath::Clamp(Options.MinHandPresenceConfidence, 0.0f, 1.0f);
		NativeOptions.MinHandTrackingConfidence = FMath::Clamp(Options.MinHandTrackingConfidence, 0.0f, 1.0f);

		FTCHARToUTF8 PoseUtf8(*PoseModelPath);
		FTCHARToUTF8 HandUtf8(bWantsHands ? *HandModelPath : TEXT(""));
		FTCHARToUTF8 HolisticUtf8(*HolisticModelPath);
		bReady = Init4Fn(PoseUtf8.Get(), HandUtf8.Get(), HolisticUtf8.Get(), &NativeOptions);
		bHandsEnabled = bReady && bWantsHands && GetHandsFn;
		bFaceEnabled = bReady && GetFaceFn;
	}
	else if (Init3Fn)
	{
		FMediaPipeNativeInitOptions NativeOptions;
		NativeOptions.EnableHands = bWantsHands ? 1 : 0;
		NativeOptions.NumPoses = FMath::Clamp(Options.NumPoses, 1, 4);
		NativeOptions.MinPoseDetectionConfidence = FMath::Clamp(Options.MinPoseDetectionConfidence, 0.0f, 1.0f);
		NativeOptions.MinPosePresenceConfidence = FMath::Clamp(Options.MinPosePresenceConfidence, 0.0f, 1.0f);
		NativeOptions.MinTrackingConfidence = FMath::Clamp(Options.MinTrackingConfidence, 0.0f, 1.0f);
		NativeOptions.OutputSegmentationMasks = Options.bOutputSegmentationMasks ? 1 : 0;
		NativeOptions.NumHands = FMath::Clamp(Options.NumHands, 1, 2);
		NativeOptions.MinHandDetectionConfidence = FMath::Clamp(Options.MinHandDetectionConfidence, 0.0f, 1.0f);
		NativeOptions.MinHandPresenceConfidence = FMath::Clamp(Options.MinHandPresenceConfidence, 0.0f, 1.0f);
		NativeOptions.MinHandTrackingConfidence = FMath::Clamp(Options.MinHandTrackingConfidence, 0.0f, 1.0f);

		FTCHARToUTF8 PoseUtf8(*PoseModelPath);
		FTCHARToUTF8 HandUtf8(bWantsHands ? *HandModelPath : TEXT(""));
		bReady = Init3Fn(PoseUtf8.Get(), HandUtf8.Get(), &NativeOptions);
		bHandsEnabled = bReady && bWantsHands && GetHandsFn;
	}
	else if (bWantsHands && Init2Fn && GetHandsFn)
	{
		FTCHARToUTF8 PoseUtf8(*PoseModelPath);
		FTCHARToUTF8 HandUtf8(*HandModelPath);
		bReady = Init2Fn(PoseUtf8.Get(), HandUtf8.Get());
		bHandsEnabled = bReady;
	}
	else
	{
		FTCHARToUTF8 PoseUtf8(*PoseModelPath);
		bReady = InitFn(PoseUtf8.Get());
	}
	if (!bReady)
	{
		UE_LOG(LogMediaPipePose, Error, TEXT("MediaPipe wrapper Init failed for pose model: %s"), *PoseModelPath);
	}
	else
	{
		UE_LOG(
			LogMediaPipePose,
			Log,
			TEXT("MediaPipe wrapper Init options: init3=%s hands=%s num_poses=%d detect=%.2f presence=%.2f tracking=%.2f segmentation=%s num_hands=%d hand_detect=%.2f hand_presence=%.2f hand_tracking=%.2f hand_model=%s"),
			Init3Fn ? TEXT("true") : TEXT("false"),
			bHandsEnabled ? TEXT("enabled") : TEXT("off"),
			FMath::Clamp(Options.NumPoses, 1, 4),
			FMath::Clamp(Options.MinPoseDetectionConfidence, 0.0f, 1.0f),
			FMath::Clamp(Options.MinPosePresenceConfidence, 0.0f, 1.0f),
			FMath::Clamp(Options.MinTrackingConfidence, 0.0f, 1.0f),
			Options.bOutputSegmentationMasks ? TEXT("on") : TEXT("off"),
			FMath::Clamp(Options.NumHands, 1, 2),
			FMath::Clamp(Options.MinHandDetectionConfidence, 0.0f, 1.0f),
			FMath::Clamp(Options.MinHandPresenceConfidence, 0.0f, 1.0f),
			FMath::Clamp(Options.MinHandTrackingConfidence, 0.0f, 1.0f),
			bWantsHands ? *HandModelPath : TEXT("none"));
		UE_LOG(
			LogMediaPipePose,
			Log,
			TEXT("MediaPipe wrapper holistic options: init4=%s holistic=%s holistic_model=%s"),
			Init4Fn ? TEXT("true") : TEXT("false"),
			bFaceEnabled ? TEXT("enabled") : TEXT("off"),
			bWantsHolistic ? *HolisticModelPath : TEXT("none"));
	}

	return bReady;
}

bool FMediaPipePoseWrapper::InitMock()
{
	bMockMode = true;
	bReady = true;
	LastTimestampUs = 0;
	return true;
}

bool FMediaPipePoseWrapper::ProcessFrame(const uint8* RgbData, int32 Width, int32 Height, int64 TimestampUs)
{
	if (!IsReady() || !ProcessFrameFn)
	{
		if (!bMockMode)
		{
			return false;
		}
	}

	if (bMockMode)
	{
		LastTimestampUs = TimestampUs;
		return true;
	}

	return ProcessFrameFn(RgbData, Width, Height, TimestampUs);
}

bool FMediaPipePoseWrapper::GetLandmarks(FMediaPipePoseLandmarks& OutNormalized, FMediaPipePoseLandmarks& OutWorld)
{
	if (!IsReady() || (!GetLandmarksFn && !bMockMode))
	{
		return false;
	}

	if (bMockMode)
	{
		const double TimeSeconds = (LastTimestampUs > 0) ? (static_cast<double>(LastTimestampUs) / 1000000.0) : FPlatformTime::Seconds();
		const float Oscillator = static_cast<float>(TimeSeconds * 2.0);
		const TStaticArray<FVector2f, MediaPipePoseLandmarkCount>& Base = GetMockBasePositions();

		for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
		{
			const float Offset = 0.02f * FMath::Sin(Oscillator + static_cast<float>(Index) * 0.35f);
			const float X = Base[Index].X + Offset;
			const float Y = Base[Index].Y + 0.5f * Offset;

			OutNormalized.Points[Index] = { X, Y, 0.0f, 1.0f, 1.0f, 1.0f };

			const float WorldX = (X - 0.5f) * 0.6f;
			const float WorldY = (0.5f - Y) * 1.2f;
			OutWorld.Points[Index] = { WorldX, WorldY, 0.0f, 1.0f, 1.0f, 1.0f };
		}

		return true;
	}

	FMediaPipeRawLandmarks RawNormalized{};
	FMediaPipeRawLandmarks RawWorld{};

	if (!GetLandmarksFn(&RawNormalized, &RawWorld))
	{
		return false;
	}

	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		const FMediaPipeRawLandmark& N = RawNormalized.Landmarks[Index];
		const float NReliability = N.Visibility * N.Presence;
		OutNormalized.Points[Index] = { N.X, N.Y, N.Z, N.Visibility, N.Presence, NReliability };

		const FMediaPipeRawLandmark& W = RawWorld.Landmarks[Index];
		const float WReliability = W.Visibility * W.Presence;
		OutWorld.Points[Index] = { W.X, W.Y, W.Z, W.Visibility, W.Presence, WReliability };
	}

	return true;
}

bool FMediaPipePoseWrapper::GetHandLandmarks(FMediaPipeRawHandPair& OutHands)
{
	if (!IsReady() || bMockMode || !GetHandsFn || !bHandsEnabled)
	{
		return false;
	}

	return GetHandsFn(&OutHands);
}

bool FMediaPipePoseWrapper::GetFacePose(FMediaPipeRawFacePose& OutFace)
{
	if (!IsReady() || !GetFaceFn || !bFaceEnabled)
	{
		return false;
	}

	return GetFaceFn(&OutFace);
}

void FMediaPipePoseWrapper::Shutdown()
{
	const MP_ShutdownFn LocalShutdownFn = ShutdownFn;
	const bool bShouldShutdown = bReady && !bMockMode && LocalShutdownFn;
	bReady = false;
	bMockMode = false;
	bHandsEnabled = false;
	bFaceEnabled = false;

	if (bShouldShutdown)
	{
		LocalShutdownFn();
	}
}

bool FMediaPipePoseWrapper::IsLoaded() const
{
	return DllHandle != nullptr;
}

bool FMediaPipePoseWrapper::IsReady() const
{
	return bReady;
}
