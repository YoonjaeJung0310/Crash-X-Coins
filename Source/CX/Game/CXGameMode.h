// CXGameMode.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/GameModeBase.h"
#include "CXGameMode.generated.h"

class ACXPlayerController;

UCLASS(MinimalAPI)
class ACXGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACXGameMode();
	void NotifyArenaReady(ACXPlayerController* Player);
	virtual void Tick(float DeltaSeconds) override;

	virtual void InitGameState() override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual void Logout(AController* Exiting) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal) override;

protected:
	//두 플레이어가 모이기 전에는 Waiting 상태로 계속 대기함
	virtual void BeginPlay() override;

	//레벨 종료나 GameMode 제거 때 경기 타이머부터 정리함
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	//대기 → 진행 → 종료 단계 함수. 타이머 핸들 하나를 단계마다 이어서 씀
	void StartWaitingCountdown();
	void HandleWaitingTimerTick();
	void StartPlayingState();
	void HandlePlayingTimerTick();
	void FinishGame();
	void HandleFinishedTimerTick();
	void ReturnToLobby();

	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/** Called after a successful login.  This is the first place it is safe to call replicated functions on the PlayerController. */
	//새 플레이어 로그인 후 서버의 최근 채팅을 그 클라에 보내줌
	virtual void PostLogin(APlayerController* NewPlayer) override;

private:
	bool IsPlayerStartAvailable(const APlayerStart* PlayerStart) const;
	FString GetAdmissionError(const FString& Options);
	void RemoveSpectators();
	void CancelWaitingCountdown();
	bool ArePlayersReady();
	void ResetEmptyArena();

private:
	UPROPERTY(EditDefaultsOnly)
	FVector VehicleSpawnCheckHalfExtent = FVector(250.0, 120.0, 80.0);

	UPROPERTY(EditDefaultsOnly)
	FVector VehicleSpawnCheckOffset = FVector(0.0, 0.0, 80.0);

	//두 명이 모이면 켜고, 시작 전에 한 명이 나가면 해제함
	bool bWaitingCountdownStarted = false;

	//대기·진행 타이머를 따로 안 만들고, 단계가 바뀔 때 해제 후 같은 핸들을 다시 씀
	FTimerHandle GameTimerHandle;

};



