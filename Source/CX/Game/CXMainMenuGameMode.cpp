#include "Game/CXMainMenuGameMode.h"
#include "Player/CXMainMenuController.h"
#include "Engine/World.h"
#include "TimerManager.h"

ACXMainMenuGameMode::ACXMainMenuGameMode()
{
	PlayerControllerClass = ACXMainMenuController::StaticClass();
	DefaultPawnClass = nullptr;
}

void ACXMainMenuGameMode::StartPlay()
{
	Super::StartPlay();
	if (GetNetMode() == NM_DedicatedServer)
	{
		//PIE가 현재 메뉴 맵으로 별도 서버를 띄워도 서버는 경기장으로 이동함
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UE_LOG(LogTemp, Log, TEXT("CX_MENU_SERVER_TO_ARENA"));
			//상대 이동으로 서버 포트를 유지하고 메뉴 GameMode 옵션만 교체함
			GetWorld()->ServerTravel(TEXT("/Game/CX/Maps/L_CXCityArena?game=/Game/VehicleTemplate/Blueprints/BP_VehicleAdvGameMode.BP_VehicleAdvGameMode_C"), false);
		}));
	}
}
