#include "Vehicle/CXVehicleAudioComponent.h"
#include "Vehicle/CXPawn.h"
#include "Vehicle/CXVehicleMovementComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

UCXVehicleAudioComponent::UCXVehicleAudioComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	static ConstructorHelpers::FObjectFinder<USoundBase> AccelerationAsset(TEXT("/Game/CX/Sound/CarAccelerate__cut_9sec_.CarAccelerate__cut_9sec_"));
	static ConstructorHelpers::FObjectFinder<USoundBase> HighSpeedAsset(TEXT("/Game/CX/Sound/CarSpeedAbove100__cut_6sec_.CarSpeedAbove100__cut_6sec_"));
	static ConstructorHelpers::FObjectFinder<USoundBase> BrakeAsset(TEXT("/Game/CX/Sound/CarBrakeSound.CarBrakeSound"));
	static ConstructorHelpers::FObjectFinder<USoundBase> CrashAsset(TEXT("/Game/CX/Sound/CarDamageOrCrash.CarDamageOrCrash"));
	static ConstructorHelpers::FObjectFinder<USoundBase> AttackAsset(TEXT("/Game/CX/Sound/LaserAttack.LaserAttack"));
	static ConstructorHelpers::FObjectFinder<USoundBase> MineInstallAsset(TEXT("/Game/CX/Sound/MineInstall.MineInstall"));
	AttackSound = AttackAsset.Object;
	MineInstallSound = MineInstallAsset.Object;
	AccelerationSound = AccelerationAsset.Object;
	HighSpeedSound = HighSpeedAsset.Object;
	BrakeSound = BrakeAsset.Object;
	CrashSound = CrashAsset.Object;
}

void UCXVehicleAudioComponent::BeginPlay()
{
	Super::BeginPlay();
	//입력 잠금이 적용된 뒤 페달 값을 읽음. 전용 서버에는 재생용 Tick도 안 만듦
	AddTickPrerequisiteActor(GetOwner());
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false);
	}
}

void UCXVehicleAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ACXPawn* Vehicle = Cast<ACXPawn>(GetOwner());
	UCXVehicleMovementComponent* Movement = IsValid(Vehicle) ? Cast<UCXVehicleMovementComponent>(Vehicle->GetChaosVehicleMovement().Get()) : nullptr;
	const bool bPlaying = IsValid(Movement) && Vehicle->IsLocallyControlled() && Vehicle->IsGamePlaying();
	const FVector Velocity = IsValid(Vehicle) ? Vehicle->GetVelocity() : FVector::ZeroVector;
	const float SpeedKmh = Velocity.Size2D() * .036f;
	const bool bHighSpeed = SpeedKmh >= 100.f;
	//무입력 감속은 빼고, 전진·후진 제동 입력이 있고 속력이 줄 때만 재생함
	const bool bBraking = bPlaying && (Movement->HasBrakingInput() || Vehicle->IsHandbrakeHeld())
		&& SpeedKmh < PreviousSpeedKmh;
	//같은 월드의 다른 차와 대기 상태의 자동 핸드브레이크는 내 주행음 조건에서 제외함
	UpdateLoop(AccelerationAudio, AccelerationSound, bPlaying && !bHighSpeed && Movement->HasAccelerationInput());
	UpdateLoop(HighSpeedAudio, HighSpeedSound, bPlaying && bHighSpeed);
	UpdateLoop(BrakeAudio, BrakeSound, bBraking);
	//대기·종료 뒤 첫 프레임에 이전 경기 속도를 감속으로 오인하지 않게 함
	PreviousSpeedKmh = bPlaying ? SpeedKmh : -1.f;
}

void UCXVehicleAudioComponent::UpdateLoop(TObjectPtr<UAudioComponent>& Audio, USoundBase* Sound, bool bShouldPlay)
{
	if (GetNetMode() == NM_DedicatedServer || !IsValid(Sound))
	{
		return;
	}
	if (bShouldPlay && !IsValid(Audio))
	{
		Audio = NewObject<UAudioComponent>(GetOwner());
		Audio->bAutoActivate = false;
		Audio->bAutoDestroy = false;
		Audio->SetupAttachment(GetOwner()->GetRootComponent());
		Audio->SetSound(Sound);
		Audio->RegisterComponent();
	}
	if (IsValid(Audio))
	{
		if (bShouldPlay && !Audio->IsPlaying())
		{
			Audio->Play();
		}
		else if (!bShouldPlay && Audio->IsPlaying())
		{
			Audio->Stop();
		}
	}
}

void UCXVehicleAudioComponent::PlayCrashSound()
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || GetNetMode() == NM_DedicatedServer || !IsValid(CrashSound)
		|| World->GetTimeSeconds() - LastCrashSoundTime < 0.35)
	{
		return;
	}
	//피격 뒤 같은 차체가 바닥에 닿아도 짧은 간격에 같은 소리가 겹치지 않게 함
	LastCrashSoundTime = World->GetTimeSeconds();
	UGameplayStatics::PlaySoundAtLocation(this, CrashSound, GetOwner()->GetActorLocation());
}

void UCXVehicleAudioComponent::PlayOneShot(USoundBase* Sound, const FVector& Location)
{
	if (IsValid(GetWorld()) && GetNetMode() != NM_DedicatedServer && IsValid(Sound))
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Location);
	}
}

void UCXVehicleAudioComponent::PlayAttackSound(const FVector& Location)
{
	PlayOneShot(AttackSound, Location);
}

void UCXVehicleAudioComponent::PlayMineInstallSound(const FVector& Location)
{
	PlayOneShot(MineInstallSound, Location);
}

void UCXVehicleAudioComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (UAudioComponent* Audio : {AccelerationAudio.Get(), HighSpeedAudio.Get(), BrakeAudio.Get()})
	{
		if (IsValid(Audio))
		{
			Audio->Stop();
			Audio->DestroyComponent();
		}
	}
	Super::EndPlay(EndPlayReason);
}
