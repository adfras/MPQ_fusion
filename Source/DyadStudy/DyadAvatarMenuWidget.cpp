#include "DyadAvatarMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "DyadLinkSubsystem.h"
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

	TitleText = WidgetTree->ConstructWidget<UTextBlock>();
	TitleText->SetText(NSLOCTEXT("DyadStudy", "MenuTitleSelf", "Choose your avatar"));
	FSlateFontInfo TitleFont = TitleText->GetFont();
	TitleFont.Size = 28;
	TitleText->SetFont(TitleFont);
	Root->AddChildToVerticalBox(TitleText);

	SelfRow = BuildChoiceRow(
		EDyadAvatarSlot::Self, NSLOCTEXT("DyadStudy", "SelfRow", "You"));
	Root->AddChildToVerticalBox(SelfRow);
	PartnerRow = BuildChoiceRow(
		EDyadAvatarSlot::Partner, NSLOCTEXT("DyadStudy", "PartnerRow", "Your partner"));
	Root->AddChildToVerticalBox(PartnerRow);

	LockButton = WidgetTree->ConstructWidget<UButton>();
	LockLabelText = WidgetTree->ConstructWidget<UTextBlock>();
	LockLabelText->SetText(NSLOCTEXT("DyadStudy", "ConfirmAvatar", "Confirm avatar"));
	FSlateFontInfo LockFont = LockLabelText->GetFont();
	LockFont.Size = 22;
	LockLabelText->SetFont(LockFont);
	LockButton->AddChild(LockLabelText);
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
		Session->ConfirmLobbyStage();
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
	const EDyadLobbyFlowStage Stage = Session->GetLobbyFlowStage();
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

	// One stage at a time: self row while choosing yourself in the mirror, partner row
	// after confirming (the recording-driven partner preview stands where the mirror was).
	const bool bSelfStage = Stage == EDyadLobbyFlowStage::SelfSelect;
	if (SelfRow)
	{
		SelfRow->SetVisibility(bSelfStage ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (PartnerRow)
	{
		PartnerRow->SetVisibility(bSelfStage ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (TitleText)
	{
		TitleText->SetText(bSelfStage
			? NSLOCTEXT("DyadStudy", "MenuTitleSelf", "Choose your avatar")
			: NSLOCTEXT("DyadStudy", "MenuTitlePartner", "Choose your partner"));
	}
	if (LockLabelText)
	{
		LockLabelText->SetText(bSelfStage
			? NSLOCTEXT("DyadStudy", "ConfirmAvatar", "Confirm avatar")
			: NSLOCTEXT("DyadStudy", "ConfirmPartner", "Confirm partner"));
	}
	if (LockButton)
	{
		const bool bStageHasChoice = bSelfStage
			? !Session->GetSelfAvatarId().IsNone()
			: !Session->GetPartnerAvatarId().IsNone();
		LockButton->SetIsEnabled(!bLocked && bStageHasChoice);
	}
	if (StatusText)
	{
		FString Status;
		const UWorld* World = GetWorld();
		UDyadLinkSubsystem* Link = World && World->GetGameInstance()
			? World->GetGameInstance()->GetSubsystem<UDyadLinkSubsystem>()
			: nullptr;
		if (bLocked && Link && Link->GetTravelState() == EDyadTravelState::WaitingForPeer)
		{
			Status = TEXT("Waiting for partner...");
		}
		else if (bLocked && Link && Link->GetTravelState() == EDyadTravelState::Traveling)
		{
			Status = TEXT("Starting...");
		}
		else
		{
			Status = FString::Printf(TEXT("%syou = %s, partner = %s"),
				bLocked ? TEXT("Locked: ") : TEXT(""),
				*Session->GetSelfAvatarId().ToString(), *Session->GetPartnerAvatarId().ToString());
		}
		StatusText->SetText(FText::FromString(Status));
	}
}
