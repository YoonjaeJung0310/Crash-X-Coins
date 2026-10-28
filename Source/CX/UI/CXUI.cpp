// Copyright Epic Games, Inc. All Rights Reserved.


#include "UI/CXUI.h"
#include "UI/ChatUI.h"
#include "Game/CXGameState.h"
#include "Player/CXPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UI/CXFrontendStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"
#include "UI/CXPedalWidget.h"
#include "Vehicle/CXPawn.h"
#include "ChaosWheeledVehicleMovementComponent.h"

void UCXUI::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(WidgetTree->RootWidget))
	{
		//화면 중앙 조준 방향을 표시함. 채팅 클릭이나 차량 입력은 가로채지 않음
		UTextBlock* Reticle = WidgetTree->ConstructWidget<UTextBlock>();
		Reticle->SetText(FText::FromString(TEXT("+")));
		Reticle->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 22));
		Reticle->SetColorAndOpacity(CXFrontend::Paper());
		Reticle->SetVisibility(ESlateVisibility::HitTestInvisible);
		UCanvasPanelSlot* ReticleSlot = Canvas->AddChildToCanvas(Reticle);
		ReticleSlot->SetAnchors(FAnchors(.5f, .5f));
		ReticleSlot->SetAlignment(FVector2D(.5f, .5f));
		ReticleSlot->SetAutoSize(true);
		for (const TCHAR* Name : {TEXT("GearLabel"), TEXT("TextBlock_1")})
		{
			if (UWidget* Gear = WidgetTree->FindWidget(FName(Name)))
			{
				Gear->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		if (UTextBlock* Info = WidgetTree->FindWidget<UTextBlock>(TEXT("PlayerStateInfo")))
		{
			Info->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 22));
			Info->SetShadowOffset(FVector2D(1.f, 1.f));
			Info->SetShadowColorAndOpacity(CXFrontend::Ink());
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Info->Slot))
			{
				CanvasSlot->SetAutoSize(true);
			}
		}

		auto Label = [&](FName Name, const FString& Text, int32 Size, FLinearColor Color,
			FVector2D Anchor, FVector2D Position, FVector2D Alignment)
		{
			UTextBlock* Item = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
			Item->SetText(FText::FromString(Text));
			Item->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", Size));
			Item->SetColorAndOpacity(Color);
			Item->SetShadowOffset(FVector2D(1.f, 1.f));
			Item->SetShadowColorAndOpacity(CXFrontend::Ink());
			Item->SetVisibility(ESlateVisibility::HitTestInvisible);
			UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(Item);
			CanvasSlot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
			CanvasSlot->SetAlignment(Alignment);
			CanvasSlot->SetPosition(Position);
			CanvasSlot->SetAutoSize(true);
			return Item;
		};
		BrakePedal = WidgetTree->ConstructWidget<UCXPedalWidget>(UCXPedalWidget::StaticClass(), TEXT("CXBrakePedal"));
		AcceleratorPedal = WidgetTree->ConstructWidget<UCXPedalWidget>(UCXPedalWidget::StaticClass(), TEXT("CXAcceleratorPedal"));
		auto PlacePedal = [&](UCXPedalWidget* Pedal, FVector2D Position, FVector2D Size)
		{
			UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(Pedal);
			CanvasSlot->SetAnchors(FAnchors(0.f, 1.f));
			CanvasSlot->SetAlignment(FVector2D(0.f, 1.f));
			CanvasSlot->SetPosition(Position);
			CanvasSlot->SetSize(Size);
		};
		PlacePedal(BrakePedal, FVector2D(300.f, -88.f), FVector2D(140.f, 80.f));
		PlacePedal(AcceleratorPedal, FVector2D(470.f, -88.f), FVector2D(64.f, 126.f));
		Label(TEXT("CXBrakeCaption"), TEXT("브레이크"), 18, CXFrontend::Paper(), FVector2D(0,1), FVector2D(300,-52), FVector2D(0,1));
		Label(TEXT("CXAcceleratorCaption"), TEXT("가속"), 18, CXFrontend::Paper(), FVector2D(0,1), FVector2D(470,-52), FVector2D(0,1));
		AcceleratorBrokenMarker = Label(TEXT("CXAcceleratorBroken"), TEXT("X"), 60, CXFrontend::Red(), FVector2D(0,1), FVector2D(502,-151), FVector2D(.5,.5));
		CriticalMarker = Label(TEXT("CXCriticalMarker"), TEXT("X"), 44, CXFrontend::Red(), FVector2D(.5,.5), FVector2D(0,0), FVector2D(.5,.5));
		CriticalCaption = Label(TEXT("CXCriticalCaption"), TEXT("CRITICAL"), 18, CXFrontend::Red(), FVector2D(.5,.5), FVector2D(0,40), FVector2D(.5,0));
		WheelHitInfo = Label(TEXT("CXWheelHitInfo"), TEXT("바퀴 피격! 최고속도 저하"), 22, CXFrontend::Red(), FVector2D(1,0), FVector2D(-64,160), FVector2D(1,0));
		for (UTextBlock* Item : {AcceleratorBrokenMarker.Get(), CriticalMarker.Get(), CriticalCaption.Get(), WheelHitInfo.Get()})
		{
			Item->SetVisibility(ESlateVisibility::Hidden);
		}
	}
}

void UCXUI::ShowCriticalHit()
{
	CriticalUntil = GetWorld()->GetTimeSeconds() + .2;
}

void UCXUI::ShowWheelHit()
{
	WheelHitUntil = GetWorld()->GetTimeSeconds() + 2.0;
}

void UCXUI::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	const double Now = GetWorld()->GetTimeSeconds();
	const ESlateVisibility CriticalVisibility = Now < CriticalUntil ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden;
	if (CriticalMarker && CriticalCaption)
	{
		CriticalMarker->SetVisibility(CriticalVisibility);
		CriticalCaption->SetVisibility(CriticalVisibility);
	}
	const APlayerController* PC = GetOwningPlayer();
	const ACXPawn* Pawn = PC ? Cast<ACXPawn>(PC->GetPawn()) : nullptr;
	UChaosWheeledVehicleMovementComponent* Movement = Pawn ? Pawn->GetChaosVehicleMovement().Get() : nullptr;
	const ACXGameState* State = GetWorld()->GetGameState<ACXGameState>();
	const bool bPlaying = State && State->GetCurrentGameState() == EGameState::Playing;
	if (BrakePedal && AcceleratorPedal)
	{
		BrakePedal->SetPressed(bPlaying && Movement && (Movement->GetBrakeInput() > 0.f || Pawn->IsHandbrakeHeld()));
		AcceleratorPedal->SetPressed(bPlaying && Movement && Movement->GetThrottleInput() > 0.f);
	}
	if (AcceleratorBrokenMarker)
	{
		//서버가 복제한 실제 고장 상태가 풀리면 X도 함께 사라짐
		AcceleratorBrokenMarker->SetVisibility(bPlaying && Pawn && Pawn->IsAcceleratorBroken() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	if (WheelHitInfo)
	{
		WheelHitInfo->SetVisibility(bPlaying && Now < WheelHitUntil ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		if (const ACXPlayerState* PlayerState = PC ? PC->GetPlayerState<ACXPlayerState>() : nullptr)
		{
			//RPC보다 속성 복제가 늦게 와도 현재 최고속도로 문구를 다시 읽음
			WheelHitInfo->SetText(FText::FromString(FString::Printf(TEXT("바퀴 피격! 최고속도 저하\nCurrentMaxSpeed: %.0f km/h"), PlayerState->GetCurrentMaxSpeed())));
		}
	}
}

FText UCXUI::Get_GameStateInfo_Text() const
{
	const UWorld* World = GetWorld();
	const ACXGameState* CXGameState = IsValid(World) ? World->GetGameState<ACXGameState>() : nullptr;
	if (IsValid(CXGameState) == false)
	{
		return FText::FromString(TEXT("경기 정보를 기다리는 중")); //접속 직후 GameState가 아직 없어도 다음 바인딩 호출 때 다시 읽음
	}

	switch (CXGameState->GetCurrentGameState())
	{
	case EGameState::Waiting:
		return FText::FromString(CXGameState->IsCountdownActive() ? FString::Printf(TEXT("GAME WILL START IN %d"), CXGameState->GetRemainingTime()) : TEXT("매칭 중…"));
	case EGameState::Playing:
		return FText::FromString(FString::Printf(TEXT("경기 진행 중\n남은 시간: %02d:%02d"), CXGameState->GetRemainingTime() / 60, CXGameState->GetRemainingTime() % 60));
	case EGameState::Finished:
		{
			//다른 플레이어 코인은 OwnerOnly라 HUD에서 점수를 비교 안 함. 서버가 정해서 복제한 승자 목록만 표시
			TArray<FString> WinnerNames;
			for (const ACXPlayerState* Winner : CXGameState->GetWinners())
			{
				if (IsValid(Winner))
				{
					WinnerNames.Add(Winner->GetPlayerName());
				}
			}
			//상태와 Actor 참조는 준비되는 시점이 다를 수 있음. 미해결 참조를 건드리거나 바로 승자가 없다고 판단하지 않음
			if (WinnerNames.IsEmpty() || WinnerNames.Num() != CXGameState->GetWinners().Num())
			{
				return FText::FromString(TEXT("경기 종료\n결과를 확인하는 중"));
			}

			//로컬 결과는 이 HUD 소유 컨트롤러의 PlayerState가 서버 승자 목록에 있는지로만 판정. 다른 사람 Coins는 안 읽음
			const APlayerController* OwningPlayer = GetOwningPlayer();
			const ACXPlayerState* LocalPlayerState = IsValid(OwningPlayer) ? OwningPlayer->GetPlayerState<ACXPlayerState>() : nullptr;
			if (IsValid(LocalPlayerState) == false)
			{
				return FText::FromString(TEXT("경기 종료\n결과를 확인하는 중")); //내 PlayerState가 아직 복제 안 됐으면 패배로 단정하지 않음
			}

			//승자가 둘 이상이고 내가 있으면 공동 승리, 하나면 승리, 없으면 패배
			const bool bLocalWinner = CXGameState->GetWinners().Contains(LocalPlayerState);
			const TCHAR* LocalResult = bLocalWinner ? (WinnerNames.Num() > 1 ? TEXT("공동 승리") : TEXT("승리")) : TEXT("패배");
			return FText::FromString(FString::Printf(TEXT("경기 종료\n결과: %s\n%s: %s\n%d초 뒤 메인 메뉴로 이동"), LocalResult, WinnerNames.Num() > 1 ? TEXT("공동 승자") : TEXT("승자"), *FString::Join(WinnerNames, TEXT(", ")), CXGameState->GetRemainingTime()));
		}
	default:
		return FText::GetEmpty();
	}
}

void UCXUI::OnNewChatMessage(const FString& InSender, const FString& InMessage)
{
	//HUD는 채팅 자식 위젯에 넘기기만 하고, 문자열 누적과 스크롤은 UChatUI가 처리
	if (IsValid(UI_Chat))
	{
		UI_Chat->OnNewChatMessage(InSender, InMessage);
	}
}

void UCXUI::FocusChatInput()
{
	if (IsValid(UI_Chat))
	{
		UI_Chat->FocusInput();
	}
}

void UCXUI::UpdateSpeed(float NewSpeed)
{
	// format the speed to KPH or MPH
	float FormattedSpeed = FMath::Abs(NewSpeed) * (bIsMPH ? 0.022f : 0.036f);

	// call the Blueprint handler
	OnSpeedUpdate(FormattedSpeed);
}

void UCXUI::UpdateGear(int32 NewGear)
{
	// call the Blueprint handler
	OnGearUpdate(NewGear);
}
