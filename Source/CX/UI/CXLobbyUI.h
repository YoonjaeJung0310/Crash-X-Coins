#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CXLobbyUI.generated.h"

class UButton;
class UOverlay;
class UTextBlock;
class UVerticalBox;

UCLASS()
class CX_API UCXLobbyUI : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

	UFUNCTION()
	void OnLeaveClicked();
	UFUNCTION()
	void OnResumeClicked();

	UPROPERTY(Transient)
	TObjectPtr<UOverlay> MatchingBackground;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> ScenePanel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PlayersLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SceneStatusLabel;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SceneHintLabel;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ResumeButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> LeaveButton;
};
