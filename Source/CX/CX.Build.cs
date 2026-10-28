// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CX : ModuleRules
{
	public CX(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory); //모듈 루트를 include 경로에 넣어서 UI/ChatUI.h, Vehicle/CXPawn.h처럼 폴더 경로로 include함

		//UUserWidget과 채팅 입력·출력 위젯 때문에 UMG를 넣고, ETextCommit 같은 Slate 타입 때문에 Slate·SlateCore도 넣음
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "ChaosVehicles", "PhysicsCore", "UMG", "Slate", "SlateCore" });
	}
}
