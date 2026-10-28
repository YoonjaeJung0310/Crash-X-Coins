// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vehicle/CXPawn.h"
#include "CXSportsCar.generated.h"

/**
 *  Sports car wheeled vehicle implementation
 */
UCLASS(abstract)
class CX_API ACXSportsCar : public ACXPawn
{
	GENERATED_BODY()
	
public:

	ACXSportsCar();
};
