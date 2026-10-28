// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChatSubsystem.generated.h"

//보낸 사람과 메시지를 한 항목으로 묶음. UPROPERTY가 있는 USTRUCT라 RPC가 배열 안 두 문자열도 직렬화할 수 있음
USTRUCT()
struct FChatData
{
	GENERATED_BODY()

	UPROPERTY()
	FString Sender;
	UPROPERTY()
	FString Message;

	FChatData() {}
	FChatData(const FString& InSender, const FString& InMessage)
		: Sender(InSender), Message(InMessage)
	{
	}
};

//서버에서는 최근 채팅을 보관하고, 클라에서는 UI 생성 전에 받은 기록을 잠깐 보관함. World마다 따로 생김
UCLASS()
class CX_API UChatSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void AddChatMessage(const FString& InSender, const FString& InMessage);
	//접속 기록 RPC로 받은 배열을 로컬 버퍼에 저장. 이 Subsystem 자체가 복제되는 건 아님
	void SetChatLog(const TArray<FChatData>& InChatLog) { ChatLog = InChatLog; }
	void ClearChatLog() { ChatLog.Empty(); }

	//배열 전체를 복사하지 않고 읽을 수 있게 const 참조로 돌려줌
	const TArray<FChatData>& GetChatLog() const { return ChatLog; }

protected:
	TArray<FChatData> ChatLog;
	static inline int32 MaxChatLog = 10; //최근 기록 최대 개수. 튜닝용 UPROPERTY는 따로 안 만듦
};
