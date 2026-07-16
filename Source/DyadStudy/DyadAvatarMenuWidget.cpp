#include "DyadAvatarMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "MediaPipeMetaHumanProfile.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogDyadMenu, Log, All);

namespace
{
const FLinearColor SelectedFrameColor(0.15f, 0.85f, 0.35f, 1.0f);
const FLinearColor IdleFrameColor(0.08f, 0.08f, 0.10f, 1.0f);
const FLinearColor LockedFrameColor(0.35f, 0.35f, 0.38f, 1.0f);

UTexture2D* LoadPortraitTexture(const FName AvatarId)
{
	const FString PortraitPath = FPaths::Combine(
		FPaths::ProjectContentDir(), TEXT("DyadStudy"), TEXT("Portraits"),
		AvatarId.ToString() + TEXT(".png"));
	if (!FPaths::FileExists(PortraitPath))
	{
		return nullptr;
	}
	return FImageUtils::ImportFileAsTexture2D(PortraitPath);
}

UDyadSessionSubsystem* ResolveSession(const UUserWidget* Widget)
{
	const UGameInstance* GameInstance = Widget ? Widget->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UDyadSessionSubsystem>() : nullptr;
}
} // namespace

void UDyadAvatarMenuButton::InitChoice(const EDyadAvatarSlot InSlot, const FName InAvatarId)
{
	ChoiceSlot = InSlot;
	AvatarId = InAvatarId;
	OnClicked.AddUniqueDynamic(this, &UDyadAvatarMenuButton::HandleClicked);
}

void UDyadAvatarMenuButton::HandleClicked()
{
	const UWorld* World = GetWorld();
	UDyadSessionSubsystem* Session = World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UDyadSessionSubsystem>()
		: nullptr;
	if (Session)
	{
		Session->SelectAvatar(ChoiceSlot, AvatarId);
	}
}

UWidget* UDyadAvatarMenuWidget::BuildChoiceRow(const EDyadAvatarSlot InSlot, const FText& Label)
{
	UVerticalBox* Row = WidgetTree->ConstructWidget<UVerticalBox>();

	UTextBlock* RowLabel = WidgetTree->ConstructWidget<UTextBlock>();
	RowLabel->SetText(Label);
	FSlateFontInfo LabelFont = RowLabel->GetFont();
	LabelFont.Size = 22;
	RowLabel->SetFont(LabelFont);
	Row->AddChildToVerticalBox(RowLabel);

	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>();
	TArray<FMediaPipeMetaHumanProfileDefinition> Profiles;
	GetMediaPipeAvailableMetaHumanProfiles(Profiles);
	for (const FMediaPipeMetaHumanProfileDefinition& Profile : Profiles)
	{
		UBorder* Frame = WidgetTree->ConstructWidget<UBorder>();
		Frame->SetPadding(FMargin(4.0f));
		Frame->SetBrushColor(IdleFrameColor);

		UDyadAvatarMenuButton* Button = WidgetTree->ConstructWidget<UDyadAvatarMenuButton>();
		Button->InitChoice(InSlot, Profile.ProfileId);

		UVerticalBox* ButtonContent = WidgetTree->ConstructWidget<UVerticalBox>();
		if (UTexture2D* Portrait = LoadPortraitTexture(Profile.ProfileId))
		{
			UImage* PortraitImage = WidgetTree->ConstructWidget<UImage>();
			PortraitImage->SetBrushFromTexture(Portrait);
			PortraitImage->SetDesiredSizeOverride(FVector2D(140.0, 150.0));
			ButtonContent->AddChildToVerticalBox(PortraitImage);
		}
		UTextBlock* NameText = WidgetTree->ConstructWidget<UTextBlock>();
		NameText->SetText(FText::FromName(Profile.ProfileId));
		FSlateFontInfo NameFont = NameText->GetFont();
		NameFont.Size = 16;
		NameText->SetFont(NameFont);
		NameText->SetJustification(ETextJustify::Center);
		ButtonContent->AddChildToVerticalBox(NameText);

		Button->AddChild(ButtonContent);
		Frame->AddChild(Button);
		UHorizontalBoxSlot* ButtonSlot = Buttons->AddChildToHorizontalBox(Frame);
		ButtonSlot->SetPadding(FMargin(6.0f, 4.0f));

		ChoiceButtons.Add(Button);
		ButtonFrames.Add(Button, Frame);
	}
	Row->AddChildToVerticalBox(Buttons);
	return Row;
}

TSharedRef<SWidget> UDyadAvatarMenuWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildMenuTree();
	}
	return Super::RebuildWidget();
}

void UDyadAvatarMenuWidget::BuildMenuTree()
{
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	WidgetTree->RootWidget = Root;

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>();
	Title->SetText(NSLOCTEXT("DyadStudy", "MenuTitle", "Choose the avatars"));
	FSlateFontInfo TitleFont = Title->GetFont();
	TitleFont.Size = 28;
	Title->SetFont(TitleFont);
	Root->AddChildToVerticalBox(Title);

	Root->AddChildToVerticalBox(BuildChoiceRow(
		EDyadAvatarSlot::Self, NSLOCTEXT("DyadStudy", "SelfRow", "You")));
	Root->AddChildToVerticalBox(BuildChoiceRow(
		EDyadAvatarSlot::Partner, NSLOCTEXT("DyadStudy", "PartnerRow", "Your partner")));

	LockButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* LockLabel = WidgetTree->ConstructWidget<UTextBlock>();
	LockLabel->SetText(NSLOCTEXT("DyadStudy", "LockButton", "Confirm choices"));
	FSlateFontInfo LockFont = LockLabel->GetFont();
	LockFont.Size = 22;
	LockLabel->SetFont(LockFont);
	LockButton->AddChild(LockLabel);
	LockButton->OnClicked.AddUniqueDynamic(this, &UDyadAvatarMenuWidget::HandleLockClicked);
	UVerticalBoxSlot* LockSlot = Root->AddChildToVerticalBox(LockButton);
	LockSlot->SetPadding(FMargin(0.0f, 14.0f, 0.0f, 4.0f));

	StatusText = WidgetTree->ConstructWidget<UTextBlock>();
	StatusText->SetText(FText::GetEmpty());
	Root->AddChildToVerticalBox(StatusText);

	RefreshFromSession();
}

void UDyadAvatarMenuWidget::HandleLockClicked()
{
	if (UDyadSessionSubsystem* Session = ResolveSession(this))
	{
		Session->LockChoices();
	}
}

void UDyadAvatarMenuWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshFromSession();
}

void UDyadAvatarMenuWidget::RefreshFromSession()
{
	UDyadSessionSubsystem* Session = ResolveSession(this);
	if (!Session)
	{
		return;
	}

	const bool bLocked = Session->AreChoicesLocked();
	for (UDyadAvatarMenuButton* Button : ChoiceButtons)
	{
		if (!Button)
		{
			continue;
		}
		const bool bSelected = Session->GetAvatarId(Button->ChoiceSlot) == Button->AvatarId;
		const bool bSlotSelectable = !bLocked && Session->GetChoiceMode(Button->ChoiceSlot) == EDyadChoiceMode::Free;
		Button->SetIsEnabled(bSlotSelectable);
		if (TObjectPtr<UBorder>* Frame = ButtonFrames.Find(Button))
		{
			(*Frame)->SetBrushColor(bSelected ? SelectedFrameColor : (bLocked ? LockedFrameColor : IdleFrameColor));
		}
	}
	if (LockButton)
	{
		LockButton->SetIsEnabled(!bLocked &&
			!Session->GetSelfAvatarId().IsNone() && !Session->GetPartnerAvatarId().IsNone());
	}
	if (StatusText)
	{
		const FString Status = bLocked
			? FString::Printf(TEXT("Locked: you = %s, partner = %s"),
				*Session->GetSelfAvatarId().ToString(), *Session->GetPartnerAvatarId().ToString())
			: FString::Printf(TEXT("you = %s, partner = %s"),
				*Session->GetSelfAvatarId().ToString(), *Session->GetPartnerAvatarId().ToString());
		StatusText->SetText(FText::FromString(Status));
	}
}
