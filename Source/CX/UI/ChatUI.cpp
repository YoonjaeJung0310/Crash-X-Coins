#include "ChatUI.h"
#include "Player/CXPlayerController.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/Border.h"
#include "Blueprint/WidgetTree.h"
#include "UI/CXFrontendStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"

namespace
{
	void SetChatTextStyle(FEditableTextBoxStyle& Style, const FLinearColor& Background)
	{
		Style.BackgroundImageNormal = FSlateColorBrush(Background);
		Style.BackgroundImageHovered = Style.BackgroundImageNormal;
		Style.BackgroundImageFocused = Style.BackgroundImageNormal;
		Style.BackgroundImageReadOnly = Style.BackgroundImageNormal;
		Style.SetBackgroundColor(FLinearColor::White);
		Style.SetForegroundColor(CXFrontend::Paper());
		Style.SetReadOnlyForegroundColor(CXFrontend::Paper());
		Style.SetFocusedForegroundColor(CXFrontend::Paper());
		Style.TextStyle.SetColorAndOpacity(CXFrontend::Paper());
		Style.TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 18));
		Style.TextStyle.SetShadowOffset(FVector2D(1.f, 1.f));
		Style.TextStyle.SetShadowColorAndOpacity(CXFrontend::Ink());
	}
}

void UChatUI::NativeConstruct()
{
	Super::NativeConstruct();

	//BP가 만든 회색 배경을 바꾸고 로그와 입력 글자는 밝게 유지함
	if (UBorder* Panel = WidgetTree->FindWidget<UBorder>(TEXT("ChatPanel")))
	{
		Panel->SetBrush(FSlateColorBrush(CXFrontend::Ink().CopyWithNewOpacity(.55f)));
		Panel->SetBrushColor(FLinearColor::White);
	}
	SetChatTextStyle(MultilineEditableTextBox_ChatLog->WidgetStyle, FLinearColor::Transparent);
	SetChatTextStyle(EditableTextBox_ChatInput->WidgetStyle, CXFrontend::Ink().CopyWithNewOpacity(.35f));
	MultilineEditableTextBox_ChatLog->SynchronizeProperties();
	EditableTextBox_ChatInput->SynchronizeProperties();

	//위젯을 다시 화면에 붙이면 NativeConstruct가 또 불릴 수 있어서 같은 이벤트를 중복 연결 안 함
	if (EditableTextBox_ChatInput->OnTextCommitted.IsAlreadyBound(this, &UChatUI::OnChatInputTextCommitted) == false)
	{
		EditableTextBox_ChatInput->OnTextCommitted.AddDynamic(this, &UChatUI::OnChatInputTextCommitted);
	}
	MultilineEditableTextBox_ChatLog->SetIsReadOnly(true); //받은 채팅 기록은 출력용이라 직접 수정 못 하게 함
}

void UChatUI::NativeDestruct()
{
	//NativeConstruct에서 연결한 대상·함수를 그대로 지정해서 이 위젯 연결만 해제
	if (EditableTextBox_ChatInput->OnTextCommitted.IsAlreadyBound(this, &UChatUI::OnChatInputTextCommitted))
	{
		EditableTextBox_ChatInput->OnTextCommitted.RemoveDynamic(this, &UChatUI::OnChatInputTextCommitted);
	}

	Super::NativeDestruct();
}

void UChatUI::OnChatInputTextCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod != ETextCommit::OnEnter)
	{
		if (ACXPlayerController* PC = GetOwningPlayer<ACXPlayerController>())
		{
			PC->FocusGameInput();
		}
		return;
	}

	//화면의 FText를 네트워크로 보낼 FString으로 바꿈. 소유 컨트롤러를 통해 보내야 이 클라의 RPC가 서버로 감
	if (ACXPlayerController* PC = GetOwningPlayer<ACXPlayerController>())
	{
		const FString ChatMessage = Text.ToString();
		// Send chat message to server
		if (Text.IsEmpty() == false)
		{
			PC->CTSChatMessage(ChatMessage);
		}
		EditableTextBox_ChatInput->SetText(FText::GetEmpty()); //전송 요청 후 입력창을 비워서 다음 메시지를 바로 쓸 수 있게 함
		PC->FocusGameInput();
	}
}

void UChatUI::FocusInput()
{
	if (APlayerController* PC = GetOwningPlayer(); IsValid(PC) && IsValid(EditableTextBox_ChatInput))
	{
		//입력 중 WASD·F·좌클릭이 차량 입력으로 같이 전달되지 않게 UI만 받음
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(EditableTextBox_ChatInput->TakeWidget());
		PC->SetInputMode(InputMode);
		EditableTextBox_ChatInput->SetKeyboardFocus();
	}
}

void UChatUI::OnNewChatMessage(const FString& InSender, const FString& InMessage)
{
	//기존 텍스트는 두고 새 메시지를 한 줄 아래에 붙임. 첫 메시지 앞엔 빈 줄을 안 만듦
	const FString NewMessage = InSender + TEXT(":") + InMessage;
	FString OldMessage = MultilineEditableTextBox_ChatLog->GetText().ToString();
	if (OldMessage.IsEmpty() == false)
	{
		OldMessage += TEXT("\n");
	}
	const FText Message = FText::FromString(OldMessage + NewMessage);
	MultilineEditableTextBox_ChatLog->SetText(Message);
	ChatScrollBox->ScrollToEnd(); //새 메시지가 붙은 로그 끝으로 스크롤해서 최신 내용이 보이게 함
}
