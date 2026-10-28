#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CXVehicleAudioComponent.generated.h"

class UAudioComponent;
class USoundBase;

//내 차량의 주행음과 서버가 알린 단발 충돌음을 화면에서 재생함
UCLASS()
class CX_API UCXVehicleAudioComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCXVehicleAudioComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	void PlayCrashSound();
	void PlayAttackSound(const FVector& Location);
	void PlayMineInstallSound(const FVector& Location);

protected:
	virtual void BeginPlay() override;

private:
	void UpdateLoop(TObjectPtr<UAudioComponent>& Audio, USoundBase* Sound, bool bShouldPlay);
	void PlayOneShot(USoundBase* Sound, const FVector& Location);

	UPROPERTY()
	TObjectPtr<USoundBase> AccelerationSound;
	UPROPERTY()
	TObjectPtr<USoundBase> HighSpeedSound;
	UPROPERTY()
	TObjectPtr<USoundBase> BrakeSound;
	UPROPERTY()
	TObjectPtr<USoundBase> CrashSound;
	UPROPERTY()
	TObjectPtr<USoundBase> AttackSound;
	UPROPERTY()
	TObjectPtr<USoundBase> MineInstallSound;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AccelerationAudio;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> HighSpeedAudio;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BrakeAudio;

	double LastCrashSoundTime = -10.0;
	float PreviousSpeedKmh = -1.f;
};
