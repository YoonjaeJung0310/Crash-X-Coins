// CXSpeedBuffItem.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CXSpeedBuffItem.generated.h"

class USceneComponent;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMeshComponent;
class UParticleSystem;
class USoundBase;

//코인과 별도 Actor 클래스. 먹으면 PlayerState의 BonusMaxSpeed를 10.f 올림
UCLASS()
class CX_API ACXSpeedBuffItem : public AActor
{
	GENERATED_BODY()

public:
	ACXSpeedBuffItem();

protected:
	virtual void BeginPlay() override;

	//코인 아이템과 같은 겹침 서명이고, 실제 보상은 이 클래스가 정함
	UFUNCTION()
	void OnOverlapSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPickupEffects();

	//컴포넌트 이름을 SceneRoot / OverlapSphere / ItemMesh로 코인과 맞춰서 자식 BP에서 같은 구조로 설정함
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpeedBuff")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpeedBuff")
	TObjectPtr<USphereComponent> OverlapSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpeedBuff")
	TObjectPtr<UStaticMeshComponent> ItemMesh;

	//파티클과 소리는 각각 선택 사항. 하나가 없어도 나머지 효과와 속도 보상은 처리됨
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpeedBuff")
	TObjectPtr<UParticleSystem> PickupParticle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SpeedBuff")
	TObjectPtr<USoundBase> PickupSound;
};
