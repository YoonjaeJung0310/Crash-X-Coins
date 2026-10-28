#include "Game/CXGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "CoreGlobals.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogCXConnection, Log, All);

void UCXGameInstance::Init()
{
	Super::Init();
	if (GEngine)
	{
		GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::HandleNetworkFailure);
		GEngine->OnTravelFailure().AddUObject(this, &ThisClass::HandleTravelFailure);
	}
}

void UCXGameInstance::Shutdown()
{
	StopMenuMusic();
	if (GEngine)
	{
		GEngine->OnNetworkFailure().RemoveAll(this);
		GEngine->OnTravelFailure().RemoveAll(this);
	}
	Super::Shutdown();
}

void UCXGameInstance::PlayMenuMusic()
{
	if (IsDedicatedServerInstance())
	{
		return;
	}
	if (IsValid(MenuMusic))
	{
		if (MenuMusic->IsPlaying() == false)
		{
			MenuMusic->Play();
		}
		return;
	}
	if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, TEXT("/Game/CX/Sound/MainMenuBGMFORCX.MainMenuBGMFORCX")))
	{
		//레벨 이동 때 오디오를 정리해도 이 곡은 유지하고, 종료는 직접 처리함
		MenuMusic = UGameplayStatics::SpawnSound2D(this, Sound, .65f, 1.f, 0.f, nullptr, true, false);
		UE_LOG(LogCXConnection, Log, TEXT("CX_MENU_MUSIC_START Component=%s Playing=%d"),
			*GetPathNameSafe(MenuMusic), IsValid(MenuMusic) && MenuMusic->IsPlaying());
	}
}

void UCXGameInstance::StopMenuMusic()
{
	if (IsValid(MenuMusic))
	{
		UE_LOG(LogCXConnection, Log, TEXT("CX_MENU_MUSIC_STOP Component=%s"), *MenuMusic->GetPathName());
		MenuMusic->Stop();
		MenuMusic->DestroyComponent();
		MenuMusic = nullptr;
	}
}

bool UCXGameInstance::ResolveRoomAddress(const FString& RoomAddress, FString& OutEndpoint, FString& OutRoomCode)
{
	OutEndpoint.Reset();
	OutRoomCode.Reset();
	FString Address = RoomAddress.TrimStartAndEnd().ToUpper();
	uint32 Octets[4] = {};
	uint32 Port = 7777;
	if (Address.StartsWith(TEXT("CX-")))
	{
		TArray<FString> Parts;
		Address.ParseIntoArray(Parts, TEXT("-"), false);
		if (Parts.Num() != 3 || Parts[1].Len() != 8 || Parts[2].Len() != 4)
		{
			return false;
		}
		for (const TCHAR Character : Parts[1] + Parts[2])
		{
			if (FChar::IsHexDigit(Character) == false)
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Octets[Index] = FParse::HexNumber(*Parts[1].Mid(Index * 2, 2));
		}
		Port = FParse::HexNumber(*Parts[2]);
	}
	else
	{
		FString Host = Address;
		FString PortText;
		if (Address.Split(TEXT(":"), &Host, &PortText))
		{
			if (PortText.IsEmpty() || PortText.Len() > 5 || PortText.IsNumeric() == false)
			{
				return false;
			}
			for (const TCHAR Character : PortText)
			{
				if (Character < TEXT('0') || Character > TEXT('9'))
				{
					return false;
				}
			}
			Port = FCString::Atoi(*PortText);
		}
		TArray<FString> Parts;
		Host.ParseIntoArray(Parts, TEXT("."), false);
		if (Parts.Num() != 4)
		{
			return false;
		}
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (Parts[Index].IsEmpty() || Parts[Index].Len() > 3)
			{
				return false;
			}
			for (const TCHAR Character : Parts[Index])
			{
				if (Character < TEXT('0') || Character > TEXT('9'))
				{
					return false;
				}
			}
			Octets[Index] = FCString::Atoi(*Parts[Index]);
			if (Octets[Index] > 255)
			{
				return false;
			}
		}
	}

	//와일드카드·멀티캐스트·브로드캐스트 주소는 참가 대상으로 쓰지 않음
	if (Port == 0 || Port > 65535 || Octets[0] == 0 || Octets[0] >= 224
		|| (Octets[0] == 255 && Octets[1] == 255 && Octets[2] == 255 && Octets[3] == 255))
	{
		return false;
	}
	OutEndpoint = FString::Printf(TEXT("%u.%u.%u.%u:%u"), Octets[0], Octets[1], Octets[2], Octets[3], Port);
	OutRoomCode = FString::Printf(TEXT("CX-%02X%02X%02X%02X-%04X"), Octets[0], Octets[1], Octets[2], Octets[3], Port);
	return true;
}

FString UCXGameInstance::SanitizePlayerName(const FString& PlayerName)
{
	FString SafeName;
	for (const TCHAR Character : PlayerName.TrimStartAndEnd().Left(16))
	{
		//접속 옵션 구분자가 이름 안에 들어가 다른 옵션을 만들지 못하게 함
		if (FChar::IsAlnum(Character) || Character == TEXT('_') || Character == TEXT('-'))
		{
			SafeName.AppendChar(Character);
		}
	}
	if (SafeName.IsEmpty())
	{
		SafeName = FString::Printf(TEXT("Driver%04d"), FMath::RandRange(0, 9999));
	}
	return SafeName;
}

FString UCXGameInstance::GetDefaultServerAddress() const
{
	if (PreferredServerAddress.IsEmpty() == false)
	{
		return PreferredServerAddress;
	}
	FString Host;
	FString PortText;
	if (MatchServerAddress.Split(TEXT(":"), &Host, &PortText) == false)
	{
		Host = MatchServerAddress;
		PortText = TEXT("7777");
	}
#if WITH_EDITOR
	const UWorld* World = GetWorld();
	if (GIsEditor || (World && World->WorldType == EWorldType::PIE)
		|| FParse::Param(FCommandLine::Get(), TEXT("PIEVIACONSOLE")))
	{
		//엔진이 합쳐 둔 Default와 Saved 설정을 읽고 파일을 다시 덮어 읽지 않음
		int32 ServerPort = 0;
		if (GConfig && GConfig->GetInt(TEXT("/Script/UnrealEd.LevelEditorPlaySettings"),
			TEXT("ServerPort"), ServerPort, GEditorPerProjectIni)
			&& ServerPort > 0 && ServerPort <= 65535)
		{
			PortText = FString::FromInt(ServerPort);
		}
	}
#endif
	FString Address;
	if (FParse::Value(FCommandLine::Get(), TEXT("CXServerAddress="), Address) == false)
	{
		Address = Host;
	}
	if (Address.Contains(TEXT(":")) == false && Address.StartsWith(TEXT("CX-"), ESearchCase::IgnoreCase) == false)
	{
		Address += TEXT(":") + PortText;
	}
	return Address;
}

bool UCXGameInstance::StartMatchmaking(const FString& PlayerName, const FString& ServerAddress)
{
	const FString Name = PlayerName.TrimStartAndEnd();
	if (Name.IsEmpty() || Name.Len() > 16)
	{
		ConnectionMessage = TEXT("닉네임은 1~16글자로 입력해 주세요.");
		return false;
	}
	for (const TCHAR Character : Name)
	{
		if (FChar::IsAlnum(Character) == false && Character != TEXT('_') && Character != TEXT('-'))
		{
			ConnectionMessage = TEXT("닉네임에는 한글·영문·숫자와 _ - 를 사용할 수 있어요.");
			return false;
		}
	}
	FString Address = ServerAddress.TrimStartAndEnd();
	const FString DefaultAddress = GetDefaultServerAddress();
	if (Address.IsEmpty())
	{
		Address = DefaultAddress;
	}
	else if (Address.Contains(TEXT(":")) == false && Address.StartsWith(TEXT("CX-"), ESearchCase::IgnoreCase) == false)
	{
		FString DefaultEndpoint;
		FString DefaultCode;
		FString DefaultPort = TEXT("7777");
		if (ResolveRoomAddress(DefaultAddress, DefaultEndpoint, DefaultCode))
		{
			DefaultEndpoint.Split(TEXT(":"), nullptr, &DefaultPort);
		}
		Address += TEXT(":") + DefaultPort;
	}
	return JoinRoom(Address, Name);
}

bool UCXGameInstance::JoinRoom(const FString& RoomAddress, const FString& PlayerName)
{
	if (bConnecting)
	{
		return false;
	}
	FString Endpoint;
	FString RoomCode;
	if (ResolveRoomAddress(RoomAddress, Endpoint, RoomCode) == false)
	{
		ConnectionMessage = TEXT("매칭 서버 주소 설정이 올바르지 않아요.");
		return false;
	}
	APlayerController* PlayerController = GetFirstLocalPlayerController();
	if (IsValid(PlayerController) == false)
	{
		ConnectionMessage = TEXT("참가 화면이 준비되지 않았어요. 잠시 후 다시 시도해 주세요.");
		return false;
	}

	const FString SafeName = SanitizePlayerName(PlayerName);
	PreferredPlayerName = SafeName;
	PreferredServerAddress = Endpoint;
	bConnecting = true;
	bReturningToMenu = false;
	ConnectionMessage = TEXT("서버에 연결 중…");
	UE_LOG(LogCXConnection, Log, TEXT("CX_JOIN_REQUEST Code=%s Endpoint=%s Player=%s"), *RoomCode, *Endpoint, *SafeName);
	PlayerController->ClientTravel(Endpoint + TEXT("?CXName=") + SafeName, TRAVEL_Absolute);
	return true;
}

void UCXGameInstance::LeaveRoom()
{
	bConnecting = false;
	ConnectionMessage.Reset();
	bReturningToMenu = true;
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/CX/Maps/L_CXMainMenu")), true);
}

void UCXGameInstance::OnMainMenuOpened()
{
	bConnecting = false;
	bReturningToMenu = false;
}

void UCXGameInstance::TryStartupJoin()
{
	if (bStartupJoinConsumed)
	{
		return;
	}
	bStartupJoinConsumed = true;
	FString StartupCode;
	FString StartupName = TEXT("Driver");
	if (FParse::Value(FCommandLine::Get(), TEXT("CXJoinCode="), StartupCode))
	{
		FParse::Value(FCommandLine::Get(), TEXT("CXPlayerName="), StartupName);
		JoinRoom(StartupCode, StartupName);
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("CXAutoMatch")))
	{
		FParse::Value(FCommandLine::Get(), TEXT("CXPlayerName="), StartupName);
		StartMatchmaking(StartupName);
	}
}

void UCXGameInstance::OnRoomJoined()
{
	bConnecting = false;
	bReturningToMenu = false;
	ConnectionMessage.Reset();
}

void UCXGameInstance::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& Error)
{
	if ((World && World->GetGameInstance() != this) || IsDedicatedServerInstance() || bReturningToMenu)
	{
		return;
	}
	UE_LOG(LogCXConnection, Warning, TEXT("CX_NETWORK_FAILURE Type=%d Error=%s"), static_cast<int32>(FailureType), *Error);
	ConnectionMessage = TEXT("매칭 서버에 연결할 수 없거나 연결이 끊겼어요. 잠시 후 다시 시작해 주세요.");
	if (Error.Contains(TEXT("CX_ROOM_FULL")))
	{
		ConnectionMessage = TEXT("현재 두 드라이버가 경기 중이에요. 잠시 후 다시 시작해 주세요.");
	}
	else if (Error.Contains(TEXT("CX_ROUND_STARTED")))
	{
		ConnectionMessage = TEXT("경기가 진행 중이에요. 종료 후 다시 시작해 주세요.");
	}
	else if (Error.Contains(TEXT("CX_SPECTATORS_NOT_ALLOWED")))
	{
		ConnectionMessage = TEXT("관전자 접속은 지원하지 않습니다. 닉네임을 입력하고 일반 참가로 접속해 주세요.");
	}
	ReturnAfterFailure();
}

void UCXGameInstance::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error)
{
	if ((World && World->GetGameInstance() != this) || IsDedicatedServerInstance() || bReturningToMenu)
	{
		return;
	}
	UE_LOG(LogCXConnection, Warning, TEXT("CX_TRAVEL_FAILURE Type=%d Error=%s"), static_cast<int32>(FailureType), *Error);
	ConnectionMessage = TEXT("경기장으로 이동하지 못했어요. 서버와 게임 버전이 같은지 확인해 주세요.");
	ReturnAfterFailure();
}

void UCXGameInstance::ReturnAfterFailure()
{
	bConnecting = false;
	bReturningToMenu = true;
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/CX/Maps/L_CXMainMenu")), true);
}
