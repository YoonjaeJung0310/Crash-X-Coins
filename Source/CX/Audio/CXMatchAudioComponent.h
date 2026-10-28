#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CXMatchAudioComponent.generated.h"

class UAudioComponent;
class USoundBase;

UCLASS()
class CX_API UCXMatchAudioComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCXMatchAudioComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void PlayPhase(USoundBase* Sound, float StartTime, const TCHAR* Phase);
	void StopPhase();

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> PhaseAudio;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CountdownSound;
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> GameplaySound;

	double LastCountdownStart = -1.0;
	bool bGameplayMusicStarted = false;
};
