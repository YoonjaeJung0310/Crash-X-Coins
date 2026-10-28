// CXSpeedBuffItem.cpp

#include "Item/CXSpeedBuffItem.h"
#include "Item/CXItemHelpers.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Player/CXPlayerState.h"

ACXSpeedBuffItem::ACXSpeedBuffItem()
{
	//코인과 같은 복제·충돌 조건을 쓰고 Tick은 안 씀
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetNetUpdateFrequency(1.f);
	SetMinNetUpdateFrequency(1.f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	OverlapSphere = CreateDefaultSubobject<USphereComponent>(TEXT("OverlapSphere"));
	ItemMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ItemMesh"));
	CXItemHelpers::ConfigurePickupComponents(*this, *SceneRoot, *OverlapSphere, *ItemMesh);
}

void ACXSpeedBuffItem::BeginPlay()
{
	Super::BeginPlay();
	//클라는 복제 결과와 효과만 받음. PlayerState를 바꾸는 겹침 처리는 서버 쪽에만 연결
	if (HasAuthority())
	{
		OverlapSphere->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnOverlapSphereBeginOverlap);
	}
}

void ACXSpeedBuffItem::OnOverlapSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ACXPlayerState* PlayerState = CXItemHelpers::FindPickupPlayerState(*this, *OverlapSphere, OtherActor);
	//PlayerState가 아직 없는 차량엔 지급도 파괴도 안 함. AddBonusMaxSpeed의 유한값 검사도 지급 전에 같이 확인
	if (IsValid(PlayerState) == false || FMath::IsFinite(PlayerState->GetBonusMaxSpeed() + 10.f) == false)
	{
		return;
	}

	//첫 지급 직전에 구 이벤트를 막아서 같은 아이템으로 속도 보상을 두 번 받지 않게 함
	OverlapSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OverlapSphere->SetGenerateOverlapEvents(false);
	PlayerState->AddBonusMaxSpeed(10.f);
	MulticastPickupEffects();
	Destroy();
}

void ACXSpeedBuffItem::MulticastPickupEffects_Implementation()
{
	CXItemHelpers::PlayPickupEffects(*this, PickupParticle, PickupSound);
}
