// Copyright Epic Games, Inc. All Rights Reserved.

#include "Vehicle/CXWheelFront.h"
#include "UObject/ConstructorHelpers.h"

UCXWheelFront::UCXWheelFront()
{
	AxleType = EAxleType::Front;
	bAffectedBySteering = true;
	MaxSteerAngle = 40.f;
}