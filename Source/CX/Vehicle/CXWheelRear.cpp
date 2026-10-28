// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicle/CXWheelRear.h"
#include "UObject/ConstructorHelpers.h"

UCXWheelRear::UCXWheelRear()
{
	AxleType = EAxleType::Rear;
	bAffectedByHandbrake = true;
	bAffectedByEngine = true;
}