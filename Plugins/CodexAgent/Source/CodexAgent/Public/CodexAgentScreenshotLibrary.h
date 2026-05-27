#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "CodexAgentScreenshotLibrary.generated.h"

UCLASS()
class CODEXAGENT_API UCodexAgentScreenshotLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Codex Agent")
	static bool CaptureLevelScreenshot(const FString& OutputPath, int32 Width, int32 Height);
};

