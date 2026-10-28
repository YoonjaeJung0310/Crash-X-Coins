#include "Audio/CXMatchAudioComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Game/CXGameState.h"
#include "Game/CXGameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Player/CXMainMenuController.h"
#include "Sound/SoundBase.h"

UCXMatchAudioComponent::UCXMatchAudioComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UCXMatchAudioComponent::BeginPlay()
{
	Super::BeginPlay();
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (GetNetMode() == NM_DedicatedServer || PC == nullptr || PC->IsLocalController() == false)
	{
		SetComponentTickEnabled(false);
		return;
	}
	if (UCXGameInstance* Instance = GetWorld()->GetGameInstance<UCXGameInstance>())
	{
		Instance->PlayMenuMusic();
	}
	if (PC->IsA<ACXMainMenuController>())
	{
		SetComponentTickEnabled(false);
		return;
	}
	CountdownSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/CX/Sound/Countdown10to0andstart.Countdown10to0andstart"));
	GameplaySound = LoadObject<USoundBase>(nullptr, TEXT("/Game/CX/Sound/WhileGamePlay.WhileGamePlay"));
}

void UCXMatchAudioComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopPhase();
	Super::EndPlay(EndPlayReason);
}

void UCXMatchAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const ACXGameState* State = GetWorld() ? GetWorld()->GetGameState<ACXGameState>() : nullptr;
	if (State == nullptr)
	{
		return;
	}
	UCXGameInstance* Instance = GetWorld()->GetGameInstance<UCXGameInstance>();
	if (State->GetCurrentGameState() == EGameState::Waiting && State->IsCountdownActive() == false)
	{
		StopPhase();
		if (Instance)
		{
			Instance->PlayMenuMusic();
		}
		LastCountdownStart = -1.0;
		bGameplayMusicStarted = false;
		return;
	}
	//City 화면으로 넘어가는 카운트다운부터 메뉴 곡을 끔
	if (Instance)
	{
		Instance->StopMenuMusic();
	}
	if (State->GetCurrentGameState() == EGameState::Finished)
	{
		StopPhase();
		LastCountdownStart = -1.0;
		bGameplayMusicStarted = false;
		return;
	}

	//같은 서버 시각에서 재생 위치를 잡아 복제가 늦어져도 10부터 다시 읽지 않게 함
	const double CountdownStart = State->GetCountdownStartServerTime();
	const double Elapsed = FMath::Max(0.0, State->GetServerWorldTimeSeconds() - CountdownStart);
	const float CountdownDuration = CountdownSound ? CountdownSound->GetDuration() : 10.f;
	if (LastCountdownStart != CountdownStart)
	{
		LastCountdownStart = CountdownStart;
		bGameplayMusicStarted = false;
		if (CountdownSound && Elapsed < CountdownDuration)
		{
			PlayPhase(CountdownSound, static_cast<float>(Elapsed), TEXT("Countdown"));
		}
	}
	if (State->GetCurrentGameState() == EGameState::Playing && bGameplayMusicStarted == false && Elapsed >= CountdownDuration)
	{
		bGameplayMusicStarted = true;
		//START 음성 꼬리까지 끝난 뒤 경기 음악을 한 번 재생함
		PlayPhase(GameplaySound, static_cast<float>(Elapsed - CountdownDuration), TEXT("Gameplay"));
	}
}

void UCXMatchAudioComponent::PlayPhase(USoundBase* Sound, float StartTime, const TCHAR* Phase)
{
	StopPhase();
	if (Sound)
	{
		PhaseAudio = UGameplayStatics::SpawnSound2D(this, Sound, .65f, 1.f, StartTime, nullptr, false, false);
		UE_LOG(LogTemp, Log, TEXT("CX_AUDIO_PHASE Phase=%s Offset=%.3f Sound=%s ComponentCreated=%d Playing=%d"),
			Phase, StartTime, *Sound->GetName(), IsValid(PhaseAudio), IsValid(PhaseAudio) && PhaseAudio->IsPlaying());
	}
}

void UCXMatchAudioComponent::StopPhase()
{
	if (IsValid(PhaseAudio))
	{
		PhaseAudio->Stop();
		PhaseAudio->DestroyComponent();
		PhaseAudio = nullptr;
	}
}
