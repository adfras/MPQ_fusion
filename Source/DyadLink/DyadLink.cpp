#include "DyadLink.h"

DEFINE_LOG_CATEGORY_STATIC(LogDyadLinkModule, Log, All);

void FDyadLinkModule::StartupModule()
{
	UE_LOG(LogDyadLinkModule, Log, TEXT("DyadLink module started."));
}

void FDyadLinkModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FDyadLinkModule, DyadLink)
