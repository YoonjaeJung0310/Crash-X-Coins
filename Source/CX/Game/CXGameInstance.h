#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/EngineBaseTypes.h"
#include "CXGameInstance.generated.h"

class UNetDriver;
class UAudioComponent;

UCLASS(Config = Game)
class CX_API UCXGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;

	//방 코드는 IPv4와 포트를 담은 주소 표현임. 서버를 새로 만들거나 인증하지는 않음
	static bool ResolveRoomAddress(const FString& RoomAddress, FString& OutEndpoint, FString& OutRoomCode);
	static FString SanitizePlayerName(const FString& PlayerName);
	FString GetDefaultServerAddress() const;
	bool StartMatchmaking(const FString& PlayerName, const FString& ServerAddress = FString());
	const FString& GetPlayerName() const { return PreferredPlayerName; }
	bool JoinRoom(const FString& RoomAddress, const FString& PlayerName);
	void LeaveRoom();
	void OnMainMenuOpened();
	void OnRoomJoined();
	void TryStartupJoin();
	const FString& GetConnectionMessage() const { return ConnectionMessage; }
	bool IsConnecting() const { return bConnecting; }
	void PlayMenuMusic();
	void StopMenuMusic();

private:
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& Error);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error);
	void ReturnAfterFailure();

	//메뉴 주소 입력의 기본값으로 쓰고, 마지막 입력 주소는 현재 실행 동안 기억함
	UPROPERTY(Config)
	FString MatchServerAddress = TEXT("127.0.0.1:7777");

	//메뉴에서 매칭 대기로 이동해도 같은 곡을 이어서 재생함
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MenuMusic;

	FString PreferredPlayerName;
	FString PreferredServerAddress;
	FString ConnectionMessage;
	bool bConnecting = false;
	bool bReturningToMenu = false;
	bool bStartupJoinConsumed = false;
};
