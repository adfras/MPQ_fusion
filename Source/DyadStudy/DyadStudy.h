#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// DYADIC_STUDY_PLAN: runtime module for the two-participant research platform. Everything
// in here is additive to the tracking stack and sits behind mp.Dyad* CVars (default 0).
class FDyadStudyModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
