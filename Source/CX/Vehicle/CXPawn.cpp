// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicle/CXPawn.h"
#include "Vehicle/CXVehicleMovementComponent.h"
#include "Vehicle/CXVehicleAudioComponent.h"
#include "Vehicle/CXRoadRecoveryArea.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Player/CXPlayerState.h"
#include "Player/CXPlayerController.h"
#include "Vehicle/CXWheelFront.h"
#include "Vehicle/CXWheelRear.h"
#include "Game/CXGameState.h"
#include "Item/CXMine.h"
#include "Engine/DamageEvents.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Particles/ParticleSystem.h"

#define LOCTEXT_NAMESPACE "VehiclePawn"

DEFINE_LOG_CATEGORY(LogTemplateVehicle);

namespace
{
	constexpr double AttackInterval = 0.5;
	constexpr double MineInterval = 3.0;
	constexpr float AttackRange = 3000.0f;
	constexpr float AttackAssistRadius = 30.0f;
}

ACXPawn::ACXPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCXVehicleMovementComponent>(VehicleMovementComponentName))
{
	// construct the front camera boom
	FrontSpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Front Spring Arm"));
	FrontSpringArm->SetupAttachment(GetMesh());
	FrontSpringArm->TargetArmLength = 0.0f;
	FrontSpringArm->bDoCollisionTest = false;
	FrontSpringArm->bEnableCameraRotationLag = true;
	FrontSpringArm->CameraRotationLagSpeed = 15.0f;
	FrontSpringArm->SetRelativeLocation(FVector(30.0f, 0.0f, 120.0f));

	FrontCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("Front Camera"));
	FrontCamera->SetupAttachment(FrontSpringArm);
	FrontCamera->bAutoActivate = false;

	// construct the back camera boom
	BackSpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Back Spring Arm"));
	BackSpringArm->SetupAttachment(GetMesh());
	BackSpringArm->TargetArmLength = 650.0f;
	BackSpringArm->SocketOffset.Z = 150.0f;
	BackSpringArm->bDoCollisionTest = false;
	BackSpringArm->bInheritPitch = false;
	BackSpringArm->bInheritRoll = false;
	BackSpringArm->bEnableCameraRotationLag = true;
	BackSpringArm->CameraRotationLagSpeed = 2.0f;
	BackSpringArm->CameraLagMaxDistance = 50.0f;

	BackCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("Back Camera"));
	BackCamera->SetupAttachment(BackSpringArm);

	// Configure the car mesh
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetCollisionProfileName(FName("Vehicle"));

	VehicleAudio = CreateDefaultSubobject<UCXVehicleAudioComponent>(TEXT("VehicleAudio"));

	// get the Chaos Wheeled movement component
	ChaosVehicleMovement = CastChecked<UChaosWheeledVehicleMovementComponent>(GetVehicleMovement());

}

void ACXPawn::BeginPlay()
{
	Super::BeginPlay();
	//실제 차체 범위를 한 번 구해서 지붕보다 15cm 위에 총구를 둠
	FBox VehicleBounds(ForceInit);
	const FTransform WorldToActor = GetActorTransform().Inverse();
	ForEachComponent<UPrimitiveComponent>(false, [&](const UPrimitiveComponent* Component)
	{
		if (Component->IsRegistered() && !Component->IsVisualizationComponent() && !Component->IsEditorOnly()
			&& (Component->IsA<USkeletalMeshComponent>() || Component->IsA<UStaticMeshComponent>()))
		{
			VehicleBounds += Component->CalcBounds(Component->GetComponentTransform() * WorldToActor).GetBox();
		}
	});
	if (VehicleBounds.IsValid)
	{
		MuzzleOffset.Z = VehicleBounds.Max.Z + 15.0f;
	}
	//지붕에서 아래로 겨냥하는 기본 시점. 상하 조준으로 높이를 바꿀 수 있음
	BackSpringArm->SetRelativeRotation(FRotator(-10.0f, BackSpringArm->GetRelativeRotation().Yaw, 0.0f));
	if (HasAuthority())
	{
		//실제 차체 충돌만 듣고 코인·버프의 Overlap은 충돌음에 포함하지 않음
		GetMesh()->SetNotifyRigidBodyCollision(true);
		GetMesh()->OnComponentHit.AddUniqueDynamic(this, &ThisClass::OnVehicleHit);
	}
}

void ACXPawn::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	//DefaultInput의 LookUp=MouseY를 재사용함
	PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &ThisClass::LookUp);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// steering 
		EnhancedInputComponent->BindAction(SteeringAction, ETriggerEvent::Triggered, this, &ACXPawn::Steering);
		EnhancedInputComponent->BindAction(SteeringAction, ETriggerEvent::Completed, this, &ACXPawn::Steering);

		// throttle 
		EnhancedInputComponent->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &ACXPawn::Throttle);
		EnhancedInputComponent->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &ACXPawn::Throttle);

		// break 
		EnhancedInputComponent->BindAction(BrakeAction, ETriggerEvent::Triggered, this, &ACXPawn::Brake);
		EnhancedInputComponent->BindAction(BrakeAction, ETriggerEvent::Started, this, &ACXPawn::StartBrake);
		EnhancedInputComponent->BindAction(BrakeAction, ETriggerEvent::Completed, this, &ACXPawn::StopBrake);
		//IA_Brake는 Pressed+Released 트리거라 누르고 있는 동안엔 Triggered가 아니라 Ongoing이 매 프레임 발생함
		//Waiting에서 S를 누른 채 Playing이 되면 잠금 중 0이 된 브레이크 값이 다시 들어오게 Ongoing에도 Brake 연결
		EnhancedInputComponent->BindAction(BrakeAction, ETriggerEvent::Ongoing, this, &ACXPawn::Brake);

		// handbrake 
		EnhancedInputComponent->BindAction(HandbrakeAction, ETriggerEvent::Started, this, &ACXPawn::StartHandbrake);
		EnhancedInputComponent->BindAction(HandbrakeAction, ETriggerEvent::Completed, this, &ACXPawn::StopHandbrake);

		// look around 
		EnhancedInputComponent->BindAction(LookAroundAction, ETriggerEvent::Triggered, this, &ACXPawn::LookAround);

		// toggle camera 
		EnhancedInputComponent->BindAction(ToggleCameraAction, ETriggerEvent::Triggered, this, &ACXPawn::ToggleCamera);

		// reset the vehicle 
		EnhancedInputComponent->BindAction(ResetVehicleAction, ETriggerEvent::Triggered, this, &ACXPawn::ResetVehicle);

		//Triggered 이벤트 사용. 액션 애셋이 아직 연결 안 된 Pawn에서는 바인딩을 안 만듦
		if (AttackAction != nullptr)
		{
			EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Triggered, this, &ACXPawn::InputAttack);
		}

		//지뢰도 공격과 같은 Triggered 바인딩. IA_PlaceMine이 Boolean + Pressed라서 F키 한 번에 한 번 호출됨
		if (PlaceMineAction != nullptr)
		{
			EnhancedInputComponent->BindAction(PlaceMineAction, ETriggerEvent::Triggered, this, &ACXPawn::InputPlaceMine);
		}
	}
	else
	{
		UE_LOG(LogTemplateVehicle, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void ACXPawn::Tick(float Delta)
{
	Super::Tick(Delta);

	// add some angular damping if the vehicle is in midair
	bool bMovingOnGround = ChaosVehicleMovement->IsMovingOnGround();
	GetMesh()->SetAngularDamping(bMovingOnGround ? 0.0f : 3.0f);

	// realign the camera yaw to face front
	float CameraYaw = BackSpringArm->GetRelativeRotation().Yaw;
	CameraYaw = FMath::FInterpTo(CameraYaw, 0.0f, Delta, 1.0f);

	BackSpringArm->SetRelativeRotation(FRotator(BackSpringArm->GetRelativeRotation().Pitch, CameraYaw, 0.0f));

	//Chaos 차량의 스로틀·조향·핸드브레이크 값은 소유 화면에서 만들어서 서버로 보냄(ServerUpdateState)
	//그래서 입력을 만드는 쪽(클라 소유 차량, 리슨 서버 호스트 차량)에서만 매 프레임 잠금을 다시 적용함
	if (IsLocallyControlled())
	{
		ApplyInputLocks();
	}
}

void ACXPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); //부모 Pawn의 Controller·PlayerState 등 기존 복제 등록 유지

	//막아야 할 스로틀은 소유 클라가 만들어 서버로 보내니까 bAcceleratorBroken은 소유 연결에만 보냄. PlayerState의 Coins와 같은 OwnerOnly 조건
	DOREPLIFETIME_CONDITION(ThisClass, bAcceleratorBroken, COND_OwnerOnly);
}

bool ACXPawn::IsGamePlaying() const
{
	const UWorld* World = GetWorld();
	const ACXGameState* CXGameState = IsValid(World) ? World->GetGameState<ACXGameState>() : nullptr;
	//접속 직후 GameState가 아직 복제 안 됐거나 다른 GameState를 쓰는 경우도 Playing이 아니니까 잠금으로 처리
	return IsValid(CXGameState) && CXGameState->GetCurrentGameState() == EGameState::Playing;
}

bool ACXPawn::IsWheelBone(FName BoneName) const
{
	if (BoneName.IsNone())
	{
		return false; //오프로드 Chassis 같은 정적 메시에 맞으면 Bone이 없어서 NAME_None임
	}

	//스포츠카는 Phys_Wheel_FL, 오프로드는 PhysWheel_FL처럼 철자가 달라서 각 차종 생성자가 넣은 바퀴 Bone 목록과 비교함
	//두 차량 물리 애셋에는 바퀴 Bone마다 구(Sphere) Body가 있음. Trace가 그 Body에 먼저 맞으면 HitResult.BoneName이 바퀴 Bone이 됨
	//Chaos가 바퀴 Body에 SetResponseToAllChannels(ECR_Ignore)를 호출하긴 하지만, 스켈레탈 메시 Body는 물리 애셋 Collision Response(Enabled)와
	//메시 컴포넌트의 Vehicle 프로필(Visibility Block)로 응답을 다시 만들어서 바퀴 Body도 Visibility Trace에 맞음(UE 5.6 BodyInstance.cpp BuildBodyFilterData)
	for (const FChaosWheelSetup& WheelSetup : ChaosVehicleMovement->WheelSetups)
	{
		if (WheelSetup.BoneName == BoneName)
		{
			return true;
		}
	}
	return false;
}

void ACXPawn::ApplyInputLocks()
{
	const bool bGamePlaying = IsGamePlaying();
	if (bGamePlaying == false)
	{
		//Waiting·Finished에서는 누르던 값이 안 남게 스로틀·조향을 계속 0으로 유지
		ChaosVehicleMovement->SetThrottleInput(0.0f);
		ChaosVehicleMovement->SetSteeringInput(0.0f);
		//bReverseAsBrake가 켜져 있어서 정지 중 브레이크 키가 후진 가속이 됨. 그래서 브레이크도 0으로 유지
		ChaosVehicleMovement->SetBrakeInput(0.0f);
	}
	else if (bAcceleratorBroken && ChaosVehicleMovement->GetThrottleInput() > 0.0f)
	{
		//지뢰 고장은 전진 가속(양수 스로틀)만 막음. 조향·브레이크·핸드브레이크는 그대로
		ChaosVehicleMovement->SetThrottleInput(0.0f);
	}

	//핸드브레이크 = 게임 상태 잠금 OR 누르고 있는 키. Playing이 되면 게임 상태 잠금만 빠지고, 키를 누르고 있으면 계속 true
	ChaosVehicleMovement->SetHandbrakeInput(bGamePlaying == false || bHandbrakeHeld);
}

void ACXPawn::Steering(const FInputActionValue& Value)
{
	// get the input magnitude for steering
	float SteeringValue = Value.Get<float>();

	//Playing이 아니면 들어온 조향 값을 버리고 0을 넣음. 입력 처리와 Tick 순서가 바뀌어도 잠금 중에 값이 안 샘
	if (IsGamePlaying() == false)
	{
		SteeringValue = 0.0f;
	}

	// add the input
	ChaosVehicleMovement->SetSteeringInput(SteeringValue);
}

void ACXPawn::Throttle(const FInputActionValue& Value)
{
	// get the input magnitude for the throttle
	float ThrottleValue = Value.Get<float>();

	//Playing이 아니면 스로틀 0, 지뢰 고장 중이면 전진(양수) 스로틀만 0
	if (IsGamePlaying() == false || (bAcceleratorBroken && ThrottleValue > 0.0f))
	{
		ThrottleValue = 0.0f;
	}

	// add the input
	ChaosVehicleMovement->SetThrottleInput(ThrottleValue);
}

void ACXPawn::Brake(const FInputActionValue& Value)
{
	// get the input magnitude for the brakes
	float BreakValue = Value.Get<float>();

	//정지 중 브레이크 키는 후진 가속(bReverseAsBrake)이라 Playing이 아닐 땐 0을 넣어 차가 안 움직이게 함
	if (IsGamePlaying() == false)
	{
		BreakValue = 0.0f;
	}

	// add the input
	ChaosVehicleMovement->SetBrakeInput(BreakValue);
}

void ACXPawn::StartBrake(const FInputActionValue& Value)
{
	// call the Blueprint hook for the break lights
	BrakeLights(true);
}

void ACXPawn::StopBrake(const FInputActionValue& Value)
{
	// call the Blueprint hook for the break lights
	BrakeLights(false);

	// reset brake input to zero
	ChaosVehicleMovement->SetBrakeInput(0.0f);
}

void ACXPawn::StartHandbrake(const FInputActionValue& Value)
{
	//키 상태를 따로 기록함. true를 넣는 건 어떤 잠금도 안 푸니까 상태와 상관없이 그대로 적용
	bHandbrakeHeld = true;

	// add the input
	ChaosVehicleMovement->SetHandbrakeInput(true);

	// call the Blueprint hook for the break lights
	BrakeLights(true);
}

void ACXPawn::StopHandbrake(const FInputActionValue& Value)
{
	bHandbrakeHeld = false;

	// add the input
	//키를 떼도 Waiting·Finished 게임 상태 잠금은 안 풀리게 false 대신 잠금 여부를 넣음
	ChaosVehicleMovement->SetHandbrakeInput(IsGamePlaying() == false);

	// call the Blueprint hook for the break lights
	BrakeLights(false);
}

void ACXPawn::LookAround(const FInputActionValue& Value)
{
	// get the flat angle value for the input 
	float LookValue = Value.Get<float>();

	// add the input
	BackSpringArm->AddLocalRotation(FRotator(0.0f, LookValue, 0.0f));
}

void ACXPawn::LookUp(float Value)
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!IsValid(PC) || PC->bShowMouseCursor || PC->IsLookInputIgnored() || FMath::IsNearlyZero(Value))
	{
		return;
	}
	USpringArmComponent* SpringArm = bFrontCameraActive ? FrontSpringArm : BackSpringArm;
	FRotator Rotation = SpringArm->GetRelativeRotation();
	//바퀴 쪽으로 내리거나 먼 거리를 겨냥해 올릴 수 있게 조준 각도만 제한함
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch + Value, -35.0f, 5.0f);
	SpringArm->SetRelativeRotation(Rotation);
}

void ACXPawn::ToggleCamera(const FInputActionValue& Value)
{
	// toggle the active camera flag
	bFrontCameraActive = !bFrontCameraActive;

	FrontCamera->SetActive(bFrontCameraActive);
	BackCamera->SetActive(!bFrontCameraActive);
}

void ACXPawn::ResetVehicle(const FInputActionValue& Value)
{
	//Waiting·Finished에서는 위치 초기화도 막음. 이 기능은 입력한 화면에서만 실행되고 서버 RPC는 없음
	if (IsGamePlaying() == false)
	{
		return;
	}

	//위의 로컬 초기화 설명은 이전 동작임. 현재는 기존 차량을 서버가 정한 도로로 복구함
	// reset to a location slightly above our current one
	// reset to our yaw. Ignore pitch and roll
	// teleport the actor to the reset spot and reset physics
	CTSResetVehicle();
}

void ACXPawn::CTSResetVehicle_Implementation()
{
	if (IsGamePlaying())
	{
		RecoverToRoad();
	}
}

bool ACXPawn::RecoverToRoad()
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !IsValid(World) || !IsGamePlaying()
		|| World->GetTimeSeconds() - LastRecoveryServerTime < 3.0)
	{
		return false;
	}
	struct FCandidate
	{
		FVector Position;
		const ACXRoadRecoveryArea* Road;
	};
	TArray<FCandidate> Candidates;
	for (TActorIterator<ACXRoadRecoveryArea> It(World); It; ++It)
	{
		TArray<FVector> RoadPoints;
		It->AddRecoveryCandidates(GetActorLocation(), 380.0f, RoadPoints);
		for (const FVector& Point : RoadPoints)
		{
			Candidates.Add({Point, *It});
		}
	}
	const FVector ProblemPosition = GetActorLocation();
	Candidates.Sort([&ProblemPosition](const FCandidate& A, const FCandidate& B)
	{
		return FVector::DistSquared2D(A.Position, ProblemPosition) < FVector::DistSquared2D(B.Position, ProblemPosition);
	});
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CXVehicleRecovery), false, this);
	for (const FCandidate& Candidate : Candidates)
	{
		FHitResult Ground;
		if (!World->LineTraceSingleByObjectType(Ground, Candidate.Position + FVector(0, 0, 600), Candidate.Position - FVector(0, 0, 600), FCollisionObjectQueryParams(ECC_WorldStatic), Params)
			|| Ground.ImpactNormal.Z < 0.94f || !Candidate.Road->ContainsRoadPoint(Ground.ImpactPoint, 300.0f)
			|| FMath::Abs(Ground.ImpactPoint.Z - Candidate.Position.Z) > 50.0f)
		{
			continue;
		}
		float Yaw = Candidate.Road->GetActorRotation().Yaw;
		if (FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, Yaw)) > 90.0f)
		{
			Yaw += 180.0f;
		}
		const FRotator Rotation(0.0f, Yaw, 0.0f);
		//두 차종의 차체·타이어보다 여유를 두고 벽이나 다른 차량과 겹치는 후보는 제외함
		const FVector Location = Ground.ImpactPoint + FVector(0, 0, 50);
		if (World->OverlapBlockingTestByChannel(Location + FVector(0, 0, 125), Rotation.Quaternion(), ECC_Vehicle, FCollisionShape::MakeBox(FVector(325, 195, 125)), Params))
		{
			continue;
		}
		bool bStableGround = true;
		for (float X : {-300.0f, 300.0f})
		{
			for (float Y : {-170.0f, 170.0f})
			{
				const FVector Corner = Ground.ImpactPoint + Rotation.RotateVector(FVector(X, Y, 0));
				FHitResult CornerGround;
				bStableGround &= World->LineTraceSingleByObjectType(CornerGround, Corner + FVector(0, 0, 100), Corner - FVector(0, 0, 100), FCollisionObjectQueryParams(ECC_WorldStatic), Params)
					&& CornerGround.ImpactNormal.Z >= 0.94f && FMath::Abs(CornerGround.ImpactPoint.Z - Ground.ImpactPoint.Z) <= 20.0f;
			}
		}
		if (!bStableGround)
		{
			continue;
		}
		LastRecoveryServerTime = World->GetTimeSeconds();
		MulticastRecoverVehicle(FTransform(Rotation, Location, GetActorScale3D()));
		ForceNetUpdate();
		UE_LOG(LogTemplateVehicle, Log, TEXT("Vehicle recovered to road: %s at %s"), *GetName(), *Location.ToString());
		return true;
	}
	//안전한 후보가 없으면 차를 없애지 않고 원래 자리에 둠
	LastRecoveryServerTime = World->GetTimeSeconds();
	UE_LOG(LogTemplateVehicle, Warning, TEXT("No safe road recovery location for %s"), *GetName());
	return false;
}

void ACXPawn::MulticastRecoverVehicle_Implementation(const FTransform& RecoveryTransform)
{
	//Pawn·Controller·PlayerState는 유지하고 남은 바퀴 상태와 속도만 초기화함
	ChaosVehicleMovement->ResetVehicle();
	SetActorTransform(RecoveryTransform, false, nullptr, ETeleportType::TeleportPhysics);
	GetMesh()->SetPhysicsLinearVelocity(FVector::ZeroVector);
	GetMesh()->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	GetMesh()->WakeAllRigidBodies();
	ApplyInputLocks();
}

void ACXPawn::OnVehicleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent,
	FVector NormalImpulse, const FHitResult& Hit)
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !IsValid(World) || !IsValid(OtherActor) || OtherActor == this || !IsGamePlaying()
		|| World->GetTimeSeconds() - LastCrashServerTime < 0.35)
	{
		return;
	}
	//차량끼리 한 번 부딪히면 두 차의 Hit 중 한 쪽만 소리를 보냄
	if (const ACXPawn* OtherVehicle = Cast<ACXPawn>(OtherActor))
	{
		if (GetUniqueID() > OtherVehicle->GetUniqueID())
		{
			return;
		}
	}
	//서 있는 바닥의 접촉 압력은 빼고 충격량이나 면 방향의 접근 속도가 큰 충돌만 재생함
	const float VelocityChange = NormalImpulse.Size() / FMath::Max(GetMesh()->GetMass(), 1.0f);
	const float ClosingSpeed = FMath::Abs(FVector::DotProduct(GetVelocity() - OtherActor->GetVelocity(), Hit.ImpactNormal));
	if (VelocityChange < 150.0f && ClosingSpeed < 150.0f)
	{
		return;
	}
	LastCrashServerTime = World->GetTimeSeconds();
	MulticastCrashSound();
}

void ACXPawn::MulticastCrashSound_Implementation()
{
	VehicleAudio->PlayCrashSound();
}

void ACXPawn::ApplyVehicleImpact(const FVector& Origin, bool bExplosion)
{
	if (!HasAuthority() || !IsGamePlaying() || Origin.ContainsNaN())
	{
		return;
	}
	FVector Direction = (GetActorLocation() - Origin).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		Direction = GetActorForwardVector().GetSafeNormal2D();
	}
	//질량이 다른 두 차종에 같은 속도 변화를 줌. 지뢰는 위로 더 크게 띄움
	const FVector VelocityChange = Direction * (bExplosion ? 650.0f : 400.0f) + FVector::UpVector * (bExplosion ? 600.0f : 180.0f);
	const FVector AngularVelocityChange = FVector::CrossProduct(Direction, FVector::UpVector) * (bExplosion ? 3.5f : 0.8f);
	MulticastVehicleImpact(VelocityChange, AngularVelocityChange);
	ForceNetUpdate();
	UE_LOG(LogTemplateVehicle, Log, TEXT("CX_VEHICLE_IMPACT Vehicle=%s Explosion=%d VelocityChange=%s AngularChange=%s"),
		*GetName(), bExplosion, *VelocityChange.ToString(), *AngularVelocityChange.ToString());
}

void ACXPawn::MulticastVehicleImpact_Implementation(const FVector& VelocityChange, const FVector& AngularVelocityChange)
{
	//원격 표시용 차체에 충격을 다시 더하면 복제된 속도와 겹침. 서버와 소유 차체만 직접 계산함
	if (HasAuthority() || IsLocallyControlled())
	{
		GetMesh()->WakeAllRigidBodies();
		GetMesh()->AddImpulse(VelocityChange, NAME_None, true);
		GetMesh()->AddAngularImpulseInRadians(AngularVelocityChange, NAME_None, true);
	}
	VehicleAudio->PlayCrashSound();
}

void ACXPawn::InputAttack(const FInputActionValue& Value)
{
	//Playing이 아니면 서버 요청 자체를 안 보냄. 서버 CTSAttack에서도 같은 조건을 다시 검사함
	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsGamePlaying() || World->GetTimeSeconds() - LastAttackRequestTime < AttackInterval)
	{
		return;
	}

	//액션 값으로 피해를 정하지 않고 서버에 공격했다는 것만 알림. Triggered 호출 횟수는 입력 액션 설정을 따름
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		if (ViewLocation.ContainsNaN() || ViewRotation.ContainsNaN())
		{
			return;
		}
		LastAttackRequestTime = World->GetTimeSeconds();
		CTSAttack(ViewLocation, ViewRotation.Vector());
	}
}

void ACXPawn::InputPlaceMine(const FInputActionValue& Value)
{
	//공격 입력과 같은 구조. 로컬에서 먼저 막고, 조작된 요청 대비로 서버 CTSPlaceMine에서도 다시 검사
	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsGamePlaying() || !IsValid(MineClass.Get())
		|| World->GetTimeSeconds() - LastMineRequestTime < MineInterval)
	{
		return;
	}

	LastMineRequestTime = World->GetTimeSeconds();
	CTSPlaceMine();
}

void ACXPawn::CTSPlaceMine_Implementation()
{
	UWorld* World = GetWorld();
	//서버의 실제 경기 상태로 다시 확인. 지뢰 클래스가 BP에서 지정 안 됐으면 안 만듦
	if (IsValid(World) == false || IsGamePlaying() == false || IsValid(MineClass.Get()) == false
		|| World->GetTimeSeconds() - LastMineServerTime < MineInterval)
	{
		return;
	}

	//차량 메시 기준 뒤쪽 위치를 월드 좌표로 바꿈. 클라가 위치를 안 보내니까 서버가 가진 차량 위치만 씀
	const FVector SpawnLocation = GetActorTransform().TransformPosition(MineSpawnOffset);
	//차가 기울어져 있어도 지뢰가 눕지 않게 Yaw만 따라감
	const FRotator SpawnRotation(0.0f, GetActorRotation().Yaw, 0.0f);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this; //지뢰 Owner를 설치한 차량으로 지정
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn; //겹침 구가 QueryOnly라 위치 조정 없이 그 자리에 생성
	//초기 겹침으로 바로 폭발해도 설치 간격은 남도록 생성과 BeginPlay를 나눔
	SpawnParams.bDeferConstruction = true;
	if (ACXMine* Mine = World->SpawnActor<ACXMine>(MineClass, SpawnLocation, SpawnRotation, SpawnParams))
	{
		//실패한 생성 요청은 간격을 소비하지 않고 설치음도 보내지 않음
		LastMineServerTime = World->GetTimeSeconds();
		UGameplayStatics::FinishSpawningActor(Mine, FTransform(SpawnRotation, SpawnLocation));
		MulticastMinePlaced(SpawnLocation);
		UE_LOG(LogTemplateVehicle, Log, TEXT("CX_MINE_ACCEPTED Time=%.3f Vehicle=%s"), LastMineServerTime, *GetName());
	}
}

void ACXPawn::MulticastMinePlaced_Implementation(const FVector& Location)
{
	VehicleAudio->PlayMineInstallSound(Location);
}

void ACXPawn::BreakAccelerator()
{
	UWorld* World = GetWorld();
	if (HasAuthority() == false || IsValid(World) == false)
	{
		return; //고장 상태 원본은 서버가 정하고 소유 클라는 복제된 값을 받음
	}

	bAcceleratorBroken = true;
	//고장 중 다시 밟으면 같은 핸들에 SetTimer를 다시 걸어서 마지막 폭발부터 2초를 셈
	World->GetTimerManager().SetTimer(AcceleratorRepairTimerHandle, this, &ThisClass::RepairAccelerator, 2.0f, false);

	//리슨 서버 호스트가 운전하는 차량은 이 서버에서 입력을 만들어서 다음 Tick 안 기다리고 바로 0으로 만듦
	//원격 클라 차량은 복제된 bAcceleratorBroken을 받은 그 화면의 Tick에서 0이 되고, 서버엔 그다음 입력 RPC로 반영됨
	if (IsLocallyControlled())
	{
		ApplyInputLocks();
	}
}

void ACXPawn::RepairAccelerator()
{
	if (HasAuthority() == false)
	{
		return;
	}

	//지뢰 고장 잠금만 풂. 게임 상태 잠금·핸드브레이크·조향은 안 건드려서 다른 잠금이 같이 안 풀림
	bAcceleratorBroken = false;
}

void ACXPawn::MulticastHitEffect_Implementation(const FVector& TraceStart, const FVector& TraceEnd, bool bHitVehicle, const FVector& ImpactPoint, const FVector& ImpactNormal)
{
	UWorld* World = GetWorld();
	if (IsValid(World) == false || GetNetMode() == NM_DedicatedServer)
	{
		return; //전용 서버엔 보여줄 화면이 없고, 종료 중인 월드엔 새 연출을 만들 수 없음
	}

	VehicleAudio->PlayAttackSound(TraceStart);
	//빗나가거나 벽에 맞아도 검사 경로를 1초 동안 보여줌. 선 표시와 차량 명중 파티클 조건은 따로 둠
	DrawDebugLine(World, TraceStart, TraceEnd, FColor(255, 208, 90), false, 0.7f, 0, 5.0f);
	DrawDebugSphere(World, TraceStart, 8.0f, 8, FColor(255, 208, 90), false, 0.7f);
	if (bHitVehicle == false || IsValid(HitParticle) == false)
	{
		return; //차량에 안 맞았으면 기본값인 충돌 위치·법선을 연출에 안 씀
	}

	//충돌점은 위치, 법선은 방향. 법선을 회전으로 바꿔서 표면 방향 기준으로 효과를 놓음
	//명중 위치 대신 서버가 보낸 피해 차체 중심에 놓아 바퀴에만 효과가 몰리지 않게 함
	UGameplayStatics::SpawnEmitterAtLocation(World, HitParticle, ImpactPoint, ImpactNormal.Rotation());
}

void ACXPawn::CTSAttack_Implementation(const FVector& ViewLocation, const FVector& ViewDirection)
{
	UWorld* World = GetWorld();
	if (IsValid(World) == false)
	{
		return; //월드가 없거나 종료 중이면 충돌 조회를 못 함
	}

	//서버의 실제 경기 상태로 다시 확인. 클라 입력 검사를 건너뛴 요청도 Playing이 아니면 Trace·피해 없음
	if (IsGamePlaying() == false || World->GetTimeSeconds() - LastAttackServerTime < AttackInterval)
	{
		return;
	}

	//카메라 중앙 목표는 서버가 다시 검사하고 실제 공격은 지붕 위에서 시작함
	if (ViewLocation.ContainsNaN() || ViewDirection.ContainsNaN()
		|| FVector::DistSquared(ViewLocation, GetActorLocation()) > FMath::Square(1500.0f)
		|| !FMath::IsNearlyEqual(ViewDirection.SizeSquared(), 1.0f, 0.02f))
	{
		return;
	}
	const FVector Start = GetActorTransform().TransformPosition(MuzzleOffset);
	UE_LOG(LogTemplateVehicle, Verbose, TEXT("CX_ATTACK_TRACE Start=%s View=%s"), *Start.ToString(), *ViewLocation.ToString());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CXAttack), false, this);
	//발사점이 벽 안에 들어간 상태에서는 바깥 표면 너머로 쏘지 않음
	if (World->OverlapBlockingTestByChannel(Start, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(8.0f), Params))
	{
		return;
	}
	//카메라가 차 뒤에 있는 거리까지 조준 검사에 더하고 실제 사거리는 지붕 총구에서 30m로 제한함
	const FVector CameraEnd = ViewLocation + ViewDirection.GetSafeNormal() * (AttackRange + FVector::Distance(ViewLocation, Start));
	FHitResult CameraHit;
	const bool bCameraHit = World->LineTraceSingleByChannel(CameraHit, ViewLocation, CameraEnd, ECC_Visibility, Params);
	const FVector AimPoint = bCameraHit ? CameraHit.ImpactPoint : CameraEnd;
	const FVector ShotDirection = (AimPoint - Start).GetSafeNormal();
	if (ShotDirection.IsNearlyZero())
	{
		return;
	}
	const FVector End = Start + ShotDirection * AttackRange;
	//잘못된 시점이나 막힌 발사점은 간격을 소비하지 않음
	LastAttackServerTime = World->GetTimeSeconds();
	UE_LOG(LogTemplateVehicle, Log, TEXT("CX_ATTACK_ACCEPTED Time=%.3f Vehicle=%s"), LastAttackServerTime, *GetName());

	bool bHitVehicle = false;
	FVector ParticleLocation = FVector::ZeroVector;
	FHitResult HitResult; //Trace 뒤 연출 RPC에서도 결과를 써야 해서 if문 밖에 둠
	World->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, Params);
	//정확한 선이 차량을 놓친 경우에만 반경 30cm로 보완함. 벽을 포함한 첫 충돌만 확인함
	if (Cast<ACXPawn>(HitResult.GetActor()) == nullptr)
	{
		FHitResult AssistHit;
		if (World->SweepSingleByChannel(AssistHit, Start, End, FQuat::Identity, ECC_Visibility,
			FCollisionShape::MakeSphere(AttackAssistRadius), Params)
			&& !AssistHit.bStartPenetrating && Cast<ACXPawn>(AssistHit.GetActor())
			&& FVector::DistSquared(Start, AssistHit.ImpactPoint) <= FMath::Square(AttackRange))
		{
			FHitResult CoverHit;
			if (!World->LineTraceSingleByChannel(CoverHit, Start, AssistHit.ImpactPoint, ECC_Visibility, Params)
				|| CoverHit.GetActor() == AssistHit.GetActor())
			{
				HitResult = AssistHit;
				//보조 범위에 바퀴가 걸린 경우는 일반 명중으로 처리함. 정확한 바퀴 명중만 크리 적용함
				HitResult.BoneName = NAME_None;
			}
		}
	}
	if (ACXPawn* CXPawn = Cast<ACXPawn>(HitResult.GetActor()))
	{
		//5.f는 맞은 차량의 피해량. 공격자 발사 비용이 아니고, 실제 코인 이동은 피격 차량이 처리함
		//ApplyPointDamage는 공격 방향과 HitResult(맞은 위치·컴포넌트·Bone 이름)를 FPointDamageEvent에 담아 TakeDamage로 넘김
		bHitVehicle = UGameplayStatics::ApplyPointDamage(CXPawn, 5.f, ShotDirection, HitResult, GetController(), this, nullptr) > 0.f;
		if (bHitVehicle)
		{
			ParticleLocation = CXPawn->GetMesh()->Bounds.Origin;
		}
	}
	UE_LOG(LogTemplateVehicle, Verbose, TEXT("CX_ATTACK_TRACE End=%s Hit=%s"), *(HitResult.bBlockingHit ? HitResult.ImpactPoint : End).ToString(), *GetNameSafe(HitResult.GetActor()));
	//맞았든 안 맞았든 한 번 호출함. 서버 판정 결과를 보내서 클라마다 Trace를 다시 안 함
	//벽을 포함한 첫 충돌점에서 표시선도 멈춰 실제 판정과 맞춤
	MulticastHitEffect(Start, HitResult.bBlockingHit ? HitResult.ImpactPoint : End, bHitVehicle, ParticleLocation, HitResult.ImpactNormal);
}

float ACXPawn::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	//서버의 유효한 차량 공격인지 먼저 확인함. 잔액은 코인 이동에만 사용함
	if (!HasAuthority() || !IsGamePlaying() || DamageCauser == this || !FMath::IsFinite(DamageAmount)
		|| DamageAmount <= 0.f || static_cast<double>(DamageAmount) > static_cast<double>(MAX_int32))
	{
		return 0.f; //양수 피해·다른 차량·서버 조건을 통과했을 때만 코인 이동 진행
	}

	ACXPawn* Attacker = Cast<ACXPawn>(DamageCauser);
	if (IsValid(Attacker) == false)
	{
		return 0.f; //차량이 아닌 Actor가 준 피해엔 차량 간 코인 이동을 안 함
	}

	ACXPlayerState* VictimPlayerState = GetPlayerState<ACXPlayerState>();
	ACXPlayerState* AttackerPlayerState = Attacker->GetPlayerState<ACXPlayerState>();
	if (IsValid(VictimPlayerState) == false || IsValid(AttackerPlayerState) == false || VictimPlayerState == AttackerPlayerState)
	{
		return 0.f; //접속·빙의 준비 전에는 State가 없을 수 있어서 두 쪽 다 확인한 뒤 차감 시작
	}

	const float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	//다른 피해량이 들어와도 int32 범위 밖 실수를 정수로 안 바꿈. double로 비교해서 정수 최대값의 float 반올림도 피함
	if (!FMath::IsFinite(ActualDamage) || ActualDamage <= 0.f || static_cast<double>(ActualDamage) > static_cast<double>(MAX_int32))
	{
		return 0.f;
	}

	//부모가 돌려준 float 피해를 정수 코인량으로 바꿈. 지금 서버 공격의 피해량은 5.f
	const int32 TransferCoinAmount = (int32)ActualDamage;
	//부족한 잔액이나 지급 상한은 코인 이동만 건너뜀. TryRemoveCoins가 음수 잔액을 막음
	if (TransferCoinAmount > 0 && AttackerPlayerState->GetCoins() <= MAX_int32 - TransferCoinAmount
		&& VictimPlayerState->TryRemoveCoins(TransferCoinAmount))
	{
		UE_LOG(LogTemplateVehicle, Log, TEXT("%s transferred %d coins to %s"), *VictimPlayerState->GetName(), TransferCoinAmount, *AttackerPlayerState->GetName());
		AttackerPlayerState->AddCoins(TransferCoinAmount); //차감이 성공했을 때만 같은 양을 공격자에게 더함
	}

	ApplyVehicleImpact(Attacker->GetActorLocation(), false);
	//ApplyPointDamage로 온 피해만 HitInfo가 있음. 맞은 Bone이 바퀴면 코인 이동과 별개로 피격 차량 BonusMaxSpeed에 음수 패널티
	//잔액과 관계없이 바퀴 패널티와 개인 명중 피드백을 적용함
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& PointDamageEvent = static_cast<const FPointDamageEvent&>(DamageEvent);
		if (IsWheelBone(PointDamageEvent.HitInfo.BoneName))
		{
			//패널티 -10. 스피드 버프 +10과 같은 크기이고 맞을 때마다 누적됨
			VictimPlayerState->ApplyBonusMaxSpeedPenalty(10.f);
			//판정은 서버의 원래 Bone으로 하고 개인 피드백은 각 소유 연결에만 보냄
			if (ACXPlayerController* AttackerPC = Cast<ACXPlayerController>(Attacker->GetController()))
			{
				AttackerPC->STCCriticalHit();
			}
			if (ACXPlayerController* VictimPC = Cast<ACXPlayerController>(GetController()))
			{
				VictimPC->STCWheelHit();
			}
			UE_LOG(LogTemplateVehicle, Log, TEXT("%s wheel hit (%s): BonusMaxSpeed %.2f"), *VictimPlayerState->GetName(), *PointDamageEvent.HitInfo.BoneName.ToString(), VictimPlayerState->GetBonusMaxSpeed());
		}
	}
	return ActualDamage; //코인 이동 여부와 별개로 유효한 피격은 명중 연출을 허용함
}

#undef LOCTEXT_NAMESPACE
