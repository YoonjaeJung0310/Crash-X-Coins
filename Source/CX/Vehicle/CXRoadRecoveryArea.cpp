#include "Vehicle/CXRoadRecoveryArea.h"
#include "Components/BoxComponent.h"

ACXRoadRecoveryArea::ACXRoadRecoveryArea()
{
	RoadBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("RoadBounds"));
	SetRootComponent(RoadBounds);
	RoadBounds->SetBoxExtent(FVector(11500.0f, 1500.0f, 300.0f));
	RoadBounds->SetCollisionProfileName(TEXT("NoCollision"));
	RoadBounds->SetGenerateOverlapEvents(false);
	RoadBounds->SetHiddenInGame(true);
	PrimaryActorTick.bCanEverTick = false;
}

void ACXRoadRecoveryArea::ConfigureRoad(const FVector& HalfExtent)
{
	RoadBounds->SetBoxExtent(HalfExtent.GetAbs());
}

bool ACXRoadRecoveryArea::ContainsRoadPoint(const FVector& Position, float Margin) const
{
	const FVector Local = RoadBounds->GetComponentTransform().InverseTransformPosition(Position);
	const FVector Extent = RoadBounds->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X - Margin && FMath::Abs(Local.Y) <= Extent.Y - Margin
		&& FMath::Abs(Local.Z) <= Extent.Z;
}

void ACXRoadRecoveryArea::AddRecoveryCandidates(const FVector& Position, float Margin, TArray<FVector>& OutCandidates) const
{
	const FTransform Transform = RoadBounds->GetComponentTransform();
	const FVector Local = Transform.InverseTransformPosition(Position);
	const FVector Extent = RoadBounds->GetUnscaledBoxExtent() - FVector(Margin, Margin, 0.0f);
	if (Extent.X <= 0.0f || Extent.Y <= 0.0f)
	{
		return;
	}
	//막힌 곳이 없으면 사각형의 정확한 최근점을 먼저 씀. 점유 중이면 2m 격자 후보를 함께 비교함
	OutCandidates.Add(Transform.TransformPosition(FVector(FMath::Clamp(Local.X, -Extent.X, Extent.X), FMath::Clamp(Local.Y, -Extent.Y, Extent.Y), 0.0f)));
	for (float X = -Extent.X; X <= Extent.X; X += 200.0f)
	{
		for (float Y = -Extent.Y; Y <= Extent.Y; Y += 200.0f)
		{
			OutCandidates.Add(Transform.TransformPosition(FVector(X, Y, 0.0f)));
		}
	}
}
