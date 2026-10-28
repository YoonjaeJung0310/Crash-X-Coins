// CXItemSpawner.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "CXItemSpawner.generated.h"

class UBillboardComponent;

//서버에서 ItemClass 아이템을 하나 유지하고, 파괴되면 SpawnInterval 뒤에 다시 생성함
UCLASS()
class CX_API ACXItemSpawner : public AActor
{
	GENERATED_BODY()

public:
	ACXItemSpawner();

protected:
	virtual void BeginPlay() override;
	//스포너가 끝나면 예약된 타이머와 델리게이트 연결을 정리해서 제거된 스포너가 아이템을 만들지 않게 함
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void SpawnItem();

	//AActor::OnDestroyed 델리게이트 서명 그대로. 들고 있던 아이템이 파괴될 때만 재생성 예약
	UFUNCTION()
	void OnItemDestroyed(AActor* DestroyedActor);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner")
	TSubclassOf<AActor> ItemClass;

	//재생성 대기 시간. 반복 타이머가 아니라 아이템이 파괴될 때마다 한 번씩 예약함
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawner")
	float SpawnInterval = 2.f;

	//런타임 참조라 레벨 기본값으로 저장 안 함. UPROPERTY는 객체 추적용이고 복제는 안 함
	UPROPERTY(Transient)
	TObjectPtr<AActor> SpawnedItem;

	FTimerHandle SpawnTimerHandle;

	//에디터에서 메시에 안 가리고 스포너 위치를 고를 수 있게 하는 루트 Billboard
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spawner")
	TObjectPtr<UBillboardComponent> BillboardComponent;
};
