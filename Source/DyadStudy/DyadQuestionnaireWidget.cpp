#include "DyadQuestionnaireWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "DyadSessionSubsystem.h"
#include "Engine/GameInstance.h"

namespace
{
UDyadSessionSubsystem* ResolveSession(const UWidget* Widget)
{
	const UWorld* World = Widget ? Widget->GetWorld() : nullptr;
	return World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UDyadSessionSubsystem>()
		: nullptr;
}
} // namespace

void UDyadLikertButton::InitScore(const int32 InItemIndex, const int32 InScore)
{
	ItemIndex = InItemIndex;
	Score = InScore;
	OnClicked.AddUniqueDynamic(this, &UDyadLikertButton::HandleClicked);
}

void UDyadLikertButton::HandleClicked()
{
	if (UDyadSessionSubsystem* Session = ResolveSession(this))
	{
		Session->AnswerQuestionnaire(ItemIndex, Score);
	}
}

TSharedRef<SWidget> UDyadQuestionnaireWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildTree();
	}
	return Super::RebuildWidget();
}

void UDyadQuestionnaireWidget::BuildTree()
{
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	WidgetTree->RootWidget = Root;

	UDyadSessionSubsystem* Session = ResolveSession(this);
	const TArray<FString> Items = Session ? Session->GetQuestionnaireItems() : TArray<FString>();

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>();
	Title->SetText(NSLOCTEXT("DyadStudy", "QuestionnaireTitle",
		"How did that feel? (1 = not at all, 7 = very much)"));
	FSlateFontInfo TitleFont = Title->GetFont();
	TitleFont.Size = 22;
	Title->SetFont(TitleFont);
	Root->AddChildToVerticalBox(Title);

	for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ++ItemIndex)
	{
		UTextBlock* ItemText = WidgetTree->ConstructWidget<UTextBlock>();
		ItemText->SetText(FText::FromString(Items[ItemIndex]));
		FSlateFontInfo ItemFont = ItemText->GetFont();
		ItemFont.Size = 17;
		ItemText->SetFont(ItemFont);
		UVerticalBoxSlot* TextSlot = Root->AddChildToVerticalBox(ItemText);
		TextSlot->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 2.0f));

		UHorizontalBox* Scores = WidgetTree->ConstructWidget<UHorizontalBox>();
		for (int32 Score = 1; Score <= 7; ++Score)
		{
			UDyadLikertButton* Button = WidgetTree->ConstructWidget<UDyadLikertButton>();
			Button->InitScore(ItemIndex, Score);
			UTextBlock* ScoreLabel = WidgetTree->ConstructWidget<UTextBlock>();
			ScoreLabel->SetText(FText::AsNumber(Score));
			FSlateFontInfo ScoreFont = ScoreLabel->GetFont();
			ScoreFont.Size = 18;
			ScoreLabel->SetFont(ScoreFont);
			Button->AddChild(ScoreLabel);
			UHorizontalBoxSlot* ButtonSlot = Scores->AddChildToHorizontalBox(Button);
			ButtonSlot->SetPadding(FMargin(6.0f, 2.0f));
			ScoreButtons.Add(Button);
		}
		Root->AddChildToVerticalBox(Scores);
	}

	StatusText = WidgetTree->ConstructWidget<UTextBlock>();
	StatusText->SetText(FText::GetEmpty());
	Root->AddChildToVerticalBox(StatusText);
}

void UDyadQuestionnaireWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UDyadSessionSubsystem* Session = ResolveSession(this);
	if (!Session)
	{
		return;
	}
	const TArray<int32>& Answers = Session->GetQuestionnaireAnswers();
	for (UDyadLikertButton* Button : ScoreButtons)
	{
		if (Button && Answers.IsValidIndex(Button->ItemIndex))
		{
			const bool bAnswered = Answers[Button->ItemIndex] != 0;
			const bool bChosen = Answers[Button->ItemIndex] == Button->Score;
			Button->SetIsEnabled(!bAnswered || bChosen);
		}
	}
	if (StatusText)
	{
		int32 AnsweredCount = 0;
		for (const int32 Answer : Answers)
		{
			AnsweredCount += Answer != 0 ? 1 : 0;
		}
		StatusText->SetText(FText::FromString(Session->IsQuestionnaireComplete()
			? TEXT("Thank you - all answered.")
			: FString::Printf(TEXT("%d / %d answered"), AnsweredCount, Answers.Num())));
	}
}
