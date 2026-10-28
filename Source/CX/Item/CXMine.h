// CXMine.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CXMine.generated.h"

class USceneComponent;
class UPrimitiveComponent;
class USphereComponent;
class UStaticMeshComponent;
class UParticleSystem;
class USoundBase;

//F키로 차량 뒤에 설치하는 지뢰. 서버에서 겹친 차량의 가속 페달을 2초간 고장 내고, 폭발 효과를 모든 화면에 보낸 뒤 사라짐
UCLASS()
class CX_API ACXMine : public AActor
{
	GENERATED_BODY()

public:
	ACXMine();

protected:
	virtual void BeginPlay() override;

	//아이템과 같은 겹침 델리게이트 서명. AddDynamic이 찾을 수 있게 UFUNCTION 붙임
	UFUNCTION()
	void OnOverlapSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	//폭발 파티클·소리를 각 화면에서 재생. 아이템 효과처럼 Reliable로 보낸 다음 서버가 Actor를 파괴함
	UFUNCTION(NetMulticast, Reliable)
	void MulticastExplosionEffects();

	//아이템과 같은 SceneRoot / OverlapSphere 구조. 메시는 지뢰라서 MineMesh로 이름 붙임
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mine")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mine")
	TObjectPtr<USphereComponent> OverlapSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mine")
	TObjectPtr<UStaticMeshComponent> MineMesh;

	//폭발 효과 애셋은 BP_CXMine에서 지정. 비어 있으면 효과만 건너뛰고 가속 페달 고장 처리엔 영향 없음
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mine")
	TObjectPtr<UParticleSystem> ExplosionParticle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mine")
	TObjectPtr<USoundBase> ExplosionSound;
};
