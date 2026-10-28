#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CXPresentationSetupCommandlet.generated.h"

UCLASS()
class CXPRESENTATIONSETUPTOOL_API UCXPresentationSetupCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UCXPresentationSetupCommandlet();
    virtual int32 Main(const FString& Params) override;
};
