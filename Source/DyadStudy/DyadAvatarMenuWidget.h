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

// DYADIC_STUDY_PLAN Phase 2: the avatar selection menu, 100% C++ (no Blueprint asset).
//
// Two rows of portrait buttons — "You" and "Your partner" — plus a lock button and a
// status line. Every button calls exactly one UDyadSessionSubsystem function; the widget
// polls the subsystem each tick for highlight/lock state, so the state machine stays the
// single source of truth. Portraits are loose PNGs under Content/DyadStudy/Portraits/
// (<AvatarId>.png), runtime-loaded; a labeled tint stands in when a PNG is missing.
// Hosted world-space on a UWidgetComponent by ADyadLobbyStageActor; clicked by Quest
// pinch through WidgetInteractionComponents (wired by the stage actor).
UCLASS()
class DYADSTUDY_API UDyadAvatarMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// The tree is built in RebuildWidget (NOT NativeConstruct, which fires after Slate
	// has already been generated from the — otherwise empty — tree).
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UFUNCTION()
	void HandleLockClicked();

	void BuildMenuTree();
	UWidget* BuildChoiceRow(EDyadAvatarSlot InSlot, const FText& Label);
	void RefreshFromSession();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDyadAvatarMenuButton>> ChoiceButtons;

	UPROPERTY(Transient)
	TObjectPtr<UButton> LockButton = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText = nullptr;

	UPROPERTY(Transient)
	TMap<TObjectPtr<UDyadAvatarMenuButton>, TObjectPtr<UBorder>> ButtonFrames;
};
