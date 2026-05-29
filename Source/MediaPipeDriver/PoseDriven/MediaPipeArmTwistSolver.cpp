#include "MediaPipeArmTwistSolver.h"

namespace
{
	bool IsFiniteTransform(const FTransform& Transform)
	{
		return !Transform.ContainsNaN();
	}

	float CalculateProjectionWeight(
		const FTransform& ReferenceSourceParentComponent,
		const FTransform& ReferenceTwistComponent,
		const FTransform& ReferenceSourceComponent)
	{
		const FVector SourceParentToSource =
			ReferenceSourceComponent.GetLocation() - ReferenceSourceParentComponent.GetLocation();
		const FVector SourceParentToTwist =
			ReferenceTwistComponent.GetLocation() - ReferenceSourceParentComponent.GetLocation();
		const float SourceLenSq = SourceParentToSource.SizeSquared();
		if (SourceLenSq <= UE_SMALL_NUMBER)
		{
			return 0.0f;
		}

		return FMath::Clamp(FVector::DotProduct(SourceParentToTwist, SourceParentToSource) / SourceLenSq, 0.0f, 1.0f);
	}
}

bool FMediaPipeArmTwistSolver::BuildInterpolatedTwistTransform(
	const FMediaPipeArmTwistInput& Input,
	FMediaPipeArmTwistResult& OutResult)
{
	OutResult = FMediaPipeArmTwistResult{};

	if (!IsFiniteTransform(Input.ParentComponent) ||
		!IsFiniteTransform(Input.SourceParentComponent) ||
		!IsFiniteTransform(Input.SourceComponent) ||
		!IsFiniteTransform(Input.ReferenceParentComponent) ||
		!IsFiniteTransform(Input.ReferenceSourceParentComponent) ||
		!IsFiniteTransform(Input.ReferenceTwistComponent) ||
		!IsFiniteTransform(Input.ReferenceSourceComponent))
	{
		return false;
	}

	const FVector ReferenceSourceParentToSource =
		Input.ReferenceSourceComponent.GetLocation() - Input.ReferenceSourceParentComponent.GetLocation();
	const FVector ReferenceSourceParentToTwist =
		Input.ReferenceTwistComponent.GetLocation() - Input.ReferenceSourceParentComponent.GetLocation();
	if (ReferenceSourceParentToSource.SizeSquared() <= UE_SMALL_NUMBER ||
		ReferenceSourceParentToTwist.SizeSquared() <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const float Weight = CalculateProjectionWeight(
		Input.ReferenceSourceParentComponent,
		Input.ReferenceTwistComponent,
		Input.ReferenceSourceComponent);
	if (Weight <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FTransform ReferenceTwistLocal =
		Input.ReferenceTwistComponent.GetRelativeTransform(Input.ReferenceParentComponent);
	const FVector FrameSourceParentToSourceLocal =
		Input.ParentComponent.GetRotation().Inverse() *
		(Input.SourceComponent.GetLocation() - Input.SourceParentComponent.GetLocation());
	const FVector ReferenceSourceParentToSourceLocal =
		Input.ReferenceParentComponent.GetRotation().Inverse() * ReferenceSourceParentToSource;
	const float ReferenceSourceParentToSourceLen = ReferenceSourceParentToSourceLocal.Length();
	const float TranslationScale = ReferenceSourceParentToSourceLen > UE_SMALL_NUMBER
		? FrameSourceParentToSourceLocal.Length() / ReferenceSourceParentToSourceLen
		: 1.0f;
	const FVector ProjectedSourceJointAlignmentLocal = ReferenceSourceParentToSourceLocal * Weight;
	const FVector AdjustedTwistLocalLocation =
		ReferenceTwistLocal.GetLocation() -
		(1.0f - TranslationScale) * ProjectedSourceJointAlignmentLocal;

	const FTransform BaseTwistComponent = ReferenceTwistLocal * Input.ParentComponent;
	const FQuat SourceLocalToTwist =
		Input.SourceComponent.GetRelativeTransform(BaseTwistComponent).GetRotation().GetNormalized();
	const FQuat ReferenceSourceLocalOffset =
		Input.ReferenceSourceComponent.GetRelativeTransform(Input.ReferenceTwistComponent).GetRotation().Inverse().GetNormalized();
	const FQuat TargetLocalRotation = (SourceLocalToTwist * ReferenceSourceLocalOffset).GetNormalized();

	const FVector ReferenceLocalAxis =
		ReferenceTwistLocal.GetRotation().RotateVector(ReferenceTwistLocal.GetLocation()).GetSafeNormal();
	const FVector TargetLocalAxis =
		TargetLocalRotation.RotateVector(ReferenceTwistLocal.GetLocation()).GetSafeNormal();
	if (ReferenceLocalAxis.IsNearlyZero() || TargetLocalAxis.IsNearlyZero())
	{
		return false;
	}

	const FQuat SwingRemoval = FQuat::FindBetweenNormals(TargetLocalAxis, ReferenceLocalAxis);
	const FQuat AxisOnlyTargetLocalRotation = (SwingRemoval * TargetLocalRotation).GetNormalized();
	const FQuat InterpolatedLocalRotation = FQuat::Slerp(
		ReferenceTwistLocal.GetRotation(),
		AxisOnlyTargetLocalRotation,
		Weight).GetNormalized();

	OutResult.TwistComponent = FTransform(
		InterpolatedLocalRotation,
		AdjustedTwistLocalLocation,
		ReferenceTwistLocal.GetScale3D()) * Input.ParentComponent;
	OutResult.Weight = Weight;
	OutResult.TranslationScale = TranslationScale;
	return IsFiniteTransform(OutResult.TwistComponent);
}
