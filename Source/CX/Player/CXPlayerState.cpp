// CXPlayerState.cpp

#include "Player/CXPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
//개발용 콘솔 입력값. Shipping에선 콘솔 등록 자체를 빼고, Coins를 public으로 열어서 직접 바꾸는 방식도 안 씀
static int32 GMyTestCoin = 0;
static FAutoConsoleVariableRef CVarAddCoinServer(
	TEXT("cheat.addcoin.server"),
	GMyTestCoin,
	TEXT("Add positive coins to the first PlayerState only from an authoritative server world."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* CVar)
	{
		//파일 범위 콜백이라 this가 없어서 GWorld에서 대상을 찾음. 확인한 World를 같은 지역 포인터로 써서 검사 대상과 조회 대상을 맞춤
		UWorld* World = GWorld;
		if (CVar != nullptr && IsValid(World) && World->GetNetMode() != NM_Client)
		{
			APlayerController* PlayerController = World->GetFirstPlayerController();
			if (IsValid(PlayerController))
			{
				ACXPlayerState* PlayerState = PlayerController->GetPlayerState<ACXPlayerState>();
				if (IsValid(PlayerState) && PlayerState->HasAuthority())
				{
					//서버 콘솔에서만 직접 지급함. 클라 입력은 서버로 전달하지 않음
					PlayerState->AddCoins(GMyTestCoin);
				}
			}
		}

		//지급하지 못한 경우도 콘솔 입력값은 비움. PlayerState의 Coins를 0으로 만드는 코드가 아님
		GMyTestCoin = 0;
	}),
	ECVF_Cheat
);
#endif

void ACXPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); //부모 PlayerState에 등록된 속성도 계속 복제되게 부모 등록 먼저 호출

	//Coins와 BonusMaxSpeed는 소유 연결에만 보냄. 소유 연결은 PlayerState의 Owner인 PlayerController로 정해짐
	DOREPLIFETIME_CONDITION(ThisClass, Coins, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ThisClass, BonusMaxSpeed, COND_OwnerOnly);
}

void ACXPlayerState::AddCoins(int32 Amount)
{
	//서버에서 양수만 추가함. 덧셈 전에 범위를 검사해서 int32 오버플로가 날 상황이면 기존 잔액 유지
	if (HasAuthority() == false || Amount <= 0 || Coins > MAX_int32 - Amount)
	{
		return;
	}

	Coins += Amount;
	//로그의 Coins는 증가 후 잔액이고 괄호 안은 이번 증가량이라 두 숫자를 다시 더해서 읽으면 안 됨
	UE_LOG(LogTemp, Warning, TEXT("%s - AddCoins: %d(+%d)"), *GetPlayerName(), Coins, Amount);
}

bool ACXPlayerState::TryRemoveCoins(int32 Amount)
{
	//요청량과 잔액을 비교함. 공격 한 번의 5원뿐 아니라 다른 양수 요청에서도 잔액보다 많이 빠져 음수가 되는 걸 막음
	if (HasAuthority() == false || Amount <= 0 || Coins < Amount)
	{
		return false;
	}

	Coins -= Amount;
	return true; //실제로 차감했을 때만 true. 공격자는 피해자 차감이 성공한 뒤에만 코인을 받음
}

void ACXPlayerState::AddBonusMaxSpeed(float Amount)
{
	//서버의 정상적인 양수 증가만 받음. NaN·무한대나 더한 결과가 무한대면 HUD와 속도 값에 안 남게 저장 전에 거절
	if (HasAuthority() == false || Amount <= 0.f || FMath::IsFinite(Amount) == false
		|| FMath::IsFinite(BonusMaxSpeed + Amount) == false)
	{
		return;
	}

	BonusMaxSpeed += Amount;
}

void ACXPlayerState::ApplyBonusMaxSpeedPenalty(float PenaltyAmount)
{
	UWorld* World = GetWorld();
	//AddBonusMaxSpeed처럼 서버·유한값 검사를 하고 0 이하 패널티는 거절. 뺀 결과가 유한한지도 저장 전에 확인
	if (HasAuthority() == false || IsValid(World) == false || PenaltyAmount <= 0.f || FMath::IsFinite(PenaltyAmount) == false
		|| FMath::IsFinite(BonusMaxSpeed - PenaltyAmount) == false)
	{
		return;
	}

	//버프와 패널티는 BonusMaxSpeed 한 값에서 더하고 뺌. 예: +20에서 10 빼면 +10이라 회복할 음수가 없고, 깎인 10은 안 돌아옴
	BonusMaxSpeed -= PenaltyAmount;
	//회복 타이머가 이미 돌고 있으면 다시 시작 안 함
	if (BonusMaxSpeed < 0.f && World->GetTimerManager().IsTimerActive(BonusMaxSpeedRecoveryTimerHandle) == false)
	{
		//1초마다 음수 패널티 회복함
		World->GetTimerManager().SetTimer(BonusMaxSpeedRecoveryTimerHandle, this, &ThisClass::RecoverBonusMaxSpeed, 1.f, true);
	}
}

void ACXPlayerState::RecoverBonusMaxSpeed()
{
	UWorld* World = GetWorld();
	if (HasAuthority() == false || IsValid(World) == false)
	{
		return;
	}

	//음수일 때만 회복함. 회복 중 버프로 이미 0 이상이면 깎지 않고 그대로 둠(남은 패널티를 버프가 상쇄)
	if (BonusMaxSpeed < 0.f)
	{
		//1초에 +2씩 회복. FMath::Min으로 0을 넘어 양수가 되지 않게 막음
		BonusMaxSpeed = FMath::Min(BonusMaxSpeed + 2.f, 0.f);
	}

	if (BonusMaxSpeed >= 0.f)
	{
		World->GetTimerManager().ClearTimer(BonusMaxSpeedRecoveryTimerHandle); //더 회복할 음수가 없으면 반복 타이머 멈춤
	}
}

float ACXPlayerState::GetCurrentMaxSpeed() const
{
	return FMath::Max(20.0f, BaseMaxSpeed + BonusMaxSpeed);
}

FString ACXPlayerState::GetPlayerStateInfo() const
{
	//%.2f는 표시 문자열만 소수 둘째 자리로 만듦. 저장된 float 값을 반올림하는 게 아님
	return FString::Printf(TEXT("Coins: %d\nCurrentMaxSpeed: %.0f km/h\nBonusMaxSpeed: %.2f"), Coins, GetCurrentMaxSpeed(), BonusMaxSpeed);
}
