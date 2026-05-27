#pragma once

#include "Modules/ModuleManager.h"

class FMediaPipeDriverModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FMediaPipeDriverModule* TryGet();

private:
#if WITH_EDITOR
	FDelegateHandle PIEReadyHandle;
#endif
};
