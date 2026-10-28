// CXItemHelpers.h

#pragma once

#include "CoreMinimal.h"

class AActor;
class ACXPlayerState;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;
class UParticleSystem;
class USoundBase;

//아이템끼리 같은 설정·효과 코드를 묶은 모듈 내부 함수. 따로 Actor 부모 클래스나 BP용 API는 안 만듦
//ACXMine도 같은 구·메시 구성과 위치 효과 재생이 필요해서 ConfigurePickupComponents, PlayPickupEffects를 같이 씀. FindPickupPlayerState는 지뢰에서 안 씀
namespace CXItemHelpers
{
	void ConfigurePickupComponents(AActor& Item, USceneComponent& SceneRoot, USphereComponent& OverlapSphere, UStaticMeshComponent& ItemMesh);
	ACXPlayerState* FindPickupPlayerState(const AActor& Item, const USphereComponent& OverlapSphere, AActor* OtherActor);
	void PlayPickupEffects(const AActor& Item, UParticleSystem* PickupParticle, USoundBase* PickupSound);
}
