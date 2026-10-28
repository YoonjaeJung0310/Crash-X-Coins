// CXCoinItem.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CXCoinItem.generated.h"

class USceneComponent;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMeshComponent;
class UParticleSystem;
class USoundBase;

//서버에서 먹은 차량을 판정하고 코인 10 지급. 효과는 따로 멀티캐스트로 각 화면에서 재생
UCLASS()
class CX_API ACXCoinItem : public AActor
{
	GENERATED_BODY()

public:
	ACXCoinItem();

protected:
	virtual void BeginPlay() override;

	//컴포넌트 겹침 델리게이트와 같은 6개 인자 서명. AddDynamic이 찾을 수 있게 UFUNCTION 붙임
	UFUNCTION()
	void OnOverlapSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	//Reliable 멀티캐스트. 서버에서 이 이름으로 호출하고 재생 내용은 cpp의 _Implementation에 있음
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPickupEffects();

	//네이티브 컴포넌트를 UPROPERTY로 잡아둬서 GC가 추적하고, BP에서 세부 설정을 볼 수 있음
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coin")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coin")
	TObjectPtr<USphereComponent> OverlapSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Coin")
	TObjectPtr<UStaticMeshComponent> ItemMesh;

	//효과 애셋은 자식 BP에서 지정. 비어 있으면 재생만 건너뛰고 코인 지급엔 영향 없음
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coin")
	TObjectPtr<UParticleSystem> PickupParticle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coin")
	TObjectPtr<USoundBase> PickupSound;
};
