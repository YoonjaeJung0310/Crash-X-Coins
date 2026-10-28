// CXGameState.cpp

#include "Game/CXGameState.h"
#include "Player/CXPlayerState.h"
#include "Net/UnrealNetwork.h"

void ACXGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); //부모 GameState의 기존 복제 등록 유지

	//모두가 같은 경기 단계·시간·우승자 목록을 받도록 일반 DOREPLIFETIME으로 등록
	DOREPLIFETIME(ThisClass, CurrentGameState);
	DOREPLIFETIME(ThisClass, RemainingTime);
	DOREPLIFETIME(ThisClass, Winners);
	DOREPLIFETIME(ThisClass, RoomCode);
	DOREPLIFETIME(ThisClass, bCountdownActive);
	DOREPLIFETIME(ThisClass, CountdownStartServerTime);
}

void ACXGameState::SetGameState(EGameState NewGameState)
{
	if (HasAuthority() == false)
	{
		return; //경기 단계 원본은 서버만 바꾸고 클라는 복제된 값을 읽음
	}

	CurrentGameState = NewGameState;
}

void ACXGameState::SetRemainingTime(int32 NewRemainingTime)
{
	if (HasAuthority() == false)
	{
		return;
	}

	RemainingTime = FMath::Max(0, NewRemainingTime); //0초에서 한 번 더 줄여도 음수 시간이 안 나오게 0을 하한으로 둠
}

void ACXGameState::SetWinners(const TArray<TObjectPtr<ACXPlayerState>>& NewWinners)
{
	if (HasAuthority() == false)
	{
		return; //서버 PlayerArray의 원본 코인으로 계산한 목록만 반영
	}

	Winners = NewWinners; //목록 교체 후 속성 복제로 각 화면에 전달됨. 대입했다고 바로 복제가 끝나는 건 아님
}


void ACXGameState::SetRoomCode(const FString& NewRoomCode)
{
	if (HasAuthority())
	{
		RoomCode = NewRoomCode;
	}
}

void ACXGameState::SetCountdownActive(bool bNewCountdownActive)
{
	if (HasAuthority())
	{
		if (bNewCountdownActive && bCountdownActive == false)
		{
			CountdownStartServerTime = GetServerWorldTimeSeconds();
		}
		bCountdownActive = bNewCountdownActive;
		ForceNetUpdate();
	}
}
