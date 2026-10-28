#include "CXAudioSmoke.h"
#include "ChaosVehicleMovementComponent.h"
#include "Vehicle/CXVehicleMovementComponent.h"
#include "Components/AudioComponent.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/PrimitiveComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "Game/CXGameInstance.h"
#include "Game/CXGameState.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundWave.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"

namespace
{
    //실행 인자를 준 검사용 프로세스에서만 조작함. 아래 시간·속도는 검사용 주입값임
    struct FAudioObservation
    {
        FString Role,Directory,LastPhase,PendingCapture;
        TArray<TSharedPtr<FJsonValue>> Frames;
        TSet<FString> Done,Captured;
        TWeakObjectPtr<APawn> Victim;
        TSharedRef<FJsonObject> Result=MakeShared<FJsonObject>();
        double StartedAt=FPlatformTime::Seconds(),LastSavedAt=-10,PendingAt=0,MenuOpenedAt=-1;
        float JoinDelay=-1;
        int32 RoundSeconds=0;
        bool bPlayed=false,bServerAudioReported=false,bPassive=false,bJoinRequested=false,bDriveOnly=false;
        double DriveStartedAt=-1;
        FString DriveScenario;
        float DriveTargetKmh=0;

        void Save()
        {
            Result->SetArrayField(TEXT("frames"),Frames);
            FString Json;
            FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Json));
            FFileHelper::SaveStringToFile(Json,*FPaths::Combine(Directory,Role+TEXT("_audio.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }

        TSharedRef<FJsonObject> PawnState(APawn* Pawn)
        {
            TSharedRef<FJsonObject> Frame=MakeShared<FJsonObject>();
            if(!Pawn) { return Frame; }
            Frame->SetStringField(TEXT("pawn"),Pawn->GetPathName());
            Frame->SetStringField(TEXT("name"),Pawn->GetPlayerState()?Pawn->GetPlayerState()->GetPlayerName():TEXT(""));
            Frame->SetStringField(TEXT("position"),Pawn->GetActorLocation().ToString());
            Frame->SetNumberField(TEXT("up_z"),Pawn->GetActorUpVector().Z);
            Frame->SetStringField(TEXT("rotation"),Pawn->GetActorRotation().ToString());
            Frame->SetStringField(TEXT("velocity"),Pawn->GetVelocity().ToString());
            if(UPrimitiveComponent* Body=Cast<UPrimitiveComponent>(Pawn->GetRootComponent()))
            {
                Frame->SetStringField(TEXT("angular_velocity_radians"),Body->GetPhysicsAngularVelocityInRadians().ToString());
                Frame->SetBoolField(TEXT("simulating_physics"),Body->IsSimulatingPhysics());
            }
            if(FBoolProperty* Broken=FindFProperty<FBoolProperty>(Pawn->GetClass(),TEXT("bAcceleratorBroken")))
            { Frame->SetBoolField(TEXT("accelerator_broken"),Broken->GetPropertyValue_InContainer(Pawn)); }
            if(UChaosVehicleMovementComponent* Movement=Pawn->FindComponentByClass<UChaosVehicleMovementComponent>())
            {
                Frame->SetNumberField(TEXT("throttle"),Movement->GetThrottleInput());
                Frame->SetNumberField(TEXT("brake"),Movement->GetBrakeInput());
                Frame->SetBoolField(TEXT("handbrake"),Movement->GetHandbrakeInput());
            }
            return Frame;
        }

        void Place(APawn* Pawn,const FTransform& Transform)
        {
            struct FParams { FTransform RecoveryTransform; } Params{Transform};
            Pawn->ProcessEvent(Pawn->FindFunctionChecked(TEXT("MulticastRecoverVehicle")),&Params);
            Pawn->ForceNetUpdate();
        }

        bool Due(const FString& Event,int32 Elapsed,int32 At)
        {
            if(Elapsed<At||Done.Contains(Event)) { return false; }
            Done.Add(Event);
            Result->SetNumberField(Event+TEXT("_elapsed"),Elapsed);
            UE_LOG(LogTemp,Display,TEXT("CX_AUDIO_FIXTURE Role=%s Event=%s Elapsed=%d"),*Role,*Event,Elapsed);
            return true;
        }

        void ServerActions(UWorld* World,int32 Elapsed)
        {
            APawn* A=Victim.Get();APawn* B=nullptr;
            for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
            {
                APawn* Pawn=It->IsValid()?It->Get()->GetPawn():nullptr;
                if(!Pawn||!Pawn->GetPlayerState()) { continue; }
                if(Pawn->GetPlayerState()->GetPlayerName()==TEXT("윤재")) { A=Pawn;Victim=Pawn; }
                else { B=Pawn; }
            }
            if(!A||!B) { return; }
            if(Due(TEXT("damage"),Elapsed,3))
            {
                Result->SetObjectField(TEXT("before_damage"),PawnState(A));
                FHitResult Hit;Hit.Location=Hit.ImpactPoint=A->GetActorLocation();
                UGameplayStatics::ApplyPointDamage(A,5.f,(A->GetActorLocation()-B->GetActorLocation()).GetSafeNormal(),Hit,B->GetController(),B,nullptr);
            }
            if(Due(TEXT("after_damage"),Elapsed,4)) { Result->SetObjectField(TEXT("after_damage"),PawnState(A)); }
            if(Due(TEXT("mine"),Elapsed,8))
            {
                Result->SetObjectField(TEXT("before_mine"),PawnState(A));
                UClass* MineClass=LoadClass<AActor>(nullptr,TEXT("/Game/VehicleTemplate/Blueprints/Item/BP_CXMine.BP_CXMine_C"));
                Result->SetBoolField(TEXT("mine_class_loaded"),MineClass!=nullptr);
                if(MineClass)
                {
                    FActorSpawnParameters Params;Params.Owner=B;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                    AActor* Mine=World->SpawnActor<AActor>(MineClass,A->GetActorLocation(),FRotator::ZeroRotator,Params);
                    Result->SetStringField(TEXT("spawned_mine"),GetNameSafe(Mine));
                    if(IsValid(Mine)&&Mine->GetRootComponent()) { Mine->GetRootComponent()->UpdateOverlaps(); }
                }
            }
            if(Due(TEXT("after_mine"),Elapsed,9)) { Result->SetObjectField(TEXT("after_mine"),PawnState(A)); }
            if(Due(TEXT("overturn"),Elapsed,14))
            {
                Result->SetStringField(TEXT("recovery_original_pawn"),A->GetPathName());
                Place(A,FTransform(FRotator(0,0,180),FVector(-6000,-9500,180),A->GetActorScale3D()));
            }
            if(Due(TEXT("before_manual_reset"),Elapsed,20))
            { Result->SetObjectField(TEXT("before_manual_reset"),PawnState(A)); }
            if(Due(TEXT("after_manual_reset"),Elapsed,23))
            { Result->SetObjectField(TEXT("after_manual_reset"),PawnState(A)); }
        }

        void ClientActions(APlayerController* PC,APawn* Pawn,int32 Elapsed)
        {
            if(Role!=TEXT("A")||!PC||!Pawn) { return; }
            if(Due(TEXT("manual_reset_rpc"),Elapsed,21))
            { Pawn->ProcessEvent(Pawn->FindFunctionChecked(TEXT("CTSResetVehicle")),nullptr); }
            UChaosVehicleMovementComponent* Movement=Pawn->FindComponentByClass<UChaosVehicleMovementComponent>();
            UPrimitiveComponent* Body=Cast<UPrimitiveComponent>(Pawn->GetRootComponent());
            if(Movement&&Body&&Elapsed>=27&&Elapsed<=30)
            {
                Movement->SetThrottleInput(1.f);Movement->SetBrakeInput(0.f);
                Body->SetPhysicsLinearVelocity(Pawn->GetActorForwardVector()*3000.f);
            }
            if(Movement&&Body&&Elapsed>=32&&Elapsed<=35)
            {
                Movement->SetThrottleInput(0.f);Movement->SetBrakeInput(1.f);
                Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
            }
            if(Movement&&Due(TEXT("release_pedals"),Elapsed,36))
            { Movement->SetThrottleInput(0.f);Movement->SetBrakeInput(0.f);Movement->SetHandbrakeInput(false); }
            if(Due(TEXT("chat_rpc"),Elapsed,38))
            {
                struct FParams { FString Message; } Params{TEXT("ㅎㅇㅎㅇ")};
                PC->ProcessEvent(PC->FindFunctionChecked(TEXT("CTSChatMessage")),&Params);
            }
        }

        bool DriveActions(UWorld* World,ACXGameState* State,APlayerController* PC,APawn* Pawn)
        {
            UEnhancedInputLocalPlayerSubsystem* Input=PC&&PC->GetLocalPlayer()
                ?PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>():nullptr;
            UPrimitiveComponent* Body=Pawn?Cast<UPrimitiveComponent>(Pawn->GetRootComponent()):nullptr;
            if(World->GetNetMode()!=NM_Standalone||!World->GetMapName().Contains(TEXT("L_CXCityArena"))
                ||!State||!State->HasAuthority()||!PC||!PC->IsLocalController()||!Pawn||!Pawn->GetPlayerState()
                ||!Pawn->InputComponent||!Input||!Body) { return false; }
            UInputAction* Throttle=LoadObject<UInputAction>(nullptr,TEXT("/Game/VehicleTemplate/Input/Actions/IA_Throttle.IA_Throttle"));
            UInputAction* Brake=LoadObject<UInputAction>(nullptr,TEXT("/Game/VehicleTemplate/Input/Actions/IA_Brake.IA_Brake"));
            UInputAction* Handbrake=LoadObject<UInputAction>(nullptr,TEXT("/Game/VehicleTemplate/Input/Actions/IA_Handbrake.IA_Handbrake"));
            if(!Throttle||!Brake||!Handbrake) { return false; }
            if(DriveStartedAt<0)
            {
                State->SetCountdownActive(false);State->SetRemainingTime(210);State->SetGameState(EGameState::Playing);
                //충돌 없는 공중에서 수평 속도만 주입함. 실제 주행 가속도 측정은 아님
                Pawn->SetActorLocationAndRotation(FVector(-6000,-9500,10000),FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics);
                DriveStartedAt=FPlatformTime::Seconds();
                Result->SetBoolField(TEXT("drive_playing_state_injected"),true);
            }
            const double Elapsed=FPlatformTime::Seconds()-DriveStartedAt;
            const FString Scenario=Elapsed<1?TEXT("settle"):Elapsed<3?TEXT("throttle_60"):
                Elapsed<5?TEXT("throttle_110"):Elapsed<7?TEXT("forward_brake_deceleration"):
                Elapsed<9?TEXT("reverse_brake_input"):Elapsed<11?TEXT("no_input_deceleration"):
                Elapsed<13?TEXT("handbrake_deceleration"):Elapsed<15?TEXT("stationary_brake_input"):
                Elapsed<17?TEXT("reverse_throttle_deceleration"):TEXT("complete");
            if(DriveScenario!=Scenario)
            {
                Input->StopContinuousInputInjectionForAction(Throttle);Input->StopContinuousInputInjectionForAction(Brake);
                Input->StopContinuousInputInjectionForAction(Handbrake);
                if((Elapsed>=1&&Elapsed<5)||(Elapsed>=15&&Elapsed<17))
                { Input->StartContinuousInputInjectionForAction(Throttle,FInputActionValue(1.f),{},{}); }
                else if((Elapsed>=5&&Elapsed<9)||(Elapsed>=13&&Elapsed<15))
                { Input->StartContinuousInputInjectionForAction(Brake,FInputActionValue(1.f),{},{}); }
                else if(Elapsed>=11&&Elapsed<13) { Input->StartContinuousInputInjectionForAction(Handbrake,FInputActionValue(true),{},{}); }
                DriveScenario=Scenario;
                UE_LOG(LogTemp,Display,TEXT("CX_DRIVE_AUDIO Scenario=%s Elapsed=%.3f"),*Scenario,Elapsed);
            }
            DriveTargetKmh=Elapsed<1?0:Elapsed<3?60:Elapsed<5?110:
                Elapsed<7?80-20*(Elapsed-5):Elapsed<9?-(30+10*(Elapsed-7)):
                Elapsed<11?80-20*(Elapsed-9):Elapsed<13?50-10*(Elapsed-11):
                Elapsed<15?0:Elapsed<17?-(80-20*(Elapsed-15)):0;
            Body->SetPhysicsLinearVelocity(Pawn->GetActorForwardVector()*DriveTargetKmh/.036f);
            Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
            return true;
        }

        bool Tick(float)
        {
            UWorld* World=nullptr;
            if(GEngine) for(const FWorldContext& Context:GEngine->GetWorldContexts())
            { if(Context.World()&&Context.World()->IsGameWorld()&&Context.World()->HasBegunPlay()) { World=Context.World();break; } }
            if(!World) { return true; }
            ACXGameState* State=World->GetGameState<ACXGameState>();
            APlayerController* PC=World->GetFirstPlayerController();APawn* Pawn=PC?PC->GetPawn():nullptr;
            if(bDriveOnly&&!DriveActions(World,State,PC,Pawn)) { return true; }
            const bool bDriveComplete=bDriveOnly&&DriveScenario==TEXT("complete");
            FString Phase=TEXT("menu");int32 Time=-1,Elapsed=-1;
            if(State)
            {
                Time=State->GetRemainingTime();
                Phase=State->GetCurrentGameState()==EGameState::Playing?TEXT("playing"):
                    State->GetCurrentGameState()==EGameState::Finished?TEXT("finished"):
                    State->IsCountdownActive()?TEXT("countdown"):TEXT("matching");
            }
            if(Phase==TEXT("playing"))
            { bPlayed=true;if(!RoundSeconds) { RoundSeconds=Time;Result->SetNumberField(TEXT("initial_playing_remaining"),Time); }Elapsed=RoundSeconds-Time; }
            if(Phase==TEXT("menu")&&bPlayed) { Phase=TEXT("returned_menu"); }
            //접속 지연 인자를 따로 준 경우에만 메뉴에서 최소 1초 머문 뒤 참가함
            if(JoinDelay>=0&&Phase==TEXT("menu")&&PC&&PC->IsLocalController())
            {
                if(MenuOpenedAt<0) { MenuOpenedAt=FPlatformTime::Seconds(); }
                if(!bJoinRequested&&FPlatformTime::Seconds()-MenuOpenedAt>=FMath::Max(1.f,JoinDelay))
                {
                    if(UCXGameInstance* Instance=World->GetGameInstance<UCXGameInstance>())
                    {
                        bJoinRequested=true;
                        FString Name=Role==TEXT("A")?TEXT("윤재"):TEXT("DriverB");
                        FParse::Value(FCommandLine::Get(),TEXT("CXPlayerName="),Name);
                        Result->SetBoolField(TEXT("delayed_join_accepted"),Instance->StartMatchmaking(Name));
                        Result->SetNumberField(TEXT("delayed_join_menu_seconds"),FPlatformTime::Seconds()-MenuOpenedAt);
                    }
                }
            }
            if(!bPassive&&Elapsed>=0)
            {
                if(World->GetNetMode()==NM_DedicatedServer) { ServerActions(World,Elapsed); }
                else { ClientActions(PC,Pawn,Elapsed); }
            }
            if(!bPassive&&Role==TEXT("A")&&!FParse::Param(FCommandLine::Get(),TEXT("nullrhi"))&&GEngine->GameViewport)
            {
                FString Capture=Phase;
                if(Phase==TEXT("playing")&&Elapsed>=39) { Capture=TEXT("chat"); }
                if(!Captured.Contains(Capture)) { Captured.Add(Capture);PendingCapture=Capture;PendingAt=FPlatformTime::Seconds()+0.35; }
                if(!PendingCapture.IsEmpty()&&FPlatformTime::Seconds()>=PendingAt)
                { FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory,Role+TEXT("_")+PendingCapture+TEXT(".png")),true,false);PendingCapture.Empty(); }
            }
            if(bDriveOnly)
            {
                if(!bDriveComplete&&FPlatformTime::Seconds()-LastSavedAt<.1) { return true; }
            }
            else if(FPlatformTime::Seconds()-LastSavedAt<1.0&&Phase==LastPhase&&!(Elapsed>=3&&Elapsed<=11)) { return true; }
            LastSavedAt=FPlatformTime::Seconds();LastPhase=Phase;
            TSharedRef<FJsonObject> Frame=MakeShared<FJsonObject>();
            Frame->SetStringField(TEXT("phase"),Phase);Frame->SetNumberField(TEXT("remaining"),Time);
            Frame->SetNumberField(TEXT("playing_elapsed"),Elapsed);Frame->SetNumberField(TEXT("wall_seconds"),FPlatformTime::Seconds()-StartedAt);
            Frame->SetNumberField(TEXT("world_seconds"),World->GetTimeSeconds());Frame->SetStringField(TEXT("map"),World->GetMapName());
            Frame->SetObjectField(TEXT("owned_pawn"),PawnState(Pawn));
            if(bDriveOnly)
            {
                Frame->SetStringField(TEXT("drive_scenario"),DriveScenario);
                Frame->SetNumberField(TEXT("drive_elapsed"),FPlatformTime::Seconds()-DriveStartedAt);
                Frame->SetNumberField(TEXT("injected_target_speed_kmh"),DriveTargetKmh);
                Frame->SetNumberField(TEXT("actual_speed_kmh"),Pawn->GetVelocity().Size2D()*.036f);
                if(UChaosVehicleMovementComponent* Movement=Pawn->FindComponentByClass<UChaosVehicleMovementComponent>())
                {
                    Frame->SetNumberField(TEXT("raw_brake"),Movement->GetBrakeInput());
                    Frame->SetNumberField(TEXT("raw_throttle"),Movement->GetThrottleInput());
                    Frame->SetBoolField(TEXT("reverse_as_brake"),Movement->bReverseAsBrake);
                    Frame->SetBoolField(TEXT("throttle_as_brake"),Movement->bThrottleAsBrake);
                    if(FFloatProperty* Brake=FindFProperty<FFloatProperty>(Movement->GetClass(),TEXT("BrakeInput")))
                    { Frame->SetNumberField(TEXT("processed_brake"),Brake->GetPropertyValue_InContainer(Movement)); }
                    if(UCXVehicleMovementComponent* CXMovement=Cast<UCXVehicleMovementComponent>(Movement))
                    { Frame->SetBoolField(TEXT("has_braking_input"),CXMovement->HasBrakingInput()); }
                }
            }
            TArray<TSharedPtr<FJsonValue>> Pawns,Audio;
            for(TActorIterator<APawn> It(World);It;++It) { Pawns.Add(MakeShared<FJsonValueObject>(PawnState(*It))); }
            //맵에 등록되지 않는 전환 유지용 메뉴 음악은 현재 GameInstance 참조만 별도로 포함함
            UAudioComponent* MenuMusic=nullptr;
            if(UGameInstance* Instance=World->GetGameInstance())
            {
                if(FObjectPropertyBase* Property=FindFProperty<FObjectPropertyBase>(Instance->GetClass(),TEXT("MenuMusic")))
                { MenuMusic=Cast<UAudioComponent>(Property->GetObjectPropertyValue_InContainer(Instance)); }
            }
            for(TObjectIterator<UAudioComponent> It;It;++It)
            {
                if(*It!=MenuMusic&&(It->GetWorld()!=World||!It->IsRegistered())) { continue; }
                TSharedRef<FJsonObject> Sound=MakeShared<FJsonObject>();
                Sound->SetStringField(TEXT("component_path"),It->GetPathName());
                Sound->SetNumberField(TEXT("unique_id"),It->GetUniqueID());
                Sound->SetStringField(TEXT("sound"),GetNameSafe(It->Sound));Sound->SetStringField(TEXT("owner"),GetPathNameSafe(It->GetOwner()));
                Sound->SetBoolField(TEXT("is_playing"),It->IsPlaying());
                Sound->SetBoolField(TEXT("is_menu_music"),*It==MenuMusic);
                Sound->SetBoolField(TEXT("registered"),It->IsRegistered());
                Sound->SetStringField(TEXT("world"),GetPathNameSafe(It->GetWorld()));
                const USoundWave* Wave=Cast<USoundWave>(It->Sound);Sound->SetBoolField(TEXT("looping"),Wave&&Wave->bLooping);
                Audio.Add(MakeShared<FJsonValueObject>(Sound));
            }
            Frame->SetArrayField(TEXT("pawns"),Pawns);Frame->SetArrayField(TEXT("audio_components"),Audio);
            for(TObjectIterator<UMultiLineEditableTextBox> It;It;++It)
            { if(It->GetWorld()==World&&It->GetName()==TEXT("MultilineEditableTextBox_ChatLog")) { Frame->SetStringField(TEXT("chat_text"),It->GetText().ToString()); } }
            if(World->GetNetMode()==NM_DedicatedServer&&Audio.Num()>0&&!bServerAudioReported)
            { UE_LOG(LogTemp,Warning,TEXT("CX_AUDIO_COUNTEREXAMPLE DedicatedServerAudioComponents=%d"),Audio.Num());bServerAudioReported=true; }
            if(bDriveComplete) { Result->SetBoolField(TEXT("fixture_actions_complete"),true); }
            Frames.Add(MakeShared<FJsonValueObject>(Frame));Save();return !bDriveComplete;
        }
    };
}

FTSTicker::FDelegateHandle StartCXAudioSmoke()
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("CXAudioSmoke"))&&!FParse::Param(FCommandLine::Get(),TEXT("CXDriveAudioOnly"))) { return {}; }
    const TSharedRef<FAudioObservation> Observation=MakeShared<FAudioObservation>();
    if(!FParse::Value(FCommandLine::Get(),TEXT("CXAudioRole="),Observation->Role)
        ||!FParse::Value(FCommandLine::Get(),TEXT("CXAudioProof="),Observation->Directory)) { return {}; }
    Observation->bDriveOnly=FParse::Param(FCommandLine::Get(),TEXT("CXDriveAudioOnly"));
    Observation->bPassive=Observation->bDriveOnly||FParse::Param(FCommandLine::Get(),TEXT("CXAudioPassive"));
    FParse::Value(FCommandLine::Get(),TEXT("CXAudioJoinDelay="),Observation->JoinDelay);
    IFileManager::Get().MakeDirectory(*Observation->Directory,true);
    Observation->Result->SetStringField(TEXT("role"),Observation->Role);
    Observation->Result->SetStringField(TEXT("scope"),Observation->bDriveOnly
        ?TEXT("Opt-in standalone City driving-audio fixture. Playing state, continuous Enhanced Input and per-frame isolated airborne horizontal velocities are injected. Component playback observations only; no claim of real driving, measured acceleration or human audio listening.")
        :Observation->bPassive
        ?TEXT("Passive runtime audio observations; optional explicit CXAudioJoinDelay only. No claim of human keyboard or audio listening.")
        :TEXT("Opt-in injected inputs/RPCs, runtime component/physics observations; no claim of human keyboard or audio listening."));
    Observation->Result->SetBoolField(TEXT("passive"),Observation->bPassive);
    Observation->Result->SetBoolField(TEXT("drive_audio_only"),Observation->bDriveOnly);
    Observation->Result->SetNumberField(TEXT("join_delay_argument"),Observation->JoinDelay);
    Observation->Result->SetBoolField(TEXT("nosound_argument"),FParse::Param(FCommandLine::Get(),TEXT("nosound")));
    return FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Observation](float Delta){return Observation->Tick(Delta);}),Observation->bDriveOnly?0.f:0.1f);
}
