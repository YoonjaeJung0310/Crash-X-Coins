// CXGameMode.cpp

#include "Game/CXGameMode.h"
#include "Player/CXPlayerController.h"
#include "Player/CXPlayerState.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/Pawn.h"
#include "Game/CXGameState.h"
#include "Subsystem/ChatSubsystem.h"
#include "TimerManager.h"
#include "Game/CXGameInstance.h"
#include "Game/CXMatchRules.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogCXMatch, Log, All);

ACXGameMode::ACXGameMode()
{
	//인원이 부족해 경기 타이머가 멈춰 있어도 영구 관전자를 정리함
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 1.f;
	PlayerControllerClass = ACXPlayerController::StaticClass();
	//코인과 추가 최대 속도를 들고 있는 CXPlayerState를 새 플레이어마다 만들게 연결
	PlayerStateClass = ACXPlayerState::StaticClass();
	//GetGameState<ACXGameState>()가 실제 경기 상태 객체를 받도록 GameStateClass 지정
	GameStateClass = ACXGameState::StaticClass();
}

void ACXGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	RemoveSpectators();
}

void ACXGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (ACXGameState* CXGameState = GetGameState<ACXGameState>(); IsValid(CXGameState) && bWaitingCountdownStarted == false)
	{
		CXGameState->SetGameState(EGameState::Waiting);
		CXGameState->SetRemainingTime(0);
		StartWaitingCountdown();
	}
}

void ACXGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld(); IsValid(World))
	{
		World->GetTimerManager().ClearTimer(GameTimerHandle); //종료 중에 남은 시간 콜백이 불리지 않게 먼저 해제
	}

	Super::EndPlay(EndPlayReason);
}

void ACXGameMode::StartWaitingCountdown()
{
	UWorld* World = GetWorld();
	ACXGameState* CXGameState = GetGameState<ACXGameState>();
	if (HasAuthority() == false || bWaitingCountdownStarted || IsValid(World) == false || IsValid(CXGameState) == false
		|| CXGameState->GetCurrentGameState() != EGameState::Waiting || ArePlayersReady() == false)
	{
		return;
	}

	bWaitingCountdownStarted = true;
	World->GetTimerManager().ClearTimer(GameTimerHandle);
	CXGameState->SetRemainingTime(CXMatchRules::WaitingSeconds);
	CXGameState->SetCountdownActive(true);
	World->GetTimerManager().SetTimer(GameTimerHandle, this, &ACXGameMode::HandleWaitingTimerTick, 1.f, true);
	UE_LOG(LogCXMatch, Log, TEXT("CX_COUNTDOWN_STARTED Players=%d Seconds=%d"), GetNumPlayers(), CXMatchRules::WaitingSeconds);
}

void ACXGameMode::HandleWaitingTimerTick()
{
	UWorld* World = GetWorld();
	ACXGameState* CXGameState = GetGameState<ACXGameState>();
	if (IsValid(World) == false)
	{
		return;
	}
	if (HasAuthority() == false || IsValid(CXGameState) == false || CXGameState->GetCurrentGameState() != EGameState::Waiting)
	{
		World->GetTimerManager().ClearTimer(GameTimerHandle); //이전 단계 콜백이거나 상태 객체가 없으면 시간 안 줄이고 타이머 정리
		return;
	}

	if (ArePlayersReady() == false)
	{
		CancelWaitingCountdown();
		return;
	}

	const int32 NewRemainingTime = CXGameState->GetRemainingTime() - 1;
	CXGameState->SetRemainingTime(NewRemainingTime);
	if (NewRemainingTime <= 0)
	{
		World->GetTimerManager().ClearTimer(GameTimerHandle);
		StartPlayingState(); //대기 시간이 0이 된 같은 서버 처리 안에서 바로 진행 단계로 넘어감
	}
}

void ACXGameMode::StartPlayingState()
{
	UWorld* World = GetWorld();
	ACXGameState* CXGameState = GetGameState<ACXGameState>();
	if (HasAuthority() == false || IsValid(World) == false || IsValid(CXGameState) == false
		|| CXGameState->GetCurrentGameState() != EGameState::Waiting)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(GameTimerHandle); //대기 타이머 정리 후 같은 핸들을 플레이 단계에 씀
	CXGameState->SetCountdownActive(false);
	CXGameState->SetGameState(EGameState::Playing);
	CXGameState->SetRemainingTime(CXMatchRules::RoundSeconds);
	UE_LOG(LogCXMatch, Log, TEXT("CX_ROUND_STARTED Players=%d Seconds=%d"), GetNumPlayers(), CXMatchRules::RoundSeconds);
	World->GetTimerManager().SetTimer(GameTimerHandle, this, &ACXGameMode::HandlePlayingTimerTick, 1.f, true);
}

void ACXGameMode::HandlePlayingTimerTick()
{
	UWorld* World = GetWorld();
	ACXGameState* CXGameState = GetGameState<ACXGameState>();
	if (IsValid(World) == false)
	{
		return;
	}
	if (HasAuthority() == false || IsValid(CXGameState) == false || CXGameState->GetCurrentGameState() != EGameState::Playing)
	{
		World->GetTimerManager().ClearTimer(GameTimerHandle);
		return;
	}

	const int32 NewRemainingTime = CXGameState->GetRemainingTime() - 1;
	CXGameState->SetRemainingTime(NewRemainingTime);
	if (NewRemainingTime <= 0)
	{
		World->GetTimerManager().ClearTimer(GameTimerHandle);
		FinishGame();
	}
}

void ACXGameMode::FinishGame()
{
	UWorld* World = GetWorld();
	ACXGameState* CXGameState = GetGameState<ACXGameState>();
	if (HasAuthority() == false || IsValid(World) == false || IsValid(CXGameState) == false)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(GameTimerHandle);
	if (CXGameState->GetCurrentGameState() == EGameState::Finished)
	{
		return; //이미 Finished면 우승자 목록 다시 계산 안 함
	}

	int32 HighestCoins = -1; //-1에서 시작해야 모두 0코인이어도 첫 플레이어부터 후보에 들어감
	TArray<TObjectPtr<ACXPlayerState>> Winners;
	for (APlayerState* PlayerState : CXGameState->PlayerArray)
	{
		if (ACXPlayerState* CXPlayerState = Cast<ACXPlayerState>(PlayerState);
			IsValid(CXPlayerState) && !CXPlayerState->IsOnlyASpectator() && !CXPlayerState->IsInactive())
		{
			const int32 PlayerCoins = CXPlayerState->GetCoins(); //코인 원본이 있는 서버에서 비교함. 다른 클라의 OwnerOnly 사본은 안 읽음
			if (HighestCoins < PlayerCoins)
			{
				HighestCoins = PlayerCoins;
				Winners.Empty(); //더 높은 기록이 나오면 이전 후보를 비우고 새 최고 기록부터 다시 모음
				Winners.Add(CXPlayerState);
			}
			else if (HighestCoins == PlayerCoins)
			{
				Winners.Add(CXPlayerState); //최고 코인과 같으면 같이 넣어서 공동 우승 처리
			}
		}
	}

	CXGameState->SetWinners(Winners); //서버가 정한 목록만 GameState에 넣음
	CXGameState->SetRemainingTime(CXMatchRules::ResultsSeconds);
	CXGameState->SetGameState(EGameState::Finished);
	World->GetTimerManager().SetTimer(GameTimerHandle, this, &ACXGameMode::HandleFinishedTimerTick, 1.f, true);
	UE_LOG(LogCXMatch, Log, TEXT("CX_ROUND_FINISHED Winners=%d ReturnSeconds=%d"), Winners.Num(), CXMatchRules::ResultsSeconds);
}

void ACXGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	RemoveSpectators();
	if (IsValid(NewPlayer) == false || IsValid(GetWorld()) == false)
	{
		return;
	}

	if (ACXPlayerController* CXPlayerController = Cast<ACXPlayerController>(NewPlayer))
	{
		// 서버 시점에 클라로 채팅 로그를 보내줌
		if (UChatSubsystem* ChatSubsystem = GetWorld()->GetSubsystem<UChatSubsystem>())
		{
			const TArray<FChatData>& ChatLog = ChatSubsystem->GetChatLog(); //읽기만 하는 서버 기록이라 복사 안 하고 참조로 받음
			if (ChatLog.IsEmpty() == false)
			{
				CXPlayerController->STCChatLog(ChatLog);
			}
		}
	}

	UE_LOG(LogCXMatch, Log, TEXT("CX_PLAYER_JOINED Players=%d Name=%s"), GetNumPlayers(), *GetNameSafe(NewPlayer->PlayerState));
	StartWaitingCountdown();
}

AActor* ACXGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	TArray<APlayerStart*> AvailablePlayerStarts;

	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		APlayerStart* PlayerStart = *It;

		if (IsPlayerStartAvailable(PlayerStart))
		{
			AvailablePlayerStarts.Add(PlayerStart);
		}
	}

	if (AvailablePlayerStarts.IsEmpty())
	{
		UE_LOG(LogCXMatch, Warning, TEXT("CX_NO_FREE_PLAYER_START Player=%s"), *GetNameSafe(Player));
		return nullptr;
	}

	const int32 RandomIndex = FMath::RandHelper(AvailablePlayerStarts.Num());
	return Cast<AActor>(AvailablePlayerStarts[RandomIndex]);
}

bool ACXGameMode::IsPlayerStartAvailable(const APlayerStart* PlayerStart) const
{
	if (IsValid(PlayerStart) == false || IsValid(GetWorld()) == false)
	{
		return false;
	}

	const FTransform PlayerStartTransform = PlayerStart->GetActorTransform();
	const FVector CheckLocation = PlayerStartTransform.TransformPosition(VehicleSpawnCheckOffset);
	const FQuat CheckRotation = PlayerStartTransform.GetRotation();
	const FCollisionShape CheckShape = FCollisionShape::MakeBox(VehicleSpawnCheckHalfExtent);
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_Vehicle);

	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(VehiclePlayerStartCheck), false);

	const bool bVehicleExists =
		GetWorld()->OverlapAnyTestByObjectType(
			CheckLocation,
			CheckRotation,
			ObjectQueryParams,
			CheckShape,
			QueryParams
		);

	return !bVehicleExists;
}


void ACXGameMode::InitGameState()
{
	Super::InitGameState();
	FString PublicAddress;
	FString Endpoint;
	FString RoomCode;
	if (FParse::Value(FCommandLine::Get(), TEXT("CXPublicAddress="), PublicAddress)
		&& UCXGameInstance::ResolveRoomAddress(PublicAddress, Endpoint, RoomCode))
	{
		if (ACXGameState* State = GetGameState<ACXGameState>())
		{
			State->SetRoomCode(RoomCode);
		}
		UE_LOG(LogCXMatch, Log, TEXT("CX_ROOM_READY Code=%s Endpoint=%s"), *RoomCode, *Endpoint);
	}
	else
	{
		UE_LOG(LogCXMatch, Warning, TEXT("CX_ROOM_PUBLIC_ADDRESS_UNSET Use -CXPublicAddress=IPv4:Port to publish the share code"));
	}
}

FString ACXGameMode::GetAdmissionError(const FString& Options)
{
	if (UGameplayStatics::HasOption(Options, TEXT("SpectatorOnly")))
	{
		return TEXT("CX_SPECTATORS_NOT_ALLOWED");
	}
	if (GetNumPlayers() >= CXMatchRules::MaximumPlayers)
	{
		return TEXT("CX_ROOM_FULL");
	}
	if (const ACXGameState* State = GetGameState<ACXGameState>(); IsValid(State) && State->GetCurrentGameState() != EGameState::Waiting)
	{
		return TEXT("CX_ROUND_STARTED");
	}
	return FString();
}

void ACXGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (ErrorMessage.IsEmpty())
	{
		ErrorMessage = GetAdmissionError(Options);
	}
	if (ErrorMessage.IsEmpty() == false)
	{
		UE_LOG(LogCXMatch, Warning, TEXT("CX_JOIN_REJECTED Reason=%s"), *ErrorMessage);
	}
}

APlayerController* ACXGameMode::Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	//PreLogin 이후 인원이 달라질 수 있어서 컨트롤러를 만들기 직전에도 검사함
	ErrorMessage = GetAdmissionError(Options);
	if (ErrorMessage.IsEmpty() == false)
	{
		UE_LOG(LogCXMatch, Warning, TEXT("CX_JOIN_REJECTED Reason=%s"), *ErrorMessage);
		return nullptr;
	}
	return Super::Login(NewPlayer, InRemoteRole, Portal, Options, UniqueId, ErrorMessage);
}

FString ACXGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	if (UGameplayStatics::HasOption(Options, TEXT("SpectatorOnly")) || (IsValid(NewPlayerController) && MustSpectate(NewPlayerController)))
	{
		return TEXT("CX_SPECTATORS_NOT_ALLOWED");
	}
	const FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	if (Error.IsEmpty() == false)
	{
		return Error;
	}
	if (IsValid(NewPlayerController) && IsValid(NewPlayerController->PlayerState))
	{
		//기본 Name 옵션은 플랫폼 닉네임으로 바뀔 수 있어서 별도 참가 이름을 서버에서 다시 검사함
		const FString SafeName = UCXGameInstance::SanitizePlayerName(UGameplayStatics::ParseOption(Options, TEXT("CXName")));
		NewPlayerController->PlayerState->SetPlayerName(SafeName);
		UE_LOG(LogCXMatch, Log, TEXT("CX_PLAYER_NAMED Name=%s"), *SafeName);
	}
	return FString();
}

void ACXGameMode::RemoveSpectators()
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !IsValid(World))
	{
		return;
	}
	TArray<TWeakObjectPtr<APlayerController>> Spectators;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* Player = It->Get(); IsValid(Player) && MustSpectate(Player))
		{
			Spectators.Add(Player);
		}
	}
	//퇴장 중 Logout이 컨트롤러 목록을 바꾸므로 대상을 먼저 모음
	for (const TWeakObjectPtr<APlayerController>& Entry : Spectators)
	{
		APlayerController* Player = Entry.Get();
		if (!IsValid(Player))
		{
			continue;
		}
		UE_LOG(LogCXMatch, Warning, TEXT("CX_SPECTATOR_REMOVED Player=%s"), *GetNameSafe(Player));
		if (IsValid(GameSession) && GameSession->KickPlayer(Player, FText::FromString(TEXT("CX_SPECTATORS_NOT_ALLOWED"))))
		{
			continue;
		}
		//KickPlayer는 원격 연결만 처리함. 로컬 관전자는 기존 메뉴 복귀 경로를 씀
		if (Player->GetLocalPlayer())
		{
			if (ACXPlayerController* CXPlayer = Cast<ACXPlayerController>(Player))
			{
				CXPlayer->STCReturnToMainMenu();
			}
		}
		if (IsValid(Player))
		{
			if (APawn* Pawn = Player->GetPawn(); IsValid(Pawn))
			{
				Pawn->Destroy();
			}
			Player->Destroy();
		}
	}
}

void ACXGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);
	int32 RemainingPlayers = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Player = It->Get();
		if (Player && Player != Exiting && IsValid(Player->PlayerState) && MustSpectate(Player) == false)
		{
			++RemainingPlayers;
		}
	}
	UE_LOG(LogCXMatch, Log, TEXT("CX_PLAYER_LEFT Players=%d"), RemainingPlayers);
	if (const ACXGameState* State = GetGameState<ACXGameState>(); IsValid(State) && State->GetCurrentGameState() == EGameState::Waiting
		&& RemainingPlayers < CXMatchRules::MinimumPlayers)
	{
		CancelWaitingCountdown();
	}
}

void ACXGameMode::CancelWaitingCountdown()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GameTimerHandle);
	}
	bWaitingCountdownStarted = false;
	if (ACXGameState* State = GetGameState<ACXGameState>())
	{
		State->SetRemainingTime(0);
		State->SetCountdownActive(false);
	}
	UE_LOG(LogCXMatch, Log, TEXT("CX_COUNTDOWN_CANCELLED Players=%d"), GetNumPlayers());
}

void ACXGameMode::HandleFinishedTimerTick()
{
	ACXGameState* State = GetGameState<ACXGameState>();
	if (IsValid(State) == false || State->GetCurrentGameState() != EGameState::Finished)
	{
		return;
	}
	State->SetRemainingTime(State->GetRemainingTime() - 1);
	if (State->GetRemainingTime() == 0)
	{
		ReturnToLobby();
	}
}

void ACXGameMode::ReturnToLobby()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GameTimerHandle);
		UE_LOG(LogCXMatch, Log, TEXT("CX_RETURN_TO_LOBBY Players=%d"), GetNumPlayers());
		//메뉴 복귀 RPC가 먼저 전달되게 하고 접속이 모두 닫힌 뒤 서버 경기장을 다시 엶
		for (TActorIterator<ACXPlayerController> It(World); It; ++It)
		{
			It->STCReturnToMainMenu();
		}
		World->GetTimerManager().SetTimer(GameTimerHandle, this, &ACXGameMode::ResetEmptyArena, 0.25f, true);
	}
}


bool ACXGameMode::ArePlayersReady()
{
	int32 ReadyPlayers = 0;
	for (TActorIterator<ACXPlayerController> It(GetWorld()); It; ++It)
	{
		if (IsValid(It->PlayerState) && !MustSpectate(*It) && IsValid(It->GetPawn()) && It->bArenaReady)
		{
			++ReadyPlayers;
		}
	}
	return ReadyPlayers >= CXMatchRules::MinimumPlayers && ReadyPlayers == GetNumPlayers();
}

void ACXGameMode::NotifyArenaReady(ACXPlayerController* Player)
{
	if (HasAuthority() && IsValid(Player) && Player->bArenaReady)
	{
		UE_LOG(LogCXMatch, Log, TEXT("CX_PLAYER_ARENA_READY Name=%s"), *GetNameSafe(Player->PlayerState));
		StartWaitingCountdown();
	}
}

void ACXGameMode::ResetEmptyArena()
{
	if (UWorld* World = GetWorld(); World && GetNumPlayers() == 0)
	{
		World->GetTimerManager().ClearTimer(GameTimerHandle);
		UE_LOG(LogCXMatch, Log, TEXT("CX_SERVER_READY_FOR_NEXT_MATCH"));
		World->ServerTravel(TEXT("/Game/CX/Maps/L_CXCityArena"), false);
	}
}
