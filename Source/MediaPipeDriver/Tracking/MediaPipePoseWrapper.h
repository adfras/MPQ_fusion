#pragma once

#include "CoreMinimal.h"
#include "MediaPipePoseTypes.h"

struct FMediaPipeRawLandmark
{
	float X;
	float Y;
	float Z;
	float Visibility;
	float Presence;
};

struct FMediaPipeRawLandmarks
{
	FMediaPipeRawLandmark Landmarks[MediaPipePoseLandmarkCount];
};

struct FMediaPipeNativeInitOptions
{
	int32 SizeBytes = sizeof(FMediaPipeNativeInitOptions);
	int32 EnableHands = 0;
	int32 NumPoses = 1;
	float MinPoseDetectionConfidence = 0.5f;
	float MinPosePresenceConfidence = 0.5f;
	float MinTrackingConfidence = 0.5f;
	int32 OutputSegmentationMasks = 0;
	int32 NumHands = 2;
	float MinHandDetectionConfidence = 0.5f;
	float MinHandPresenceConfidence = 0.5f;
	float MinHandTrackingConfidence = 0.5f;
	int32 Reserved0 = 0;
};

struct FMediaPipePoseNativeOptions
{
	bool bEnableHands = false;
	int32 NumPoses = 1;
	float MinPoseDetectionConfidence = 0.5f;
	float MinPosePresenceConfidence = 0.5f;
	float MinTrackingConfidence = 0.5f;
	bool bOutputSegmentationMasks = false;
	int32 NumHands = 2;
	float MinHandDetectionConfidence = 0.5f;
	float MinHandPresenceConfidence = 0.5f;
	float MinHandTrackingConfidence = 0.5f;
};

class FMediaPipePoseWrapper
{
public:
	FMediaPipePoseWrapper();
	~FMediaPipePoseWrapper();

	bool Load(const FString& DllPath);
	void Unload();
	bool Init(const FString& PoseModelPath, const FString& HandModelPath = FString(), const FMediaPipePoseNativeOptions& Options = FMediaPipePoseNativeOptions());
	bool InitMock();
	bool ProcessFrame(const uint8* RgbData, int32 Width, int32 Height, int64 TimestampUs);
	bool GetLandmarks(FMediaPipePoseLandmarks& OutNormalized, FMediaPipePoseLandmarks& OutWorld);
	bool GetHandLandmarks(FMediaPipeRawHandPair& OutHands);
	void Shutdown();

	bool IsLoaded() const;
	bool IsReady() const;
	bool AreHandsEnabled() const { return bHandsEnabled; }

private:
	typedef bool (*MP_InitFn)(const char* ConfigPath);
	typedef bool (*MP_Init2Fn)(const char* PoseModelPath, const char* HandModelPath);
	typedef bool (*MP_Init3Fn)(const char* PoseModelPath, const char* HandModelPath, const FMediaPipeNativeInitOptions* Options);
	typedef bool (*MP_ProcessFrameFn)(const uint8* RgbData, int32 Width, int32 Height, int64 TimestampUs);
	typedef bool (*MP_GetLandmarksFn)(FMediaPipeRawLandmarks* OutNormalized, FMediaPipeRawLandmarks* OutWorld);
	typedef bool (*MP_GetHandLandmarksFn)(FMediaPipeRawHandPair* OutHands);
	typedef void (*MP_ShutdownFn)();

	void* DllHandle = nullptr;
	MP_InitFn InitFn = nullptr;
	MP_Init2Fn Init2Fn = nullptr;
	MP_Init3Fn Init3Fn = nullptr;
	MP_ProcessFrameFn ProcessFrameFn = nullptr;
	MP_GetLandmarksFn GetLandmarksFn = nullptr;
	MP_GetHandLandmarksFn GetHandsFn = nullptr;
	MP_ShutdownFn ShutdownFn = nullptr;
	bool bReady = false;
	bool bMockMode = false;
	bool bHandsEnabled = false;
	int64 LastTimestampUs = 0;
	FString LoadedPath;
};
