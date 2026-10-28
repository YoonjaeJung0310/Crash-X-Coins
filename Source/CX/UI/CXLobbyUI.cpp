#include "UI/CXLobbyUI.h"
#include "UI/CXFrontendStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Game/CXGameInstance.h"
#include "Game/CXGameState.h"
#include "Game/CXMatchRules.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Player/CXPlayerState.h"
#include "Player/CXPlayerController.h"

void UCXLobbyUI::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>();
	MatchingBackground = CXFrontend::Background(WidgetTree, true);
	WidgetTree->RootWidget = Root;
	UOverlaySlot* CoverSlot = Root->AddChildToOverlay(MatchingBackground);
	CoverSlot->SetHorizontalAlignment(HAlign_Fill);
	CoverSlot->SetVerticalAlignment(VAlign_Fill);
	UVerticalBox* MatchPanel = CXFrontend::Panel(WidgetTree, MatchingBackground, 600.f);
	CXFrontend::Text(WidgetTree, MatchPanel, TEXT("CRASH X COINS"), 48, CXFrontend::Paper());
	CXFrontend::Text(WidgetTree, MatchPanel, TEXT("CX · 2인 차량 대전"), 22, CXFrontend::Red());
	CXFrontend::Gap(WidgetTree, MatchPanel);
	StatusLabel = CXFrontend::Text(WidgetTree, MatchPanel, TEXT("매칭 중…"), 32, CXFrontend::Gold());
	PlayersLabel = CXFrontend::Text(WidgetTree, MatchPanel, TEXT("다른 드라이버를 찾고 있어요."), 22, CXFrontend::Paper());
	CXFrontend::Gap(WidgetTree, MatchPanel);
	CXFrontend::Text(WidgetTree, MatchPanel, TEXT("두 드라이버의 준비가 끝나면 도시에서 카운트다운이 시작돼요."), 18, CXFrontend::Muted());
	CXFrontend::Gap(WidgetTree, MatchPanel);
	UButton* CancelButton = CXFrontend::Button(WidgetTree, MatchPanel, TEXT("매칭 취소"), false);
	CancelButton->OnClicked.AddDynamic(this, &ThisClass::OnLeaveClicked);

	//카운트다운과 결과에는 표지 이미지를 깔지 않고 실제 도시 화면을 남겨둠
	ScenePanel = CXFrontend::Panel(WidgetTree, Root, 720.f, HAlign_Center);
	SceneStatusLabel = CXFrontend::Text(WidgetTree, ScenePanel, TEXT(""), 48, CXFrontend::Gold());
	SceneStatusLabel->SetJustification(ETextJustify::Center);
	SceneHintLabel = CXFrontend::Text(WidgetTree, ScenePanel, TEXT(""), 22, CXFrontend::Paper());
	SceneHintLabel->SetJustification(ETextJustify::Center);
	CXFrontend::Gap(WidgetTree, ScenePanel);
	ResumeButton = CXFrontend::Button(WidgetTree, ScenePanel, TEXT("계속 플레이"), true);
	ResumeButton->OnClicked.AddDynamic(this, &ThisClass::OnResumeClicked);
	CXFrontend::Gap(WidgetTree, ScenePanel);
	LeaveButton = CXFrontend::Button(WidgetTree, ScenePanel, TEXT("메인 메뉴"), false);
	LeaveButton->OnClicked.AddDynamic(this, &ThisClass::OnLeaveClicked);
}

void UCXLobbyUI::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	const ACXGameState* State = GetWorld() ? GetWorld()->GetGameState<ACXGameState>() : nullptr;
	if (IsValid(State) == false)
	{
		return;
	}
	const bool bMatching = State->GetCurrentGameState() == EGameState::Waiting && State->IsCountdownActive() == false;
	MatchingBackground->SetVisibility(bMatching ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	ScenePanel->SetVisibility(bMatching ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	if (bMatching)
	{
		FString Roster = FString::Printf(TEXT("참가자 %d / 2\n"), State->PlayerArray.Num());
		for (const APlayerState* Player : State->PlayerArray)
		{
			if (IsValid(Player))
			{
				Roster += TEXT("\n") + Player->GetPlayerName();
				if (GetOwningPlayer() && Player == GetOwningPlayer()->PlayerState)
				{
					Roster += TEXT("  (나)");
				}
			}
		}
		PlayersLabel->SetText(FText::FromString(Roster));
		StatusLabel->SetText(FText::FromString(State->PlayerArray.Num() >= CXMatchRules::MinimumPlayers ? TEXT("매칭 완료 · 경기장 준비 중…") : TEXT("매칭 중…")));
		return;
	}
	const bool bPlaying = State->GetCurrentGameState() == EGameState::Playing;
	const bool bStarting = bPlaying && State->GetRemainingTime() == CXMatchRules::RoundSeconds;
	ResumeButton->SetVisibility(bPlaying && bStarting == false ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	LeaveButton->SetVisibility(bPlaying && bStarting == false ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (State->GetCurrentGameState() == EGameState::Waiting)
	{
		SceneStatusLabel->SetText(FText::FromString(FString::Printf(TEXT("GAME WILL START IN\n%d"), State->GetRemainingTime())));
		SceneHintLabel->SetText(FText::FromString(TEXT("START! 이후에 주행·공격할 수 있어요.")));
	}
	else if (bPlaying)
	{
		SceneStatusLabel->SetText(FText::FromString(bStarting ? TEXT("0\nSTART!") : TEXT("경기 메뉴")));
		SceneHintLabel->SetText(FText::FromString(bStarting ? TEXT("03:30 · 코인을 모으세요!") : TEXT("경기 시간은 계속 흘러가요. M키로 돌아갈 수 있어요.")));
	}
	else
	{
		TArray<FString> WinnerNames;
		for (const ACXPlayerState* Winner : State->GetWinners())
		{
			if (IsValid(Winner))
			{
				WinnerNames.Add(Winner->GetPlayerName());
			}
		}
		const ACXPlayerState* LocalState = GetOwningPlayer() ? GetOwningPlayer()->GetPlayerState<ACXPlayerState>() : nullptr;
		if (WinnerNames.IsEmpty() || WinnerNames.Num() != State->GetWinners().Num() || IsValid(LocalState) == false)
		{
			SceneStatusLabel->SetText(FText::FromString(TEXT("경기 종료 · 결과 확인 중")));
		}
		else
		{
			const bool bWinner = State->GetWinners().Contains(LocalState);
			const TCHAR* Result = bWinner ? (WinnerNames.Num() > 1 ? TEXT("공동 승리!") : TEXT("승리!")) : TEXT("패배");
			SceneStatusLabel->SetText(FText::FromString(FString::Printf(TEXT("%s\n%s"), Result, *FString::Join(WinnerNames, TEXT(", ")))));
		}
		SceneHintLabel->SetText(FText::FromString(FString::Printf(TEXT("차량 조작이 잠겼어요.\n%d초 뒤 자동으로 메인 메뉴로 돌아갑니다."), State->GetRemainingTime())));
	}
}

void UCXLobbyUI::OnLeaveClicked()
{
	if (UCXGameInstance* Instance = GetGameInstance<UCXGameInstance>())
	{
		Instance->LeaveRoom();
	}
}

void UCXLobbyUI::OnResumeClicked()
{
	if (ACXPlayerController* Controller = Cast<ACXPlayerController>(GetOwningPlayer()))
	{
		Controller->ToggleLobbyOverlay();
	}
}
