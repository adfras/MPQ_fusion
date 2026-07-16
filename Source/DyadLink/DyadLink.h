#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// DYADIC_STUDY_PLAN Phase 3: the dyad wire. One TCP connection of newline-delimited JSON
// between two independent apps — control messages plus the source-row stream. No UE
// replication; bit-exact agreement between machines is not a goal.
class FDyadLinkModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
