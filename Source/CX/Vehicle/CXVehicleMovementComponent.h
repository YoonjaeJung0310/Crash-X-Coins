#pragma once

#include "CoreMinimal.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "CXVehicleMovementComponent.generated.h"

//소유 화면뿐 아니라 서버가 물리 입력을 읽는 시점에도 경기 잠금을 적용함
UCLASS()
class CX_API UCXVehicleMovementComponent : public UChaosWheeledVehicleMovementComponent
{
	GENERATED_BODY()

protected:
	virtual void UpdateState(float DeltaTime) override;

public:
	virtual void PreTickGT(float DeltaTime) override;

	//W·S를 전진 제동과 후진 가속으로 나눈 뒤의 스로틀을 확인함
	bool HasAccelerationInput() const { return ThrottleInput > 0.f && (RawThrottleInput > 0.f || RawBrakeInput > 0.f); }

	//W·S가 제동으로 처리됐을 때만 확인함. 입력 없는 자동 제동은 제외함
	bool HasBrakingInput() const { return BrakeInput > 0.f && (RawThrottleInput > 0.f || RawBrakeInput > 0.f); }

private:
	bool bGameStateLocked = false;
};
