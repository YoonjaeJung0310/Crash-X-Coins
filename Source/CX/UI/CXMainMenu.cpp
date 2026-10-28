#include "UI/CXMainMenu.h"
#include "UI/CXFrontendStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Game/CXGameInstance.h"
#include "Kismet/KismetSystemLibrary.h"

void UCXMainMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UOverlay* Root = CXFrontend::Background(WidgetTree, true);
	MenuPanel = CXFrontend::Panel(WidgetTree, Root, 600.f);
	CXFrontend::Text(WidgetTree, MenuPanel, TEXT("CRASH X COINS"), 48, CXFrontend::Paper());
	CXFrontend::Text(WidgetTree, MenuPanel, TEXT("CX · 2인 차량 대전"), 22, CXFrontend::Red());
	CXFrontend::Gap(WidgetTree, MenuPanel);
	CXFrontend::Text(WidgetTree, MenuPanel, TEXT("3분 30초 동안 더 많은 코인을 모으면 승리"), 18, CXFrontend::Muted());
	CXFrontend::Gap(WidgetTree, MenuPanel);
	CXFrontend::Text(WidgetTree, MenuPanel, TEXT("닉네임"), 18, CXFrontend::Muted());
	NameInput = CXFrontend::Input(WidgetTree, MenuPanel, TEXT("1~16글자 · 한글/영문/숫자/_/-"));
	NameInput->OnTextCommitted.AddDynamic(this, &ThisClass::OnNameCommitted);
	CXFrontend::Gap(WidgetTree, MenuPanel);
	CXFrontend::Text(WidgetTree, MenuPanel, TEXT("서버 주소 · IP 또는 IP:포트"), 18, CXFrontend::Muted());
	AddressInput = CXFrontend::Input(WidgetTree, MenuPanel, TEXT("서버를 실행한 PC의 IP 주소"));
	if (const UCXGameInstance* Instance = GetGameInstance<UCXGameInstance>())
	{
		NameInput->SetText(FText::FromString(Instance->GetPlayerName()));
		AddressInput->SetText(FText::FromString(Instance->GetDefaultServerAddress()));
		CXFrontend::Text(WidgetTree, MenuPanel,
			FString::Printf(TEXT("예: %s · IP만 입력해도 돼요."), *Instance->GetDefaultServerAddress()),
			18, CXFrontend::Muted());
	}
	AddressInput->OnTextCommitted.AddDynamic(this, &ThisClass::OnNameCommitted);
	CXFrontend::Gap(WidgetTree, MenuPanel);
	JoinButton = CXFrontend::Button(WidgetTree, MenuPanel, TEXT("게임 시작"), true);
	JoinButton->OnClicked.AddDynamic(this, &ThisClass::OnJoinClicked);
	CXFrontend::Gap(WidgetTree, MenuPanel);
	StatusLabel = CXFrontend::Text(WidgetTree, MenuPanel, TEXT("닉네임과 서버 주소를 입력하세요."), 18, CXFrontend::Gold());
	CXFrontend::Gap(WidgetTree, MenuPanel);
	UButton* HelpButton = CXFrontend::Button(WidgetTree, MenuPanel, TEXT("게임 설명"), false);
	HelpButton->OnClicked.AddDynamic(this, &ThisClass::OnHelpClicked);
	CXFrontend::Gap(WidgetTree, MenuPanel);
	UButton* ExitButton = CXFrontend::Button(WidgetTree, MenuPanel, TEXT("나가기"), false);
	ExitButton->OnClicked.AddDynamic(this, &ThisClass::OnExitClicked);

	HelpPanel = CXFrontend::Panel(WidgetTree, Root, 720.f);
	CXFrontend::Text(WidgetTree, HelpPanel, TEXT("CRASH X COINS"), 40, CXFrontend::Paper());
	CXFrontend::Text(WidgetTree, HelpPanel, TEXT("2인 플레이 안내"), 22, CXFrontend::Red());
	CXFrontend::Gap(WidgetTree, HelpPanel);
	//설명이 길어져도 닫기 버튼은 화면에 남겨둠
	USizeBox* HelpSize = WidgetTree->ConstructWidget<USizeBox>();
	HelpSize->SetHeightOverride(480.f);
	HelpPanel->AddChildToVerticalBox(HelpSize);
	UScrollBox* HelpScroll = WidgetTree->ConstructWidget<UScrollBox>();
	HelpSize->AddChild(HelpScroll);
	UVerticalBox* HelpContent = WidgetTree->ConstructWidget<UVerticalBox>();
	HelpScroll->AddChild(HelpContent);
	auto HelpText = [&](const FString& Value)
	{
		UTextBlock* Label = CXFrontend::Text(WidgetTree, HelpContent, Value, 18, CXFrontend::Paper());
		//스크롤 밖의 문단도 높이를 먼저 계산해 끝으로 이동할 때 밀리지 않게 함
		Label->SetAutoWrapText(false);
		Label->SetWrapTextAt(640.f);
	};
	CXFrontend::Text(WidgetTree, HelpContent, TEXT("1. 개요 및 승리 조건"), 22, CXFrontend::Red());
	HelpText(
		TEXT("목표: 3분 30초 동안 코인을 모아 상대보다 높은 점수 달성.\n")
		TEXT("승리: 종료 시 코인이 더 많은 플레이어. 동점은 공동 승리.\n패배: 종료 시 상대보다 코인이 적은 플레이어.\n\n")
		TEXT("진입 순서: 같은 서버 주소 입력 → 게임 시작 → 두 명 준비 → City에서 10초 카운트다운 → START!"));
	CXFrontend::Gap(WidgetTree, HelpContent);
	CXFrontend::Text(WidgetTree, HelpContent, TEXT("2. 조작 방식 및 조작키"), 22, CXFrontend::Red());
	HelpText(
		TEXT("W: 전진 가속 / S: 브레이크·정지 후 후진\nA·D: 조향 / Space: 핸드브레이크\n")
		TEXT("마우스 상하: 조준 높이 조절 / 좌우: 시점 회전\n좌클릭: 일반 공격 (0.5초 간격)\nF: 지뢰 설치 (3초 간격)\nBackspace: 가까운 안전한 도로로 차량 복구\n")
		TEXT("Enter: 채팅 열기·전송 / M: 경기 메뉴\n\n하단 페달: 기본 흰색 반투명, 가속·브레이크 입력 시 노란색. 가속 페달의 빨간 X는 지뢰 고장 상태를 뜻한다."));
	CXFrontend::Gap(WidgetTree, HelpContent);
	CXFrontend::Text(WidgetTree, HelpContent, TEXT("3. 핵심 규칙 및 팁"), 22, CXFrontend::Red());
	HelpText(
		TEXT("코인: 1개 획득 시 +10. 같은 위치에 8초 후 재생성.\n\n")
		TEXT("일반 공격: 화면 중앙을 조준해 지붕 위에서 발사하며 사거리는 30m. 마우스를 위아래로 움직여 바퀴 높이를 겨냥할 수 있다. 차체 명중은 반경 30cm로 보완하며 바퀴 크리티컬은 정확한 명중에만 적용한다. 벽에 막히며 상대 코인이 5 이상일 때 5를 빼앗는다. 5 미만이면 코인은 유지되고 충격·피격음·명중 효과·바퀴 패널티는 그대로 적용된다.\n\n")
		TEXT("바퀴 명중: 공격자 중앙에 빨간 X·CRITICAL이 0.2초 표시되고 전용 확인음이 재생된다. 연속 명중 시 표시 시간이 갱신된다. 피해자는 오른쪽 최고속도 저하 알림으로 확인한다. 코인 피해량은 일반 명중과 같다.\n\n")
		TEXT("현재 최고속도: CurrentMaxSpeed = max(20, 120 + BonusMaxSpeed) km/h. Coins 아래에서 확인한다. 스피드 아이템은 Bonus에 +10씩 누적되며 시간 제한이 없다. 현재 최고속도에 비례해 가속용 엔진 토크도 증가한다.\n\n")
		TEXT("바퀴 패널티: Bonus -10. 음수일 때만 1초마다 +2씩 0까지 회복한다. 예: 0 → -10 → -8 … → 0. +20에서 +10이 된 경우에는 깎인 10이 자동 회복되지 않는다.\n\n")
		TEXT("지뢰: 밟으면 전진 가속이 2초 동안 0으로 잠기며 가속 페달에 X가 표시된다. 자기 지뢰에도 적용된다. 다시 밟으면 마지막 폭발부터 2초를 센다."));
	CXFrontend::Gap(WidgetTree, HelpContent);
	CXFrontend::Text(WidgetTree, HelpContent, TEXT("4. 제한 및 주의사항"), 22, CXFrontend::Red());
	HelpText(
		TEXT("대기·종료: 가속·조향 0, 핸드브레이크 잠금. 공격·지뢰 설치·차량 복구 사용 불가. START! 이후 조작 가능.\n\n")
		TEXT("차량 복구: 자동 복구 없음. 뒤집히거나 끼면 Backspace를 사용한다. 안전한 복구 공간이 없으면 이동하지 않는다.\n\n")
		TEXT("경기 종료: City 화면에 결과를 표시하고 차량 조작을 잠근다. 10초 후 자동으로 메인 메뉴로 이동한다.\n\n")
		TEXT("접속: 두 플레이어가 같은 전용 서버 주소를 사용해야 한다. 서버를 먼저 실행해야 하며, 현재 다른 방을 자동 배정하는 기능은 없다."));
	CXFrontend::Gap(WidgetTree, HelpPanel);
	UButton* CloseHelpButton = CXFrontend::Button(WidgetTree, HelpPanel, TEXT("닫기"), true);
	CloseHelpButton->OnClicked.AddDynamic(this, &ThisClass::OnHelpClicked);
	HelpPanel->SetVisibility(ESlateVisibility::Collapsed);
}

void UCXMainMenu::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (const UCXGameInstance* Instance = GetGameInstance<UCXGameInstance>())
	{
		const FString& Message = Instance->GetConnectionMessage();
		StatusLabel->SetText(FText::FromString(Message.IsEmpty() ? TEXT("닉네임과 서버 주소를 입력하세요.") : Message));
		JoinButton->SetIsEnabled(Instance->IsConnecting() == false);
		NameInput->SetIsEnabled(Instance->IsConnecting() == false);
		AddressInput->SetIsEnabled(Instance->IsConnecting() == false);
	}
}

void UCXMainMenu::OnJoinClicked()
{
	if (UCXGameInstance* Instance = GetGameInstance<UCXGameInstance>())
	{
		Instance->StartMatchmaking(NameInput->GetText().ToString(), AddressInput->GetText().ToString());
	}
}

void UCXMainMenu::OnNameCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		OnJoinClicked();
	}
}

void UCXMainMenu::OnHelpClicked()
{
	const bool bShowHelp = HelpPanel->GetVisibility() == ESlateVisibility::Collapsed;
	HelpPanel->SetVisibility(bShowHelp ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	MenuPanel->SetVisibility(bShowHelp ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
}

void UCXMainMenu::OnExitClicked()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
