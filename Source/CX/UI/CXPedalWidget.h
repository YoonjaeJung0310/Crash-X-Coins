#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CXPedalWidget.generated.h"

//페달 모양과 밟는 색만 그림. 입력과 고장 판정은 차량이 맡음
UCLASS()
class CX_API UCXPedalWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetPressed(bool bPressed);

protected:
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	bool bInputPressed = false;
};
