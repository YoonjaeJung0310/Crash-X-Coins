#include "UI/CXPedalWidget.h"
#include "UI/CXFrontendStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"

void UCXPedalWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	WidgetTree->RootWidget = WidgetTree->ConstructWidget<USizeBox>();
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UCXPedalWidget::SetPressed(bool bPressed)
{
	if (bInputPressed != bPressed)
	{
		bInputPressed = bPressed;
		InvalidateLayoutAndVolatility();
	}
}

int32 UCXPedalWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry,
	const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 Layer,
	const FWidgetStyle& Style, bool bParentEnabled) const
{
	const int32 BaseLayer = Super::NativePaint(Args, Geometry, CullingRect, Elements, Layer, Style, bParentEnabled);
	const FVector2D Size = Geometry.GetLocalSize();
	const FLinearColor Color = bInputPressed ? CXFrontend::Gold() : CXFrontend::Paper();
	FSlateRoundedBoxBrush Body(Color.CopyWithNewOpacity(.22f), 8.f, Color.CopyWithNewOpacity(.8f), 2.f);
	Body.OutlineSettings.bUseBrushTransparency = false;
	FSlateDrawElement::MakeBox(Elements, BaseLayer + 1, Geometry.ToPaintGeometry(), &Body,
		ESlateDrawEffect::None, Color.CopyWithNewOpacity(.22f) * Style.GetColorAndOpacityTint());
	//넓은 브레이크와 세로 가속 페달 모두 홈 간격을 일정하게 둠
	for (float X = 12.f; X < Size.X - 6.f; X += 12.f)
	{
		TArray<FVector2D> Points{FVector2D(X, 10.f), FVector2D(X, Size.Y - 10.f)};
		FSlateDrawElement::MakeLines(Elements, BaseLayer + 2, Geometry.ToPaintGeometry(), Points,
			ESlateDrawEffect::None, Color.CopyWithNewOpacity(.65f), true, 2.f);
	}
	return BaseLayer + 2;
}
