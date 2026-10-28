#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CXMainMenuController.generated.h"

class UCXMainMenu;
class UCXMatchAudioComponent;

UCLASS()
class CX_API ACXMainMenuController : public APlayerController
{
	GENERATED_BODY()

public:
	ACXMainMenuController();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Audio")
	TObjectPtr<UCXMatchAudioComponent> MatchAudio;

	UPROPERTY(Transient)
	TObjectPtr<UCXMainMenu> MainMenu;
};
