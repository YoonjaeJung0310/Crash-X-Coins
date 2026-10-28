// CXPlayerState.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Engine/TimerHandle.h"
#include "CXPlayerState.generated.h"

//플레이어별 코인과 추가 최대 속도를 들고 있음. APlayerState 기본 복제를 그대로 써서 생성자에서 따로 설정 안 함
UCLASS()
class CX_API ACXPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	static constexpr float BaseMaxSpeed = 120.0f;

	//부모 복제 속성은 유지하고 아래 두 값에 OwnerOnly 조건을 추가함
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	//값으로 돌려줘서 호출하는 쪽이 Coins를 직접 못 바꿈. BP에서도 자기 상태를 읽을 수 있음
	UFUNCTION(BlueprintCallable)
	int32 GetCoins() const { return Coins; }

	//추가 최대 속도도 조회만. 실제 차량 속도에 적용하는 건 이 클래스 역할이 아님
	UFUNCTION(BlueprintCallable)
	float GetBonusMaxSpeed() const { return BonusMaxSpeed; }

	//HUD와 차량 물리가 같은 km/h 상한을 읽음
	UFUNCTION(BlueprintPure)
	float GetCurrentMaxSpeed() const;

	//증가·차감 함수는 public이지만 안에서 서버 권한과 수량을 검사해서 클라가 불러도 값이 안 바뀜
	void AddCoins(int32 Amount);
	bool TryRemoveCoins(int32 Amount);
	void AddBonusMaxSpeed(float Amount);

	//바퀴 피격 패널티. 양수 전용 AddBonusMaxSpeed와 따로 둔 서버 전용 차감 함수이고, PenaltyAmount는 뺄 크기(양수)
	//BonusMaxSpeed가 음수가 되면 회복 타이머를 켜서 시간이 지나며 0까지만 되돌림
	void ApplyBonusMaxSpeedPenalty(float PenaltyAmount);

	//HUD 바인딩에서 쓰는 표시 문자열. const라 상태는 안 바꾸고 현재 코인과 추가 속도만 읽음
	UFUNCTION(BlueprintCallable)
	FString GetPlayerStateInfo() const;

protected:
	//protected로 둬서 밖에서 직접 못 바꿈. 값은 서버 변경 함수로만 갱신함
	UPROPERTY(Replicated)
	int32 Coins = 0;

	//Replicated 선언과 cpp의 OwnerOnly 등록이 같이 동작함. Actor가 다른 플레이어에게 복제되는 것과 이 값의 전달 조건은 별개임
	UPROPERTY(Replicated)
	float BonusMaxSpeed = 0.f;

	//음수 BonusMaxSpeed를 1초마다 0 쪽으로 회복함. 0이 되거나 버프로 0 이상이 되면 타이머 멈춤
	void RecoverBonusMaxSpeed();

	//패널티 회복용 서버 타이머. 이미 회복 중이면 새 패널티가 와도 다시 시작 안 해서 회복 주기가 안 밀림
	FTimerHandle BonusMaxSpeedRecoveryTimerHandle;
};
