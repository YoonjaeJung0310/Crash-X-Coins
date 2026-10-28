#include "UI/CXFrontendStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"

namespace CXFrontend
{
	FLinearColor Ink() { return FLinearColor(FColor::FromHex(TEXT("13151A"))); }
	FLinearColor Paper() { return FLinearColor(FColor::FromHex(TEXT("F5F2EC"))); }
	FLinearColor Red() { return FLinearColor(FColor::FromHex(TEXT("D91C2B"))); }
	FLinearColor Muted() { return FLinearColor(FColor::FromHex(TEXT("AEB4BE"))); }
	FLinearColor Gold() { return FLinearColor(FColor::FromHex(TEXT("FFD05A"))); }

	UOverlay* Background(UWidgetTree* Tree, bool bUseCover)
	{
		UOverlay* Root = Tree->ConstructWidget<UOverlay>();
		Tree->RootWidget = Root;
		UBorder* Base = Tree->ConstructWidget<UBorder>();
		Base->SetBrushColor(Ink());
		UOverlaySlot* BaseSlot = Root->AddChildToOverlay(Base);
		BaseSlot->SetHorizontalAlignment(HAlign_Fill);
		BaseSlot->SetVerticalAlignment(VAlign_Fill);
		if (bUseCover)
		{
			if (UTexture2D* Cover = LoadObject<UTexture2D>(nullptr, TEXT("/Game/CX/Art/T_CXCover.T_CXCover")))
			{
				UImage* Image = Tree->ConstructWidget<UImage>();
				Image->SetBrushFromTexture(Cover);
				UOverlaySlot* ImageSlot = Root->AddChildToOverlay(Image);
				ImageSlot->SetHorizontalAlignment(HAlign_Fill);
				ImageSlot->SetVerticalAlignment(VAlign_Fill);
			}
		}
		return Root;
	}

	UVerticalBox* Panel(UWidgetTree* Tree, UOverlay* Root, float Width, EHorizontalAlignment Alignment)
	{
		USizeBox* Size = Tree->ConstructWidget<USizeBox>();
		Size->SetWidthOverride(Width);
		UOverlaySlot* SizeSlot = Root->AddChildToOverlay(Size);
		SizeSlot->SetHorizontalAlignment(Alignment);
		SizeSlot->SetVerticalAlignment(VAlign_Center);
		SizeSlot->SetPadding(FMargin(64.f));
		UBorder* Border = Tree->ConstructWidget<UBorder>();
		Border->SetBrushColor(Ink().CopyWithNewOpacity(.96f));
		Border->SetPadding(FMargin(32.f));
		Size->AddChild(Border);
		UVerticalBox* Content = Tree->ConstructWidget<UVerticalBox>();
		Border->AddChild(Content);
		return Content;
	}

	UTextBlock* Text(UWidgetTree* Tree, UVerticalBox* Parent, const FString& Value, int32 Size, FLinearColor Color)
	{
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>();
		Label->SetText(FText::FromString(Value));
		Label->SetFont(FCoreStyle::GetDefaultFontStyle(Size >= 32 ? "Bold" : "Regular", Size));
		Label->SetColorAndOpacity(Color);
		Label->SetAutoWrapText(true);
		Parent->AddChildToVerticalBox(Label);
		return Label;
	}

	void Gap(UWidgetTree* Tree, UVerticalBox* Parent, float Height)
	{
		USpacer* Spacer = Tree->ConstructWidget<USpacer>();
		Spacer->SetSize(FVector2D(1.f, Height));
		Parent->AddChildToVerticalBox(Spacer);
	}

	UEditableTextBox* Input(UWidgetTree* Tree, UVerticalBox* Parent, const FString& Hint)
	{
		USizeBox* Size = Tree->ConstructWidget<USizeBox>();
		Size->SetHeightOverride(56.f);
		Parent->AddChildToVerticalBox(Size);
		UEditableTextBox* Entry = Tree->ConstructWidget<UEditableTextBox>();
		Entry->SetHintText(FText::FromString(Hint));
		Entry->WidgetStyle.TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 22));
		Entry->SetForegroundColor(Ink());
		Size->AddChild(Entry);
		return Entry;
	}

	UButton* Button(UWidgetTree* Tree, UVerticalBox* Parent, const FString& Caption, bool bPrimary)
	{
		USizeBox* Size = Tree->ConstructWidget<USizeBox>();
		Size->SetHeightOverride(56.f);
		Parent->AddChildToVerticalBox(Size);
		UButton* Result = Tree->ConstructWidget<UButton>();
		Result->SetBackgroundColor(bPrimary ? Red() : Ink());
		UTextBlock* CaptionLabel = Tree->ConstructWidget<UTextBlock>();
		CaptionLabel->SetText(FText::FromString(Caption));
		CaptionLabel->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 22));
		CaptionLabel->SetColorAndOpacity(Paper());
		CaptionLabel->SetJustification(ETextJustify::Center);
		Result->AddChild(CaptionLabel);
		Size->AddChild(Result);
		return Result;
	}
}
