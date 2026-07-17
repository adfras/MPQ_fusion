#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"

#include "DyadQuestionnaireWidget.generated.h"

class UTextBlock;
class UVerticalBox;

// One Likert score button (1-7) for one item; forwards to the session subsystem.
UCLASS()
class DYADSTUDY_API UDyadLikertButton : public UButton
{
	GENERATED_BODY()

public:
	void InitScore(int32 InItemIndex, int32 InScore);

	int32 ItemIndex = 0;
	int32 Score = 0;

private:
	UFUNCTION()
	void HandleClicked();
};

// DYADIC_STUDY_PLAN Phase 5: the end-of-block questionnaire, 100% C++ like the menu.
// Items come from the condition file via the session subsystem; every answer is a
// timestamped session event (questionnaire_answer). Which instrument the items encode is
// a study-design choice; this widget just renders items and records answers. The desk
// path (mp.DyadAnswerQuestionnaire <item> <score>) calls the same subsystem function.
UCLASS()
class DYADSTUDY_API UDyadQuestionnaireWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void BuildTree();

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDyadLikertButton>> ScoreButtons;
};
