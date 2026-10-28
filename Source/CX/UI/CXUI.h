// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CXUI.generated.h"

class UChatUI;
class UTextBlock;
class UCXPedalWidget;

/**
 *  Simple Vehicle HUD class
 *  Displays the current speed and gear.
 *  Widget setup is handled in a Blueprint subclass.
 */
UCLASS(abstract)
class CX_API UCXUI : public UUserWidget
{
	GENERATED_BODY()
	
#pragma region ChatUI
public:
	//컨트롤러가 받은 채팅을 HUD 안 채팅 위젯으로 넘김. 속도·기어 표시와는 별개 역할
	void OnNewChatMessage(const FString& InSender, const FString& InMessage);
	void FocusChatInput();
	void ShowCriticalHit();
	void ShowWheelHit();

protected:
	//로컬 플레이어가 보는 채팅 위젯. UPROPERTY로 들고 있어서 GC 중에도 참조 유지
	//HUD BP 안의 UChatUI 자식 위젯 이름이 UI_Chat이어야 이 포인터에 자동 연결됨
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UChatUI> UI_Chat;
#pragma endregion

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Controls the display of speed in Km/h or MPH */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Vehicle)
	bool bIsMPH = false;

public:
	//복제된 GameState 값으로 경기 상태·남은 시간·결과 문구를 만듦. HUD는 타이머를 돌리거나 승자를 계산하지 않음
	UFUNCTION(BlueprintPure, Category = "Game State")
	FText Get_GameStateInfo_Text() const;

	/** Called to update the speed display */
	void UpdateSpeed(float NewSpeed);

	/** Called to update the gear display */
	void UpdateGear(int32 NewGear);

private:
	UPROPERTY(Transient)
	TObjectPtr<UCXPedalWidget> BrakePedal;
	UPROPERTY(Transient)
	TObjectPtr<UCXPedalWidget> AcceleratorPedal;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CriticalMarker;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> CriticalCaption;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> WheelHitInfo;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> AcceleratorBrokenMarker;

	double CriticalUntil = -1.0;
	double WheelHitUntil = -1.0;

protected:

	/** Implemented in Blueprint to display the new speed */
	UFUNCTION(BlueprintImplementableEvent, Category = Vehicle)
	void OnSpeedUpdate(float NewSpeed);

	/** Implemented in Blueprint to display the new gear */
	UFUNCTION(BlueprintImplementableEvent, Category = Vehicle)
	void OnGearUpdate(int32 NewGear);
};
