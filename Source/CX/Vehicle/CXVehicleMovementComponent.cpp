#include "Vehicle/CXVehicleMovementComponent.h"
#include "Vehicle/CXPawn.h"
#include "Player/CXPlayerState.h"
#include "Components/SkeletalMeshComponent.h"

void UCXVehicleMovementComponent::UpdateState(float DeltaTime)
{
	Super::UpdateState(DeltaTime);
	const ACXPawn* Vehicle = Cast<ACXPawn>(GetOwner());
	if (!IsValid(Vehicle))
	{
		return;
	}
	if (!Vehicle->IsGamePlaying())
	{
		//원격 차량은 ReplicatedState를 다시 읽으므로 원시값과 보간값을 함께 막음
		ClearAllInput();
		SteeringInput = ReplicatedState.SteeringInput = 0.0f;
		ThrottleInput = ReplicatedState.ThrottleInput = 0.0f;
		BrakeInput = ReplicatedState.BrakeInput = 0.0f;
		HandbrakeInput = ReplicatedState.HandbrakeInput = 1.0f;
		PitchInput = ReplicatedState.PitchInput = 0.0f;
		RollInput = ReplicatedState.RollInput = 0.0f;
		YawInput = ReplicatedState.YawInput = 0.0f;
		SetHandbrakeInput(true);
		bGameStateLocked = true;
	}
	else
	{
		if (bGameStateLocked)
		{
			//Playing으로 바뀔 때 경기 잠금만 풀고 누르고 있는 키는 유지함
			const bool bHeld = Vehicle->IsLocallyControlled() && Vehicle->bHandbrakeHeld;
			SetHandbrakeInput(bHeld);
			HandbrakeInput = ReplicatedState.HandbrakeInput = bHeld ? 1.0f : 0.0f;
			bGameStateLocked = false;
		}
		if (Vehicle->bAcceleratorBroken && GetCurrentGear() >= 0)
		{
			//전진 가속만 고장남. 후진 기어의 스로틀까지 막지 않음
			RawThrottleInput = ThrottleInput = ReplicatedState.ThrottleInput = 0.0f;
		}
	}
}

void UCXVehicleMovementComponent::PreTickGT(float DeltaTime)
{
	Super::PreTickGT(DeltaTime);
	const ACXPawn* Vehicle = Cast<ACXPawn>(GetOwner());
	if (!IsValid(Vehicle) || (!Vehicle->HasAuthority() && !Vehicle->IsLocallyControlled()))
	{
		return;
	}
	const ACXPlayerState* State = Vehicle->GetPlayerState<ACXPlayerState>();
	if (!IsValid(State))
	{
		return;
	}
	const float CurrentMaxSpeed = State->GetCurrentMaxSpeed();
	if (bMechanicalSimEnabled)
	{
		//EngineSetup은 차종별 기준값임. 물리 엔진 토크만 바꿔서 배율이 누적되지 않음
		SetMaxEngineTorque(EngineSetup.MaxTorque * CurrentMaxSpeed / ACXPlayerState::BaseMaxSpeed);
	}
	//BonusMaxSpeed의 단위는 km/h. cm/s로 바꾸고 낙하 속도는 그대로 둠
	const float MaxSpeed = CurrentMaxSpeed / 0.036f;
	FVector HorizontalVelocity = Vehicle->GetMesh()->GetPhysicsLinearVelocity();
	const float HorizontalSpeed = HorizontalVelocity.Size2D();
	if (HorizontalSpeed > MaxSpeed)
	{
		HorizontalVelocity.X *= MaxSpeed / HorizontalSpeed;
		HorizontalVelocity.Y *= MaxSpeed / HorizontalSpeed;
		Vehicle->GetMesh()->SetPhysicsLinearVelocity(HorizontalVelocity);
	}
}
