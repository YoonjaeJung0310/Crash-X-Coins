#pragma once

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"

class UBorder;
class UButton;
class UEditableTextBox;
class UOverlay;
class UTextBlock;
class UVerticalBox;
class UWidgetTree;

//메뉴와 대기실에서 같은 색·글자·버튼 규칙을 씀
namespace CXFrontend
{
	FLinearColor Ink();
	FLinearColor Paper();
	FLinearColor Red();
	FLinearColor Muted();
	FLinearColor Gold();
	UOverlay* Background(UWidgetTree* Tree, bool bUseCover);
	UVerticalBox* Panel(UWidgetTree* Tree, UOverlay* Root, float Width, EHorizontalAlignment Alignment = HAlign_Left);
	UTextBlock* Text(UWidgetTree* Tree, UVerticalBox* Parent, const FString& Value, int32 Size, FLinearColor Color);
	void Gap(UWidgetTree* Tree, UVerticalBox* Parent, float Height = 16.f);
	UEditableTextBox* Input(UWidgetTree* Tree, UVerticalBox* Parent, const FString& Hint);
	UButton* Button(UWidgetTree* Tree, UVerticalBox* Parent, const FString& Caption, bool bPrimary);
}
