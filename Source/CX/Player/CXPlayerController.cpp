#include "Audio/CXMatchAudioComponent.h"
// CXPlayerController.cpp


#include "Player/CXPlayerController.h"
#include "Vehicle/CXPawn.h"
#include "UI/CXUI.h"
#include "Subsystem/ChatSubsystem.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "EngineUtils.h"
#include "Game/CXGameState.h"
#include "Game/CXGameMode.h"
#include "Game/CXMatchRules.h"
#include "Game/CXGameInstance.h"
#include "UI/CXLobbyUI.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

ACXPlayerController::ACXPlayerController()
{
	MatchAudio = CreateDefaultSubobject<UCXMatchAudioComponent>(TEXT("MatchAudio"));
}

void ACXPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalController() == false)
	{
		return;
	}

	LobbyUI = CreateWidget<UCXLobbyUI>(this);
	if (IsValid(LobbyUI))
	{
		LobbyUI->AddToViewport(20);
	}
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
	if (UCXGameInstance* CXGameInstance = GetGameInstance<UCXGameInstance>())
	{
		CXGameInstance->OnRoomJoined();
	}
	UpdateSessionUI();
	UE_LOG(LogTemp, Log, TEXT("CX_CLIENT_ARENA_READY Controller=%s"), *GetName());
}

void ACXPlayerController::STCChatLog_Implementation(const TArray<FChatData>& ChatLog)
{
	// Client 시점
	// 신규 접속자가 서버로 부터 채팅 로그를 받음
	if (IsValid(GetWorld()) == false)
	{
		return;
	}

	if (UChatSubsystem* ChatSubsystem = GetWorld()->GetSubsystem<UChatSubsystem>())
	{
		ChatSubsystem->SetChatLog(ChatLog);
		ReplayChatLog(); //HUD가 이미 있으면 바로 재생, 없으면 경기 HUD가 준비된 뒤 재생
	}
}

void ACXPlayerController::CTSChatMessage_Implementation(const FString& Message)
{
	//RPC 인자는 클라가 보낸 값이라 서버에서도 빈 입력 검사하고 200자까지만 씀
	const FString ValidatedMessage = Message.TrimStartAndEnd().Left(200);
	if (ValidatedMessage.IsEmpty() || IsValid(PlayerState) == false || IsValid(GetWorld()) == false)
	{
		return;
	}

	//보낸 사람 이름은 클라가 보낸 문자열이 아니라 서버의 PlayerState에서 가져옴
	const FString PlayerName = PlayerState->GetPlayerName();
	if (UChatSubsystem* ChatSubsystem = GetWorld()->GetSubsystem<UChatSubsystem>())
	{
		ChatSubsystem->AddChatMessage(PlayerName, ValidatedMessage); //서버 최근 기록은 새로 들어온 플레이어에게 보내려고 저장해둠
	}

	//PlayerController는 서버와 그 소유 클라에만 있어서 서버에서 컨트롤러마다 Client RPC를 호출함
	for (TActorIterator<ACXPlayerController> It(GetWorld()); It; ++It)
	{
		if (ACXPlayerController* PC = *It)
		{
			PC->STCChatMessage(PlayerName, ValidatedMessage);
		}
	}
}

void ACXPlayerController::STCChatMessage_Implementation(const FString& Sender, const FString& Message)
{
	//받은 채팅은 각자 로컬 위젯에 쌓음. 위젯이 아직 없으면 접근 안 함
	if (IsValid(VehicleUI))
	{
		VehicleUI->OnNewChatMessage(Sender, Message);
	}
	else if (HasAuthority() == false && IsValid(GetWorld()))
	{
		//클라 UI가 아직 없으면 같은 FChatData 버퍼에 보관. 리슨 서버는 CTS에서 이미 서버 기록에 넣어서 중복 저장 안 함
		if (UChatSubsystem* ChatSubsystem = GetWorld()->GetSubsystem<UChatSubsystem>())
		{
			ChatSubsystem->AddChatMessage(Sender, Message);
		}
	}
}

void ACXPlayerController::ReplayChatLog()
{
	if (IsValid(VehicleUI) == false || IsValid(GetWorld()) == false)
	{
		return;
	}

	if (UChatSubsystem* ChatSubsystem = GetWorld()->GetSubsystem<UChatSubsystem>())
	{
		const TArray<FChatData>& ChatLog = ChatSubsystem->GetChatLog();
		for (const FChatData& ChatData : ChatLog)
		{
			VehicleUI->OnNewChatMessage(ChatData.Sender, ChatData.Message);
		}

		//클라의 준비 전 버퍼만 비움. 리슨 서버는 로컬 UI와 서버 기록이 같은 WorldSubsystem이라 서버 기록은 남김
		if (HasAuthority() == false)
		{
			ChatSubsystem->ClearChatLog();
		}
	}
}

void ACXPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::M, IE_Pressed, this, &ACXPlayerController::ToggleLobbyOverlay);
		InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ACXPlayerController::FocusChatInput);
	}

	// get the enhanced input subsystem
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		// add the mapping context so we get controls
		if (IsValid(InputMappingContext))
		{
			Subsystem->AddMappingContext(InputMappingContext, 0);
		}

		// optionally add the steering wheel context
		if (bUseSteeringWheelControls && SteeringWheelInputMappingContext)
		{
			Subsystem->AddMappingContext(SteeringWheelInputMappingContext, 1);
		}
	}
}

void ACXPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, VehiclePawn);
}

void ACXPlayerController::Tick(float Delta)
{
	Super::Tick(Delta);
	if (IsLocalController())
	{
		UpdateSessionUI();
	}

	if (IsValid(VehiclePawn) && IsValid(VehicleUI))
	{
		VehicleUI->UpdateSpeed(VehiclePawn->GetChaosVehicleMovement()->GetForwardSpeed());
		VehicleUI->UpdateGear(VehiclePawn->GetChaosVehicleMovement()->GetCurrentGear());
	}
}

void ACXPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// get a pointer to the controlled pawn
	VehiclePawn = Cast<ACXPawn>(InPawn);
}


void ACXPlayerController::UpdateSessionUI()
{
	const ACXGameState* State = GetWorld() ? GetWorld()->GetGameState<ACXGameState>() : nullptr;
	if (IsValid(State) == false)
	{
		return;
	}
	if (bGameplayUIReady == false && IsValid(GetPawn()) && IsValid(PlayerState))
	{
		//소유 컨트롤러를 this로 줘야 채팅 위젯도 올바른 RPC 통로를 얻음
		VehicleUI = CreateWidget<UCXUI>(this, VehicleUIClass);
		if (IsValid(VehicleUI))
		{
			VehicleUI->AddToViewport();
			bGameplayUIReady = true;
			ReplayChatLog();
		}
	}
	if (bArenaReady == false && bGameplayUIReady && IsValid(LobbyUI) && IsValid(GetPawn()) && IsValid(PlayerState))
	{
		//맵·차량·두 UI가 준비된 소유 화면에서 한 번만 서버에 준비를 알림
		bArenaReady = true;
		CTSArenaReady();
	}
	const bool bWaitingForOpponent = State->GetCurrentGameState() == EGameState::Waiting && State->IsCountdownActive() == false;
	const bool bPlaying = State->GetCurrentGameState() == EGameState::Playing;
	const bool bStarting = bPlaying && State->GetRemainingTime() == CXMatchRules::RoundSeconds;
	if (IsValid(VehicleUI))
	{
		VehicleUI->SetVisibility(bWaitingForOpponent ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (IsValid(LobbyUI))
	{
		LobbyUI->SetVisibility(bPlaying == false || bStarting || bLobbyOverlayOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	const bool bNeedsCursor = bWaitingForOpponent || bLobbyOverlayOpen;
	if (bShowMouseCursor != bNeedsCursor)
	{
		bShowMouseCursor = bNeedsCursor;
		if (bNeedsCursor)
		{
			FInputModeGameAndUI InputMode;
			InputMode.SetHideCursorDuringCapture(false);
			SetInputMode(InputMode);
		}
		else
		{
			SetInputMode(FInputModeGameOnly());
		}
	}
}

void ACXPlayerController::ToggleLobbyOverlay()
{
	const ACXGameState* State = GetWorld() ? GetWorld()->GetGameState<ACXGameState>() : nullptr;
	if (IsValid(State) && State->GetCurrentGameState() == EGameState::Playing)
	{
		bLobbyOverlayOpen = !bLobbyOverlayOpen;
		UpdateSessionUI();
	}
}

void ACXPlayerController::CTSArenaReady_Implementation()
{
	if (IsValid(GetPawn()) == false || IsValid(PlayerState) == false)
	{
		return;
	}
	bArenaReady = true;
	if (ACXGameMode* Mode = GetWorld()->GetAuthGameMode<ACXGameMode>())
	{
		Mode->NotifyArenaReady(this);
	}
}

void ACXPlayerController::STCReturnToMainMenu_Implementation()
{
	if (UCXGameInstance* Instance = GetGameInstance<UCXGameInstance>())
	{
		Instance->LeaveRoom();
	}
}

void ACXPlayerController::FocusChatInput()
{
	const ACXGameState* State = GetWorld() ? GetWorld()->GetGameState<ACXGameState>() : nullptr;
	if (IsValid(State) && State->GetCurrentGameState() == EGameState::Playing && IsValid(VehicleUI) && bLobbyOverlayOpen == false)
	{
		FlushPressedKeys();
		if (IsValid(VehiclePawn))
		{
			//채팅에 포커스를 넘기기 전에 누르고 있던 축 입력을 비움
			VehiclePawn->GetChaosVehicleMovement()->SetThrottleInput(0.f);
			VehiclePawn->GetChaosVehicleMovement()->SetSteeringInput(0.f);
			VehiclePawn->GetChaosVehicleMovement()->SetBrakeInput(0.f);
		}
		VehicleUI->FocusChatInput();
	}
}

void ACXPlayerController::FocusGameInput()
{
	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
}

void ACXPlayerController::STCCriticalHit_Implementation()
{
	if (IsLocalController() == false || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (VehicleUI)
	{
		VehicleUI->ShowCriticalHit();
	}
	//명중 확인음은 공격한 내 화면에서만 재생함
	if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, TEXT("/Game/CX/Sound/Critical_hit.Critical_hit")))
	{
		UGameplayStatics::PlaySound2D(this, Sound);
	}
	UE_LOG(LogTemp, Log, TEXT("CX_CRITICAL_FEEDBACK Player=%s Time=%.3f"), PlayerState ? *PlayerState->GetPlayerName() : TEXT("None"), GetWorld()->GetTimeSeconds());
}

void ACXPlayerController::STCWheelHit_Implementation()
{
	if (IsLocalController() && GetNetMode() != NM_DedicatedServer)
	{
		if (VehicleUI)
		{
			VehicleUI->ShowWheelHit();
		}
		UE_LOG(LogTemp, Log, TEXT("CX_WHEEL_FEEDBACK Player=%s Time=%.3f"), PlayerState ? *PlayerState->GetPlayerName() : TEXT("None"), GetWorld()->GetTimeSeconds());
	}
}
