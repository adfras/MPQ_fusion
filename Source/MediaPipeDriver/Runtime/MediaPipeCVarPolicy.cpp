#include "MediaPipeCVarPolicy.h"

#include "HAL/IConsoleManager.h"
#include "MediaPipePoseLog.h"

FMediaPipeCVarPolicyStack& FMediaPipeCVarPolicyStack::Get()
{
	static FMediaPipeCVarPolicyStack Stack;
	return Stack;
}

void FMediaPipeCVarPolicyStack::WriteSetting(const FMediaPipeCVarSetting& Setting)
{
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Setting.Name);
	if (!Variable)
	{
		return;
	}
	switch (Setting.Type)
	{
	case FMediaPipeCVarSetting::EType::Int:
		Variable->Set(Setting.IntValue, ECVF_SetByConsole);
		break;
	case FMediaPipeCVarSetting::EType::Float:
		Variable->Set(Setting.FloatValue, ECVF_SetByConsole);
		break;
	case FMediaPipeCVarSetting::EType::String:
		Variable->Set(*Setting.StringValue, ECVF_SetByConsole);
		break;
	}
}

bool FMediaPipeCVarPolicyStack::IsCoveredAbovePriority(const FString& Name, const EMediaPipeCVarPolicyPriority Priority) const
{
	for (const FMediaPipeCVarPolicyLayer& Layer : ActiveLayers)
	{
		if (Layer.Priority <= Priority)
		{
			continue;
		}
		for (const FMediaPipeCVarSetting& Setting : Layer.Settings)
		{
			if (Setting.Name == Name)
			{
				return true;
			}
		}
	}
	return false;
}

const FMediaPipeCVarSetting* FMediaPipeCVarPolicyStack::FindHighestSettingFor(const FString& Name) const
{
	const FMediaPipeCVarSetting* Best = nullptr;
	EMediaPipeCVarPolicyPriority BestPriority = EMediaPipeCVarPolicyPriority::Baseline;
	for (const FMediaPipeCVarPolicyLayer& Layer : ActiveLayers)
	{
		for (const FMediaPipeCVarSetting& Setting : Layer.Settings)
		{
			if (Setting.Name == Name && (!Best || Layer.Priority >= BestPriority))
			{
				Best = &Setting;
				BestPriority = Layer.Priority;
			}
		}
	}
	return Best;
}

void FMediaPipeCVarPolicyStack::Apply(const FMediaPipeCVarPolicyLayer& Layer)
{
	ActiveLayers.RemoveAll([&Layer](const FMediaPipeCVarPolicyLayer& Existing)
	{
		return Existing.PolicyId == Layer.PolicyId;
	});
	ActiveLayers.Add(Layer);

	int32 Written = 0;
	int32 CoveredByHigher = 0;
	for (const FMediaPipeCVarSetting& Setting : Layer.Settings)
	{
		if (IsCoveredAbovePriority(Setting.Name, Layer.Priority))
		{
			++CoveredByHigher;
			continue;
		}
		WriteSetting(Setting);
		++Written;
	}

	UE_LOG(LogMediaPipePose, Log,
		TEXT("mp.Policy: apply=%s prio=%d settings=%d written=%d coveredByHigher=%d activeLayers=%d"),
		*Layer.PolicyId.ToString(),
		static_cast<int32>(Layer.Priority),
		Layer.Settings.Num(),
		Written,
		CoveredByHigher,
		ActiveLayers.Num());
}

void FMediaPipeCVarPolicyStack::Remove(const FName PolicyId)
{
	const int32 IndexToRemove = ActiveLayers.IndexOfByPredicate([PolicyId](const FMediaPipeCVarPolicyLayer& Existing)
	{
		return Existing.PolicyId == PolicyId;
	});
	if (IndexToRemove == INDEX_NONE)
	{
		return;
	}

	const FMediaPipeCVarPolicyLayer RemovedLayer = ActiveLayers[IndexToRemove];
	ActiveLayers.RemoveAt(IndexToRemove);

	int32 Rewritten = 0;
	for (const FMediaPipeCVarSetting& Setting : RemovedLayer.Settings)
	{
		if (const FMediaPipeCVarSetting* Fallback = FindHighestSettingFor(Setting.Name))
		{
			WriteSetting(*Fallback);
			++Rewritten;
		}
		// No remaining layer covers this CVar: keep its current value, matching the
		// historical apply-only semantics of the profile and policy functions.
	}

	UE_LOG(LogMediaPipePose, Log,
		TEXT("mp.Policy: remove=%s rewrittenFromLowerLayers=%d activeLayers=%d"),
		*PolicyId.ToString(),
		Rewritten,
		ActiveLayers.Num());
}

bool FMediaPipeCVarPolicyStack::IsLayerActive(const FName PolicyId) const
{
	return ActiveLayers.ContainsByPredicate([PolicyId](const FMediaPipeCVarPolicyLayer& Existing)
	{
		return Existing.PolicyId == PolicyId;
	});
}
