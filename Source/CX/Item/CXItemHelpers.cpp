// CXItemHelpers.cpp

#include "Item/CXItemHelpers.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Vehicle/CXPawn.h"
#include "Player/CXPlayerState.h"
#include "Game/CXGameState.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"

void CXItemHelpers::ConfigurePickupComponents(AActor& Item, USceneComponent& SceneRoot, USphereComponent& OverlapSphere, UStaticMeshComponent& ItemMesh)
{
	//모양과 획득 판정을 분리함. SceneRoot에 구와 메시를 따로 붙여서 메시 외형을 바꿔도 획득 반경 100은 그대로
	Item.SetRootComponent(&SceneRoot);
	OverlapSphere.SetupAttachment(&SceneRoot);
	OverlapSphere.SetSphereRadius(100.f);
	OverlapSphere.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	OverlapSphere.SetCollisionObjectType(ECC_WorldDynamic);
	//Vehicle 채널만 겹침 대상. 차량 쪽도 WorldDynamic을 무시하지 않고 겹침 이벤트를 켜야 실제로 이벤트가 생김
	OverlapSphere.SetCollisionResponseToAllChannels(ECR_Ignore);
	OverlapSphere.SetCollisionResponseToChannel(ECC_Vehicle, ECR_Overlap);
	OverlapSphere.SetGenerateOverlapEvents(true);
	ItemMesh.SetupAttachment(&SceneRoot);
	ItemMesh.SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

ACXPlayerState* CXItemHelpers::FindPickupPlayerState(const AActor& Item, const USphereComponent& OverlapSphere, AActor* OtherActor)
{
	//서버 판정만 허용. 첫 획득 때 충돌을 끈 뒤 남은 겹침 콜백이 와도 같은 아이템을 다시 지급 안 함
	if (Item.HasAuthority() == false || Item.IsActorBeingDestroyed()
		|| OverlapSphere.GetCollisionEnabled() == ECollisionEnabled::NoCollision)
	{
		return nullptr;
	}

	const UWorld* World = Item.GetWorld();
	const ACXGameState* GameState = IsValid(World) ? World->GetGameState<ACXGameState>() : nullptr;
	//대기나 결과 화면에서 코인을 먹어 점수와 승자 판정이 어긋나지 않게 막음
	if (IsValid(GameState) == false || GameState->GetCurrentGameState() != EGameState::Playing)
	{
		return nullptr;
	}

	ACXPawn* VehiclePawn = Cast<ACXPawn>(OtherActor);
	if (IsValid(VehiclePawn) == false)
	{
		return nullptr;
	}

	//차량은 있어도 PlayerState 연결이 아직 없을 수 있음. 그러면 지급·파괴 둘 다 건너뛰고 다음 겹침을 기다림
	ACXPlayerState* PlayerState = VehiclePawn->GetPlayerState<ACXPlayerState>();
	return IsValid(PlayerState) && PlayerState->HasAuthority() ? PlayerState : nullptr;
}

void CXItemHelpers::PlayPickupEffects(const AActor& Item, UParticleSystem* PickupParticle, USoundBase* PickupSound)
{
	UWorld* World = Item.GetWorld();
	//멀티캐스트는 서버에서도 실행됨. 전용 서버엔 화면·소리가 없어서 표시용 객체를 안 만듦
	if (IsValid(World) == false || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	//아이템에 붙이지 않고 위치에 생성해서, 아이템이 바로 파괴돼도 파티클·소리는 자기 수명대로 재생됨
	if (IsValid(PickupParticle))
	{
		UGameplayStatics::SpawnEmitterAtLocation(World, PickupParticle, Item.GetActorLocation());
	}
	if (IsValid(PickupSound))
	{
		UGameplayStatics::PlaySoundAtLocation(World, PickupSound, Item.GetActorLocation());
	}
}
