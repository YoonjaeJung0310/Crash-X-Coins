#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/SlateEnums.h"
#include "ChatUI.generated.h"

class UEditableTextBox;
class UScrollBox;
class UMultiLineEditableTextBox;

//채팅 위젯은 입력 이벤트와 화면 표시만 담당. 서버 전송과 플레이어별 전달은 PlayerController 담당
//C++ 클래스는 동작을 맡고, 화면 배치는 이걸 부모로 하는 위젯 BP에서 정함
//자식 위젯 BP는 차량 HUD 안에 UI_Chat으로 배치됨. 채팅 위젯은 HUD가 들고 있음
UCLASS()
class CX_API UChatUI : public UUserWidget
{
	GENERATED_BODY()

public:
	//서버에서 받은 메시지를 기존 로그 아래에 붙임. 부모 HUD(UCXUI)가 부를 수 있게 public
	//컨트롤러 → 차량 HUD의 OnNewChatMessage → 채팅 위젯의 같은 이름 함수 순서로 호출됨
	void OnNewChatMessage(const FString& InSender, const FString& InMessage);
	void FocusInput();

protected:
	//위젯이 구성될 때 입력 이벤트를 연결하고 출력창을 읽기 전용으로 둠
	virtual void NativeConstruct() override;

	//위젯이 화면에서 빠질 때 연결한 이벤트를 해제해서, 다시 추가돼도 예전 연결이 안 남게 함
	virtual void NativeDestruct() override;

	//BindWidget은 BP에서 이름·형식이 같은 위젯을 자동 연결해서 세 위젯 이름을 그대로 맞춰야 함
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UEditableTextBox> EditableTextBox_ChatInput;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UScrollBox> ChatScrollBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UMultiLineEditableTextBox> MultilineEditableTextBox_ChatLog;

protected:
	//OnTextCommitted는 Enter 말고 포커스가 바뀔 때도 불려서 CommitMethod로 Enter만 전송함
	UFUNCTION()
	void OnChatInputTextCommitted(const FText& Text, ETextCommit::Type CommitMethod);
};
