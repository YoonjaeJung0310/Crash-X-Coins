// CXPlayerController.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Subsystem/ChatSubsystem.h"
#include "CXPlayerController.generated.h"

class UInputMappingContext;
class ACXPawn;
class UCXUI;
class UCXMatchAudioComponent;
class UCXLobbyUI;

/**
 *  Vehicle Player Controller class
 *  Handles input mapping and user interface
 */
UCLASS(abstract)
class CX_API ACXPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACXPlayerController();

protected:
	//UChatUI와 GameMode가 protected RPC를 부를 수 있게 friend로 열어둠
	friend class UChatUI;
	friend class ACXGameMode;

	//접속 직후 서버가 들고 있는 최근 채팅을 이 컨트롤러의 클라로 보냄
	UFUNCTION(Client, Reliable)
	void STCChatLog(const TArray<FChatData>& ChatLog);

	//클라 → 서버 채팅 전송. 위젯은 복제가 안 돼서 소유 클라의 PlayerController를 RPC 통로로 씀
	UFUNCTION(Server, Reliable)
	void CTSChatMessage(const FString& Message);

	//서버 → 이 컨트롤러의 클라로 채팅 전달. 서버가 플레이어마다 호출해야 모두 같은 메시지를 받음
	UFUNCTION(Client, Reliable)
	void STCChatMessage(const FString& Sender, const FString& Message);

	UFUNCTION(Server, Reliable)
	void CTSArenaReady();
	UFUNCTION(Client, Reliable)
	void STCReturnToMainMenu();

protected:

	/** Input Mapping Context to be used for player input */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputMappingContext* InputMappingContext;

	/** If true, the optional steering wheel input mapping context will be registered */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	bool bUseSteeringWheelControls = false;

	/** Optional Input Mapping Context to be used for steering wheel input.
	 *  This is added alongside the default Input Mapping Context and does not block other forms of input.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input, meta=(EditCondition="bUseSteeringWheelControls"))
	UInputMappingContext* SteeringWheelInputMappingContext;

	/** Pointer to the controlled vehicle pawn */
	UPROPERTY(Replicated)
	TObjectPtr<ACXPawn> VehiclePawn;

	/** Type of the UI to spawn */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = UI)
	TSubclassOf<UCXUI> VehicleUIClass;

	/** Pointer to the UI widget */
	//만든 차량 HUD를 GC가 추적하게 UPROPERTY 붙임. 채팅 자식 위젯은 이 HUD가 들고 있음
	UPROPERTY(Transient)
	TObjectPtr<UCXUI> VehicleUI;

	UPROPERTY(Transient)
	TObjectPtr<UCXLobbyUI> LobbyUI;

	// Begin Actor interface
protected:

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const;

public:

	virtual void Tick(float Delta) override;
	void ToggleLobbyOverlay();

	UFUNCTION(Client, Reliable)
	void STCCriticalHit();
	UFUNCTION(Client, Reliable)
	void STCWheelHit();

	// End Actor interface

	// Begin PlayerController interface
protected:

	virtual void OnPossess(APawn* InPawn) override;

	// End PlayerController interface

private:
	UPROPERTY(VisibleAnywhere, Category = "Audio")
	TObjectPtr<UCXMatchAudioComponent> MatchAudio;

	//접속 기록 RPC와 경기 HUD 준비 때 쓰는 재생 함수. UI가 아직 없으면 버퍼 그대로 둠
	void ReplayChatLog();
	void UpdateSessionUI();
	void FocusChatInput();
	void FocusGameInput();
	bool bGameplayUIReady = false;
	bool bArenaReady = false;
	bool bLobbyOverlayOpen = false;
};
