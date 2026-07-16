#include "DyadStudy.h"

DEFINE_LOG_CATEGORY_STATIC(LogDyadStudyModule, Log, All);

void FDyadStudyModule::StartupModule()
{
	UE_LOG(LogDyadStudyModule, Log, TEXT("DyadStudy module started."));
}

void FDyadStudyModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FDyadStudyModule, DyadStudy)
