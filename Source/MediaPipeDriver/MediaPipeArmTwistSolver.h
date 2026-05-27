#pragma once

#include "CoreMinimal.h"

struct FMediaPipeArmTwistInput
{
	FTransform ParentComponent = FTransform::Identity;
	FTransform SourceParentComponent = FTransform::Identity;
	FTransform SourceComponent = FTransform::Identity;
	FTransform ReferenceParentComponent = FTransform::Identity;
	FTransform ReferenceSourceParentComponent = FTransform::Identity;
	FTransform ReferenceTwistComponent = FTransform::Identity;
	FTransform ReferenceSourceComponent = FTransform::Identity;
};

struct FMediaPipeArmTwistResult
{
	FTransform TwistComponent = FTransform::Identity;
	float Weight = 0.0f;
	float TranslationScale = 1.0f;
};

class MEDIAPIPEDRIVER_API FMediaPipeArmTwistSolver
{
public:
	static bool BuildInterpolatedTwistTransform(
		const FMediaPipeArmTwistInput& Input,
		FMediaPipeArmTwistResult& OutResult);
};
