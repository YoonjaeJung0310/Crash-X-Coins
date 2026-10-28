// CXItemSpawner.cpp

#include "Item/CXItemSpawner.h"
#include "Components/BillboardComponent.h"
#include "Engine/World.h"

ACXItemSpawner::ACXItemSpawner()
{
	//생성·재생성은 BeginPlay / 파괴 알림 / 타이머로 이어져서 매 프레임 검사 안 함
	PrimaryActorTick.bCanEverTick = false;
	BillboardComponent = CreateDefaultSubobject<UBillboardComponent>(TEXT("Billboard"));
	SetRootComponent(BillboardComponent);
}

void ACXItemSpawner::BeginPlay()
{
	Super::BeginPlay();
	//스포너는 클라 World에도 있지만 실제 아이템은 서버만 만듦. 생성된 아이템이 복제됨
	if (HasAuthority())
	{
		SpawnItem();
	}
}

void ACXItemSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	//World가 남아 있는 종료 경로에서 타이머를 해제하고, 남은 아이템이 나중에 파괴돼도 이 스포너를 다시 안 부르게 연결 끊음
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpawnTimerHandle);
	}
	if (IsValid(SpawnedItem))
	{
		SpawnedItem->OnDestroyed.RemoveDynamic(this, &ThisClass::OnItemDestroyed);
	}
	SpawnedItem = nullptr;
	Super::EndPlay(EndPlayReason);
}

void ACXItemSpawner::SpawnItem()
{
	//서버에서 유효한 클래스만 생성. 기존 아이템이 살아 있거나 스포너가 파괴 중이면 새로 안 만듦
	if (HasAuthority() == false || IsActorBeingDestroyed() || IsValid(ItemClass.Get()) == false || IsValid(SpawnedItem))
	{
		return;
	}
	UWorld* World = GetWorld();
	if (IsValid(World) == false)
	{
		return;
	}

	//지연 생성. FinishSpawning에서 겹침·BeginPlay가 돌기 전에 파괴 알림을 연결해야 바로 먹혀도 안 놓침
	SpawnedItem = World->SpawnActorDeferred<AActor>(ItemClass, GetActorTransform(), this, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (IsValid(SpawnedItem) == false)
	{
		return;
	}
	SpawnedItem->OnDestroyed.AddDynamic(this, &ThisClass::OnItemDestroyed);
	SpawnedItem->FinishSpawning(GetActorTransform());
	//FinishSpawning 안에서 바로 먹히면 OnItemDestroyed가 SpawnedItem을 이미 비웠을 수 있어서 아래에서 다시 안 씀
}

void ACXItemSpawner::OnItemDestroyed(AActor* DestroyedActor)
{
	//다른 Actor 알림이거나 스포너가 종료 중이면 재예약 안 함. 파괴 알림의 Actor는 이미 파괴 중이라 IsValid 대신 보관 참조와 비교
	if (HasAuthority() == false || IsActorBeingDestroyed() || DestroyedActor != SpawnedItem)
	{
		return;
	}
	SpawnedItem = nullptr;
	UWorld* World = GetWorld();
	//0 이하 간격은 SetTimer에서 타이머 해제로 처리됨. 유한한 양수일 때만 예약해서 잘못된 값으로 즉시 생성이 반복되는 걸 막음
	if (IsValid(World) == false || IsValid(ItemClass.Get()) == false || FMath::IsFinite(SpawnInterval) == false || SpawnInterval <= 0.f)
	{
		return;
	}
	World->GetTimerManager().SetTimer(SpawnTimerHandle, this, &ThisClass::SpawnItem, SpawnInterval, false);
}
