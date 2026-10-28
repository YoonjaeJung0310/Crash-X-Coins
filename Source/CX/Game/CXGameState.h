// CXGameState.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "CXGameState.generated.h"

class ACXPlayerState;

//모든 플레이어가 같은 경기 단계를 읽도록 Waiting/Playing/Finished를 복제 가능한 enum으로 정의
UENUM(BlueprintType)
enum class EGameState : uint8
{
	Waiting,
	Playing,
	Finished
};

//서버 GameMode가 정한 경기 상태·결과를 모든 클라에 전달. 개인 코인과 달리 세 값 모두 전체 복제
UCLASS()
class CX_API ACXGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	//부모 GameState 복제는 유지하고 현재 상태·남은 시간·우승자 목록을 추가 등록
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	//변경 함수는 서버 권한을 검사함. 클라 HUD에서 불러도 값 안 바뀜
	void SetGameState(EGameState NewGameState);
	void SetRemainingTime(int32 NewRemainingTime);
	void SetWinners(const TArray<TObjectPtr<ACXPlayerState>>& NewWinners);
	void SetRoomCode(const FString& NewRoomCode);
	void SetCountdownActive(bool bNewCountdownActive);

	//HUD는 자기 쪽에 복제된 현재 값을 읽음. getter가 서버를 호출하거나 값을 바꾸지 않음
	EGameState GetCurrentGameState() const { return CurrentGameState; }
	int32 GetRemainingTime() const { return RemainingTime; }
	bool IsCountdownActive() const { return bCountdownActive; }
	double GetCountdownStartServerTime() const { return CountdownStartServerTime; }
	const FString& GetRoomCode() const { return RoomCode; }

	//읽기 전용 참조로 돌려줘서 복사도 안 하고 밖에서 배열을 못 바꾸게 함
	const TArray<TObjectPtr<ACXPlayerState>>& GetWinners() const { return Winners; }

protected:
	//OwnerOnly 같은 조건 없음. 경기 단계는 모든 플레이어가 같은 값을 받아야 함
	UPROPERTY(Replicated)
	EGameState CurrentGameState = EGameState::Waiting;

	//서버 GameMode가 갱신하고 클라는 복제된 숫자를 표시만 함
	UPROPERTY(Replicated)
	int32 RemainingTime = 0;

	//우승자 PlayerState 목록. 각 화면이 이름과 자기 포함 여부를 읽음. 다른 사람 OwnerOnly 코인은 여기서 못 읽음
	UPROPERTY(Replicated)
	TArray<TObjectPtr<ACXPlayerState>> Winners;

	//두 화면 준비가 끝난 뒤에만 도로 위 시작 카운트다운을 표시함
	UPROPERTY(Replicated)
	bool bCountdownActive = false;

	//음성 재생 위치도 서버의 카운트다운 시작 시각을 기준으로 맞춤
	UPROPERTY(Replicated)
	double CountdownStartServerTime = 0.0;

	//이 서버의 공개 주소를 담은 참가 코드. 비밀번호나 세션 인증값은 아님
	UPROPERTY(Replicated)
	FString RoomCode;
};
