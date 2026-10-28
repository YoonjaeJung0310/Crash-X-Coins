// CXCoinItem.cpp

#include "Item/CXCoinItem.h"
#include "Item/CXItemHelpers.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Player/CXPlayerState.h"

ACXCoinItem::ACXCoinItem()
{
	//획득은 겹침 이벤트로 처리해서 Tick 필요 없음. 서버의 생성·파괴가 클라에 가도록 Actor 복제 켬
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	//갱신 빈도 1.f. UE 5.6 권장 함수로 설정해서 폐기 예정인 필드 직접 접근을 피함
	SetNetUpdateFrequency(1.f);
	SetMinNetUpdateFrequency(1.f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	OverlapSphere = CreateDefaultSubobject<USphereComponent>(TEXT("OverlapSphere"));
	ItemMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ItemMesh"));
	CXItemHelpers::ConfigurePickupComponents(*this, *SceneRoot, *OverlapSphere, *ItemMesh);
}

void ACXCoinItem::BeginPlay()
{
	Super::BeginPlay();
	//클라 복제본엔 획득 콜백을 안 붙임. 콜백 안에서도 권한을 다시 검사해서 지급은 서버에서만 일어남
	if (HasAuthority())
	{
		OverlapSphere->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnOverlapSphereBeginOverlap);
	}
}

void ACXCoinItem::OnOverlapSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	ACXPlayerState* PlayerState = CXItemHelpers::FindPickupPlayerState(*this, *OverlapSphere, OtherActor);
	//AddCoins가 int32 범위를 넘는 요청을 거절해서 아이템도 지급 전에 같은 조건 확인. 지급은 못 하고 아이템만 사라지는 걸 막음
	if (IsValid(PlayerState) == false || PlayerState->GetCoins() > MAX_int32 - 10)
	{
		return;
	}

	//파괴 전에 다른 차량·차체 콜백이 또 들어올 수 있어서 충돌부터 끔. 획득 여부 변수는 따로 안 둠
	OverlapSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OverlapSphere->SetGenerateOverlapEvents(false);
	PlayerState->AddCoins(10);
	MulticastPickupEffects();
	Destroy(); //서버 파괴가 클라에도 전달되고, 스포너의 OnDestroyed도 이때 불림
}

void ACXCoinItem::MulticastPickupEffects_Implementation()
{
	//두 아이템이 같은 방식으로 효과를 재생해서 작은 내부 함수 하나를 같이 씀
	CXItemHelpers::PlayPickupEffects(*this, PickupParticle, PickupSound);
}
