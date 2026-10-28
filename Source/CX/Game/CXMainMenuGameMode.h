#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CXMainMenuGameMode.generated.h"

UCLASS()
class CX_API ACXMainMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACXMainMenuGameMode();
	virtual void StartPlay() override;
};
