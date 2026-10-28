#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/SlateEnums.h"
#include "CXMainMenu.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;

UCLASS()
class CX_API UCXMainMenu : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

	UFUNCTION()
	void OnJoinClicked();
	UFUNCTION()
	void OnHelpClicked();
	UFUNCTION()
	void OnExitClicked();
	UFUNCTION()
	void OnNameCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> NameInput;
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> AddressInput;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> MenuPanel;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> HelpPanel;
	UPROPERTY(Transient)
	TObjectPtr<UButton> JoinButton;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusLabel;
};
