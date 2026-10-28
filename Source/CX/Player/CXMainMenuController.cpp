#include "Audio/CXMatchAudioComponent.h"
#include "Player/CXMainMenuController.h"
#include "Game/CXGameInstance.h"
#include "UI/CXMainMenu.h"

ACXMainMenuController::ACXMainMenuController()
{
	MatchAudio = CreateDefaultSubobject<UCXMatchAudioComponent>(TEXT("MatchAudio"));
}

void ACXMainMenuController::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer || IsLocalController() == false)
	{
		return;
	}
	MainMenu = CreateWidget<UCXMainMenu>(this);
	if (IsValid(MainMenu))
	{
		MainMenu->AddToViewport();
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(MainMenu->TakeWidget());
		SetInputMode(InputMode);
		bShowMouseCursor = true;
	}
	if (UCXGameInstance* CXGameInstance = GetGameInstance<UCXGameInstance>())
	{
		CXGameInstance->OnMainMenuOpened();
		CXGameInstance->TryStartupJoin();
	}
}
