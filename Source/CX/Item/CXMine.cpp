// CXMine.cpp

#include "Item/CXMine.h"
#include "Item/CXItemHelpers.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Vehicle/CXPawn.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

ACXMine::ACXMine()
{
	//폭발은 겹침 이벤트로 처리해서 Tick 필요 없음. 서버가 만든 지뢰를 모든 클라가 보도록 Actor 복제 켬
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	OverlapSphere = CreateDefaultSubobject<USphereComponent>(TEXT("OverlapSphere"));
	MineMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MineMesh"));
	//QueryOnly 구가 Vehicle 채널만 겹치고 메시는 충돌 없는 아이템 구성을 그대로 쓰려고 기존 설정 함수 재사용
	//그래서 겹침 반경도 아이템과 같은 100. 두 차종의 MineSpawnOffset은 이 반경 100 기준으로 계산함
	CXItemHelpers::ConfigurePickupComponents(*this, *SceneRoot, *OverlapSphere, *MineMesh);
	static ConstructorHelpers::FObjectFinder<USoundBase> ExplosionAsset(TEXT("/Game/CX/Sound/MineExplosion.MineExplosion"));
	ExplosionSound = ExplosionAsset.Object;
}

void ACXMine::BeginPlay()
{
	Super::BeginPlay();
	//클라 복제본엔 폭발 판정을 안 붙임. UE 5.6은 BeginPlay 직후 초기 겹침을 갱신해서 생성 위치에 이미 있던 차량도 이 콜백을 받음
	if (HasAuthority())
	{
		OverlapSphere->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::OnOverlapSphereBeginOverlap);
	}
}

void ACXMine::OnOverlapSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	//서버에서 아직 안 터진 지뢰만 처리. 첫 폭발 때 구 충돌을 끄니까 같은 프레임의 다른 겹침 콜백은 여기서 걸러짐
	if (HasAuthority() == false || IsActorBeingDestroyed() || OverlapSphere->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
	{
		return;
	}

	//겹친 차량이면 설치한 차량(Owner)도 예외 없음. 후진해서 자기 지뢰를 밟으면 자기 가속 페달도 고장 남
	ACXPawn* VehiclePawn = Cast<ACXPawn>(OtherActor);
	if (IsValid(VehiclePawn) == false)
	{
		return;
	}

	//중복 폭발을 막으려고 효과·파괴보다 먼저 구 충돌과 겹침 이벤트를 끔. bExploded 같은 변수는 따로 안 둠
	OverlapSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OverlapSphere->SetGenerateOverlapEvents(false);
	VehiclePawn->BreakAccelerator();
	VehiclePawn->ApplyVehicleImpact(GetActorLocation(), true);
	MulticastExplosionEffects();
	Destroy(); //서버 파괴가 복제돼서 모든 화면에서 지뢰가 사라짐
}

void ACXMine::MulticastExplosionEffects_Implementation()
{
	//아이템 효과와 같은 "위치에 파티클·소리 생성, 전용 서버 제외" 동작이라 기존 함수 재사용
	CXItemHelpers::PlayPickupEffects(*this, ExplosionParticle, ExplosionSound);
}
