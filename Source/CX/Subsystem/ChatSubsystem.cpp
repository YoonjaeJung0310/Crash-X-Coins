// Fill out your copyright notice in the Description page of Project Settings.

#include "Subsystem/ChatSubsystem.h"

void UChatSubsystem::AddChatMessage(const FString& InSender, const FString& InMessage)
{
	//추가 전에 이미 10개면 가장 오래된 항목 하나를 지움. >= 조건이라 추가한 뒤에도 10개 유지
	if (ChatLog.Num() >= MaxChatLog)
	{
		ChatLog.RemoveAt(0);
	}

	ChatLog.Emplace(InSender, InMessage); //임시 FChatData 없이 배열 새 항목을 바로 만듦
}
