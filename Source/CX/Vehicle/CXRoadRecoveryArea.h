#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CXRoadRecoveryArea.generated.h"

class UBoxComponent;

//맵에서 실제 도로 범위를 지정함. 바닥 트레이스만으로 건물 지붕을 도로로 고르지 않게 함
UCLASS()
class CX_API ACXRoadRecoveryArea : public AActor
{
	GENERATED_BODY()

public:
	ACXRoadRecoveryArea();
	void ConfigureRoad(const FVector& HalfExtent);
	const UBoxComponent* GetRoadBounds() const { return RoadBounds; }
	bool ContainsRoadPoint(const FVector& Position, float Margin = 0.0f) const;
	void AddRecoveryCandidates(const FVector& Position, float Margin, TArray<FVector>& OutCandidates) const;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> RoadBounds;
};
