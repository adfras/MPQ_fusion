#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "DyadSessionSubsystem.h"

#include "DyadAvatarMenuWidget.generated.h"

class UBorder;
class UHorizontalBox;
class UTextBlock;
class UVerticalBox;

// One portrait button: carries its slot + avatar id and forwards clicks to the session
// subsystem (dynamic delegates carry no sender, so the button knows its own payload).
UCLASS()
class DYADSTUDY_API UDyadAvatarMenuButton : public UButton
{
	GENERATED_BODY()

public:
	void InitChoice(EDyadAvatarSlot InSlot, FName InAvatarId);

	EDyadAvatarSlot ChoiceSlot = EDyadAvatarSlot::Self;
	FName AvatarId;

private:
	UFUNCTION()
	void HandleClicked();
};

// Click payload for PLAIN UButtons in a designer-authored tree (the MCP widget
// authoring places stock buttons): dynamic delegates carry no sender, so discovery
// binds each button to one of these instead — keeping the WidgetBlueprint a pure
// layout with zero graph nodes.
UCLASS()
class DYADSTUDY_API UDyadMenuClickRelay : public UObject
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<class UDyadAvatarMenuWidget> OwnerMenu;
	EDyadAvatarSlot ChoiceSlot = EDyadAvatarSlot::Self;
	FName AvatarId;

	UFUNCTION()
	void HandleClicked();
};

// DYADIC_STUDY_PLAN Phase 2: the avatar selection menu.
//
// Two rows of portrait buttons — "You" and "Your partner" — plus a lock button and a
// status line. Every button calls exactly one UDyadSessionSubsystem function; the widget
// polls the subsystem each tick for highlight/lock state, so the state machine stays the
// single source of truth. Portraits are loose PNGs under Content/DyadStudy/Portraits/
// (<AvatarId>.png), runtime-loaded; a labeled tint stands in when a PNG is missing.
// Hosted world-space on a UWidgetComponent by ADyadLobbyStageActor; clicked by Quest
// pinch through WidgetInteractionComponents (wired by the stage actor).
//
// TWO SKINS, ONE BRAIN (Alan's 2026-07-17 direction — the visible layer must be a real
// editor-editable asset): a WidgetBlueprint child of this class may own the whole visual
// tree in the UMG Designer. When one exists (the stage actor prefers
// /Game/DyadStudy/UI/WBP_DyadAvatarMenu), RebuildWidget sees a designer RootWidget and
// skips the C++ tree; NativeOnInitialized then DISCOVERS the designer widgets by NAME and
// drives them with the same refresh logic:
//   AvatarButton_<Self|Partner>_<ProfileId>  (UDyadAvatarMenuButton — auto-bound; a plain
//                                             UButton also works if its OnClicked calls
//                                             SelectAvatarChoice in the BP graph)
//   AvatarFrame_<Self|Partner>_<ProfileId>   (UBorder highlight frame)
//   Portrait_<Self|Partner>_<ProfileId>      (UImage — C++ fills it from the loose PNG)
//   TitleText, StatusText, ConfirmLabel      (UTextBlock)
//   ConfirmButton                            (UButton)
//   SelfRow, PartnerRow                      (any UWidget; stage visibility)
// With no Blueprint asset present (headless tests, fresh checkouts) the C++ tree builds
// exactly as before — behavior without the asset is unchanged.
UCLASS()
class DYADSTUDY_API UDyadAvatarMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// The C++ tree is built in RebuildWidget (NOT NativeConstruct, which fires after
	// Slate has already been generated from the — otherwise empty — tree). A designer
	// tree from a WidgetBlueprint child suppresses the C++ build.
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// One-liners for WidgetBlueprint button graphs (plain-UButton route): each button
	// calls exactly one of these, keeping the "thin skin" rule intact.
	UFUNCTION(BlueprintCallable, Category = "DyadStudy")
	void SelectAvatarChoice(EDyadAvatarSlot InSlot, FName AvatarId);

	UFUNCTION(BlueprintCallable, Category = "DyadStudy")
	void ConfirmStage();

private:
	UFUNCTION()
	void HandleLockClicked();

	void BuildMenuTree();
	UWidget* BuildChoiceRow(EDyadAvatarSlot InSlot, const FText& Label);
	void DiscoverDesignerTree();
	void RefreshFromSession();

	// Unified view of the choice buttons — filled by BuildChoiceRow (C++ tree) or
	// DiscoverDesignerTree (WidgetBlueprint tree). RefreshFromSession drives ONLY this.
	// Weak refs on purpose: the widgets are GC-owned by the WidgetTree (and, for the
	// C++ path, by the UPROPERTY anchors below); entries just index them.
	struct FChoiceEntry
	{
		TWeakObjectPtr<UButton> Button;
		TWeakObjectPtr<UBorder> Frame;
		EDyadAvatarSlot Slot = EDyadAvatarSlot::Self;
		FName AvatarId;
	};
	TArray<FChoiceEntry> ChoiceEntries;

	// GC anchors for the click relays bound to plain designer buttons.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDyadMenuClickRelay>> ClickRelays;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDyadAvatarMenuButton>> ChoiceButtons;

	UPROPERTY(Transient)
	TObjectPtr<UButton> LockButton = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText = nullptr;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UDyadAvatarMenuButton>, TObjectPtr<UBorder>> ButtonFrames;

	// Sequential-flow presentation (one stage at a time): the title, the visible row,
	// and the confirm label all follow the session's lobby flow stage each tick.
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TitleText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> LockLabelText = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> SelfRow = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UWidget> PartnerRow = nullptr;
};
