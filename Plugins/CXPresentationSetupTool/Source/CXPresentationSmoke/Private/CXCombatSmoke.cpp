#include "CXCombatSmoke.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/ScrollBox.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextBlock.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "Game/CXGameState.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/CXPlayerState.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"

namespace
{
    //명시한 검수 프로세스에서만 요청·피해·입력을 주입함
    struct FCombatObservation
    {
        FString Role,Directory,AttackerName=TEXT("Combat_A"),VictimName=TEXT("Combat_B");
        TSharedRef<FJsonObject> Result=MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> Frames;
        TSet<FString> Done,Captured;
        double PlayingAt=-1,LastFrameAt=-1,LastSavedAt=-1,LastSpamAt=-1;
        double HelpOpenedAt=-1;
        int32 SpamRequests=0;

        void Save()
        {
            Result->SetArrayField(TEXT("frames"),Frames);
            FString Json;
            FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Json));
            FFileHelper::SaveStringToFile(Json,*FPaths::Combine(Directory,Role+TEXT("_combat.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }

        bool Once(const FString& Key,double Elapsed,double At)
        {
            if(Elapsed<At||Done.Contains(Key)) { return false; }
            Done.Add(Key);
            Result->SetNumberField(Key+TEXT("_elapsed"),Elapsed);
            UE_LOG(LogTemp,Display,TEXT("CX_COMBAT_FIXTURE Role=%s Event=%s Elapsed=%.3f"),*Role,*Key,Elapsed);
            return true;
        }

        static UChaosWheeledVehicleMovementComponent* Movement(APawn* Pawn)
        { return Pawn?Pawn->FindComponentByClass<UChaosWheeledVehicleMovementComponent>():nullptr; }

        static bool Broken(APawn* Pawn)
        {
            const FBoolProperty* Property=Pawn?FindFProperty<FBoolProperty>(Pawn->GetClass(),TEXT("bAcceleratorBroken")):nullptr;
            return Property&&Property->GetPropertyValue_InContainer(Pawn);
        }

        static TSharedRef<FJsonObject> PawnState(APawn* Pawn)
        {
            TSharedRef<FJsonObject> State=MakeShared<FJsonObject>();
            if(!Pawn) { return State; }
            State->SetStringField(TEXT("pawn"),Pawn->GetName());
            State->SetStringField(TEXT("player"),Pawn->GetPlayerState()?Pawn->GetPlayerState()->GetPlayerName():TEXT(""));
            State->SetStringField(TEXT("position"),Pawn->GetActorLocation().ToString());
            State->SetBoolField(TEXT("accelerator_broken"),Broken(Pawn));
            if(ACXPlayerState* Player=Pawn->GetPlayerState<ACXPlayerState>())
            {
                State->SetNumberField(TEXT("coins"),Player->GetCoins());
                State->SetNumberField(TEXT("bonus_max_speed"),Player->GetBonusMaxSpeed());
                State->SetNumberField(TEXT("current_max_speed"),Player->GetCurrentMaxSpeed());
                State->SetStringField(TEXT("player_info"),Player->GetPlayerStateInfo());
                if(UChaosWheeledVehicleMovementComponent* Vehicle=Movement(Pawn))
                {
                    State->SetNumberField(TEXT("baseline_engine_setup_torque"),Vehicle->EngineSetup.MaxTorque);
                    State->SetNumberField(TEXT("expected_set_max_engine_torque_argument"),Vehicle->EngineSetup.MaxTorque*Player->GetCurrentMaxSpeed()/120.0f);
                    State->SetNumberField(TEXT("throttle"),Vehicle->GetThrottleInput());
                    State->SetNumberField(TEXT("brake"),Vehicle->GetBrakeInput());
                }
            }
            return State;
        }

        void Place(APawn* Pawn,const FVector& Location)
        {
            struct FParams { FTransform RecoveryTransform; } Params{FTransform(FRotator::ZeroRotator,Location,Pawn->GetActorScale3D())};
            Pawn->ProcessEvent(Pawn->FindFunctionChecked(TEXT("MulticastRecoverVehicle")),&Params);
            Pawn->ForceNetUpdate();
        }

        void ServerActions(UWorld* World,double Elapsed)
        {
            APawn* A=nullptr;APawn* B=nullptr;
            for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
            {
                APawn* Pawn=It->IsValid()?It->Get()->GetPawn():nullptr;
                if(!Pawn||!Pawn->GetPlayerState()) { continue; }
                if(Pawn->GetPlayerState()->GetPlayerName()==AttackerName) { A=Pawn; }
                if(Pawn->GetPlayerState()->GetPlayerName()==VictimName) { B=Pawn; }
            }
            if(!A||!B) { return; }
            if(Once(TEXT("setup"),Elapsed,0))
            {
                Place(A,FVector(-6000,-9500,100));Place(B,FVector(-6000,-3000,100));
                B->GetPlayerState<ACXPlayerState>()->AddCoins(20);
                Result->SetObjectField(TEXT("attacker_baseline"),PawnState(A));
                Result->SetObjectField(TEXT("victim_baseline"),PawnState(B));
            }
            if(Once(TEXT("wheel_point_damage"),Elapsed,7))
            {
                UChaosWheeledVehicleMovementComponent* Vehicle=Movement(B);
                USkeletalMeshComponent* Mesh=B->FindComponentByClass<USkeletalMeshComponent>();
                if(Vehicle&&Mesh&&!Vehicle->WheelSetups.IsEmpty())
                {
                    const FName Bone=Vehicle->WheelSetups[0].BoneName;
                    const FVector Point=Mesh->GetSocketLocation(Bone);
                    FHitResult Hit(B,Mesh,Point,FVector::UpVector);
                    Hit.BoneName=Bone;Hit.bBlockingHit=true;Hit.Location=Hit.ImpactPoint=Point;
                    Result->SetStringField(TEXT("injected_wheel_bone"),Bone.ToString());
                    Result->SetNumberField(TEXT("point_damage_return"),UGameplayStatics::ApplyPointDamage(B,5.f,(B->GetActorLocation()-A->GetActorLocation()).GetSafeNormal(),Hit,A->GetController(),A,nullptr));
                    Result->SetObjectField(TEXT("victim_after_wheel_damage"),PawnState(B));
                }
            }
            if(Once(TEXT("speed_buff_120"),Elapsed,9))
            {
                A->GetPlayerState<ACXPlayerState>()->AddBonusMaxSpeed(120.f);
                A->GetPlayerState()->ForceNetUpdate();
            }
            if(Once(TEXT("after_torque_update"),Elapsed,9.3))
            { Result->SetObjectField(TEXT("attacker_after_buff"),PawnState(A)); }
            if(Once(TEXT("mine_overlap"),Elapsed,11))
            {
                UClass* MineClass=LoadClass<AActor>(nullptr,TEXT("/Game/VehicleTemplate/Blueprints/Item/BP_CXMine.BP_CXMine_C"));
                Result->SetBoolField(TEXT("mine_class_loaded"),MineClass!=nullptr);
                if(MineClass)
                {
                    FActorSpawnParameters Params;Params.Owner=A;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                    AActor* Mine=World->SpawnActor<AActor>(MineClass,B->GetActorLocation(),FRotator::ZeroRotator,Params);
                    if(IsValid(Mine)&&Mine->GetRootComponent()) { Mine->GetRootComponent()->UpdateOverlaps(); }
                    Result->SetObjectField(TEXT("victim_immediate_mine"),PawnState(B));
                }
            }
        }

        void Capture(UWorld* World,const FString& Key)
        {
            if(Captured.Contains(Key)||!GEngine->GameViewport||FParse::Param(FCommandLine::Get(),TEXT("nullrhi"))) { return; }
            Captured.Add(Key);
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory,Role+TEXT("_")+Key+TEXT(".png")),true,false);
            Result->SetNumberField(Key+TEXT("_capture_world_seconds"),World->GetTimeSeconds());
        }

        void ClientActions(UWorld* World,APlayerController* PC,APawn* Pawn,double Elapsed)
        {
            if(!PC||!Pawn) { return; }
            if(Role==TEXT("A")&&Elapsed>=0.5&&Elapsed<6.6&&Elapsed-LastSpamAt>=0.095)
            {
                LastSpamAt=Elapsed;
                struct FAttack { FVector ViewLocation,ViewDirection; } Attack{Pawn->GetActorLocation()+FVector(0,0,400),FVector::UpVector};
                Pawn->ProcessEvent(Pawn->FindFunctionChecked(TEXT("CTSAttack")),&Attack);
                Pawn->ProcessEvent(Pawn->FindFunctionChecked(TEXT("CTSPlaceMine")),nullptr);
                Result->SetNumberField(TEXT("attack_rpc_attempts"),++SpamRequests);
                Result->SetNumberField(TEXT("mine_rpc_attempts"),SpamRequests);
                Result->SetNumberField(TEXT("last_spam_elapsed"),Elapsed);
            }
            if(Role==TEXT("A")&&Elapsed>=14&&Elapsed<17.5)
            {
                UEnhancedInputLocalPlayerSubsystem* Input=ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
                UInputAction* Throttle=LoadObject<UInputAction>(nullptr,TEXT("/Game/VehicleTemplate/Input/Actions/IA_Throttle.IA_Throttle"));
                UInputAction* Brake=LoadObject<UInputAction>(nullptr,TEXT("/Game/VehicleTemplate/Input/Actions/IA_Brake.IA_Brake"));
                if(Input&&Throttle&&Brake)
                {
                    if(Once(TEXT("continuous_throttle_started"),Elapsed,14))
                    {
                        Input->StartContinuousInputInjectionForAction(Throttle,FInputActionValue(1.f),{},{});
                        Result->SetBoolField(TEXT("pedal_input_actions_injected"),true);
                    }
                    if(Once(TEXT("continuous_throttle_stopped"),Elapsed,15.5)) { Input->StopContinuousInputInjectionForAction(Throttle); }
                    if(Once(TEXT("continuous_brake_started"),Elapsed,16)) { Input->StartContinuousInputInjectionForAction(Brake,FInputActionValue(1.f),{},{}); }
                    if(Once(TEXT("continuous_brake_stopped"),Elapsed,17)) { Input->StopContinuousInputInjectionForAction(Brake); }
                }
                if(Elapsed>14.5&&Elapsed<15.5) { Capture(World,TEXT("accelerator_pressed")); }
                if(Elapsed>16.4&&Elapsed<17) { Capture(World,TEXT("brake_pressed")); }
            }
        }

        void Observe(UWorld* World,APawn* Pawn,double Elapsed)
        {
            const bool bFast=Elapsed>=6.7&&Elapsed<13.5;
            if(Elapsed-LastFrameAt<(bFast?0.045:0.2)) { return; }
            LastFrameAt=Elapsed;
            TSharedRef<FJsonObject> Frame=MakeShared<FJsonObject>();
            Frame->SetNumberField(TEXT("world_seconds"),World->GetTimeSeconds());
            Frame->SetNumberField(TEXT("playing_elapsed"),Elapsed);
            Frame->SetObjectField(TEXT("owned_pawn"),PawnState(Pawn));
            if(World->GetNetMode()==NM_DedicatedServer)
            {
                TArray<TSharedPtr<FJsonValue>> Pawns;
                for(TActorIterator<APawn> It(World);It;++It) { if(It->GetPlayerState()) { Pawns.Add(MakeShared<FJsonValueObject>(PawnState(*It))); } }
                Frame->SetArrayField(TEXT("server_pawns"),Pawns);
            }
            TArray<TSharedPtr<FJsonValue>> Texts;
            for(TObjectIterator<UTextBlock> It;It;++It)
            {
                if(It->GetWorld()!=World) { continue; }
                const FString Name=It->GetName();
                if(!Name.StartsWith(TEXT("CX"))&&Name!=TEXT("TextBlock_PlayerStateInfo")) { continue; }
                TSharedRef<FJsonObject> Text=MakeShared<FJsonObject>();
                Text->SetStringField(TEXT("name"),Name);Text->SetStringField(TEXT("text"),It->GetText().ToString());
                Text->SetNumberField(TEXT("visibility"),static_cast<int32>(It->GetVisibility()));
                Text->SetBoolField(TEXT("visible"),It->IsVisible());
                Texts.Add(MakeShared<FJsonValueObject>(Text));
                if(Name==TEXT("CXCriticalMarker")&&It->IsVisible()) { Capture(World,TEXT("critical")); }
                if(Name==TEXT("CXWheelHitInfo")&&It->IsVisible()) { Capture(World,TEXT("wheel_hit")); }
                if(Name==TEXT("CXAcceleratorBroken")&&It->IsVisible()) { Capture(World,TEXT("accelerator_broken")); }
            }
            Frame->SetArrayField(TEXT("hud_texts"),Texts);
            Frames.Add(MakeShared<FJsonValueObject>(Frame));
        }

        bool MenuTick(UWorld* World)
        {
            UUserWidget* Menu=nullptr;
            for(TObjectIterator<UUserWidget> It;It;++It)
            {
                if(It->GetWorld()==World&&It->IsInViewport()&&It->FindFunction(TEXT("OnHelpClicked"))) { Menu=*It;break; }
            }
            if(!Menu||!Menu->WidgetTree) { return true; }
            if(HelpOpenedAt<0)
            {
                Menu->ProcessEvent(Menu->FindFunctionChecked(TEXT("OnHelpClicked")),nullptr);
                HelpOpenedAt=FPlatformTime::Seconds();
                Result->SetStringField(TEXT("menu_widget"),Menu->GetPathName());
                Result->SetBoolField(TEXT("help_callback_invoked"),true);
                Save();
            }
            UScrollBox* Scroll=nullptr;
            TArray<UWidget*> Widgets;
            Menu->WidgetTree->GetAllWidgets(Widgets);
            for(UWidget* Widget:Widgets) { if(UScrollBox* Candidate=Cast<UScrollBox>(Widget)) { Scroll=Candidate;break; } }
            if(!Scroll) { return true; }
            const double Elapsed=FPlatformTime::Seconds()-HelpOpenedAt;
            auto SaveMenuObservation=[&](const FString& Key)
            {
                TSharedRef<FJsonObject> Frame=MakeShared<FJsonObject>();
                Frame->SetNumberField(TEXT("seconds_since_help_opened"),Elapsed);
                Frame->SetNumberField(TEXT("scroll_offset"),Scroll->GetScrollOffset());
                Frame->SetNumberField(TEXT("scroll_offset_end"),Scroll->GetScrollOffsetOfEnd());
                Frame->SetNumberField(TEXT("view_fraction"),Scroll->GetViewFraction());
                Frame->SetNumberField(TEXT("scroll_local_width"),Scroll->GetCachedGeometry().GetLocalSize().X);
                Frame->SetNumberField(TEXT("scroll_local_height"),Scroll->GetCachedGeometry().GetLocalSize().Y);
                Frame->SetBoolField(TEXT("scroll_visible"),Scroll->IsVisible());
                TArray<TSharedPtr<FJsonValue>> Texts;
                for(UWidget* Widget:Widgets)
                {
                    if(UTextBlock* Text=Cast<UTextBlock>(Widget))
                    {
                        TSharedRef<FJsonObject> Item=MakeShared<FJsonObject>();
                        Item->SetStringField(TEXT("text"),Text->GetText().ToString());
                        Item->SetNumberField(TEXT("visibility"),static_cast<int32>(Text->GetVisibility()));
                        Texts.Add(MakeShared<FJsonValueObject>(Item));
                    }
                }
                Frame->SetArrayField(TEXT("menu_texts"),Texts);
                Result->SetObjectField(Key,Frame);
                Capture(World,Key);
                Save();
            };
            if(Once(TEXT("help_top_observation"),Elapsed,2)) { SaveMenuObservation(TEXT("help_top")); }
            if(Once(TEXT("help_scroll_to_end"),Elapsed,4)) { Scroll->ScrollToEnd(); }
            if(Once(TEXT("help_bottom_observation"),Elapsed,4.5))
            {
                SaveMenuObservation(TEXT("help_bottom"));
            }
            if(Elapsed>=5&&Done.Contains(TEXT("help_bottom_observation")))
            {
                const bool bTopSaved=IFileManager::Get().FileExists(*FPaths::Combine(Directory,Role+TEXT("_help_top.png")));
                const bool bBottomSaved=IFileManager::Get().FileExists(*FPaths::Combine(Directory,Role+TEXT("_help_bottom.png")));
                if((!bTopSaved||!bBottomSaved)&&Elapsed<8) { return true; }
                Result->SetBoolField(TEXT("help_top_png_exists"),bTopSaved);
                Result->SetBoolField(TEXT("help_bottom_png_exists"),bBottomSaved);
                Result->SetBoolField(TEXT("fixture_actions_complete"),bTopSaved&&bBottomSaved);
                Save();
                UE_LOG(LogTemp,Display,TEXT("CX_COMBAT_FIXTURE_COMPLETE Role=Menu WorldTime=%.3f"),World->GetTimeSeconds());
                return false;
            }
            return true;
        }

        bool Tick(float)
        {
            UWorld* World=nullptr;
            if(GEngine) for(const FWorldContext& Context:GEngine->GetWorldContexts())
            { if(Context.World()&&Context.World()->IsGameWorld()&&Context.World()->HasBegunPlay()) { World=Context.World();break; } }
            if(!World) { return true; }
            if(Role==TEXT("Menu")) { return MenuTick(World); }
            const ACXGameState* State=World->GetGameState<ACXGameState>();
            if(!State||State->GetCurrentGameState()!=EGameState::Playing) { return true; }
            if(PlayingAt<0) { PlayingAt=World->GetTimeSeconds();Result->SetNumberField(TEXT("playing_begin_world_seconds"),PlayingAt); }
            const double Elapsed=World->GetTimeSeconds()-PlayingAt;
            APlayerController* PC=World->GetFirstPlayerController();APawn* Pawn=PC?PC->GetPawn():nullptr;
            if(World->GetNetMode()==NM_DedicatedServer) { ServerActions(World,Elapsed); }
            else { ClientActions(World,PC,Pawn,Elapsed); }
            Observe(World,Pawn,Elapsed);
            if(Elapsed-LastSavedAt>=0.5) { LastSavedAt=Elapsed;Save(); }
            if(Elapsed>=18)
            {
                Result->SetBoolField(TEXT("fixture_actions_complete"),true);Save();
                UE_LOG(LogTemp,Display,TEXT("CX_COMBAT_FIXTURE_COMPLETE Role=%s WorldTime=%.3f"),*Role,World->GetTimeSeconds());
                return false;
            }
            return true;
        }
    };
}

FTSTicker::FDelegateHandle StartCXCombatSmoke()
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("CXCombatSmoke"))) { return {}; }
    const TSharedRef<FCombatObservation> Observation=MakeShared<FCombatObservation>();
    if(!FParse::Value(FCommandLine::Get(),TEXT("CXCombatRole="),Observation->Role)
        ||!FParse::Value(FCommandLine::Get(),TEXT("CXCombatProof="),Observation->Directory)) { return {}; }
    FParse::Value(FCommandLine::Get(),TEXT("CXCombatAttackerName="),Observation->AttackerName);
    FParse::Value(FCommandLine::Get(),TEXT("CXCombatVictimName="),Observation->VictimName);
    IFileManager::Get().MakeDirectory(*Observation->Directory,true);
    Observation->Result->SetStringField(TEXT("role"),Observation->Role);
    Observation->Result->SetStringField(TEXT("scope"),Observation->Role==TEXT("Menu")
        ?TEXT("Opt-in menu Help callback invocation, scroll-to-end and actual game-viewport screenshot requests; no human menu clicking.")
        :TEXT("Opt-in RPC spam, point-damage bone, +120 speed buff, actual mine overlap and input-action injections. HUD/runtime observations; no human input/audio listening or direct physics MaxTorque read."));
    Observation->Save();
    return FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Observation](float Delta){return Observation->Tick(Delta);}),0.05f);
}
