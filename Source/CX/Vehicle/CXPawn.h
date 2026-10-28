// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"
#include "Engine/TimerHandle.h"
#include "CXPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UParticleSystem;
class UPrimitiveComponent;
class UChaosWheeledVehicleMovementComponent;
class ACXMine;
class UCXVehicleMovementComponent;
class UCXVehicleAudioComponent;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateVehicle, Log, All);

/**
 *  Vehicle Pawn class
 *  Handles common functionality for all vehicle types,
 *  including input handling and camera management.
 *  
 *  Specific vehicle configurations are handled in subclasses.
 */
UCLASS(abstract)
class ACXPawn : public AWheeledVehiclePawn
{
	GENERATED_BODY()

	friend class UCXVehicleMovementComponent;
	friend class UCXVehicleAudioComponent;

	UPROPERTY(VisibleAnywhere, Category = Vehicle)
	TObjectPtr<UCXVehicleAudioComponent> VehicleAudio;

	/** Spring Arm for the front camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* FrontSpringArm;

	/** Front Camera component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FrontCamera;

	/** Spring Arm for the back camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* BackSpringArm;

	/** Back Camera component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Camera, meta = (AllowPrivateAccess = "true"))
	UCameraComponent* BackCamera;

	/** Cast pointer to the Chaos Vehicle movement component */
	TObjectPtr<UChaosWheeledVehicleMovementComponent> ChaosVehicleMovement;

protected:

	/** Steering Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputAction* SteeringAction;

	/** Throttle Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputAction* ThrottleAction;

	/** Brake Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputAction* BrakeAction;

	/** Handbrake Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputAction* HandbrakeAction;

	/** Look Around Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputAction* LookAroundAction;

	/** Toggle Camera Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputAction* ToggleCameraAction;

	/** Reset Vehicle Action */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputAction* ResetVehicleAction;

	//좌클릭 공격 액션. 이동 액션처럼 Pawn BP에서 IA_Attack을 연결함
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputAction* AttackAction;

	//명중 표시용 파티클. 피해·코인은 서버에서 처리하고 이건 각 화면 연출에만 씀
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Vehicle)
	UParticleSystem* HitParticle;

	//F키 지뢰 설치 액션. AttackAction처럼 두 차량 Pawn BP에서 IA_PlaceMine을 연결함
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputAction* PlaceMineAction;

	//서버가 차 뒤에 만들 지뢰 클래스. 메시·폭발 효과가 설정된 BP_CXMine을 BP에서 지정
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Vehicle)
	TSubclassOf<ACXMine> MineClass;

	//공격 Trace 시작점인 지붕 위 중앙. 실제 차체 높이를 구한 뒤 지붕보다 위로 올림
	//템플릿도 차종별 섀시 높이·바퀴 값을 자식 생성자에서 정해서 같은 방식으로 둠. BP 튜닝용 UPROPERTY는 안 만듦
	FVector MuzzleOffset = FVector::ZeroVector;

	//지뢰를 만들 차량 뒤쪽 위치. 생성 순간 지뢰 겹침 구가 설치한 차에 안 닿게 차종별 생성자에서 정함
	FVector MineSpawnOffset = FVector::ZeroVector;

	//지뢰를 밟아 전진 가속이 막힌 상태. 서버가 바꾸고, Chaos 차량 입력은 소유 클라가 만들어서 소유 연결에만 복제함
	UPROPERTY(Replicated)
	bool bAcceleratorBroken = false;

	//지뢰 고장을 2초 뒤 푸는 서버 타이머. 고장 중 다시 밟으면 같은 핸들로 2초를 새로 셈
	FTimerHandle AcceleratorRepairTimerHandle;

	//핸드브레이크 키를 누르고 있는지 따로 기록해둠. 게임 상태 잠금이 풀려도 누르고 있는 핸드브레이크는 유지되게 함
	bool bHandbrakeHeld = false;

	/** Keeps track of which camera is active */
	bool bFrontCameraActive = false;

	double LastRecoveryServerTime = -10.0;
	double LastCrashServerTime = -10.0;

	//로컬은 요청 빈도를 줄이고 서버는 실제 허용 간격을 따로 검사함
	double LastAttackRequestTime = -10.0;
	double LastMineRequestTime = -10.0;
	double LastAttackServerTime = -10.0;
	double LastMineServerTime = -10.0;

public:
	ACXPawn(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void BeginPlay() override;

	// Begin Pawn interface

	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

	// End Pawn interface

	// Begin Actor interface

	virtual void Tick(float Delta) override;

	//피격 차량이 서버에서 자기 코인을 빼고 공격자 PlayerState에 넘기도록 피해 처리 재정의
	virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	//서버가 판정한 공격 경로·명중 위치를 클라들에 알림. Unreliable이라 빠져도 코인 판정엔 영향 없음
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastHitEffect(const FVector& TraceStart, const FVector& TraceEnd, bool bHitVehicle, const FVector& ImpactPoint, const FVector& ImpactNormal);

	//가속 페달 고장 상태를 소유 연결에 복제하려고 등록함. 부모 Pawn 복제도 그대로 유지
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	//서버의 지뢰가 호출함. 전진 가속을 막고 2초 뒤 이 고장만 풀리게 타이머 예약
	void BreakAccelerator();

	//공격·지뢰가 정한 위치에서 차체에 충격을 줌. 방향과 세기는 서버만 확정함
	void ApplyVehicleImpact(const FVector& Origin, bool bExplosion);

	// End Actor interface

protected:

	/** Handles steering input */
	void Steering(const FInputActionValue& Value);

	/** Handles throttle input */
	void Throttle(const FInputActionValue& Value);

	/** Handles brake input */
	void Brake(const FInputActionValue& Value);

	/** Handles brake start/stop inputs */
	void StartBrake(const FInputActionValue& Value);
	void StopBrake(const FInputActionValue& Value);

	/** Handles handbrake start/stop inputs */
	void StartHandbrake(const FInputActionValue& Value);
	void StopHandbrake(const FInputActionValue& Value);

	/** Handles look around input */
	void LookAround(const FInputActionValue& Value);
	//마우스 상하 입력으로 활성 카메라의 조준 높이를 바꿈
	void LookUp(float Value);

	/** Handles toggle camera input */
	void ToggleCamera(const FInputActionValue& Value);

	/** Handles reset vehicle input */
	void ResetVehicle(const FInputActionValue& Value);

	//입력은 요청만 보내고 안전한 도로 선택과 위치 확정은 서버가 함
	UFUNCTION(Server, Reliable)
	void CTSResetVehicle();

	//같은 차량의 물리 상태와 위치를 서버·소유 화면·다른 화면에서 함께 맞춤
	UFUNCTION(NetMulticast, Reliable)
	void MulticastRecoverVehicle(const FTransform& RecoveryTransform);

	bool RecoverToRoad();

	UFUNCTION()
	void OnVehicleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse, const FHitResult& Hit);

	//물리 충돌음은 서버에서 한 번만 알리고 각 화면에서 재생함
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastCrashSound();

	//지뢰가 실제로 만들어진 경우에만 각 화면에서 설치음을 재생함
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastMinePlaced(const FVector& Location);

	//서버와 소유 화면의 Chaos에 같은 속도 변화를 줌. 다른 화면은 기존 물리 복제를 따름
	UFUNCTION(NetMulticast, Reliable)
	void MulticastVehicleImpact(const FVector& VelocityChange, const FVector& AngularVelocityChange);

	//로컬 입력은 공격 요청만 보냄. 위치 조회와 충돌 판정은 서버의 차량 기준으로 함
	void InputAttack(const FInputActionValue& Value);

	//내가 소유한 차량의 서버 사본에 요청함. 클라가 피격 대상이나 코인량을 보내지 않음
	UFUNCTION(Server, Reliable)
	void CTSAttack(const FVector& ViewLocation, const FVector& ViewDirection);

	//F키 입력은 설치 요청만 보냄. 설치 위치와 지뢰 생성은 서버가 자기 차량 사본 기준으로 정함
	void InputPlaceMine(const FInputActionValue& Value);

	//소유한 차량의 서버 사본에서 차 뒤에 지뢰를 만들고 Owner를 이 차량으로 지정
	UFUNCTION(Server, Reliable)
	void CTSPlaceMine();

	//지뢰 고장 타이머가 끝나면 서버에서 호출됨. 가속 페달 고장 잠금 하나만 풂
	void RepairAccelerator();

	//조작·공격·지뢰 설치를 허용할지만 확인하는 함수. 복제된 GameState가 Playing일 때만 true
	bool IsGamePlaying() const;

	//맞은 Bone이 이 차량 WheelSetups에 등록된 바퀴 Bone인지 확인. 차종마다 다른 Bone 철자를 직접 안 적음
	bool IsWheelBone(FName BoneName) const;

	//소유 화면에서 매 프레임 게임 상태 잠금과 지뢰 고장 잠금을 다시 적용함. 키를 누른 채 상태가 바뀌어도 예전 입력값이 안 남게 함
	void ApplyInputLocks();

	/** Called when the brake lights are turned on or off */
	UFUNCTION(BlueprintImplementableEvent, Category="Vehicle")
	void BrakeLights(bool bBraking);

public:
	bool IsAcceleratorBroken() const { return bAcceleratorBroken; }
	bool IsHandbrakeHeld() const { return bHandbrakeHeld; }

	/** Returns the front spring arm subobject */
	FORCEINLINE USpringArmComponent* GetFrontSpringArm() const { return FrontSpringArm; }
	/** Returns the front camera subobject */
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FrontCamera; }
	/** Returns the back spring arm subobject */
	FORCEINLINE USpringArmComponent* GetBackSpringArm() const { return BackSpringArm; }
	/** Returns the back camera subobject */
	FORCEINLINE UCameraComponent* GetBackCamera() const { return BackCamera; }
	/** Returns the cast Chaos Vehicle Movement subobject */
	FORCEINLINE const TObjectPtr<UChaosWheeledVehicleMovementComponent>& GetChaosVehicleMovement() const { return ChaosVehicleMovement; }
};
