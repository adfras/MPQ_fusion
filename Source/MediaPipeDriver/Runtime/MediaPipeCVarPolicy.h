#pragma once

#include "CoreMinimal.h"

// Priority-layered console-variable policy.
//
// Multiple uncoordinated writers of the same CVars (live retarget profiles vs the replay
// evaluation policy vs capture scopes) caused the 2026-06-10 leg-freeze regression: a live
// profile silently stomped the replay body-drive policy. This stack makes ownership explicit:
// a layer's setting is only written while no higher-priority active layer covers the same
// CVar, and every transition is logged. See Docs/CVAR_REFERENCE.md for the writer inventory
// and Docs/REFACTOR_PLAN.md Phase 11 for the migration plan (replay layer first, live
// profiles and capture scopes follow).
enum class EMediaPipeCVarPolicyPriority : uint8
{
	Baseline = 0,
	LiveProfile = 1,
	CaptureScope = 2,
	ReplayEvaluation = 3
};

struct FMediaPipeCVarSetting
{
	enum class EType : uint8 { Int, Float, String };

	FString Name;
	EType Type = EType::Int;
	int32 IntValue = 0;
	float FloatValue = 0.0f;
	FString StringValue;

	static FMediaPipeCVarSetting MakeInt(const TCHAR* InName, int32 InValue)
	{
		FMediaPipeCVarSetting Setting;
		Setting.Name = InName;
		Setting.Type = EType::Int;
		Setting.IntValue = InValue;
		return Setting;
	}

	static FMediaPipeCVarSetting MakeFloat(const TCHAR* InName, float InValue)
	{
		FMediaPipeCVarSetting Setting;
		Setting.Name = InName;
		Setting.Type = EType::Float;
		Setting.FloatValue = InValue;
		return Setting;
	}

	static FMediaPipeCVarSetting MakeString(const TCHAR* InName, const FString& InValue)
	{
		FMediaPipeCVarSetting Setting;
		Setting.Name = InName;
		Setting.Type = EType::String;
		Setting.StringValue = InValue;
		return Setting;
	}
};

struct FMediaPipeCVarPolicyLayer
{
	FName PolicyId;
	EMediaPipeCVarPolicyPriority Priority = EMediaPipeCVarPolicyPriority::Baseline;
	TArray<FMediaPipeCVarSetting> Settings;
};

class MEDIAPIPEDRIVER_API FMediaPipeCVarPolicyStack
{
public:
	static FMediaPipeCVarPolicyStack& Get();

	// Applies or refreshes a layer (replacing any active layer with the same PolicyId).
	// Each setting is written with ECVF_SetByConsole unless a higher-priority active layer
	// covers the same CVar, in which case it is counted and logged instead of written.
	// Game thread only.
	void Apply(const FMediaPipeCVarPolicyLayer& Layer);

	// Removes a layer. CVars it covered are re-written from the highest remaining active
	// layer that covers them; CVars covered by no remaining layer keep their current values
	// (matching the historical apply-only semantics of the profile functions).
	void Remove(FName PolicyId);

	bool IsLayerActive(FName PolicyId) const;

private:
	TArray<FMediaPipeCVarPolicyLayer> ActiveLayers;

	static void WriteSetting(const FMediaPipeCVarSetting& Setting);
	bool IsCoveredAbovePriority(const FString& Name, EMediaPipeCVarPolicyPriority Priority) const;
	const FMediaPipeCVarSetting* FindHighestSettingFor(const FString& Name) const;
};
