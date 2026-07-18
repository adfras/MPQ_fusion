// DYADIC_STUDY_PLAN — editor-only authoring command for the lobby menu's
// WidgetBlueprint skin (Alan's 2026-07-17 direction: the visible menu must be a real
// asset he can open and edit in the UMG Designer).
//
// mp.DyadAuthorMenuWidget rebuilds /Game/DyadStudy/UI/WBP_DyadAvatarMenu's designer
// tree to the NAME CONTRACT UDyadAvatarMenuWidget::DiscoverDesignerTree drives
// (AvatarButton_/AvatarFrame_/Portrait_<Self|Partner>_<Id>, TitleText, StatusText,
// ConfirmButton, ConfirmLabel, SelfRow, PartnerRow). It exists because the in-editor
// MCP's widget-tree verbs (add_border etc.) discard requested names and ensure-fail
// (2026-07-17); the command is idempotent and re-runnable after hand edits go wrong.
// Portrait brushes are NOT baked — the widget fills them at runtime from the loose
// PNGs, identically to the C++-built skin.

#if WITH_EDITOR

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MediaPipeMetaHumanProfile.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

DEFINE_LOG_CATEGORY_STATIC(LogDyadMenuAuthor, Log, All);

namespace
{
const TCHAR* GDyadMenuWidgetAssetPath = TEXT("/Game/DyadStudy/UI/WBP_DyadAvatarMenu.WBP_DyadAvatarMenu");

void SetFontSize(UTextBlock* Text, const int32 Size)
{
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = Size;
	Text->SetFont(Font);
}

void AuthorDyadMenuWidgetAsset()
{
	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, GDyadMenuWidgetAssetPath);
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		UE_LOG(LogDyadMenuAuthor, Error,
			TEXT("mp.DyadAuthorMenuWidget: %s missing (create the WidgetBlueprint first)."),
			GDyadMenuWidgetAssetPath);
		return;
	}
	UWidgetTree* Tree = WidgetBP->WidgetTree;

	// Idempotence: push every pre-existing widget out of the asset so reruns (and the
	// MCP tool's mis-named leftovers) leave no zombies behind the new tree.
	TArray<UWidget*> OldWidgets;
	Tree->GetAllWidgets(OldWidgets);
	Tree->RootWidget = nullptr;
	for (UWidget* Old : OldWidgets)
	{
		Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	}

	TArray<FMediaPipeMetaHumanProfileDefinition> Profiles;
	GetMediaPipeAvailableMetaHumanProfiles(Profiles);

	// The root always stretches to the WidgetComponent's DrawSize (1400x700); the dark
	// card must NOT — a root-level Border rendered as a half-empty black slab in the
	// 2026-07-17 desk check. The outer box top-aligns the card and lets it hug content.
	UVerticalBox* Outer = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MenuOuter"));
	Tree->RootWidget = Outer;
	UBorder* Backdrop = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MenuBackdrop"));
	Backdrop->SetBrushColor(FLinearColor(0.015f, 0.015f, 0.025f, 0.94f));
	Backdrop->SetPadding(FMargin(28.0f, 20.0f, 28.0f, 16.0f));
	Outer->AddChildToVerticalBox(Backdrop);

	UVerticalBox* Root = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MenuRoot"));
	Backdrop->SetContent(Root);

	// Sizes revised 2026-07-18 (worn): buttons were too small to pinch-click and the
	// text was not vision friendly at panel distance.
	UTextBlock* Title = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	Title->SetText(NSLOCTEXT("DyadStudy", "MenuTitleSelf", "Choose your avatar"));
	SetFontSize(Title, 34);
	Root->AddChildToVerticalBox(Title)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));

	struct FRowSpec { const TCHAR* Prefix; FText Label; };
	const FRowSpec Rows[] = {
		{ TEXT("Self"), NSLOCTEXT("DyadStudy", "SelfRow", "You") },
		{ TEXT("Partner"), NSLOCTEXT("DyadStudy", "PartnerRow", "Your partner") },
	};
	for (const FRowSpec& Spec : Rows)
	{
		UVerticalBox* RowBox = Tree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), FName(*FString::Printf(TEXT("%sRow"), Spec.Prefix)));
		Root->AddChildToVerticalBox(RowBox)->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 4.0f));

		UTextBlock* RowLabel = Tree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), FName(*FString::Printf(TEXT("%sRowLabel"), Spec.Prefix)));
		RowLabel->SetText(Spec.Label);
		SetFontSize(RowLabel, 24);
		RowBox->AddChildToVerticalBox(RowLabel)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));

		UHorizontalBox* Buttons = Tree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(), FName(*FString::Printf(TEXT("%sButtons"), Spec.Prefix)));
		RowBox->AddChildToVerticalBox(Buttons);

		for (const FMediaPipeMetaHumanProfileDefinition& Profile : Profiles)
		{
			const FString Suffix = FString::Printf(TEXT("%s_%s"), Spec.Prefix, *Profile.ProfileId.ToString());
			UBorder* Frame = Tree->ConstructWidget<UBorder>(
				UBorder::StaticClass(), FName(*(TEXT("AvatarFrame_") + Suffix)));
			Frame->SetBrushColor(FLinearColor(0.16f, 0.16f, 0.20f, 1.0f));
			Frame->SetPadding(FMargin(5.0f));
			Buttons->AddChildToHorizontalBox(Frame)->SetPadding(FMargin(8.0f, 5.0f));

			UButton* Button = Tree->ConstructWidget<UButton>(
				UButton::StaticClass(), FName(*(TEXT("AvatarButton_") + Suffix)));
			Frame->SetContent(Button);

			UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>(
				UVerticalBox::StaticClass(), FName(*(TEXT("AvatarCol_") + Suffix)));
			Button->SetContent(Column);

			USizeBox* PortraitBox = Tree->ConstructWidget<USizeBox>(
				USizeBox::StaticClass(), FName(*(TEXT("PortraitBox_") + Suffix)));
			PortraitBox->SetWidthOverride(180.0f);
			PortraitBox->SetHeightOverride(214.0f);
			Column->AddChildToVerticalBox(PortraitBox);

			UImage* Portrait = Tree->ConstructWidget<UImage>(
				UImage::StaticClass(), FName(*(TEXT("Portrait_") + Suffix)));
			PortraitBox->SetContent(Portrait);

			UTextBlock* NameText = Tree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), FName(*(TEXT("Name_") + Suffix)));
			NameText->SetText(FText::FromName(Profile.ProfileId));
			SetFontSize(NameText, 20);
			NameText->SetJustification(ETextJustify::Center);
			Column->AddChildToVerticalBox(NameText)->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
		}
	}

	UButton* Confirm = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ConfirmButton"));
	UVerticalBoxSlot* ConfirmSlot = Root->AddChildToVerticalBox(Confirm);
	ConfirmSlot->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 6.0f));
	ConfirmSlot->SetHorizontalAlignment(HAlign_Center);
	// A generous fixed hit target: the worn complaint included the confirm click not
	// landing — the old label-sized button was a sliver at panel distance.
	USizeBox* ConfirmBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ConfirmBox"));
	ConfirmBox->SetWidthOverride(420.0f);
	ConfirmBox->SetHeightOverride(80.0f);
	Confirm->SetContent(ConfirmBox);
	UTextBlock* ConfirmLabel = Tree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("ConfirmLabel"));
	ConfirmLabel->SetText(NSLOCTEXT("DyadStudy", "ConfirmAvatar", "Confirm avatar"));
	SetFontSize(ConfirmLabel, 30);
	ConfirmLabel->SetJustification(ETextJustify::Center);
	ConfirmBox->SetContent(ConfirmLabel);

	UTextBlock* Status = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	Status->SetText(FText::GetEmpty());
	SetFontSize(Status, 18);
	Root->AddChildToVerticalBox(Status);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);
	WidgetBP->MarkPackageDirty();
	TArray<UPackage*> Packages = { WidgetBP->GetOutermost() };
	const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(Packages, /*bOnlyDirty*/ false);

	TArray<UWidget*> NewWidgets;
	Tree->GetAllWidgets(NewWidgets);
	UE_LOG(LogDyadMenuAuthor, Log,
		TEXT("mp.DyadAuthorMenuWidget: rebuilt %s — %d widgets, %d cast profiles, saved=%d."),
		GDyadMenuWidgetAssetPath, NewWidgets.Num(), Profiles.Num(), bSaved ? 1 : 0);
}

FAutoConsoleCommand CmdDyadAuthorMenuWidget(
	TEXT("mp.DyadAuthorMenuWidget"),
	TEXT("Rebuild the WBP_DyadAvatarMenu designer tree to the lobby menu name contract ")
	TEXT("(idempotent; editor only). See DyadMenuWidgetAssetAuthor.cpp."),
	FConsoleCommandDelegate::CreateStatic(&AuthorDyadMenuWidgetAsset));
} // namespace

#endif // WITH_EDITOR
