#include "CXFlowSmoke.h"
#include "ChaosVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/LineBatchComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/CXGameState.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/CXPlayerState.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "Vehicle/CXPawn.h"
#include "Vehicle/CXRoadRecoveryArea.h"

namespace
{
    struct FFlowObservation
    {
        FString Role,Directory,Previous;
        TArray<TSharedPtr<FJsonValue>> Frames;
        TSet<FString> Captured;
        TWeakObjectPtr<ACXPawn> TestedPawn;
        TWeakObjectPtr<ACXPlayerState> OriginalState;
        int32 OriginalCoins=0,RecoveryStage=0;
        bool bWasPlaying=false,bShotSent=false,bLineCaptured=false,bAimSetup=false;
        double RecoveryAt=0,LockInjectedAt=0,PendingCaptureAt=0;
        FString LockStage,PendingCapture;
        TSharedRef<FJsonObject> Result=MakeShared<FJsonObject>();

        void Save()
        {
            Result->SetArrayField(TEXT("frames"),Frames);
            FString Json;
            const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json);
            FJsonSerializer::Serialize(Result,Writer);
            FFileHelper::SaveStringToFile(Json,*FPaths::Combine(Directory,Role+TEXT("_flow.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        }

        void Place(ACXPawn* Pawn,const FVector& Location,const FRotator& Rotation)
        {
            struct FParams { FTransform RecoveryTransform; } Params{FTransform(Rotation,Location,Pawn->GetActorScale3D())};
            Pawn->ProcessEvent(Pawn->FindFunctionChecked(TEXT("MulticastRecoverVehicle")),&Params);
            Pawn->ForceNetUpdate();
        }

        bool Tick(float)
        {
            UWorld* World=nullptr;
            if(GEngine) for(const FWorldContext& Context:GEngine->GetWorldContexts())
            {
                if(Context.World()&&Context.World()->IsGameWorld()&&Context.World()->HasBegunPlay()) { World=Context.World();break; }
            }
            if(!World) { return true; }
            ACXGameState* State=World->GetGameState<ACXGameState>();
            FString Phase=TEXT("menu");
            int32 Time=-1;
            if(State)
            {
                Time=State->GetRemainingTime();
                Phase=State->GetCurrentGameState()==EGameState::Playing?TEXT("playing"):
                    State->GetCurrentGameState()==EGameState::Finished?TEXT("finished"):
                    State->IsCountdownActive()?TEXT("countdown"):TEXT("matching");
            }
            if(Phase==TEXT("playing")) { bWasPlaying=true; }
            if(Phase==TEXT("menu")&&bWasPlaying) { Phase=TEXT("returned_menu");Result->SetBoolField(TEXT("returned_to_menu"),true); }
            const FString Key=Phase+FString::FromInt(Time);
            ACXPawn* FirstPawn=nullptr;
            APlayerController* FirstPC=World->GetFirstPlayerController();
            if(FirstPC) { FirstPawn=Cast<ACXPawn>(FirstPC->GetPawn()); }
            if(Key!=Previous)
            {
                Previous=Key;
                TSharedRef<FJsonObject> Frame=MakeShared<FJsonObject>();
                Frame->SetStringField(TEXT("phase"),Phase);
                Frame->SetNumberField(TEXT("remaining"),Time);
                Frame->SetNumberField(TEXT("world_seconds"),World->GetTimeSeconds());
                Frame->SetStringField(TEXT("map"),World->GetMapName());
                if(FirstPawn)
                {
                    Frame->SetStringField(TEXT("pawn"),FirstPawn->GetName());
                    Frame->SetStringField(TEXT("position"),FirstPawn->GetActorLocation().ToString());
                    Frame->SetNumberField(TEXT("up_z"),FirstPawn->GetActorUpVector().Z);
                    Frame->SetStringField(TEXT("movement_class"),FirstPawn->GetVehicleMovementComponent()->GetClass()->GetName());
                }
                Frames.Add(MakeShared<FJsonValueObject>(Frame));
                Save();
            }
            if(World->GetNetMode()!=NM_DedicatedServer&&GEngine->GameViewport)
            {
                if(Role==TEXT("B")&&FirstPawn&&FirstPC&&Phase==TEXT("playing")&&Time<=170&&!bShotSent)
                {
                    struct FAttack { FVector ViewLocation;FVector ViewDirection; } Attack;
                    FRotator ViewRotation;
                    FirstPC->GetPlayerViewPoint(Attack.ViewLocation,ViewRotation);
                    Attack.ViewDirection=ViewRotation.Vector();
                    UInputAction* Action=LoadObject<UInputAction>(nullptr,TEXT("/Game/VehicleTemplate/Input/Actions/IA_Attack.IA_Attack"));
                    UEnhancedInputLocalPlayerSubsystem* Input=ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(FirstPC->GetLocalPlayer());
                    if(Input&&Action) { Input->InjectInputForAction(Action,FInputActionValue(true),{},{}); }
                    Result->SetStringField(TEXT("camera_view_location"),Attack.ViewLocation.ToString());
                    Result->SetStringField(TEXT("camera_view_rotation"),ViewRotation.ToString());
                    Result->SetBoolField(TEXT("owned_client_attack_action_injected"),Input&&Action);
                    bShotSent=true;
                    Save();
                }
                if(bShotSent&&!bLineCaptured)
                {
                    if(ULineBatchComponent* Batch=World->GetLineBatcher(UWorld::ELineBatcherType::WorldPersistent))
                    {
                        for(const FBatchedLine& Line:Batch->BatchedLines) if(Line.Thickness>=4.9f)
                        {
                            Result->SetBoolField(TEXT("actual_attack_line_received"),true);
                            Result->SetStringField(TEXT("actual_line_start"),Line.Start.ToString());
                            Result->SetStringField(TEXT("actual_line_end"),Line.End.ToString());
                            Result->SetNumberField(TEXT("actual_line_remaining_lifetime"),Line.RemainingLifeTime);
                            bLineCaptured=true;
                            if(FParse::Param(FCommandLine::Get(),TEXT("CXFlowCapture"))) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory,Role+TEXT("_roof_aim.png")),true,false); }
                            Save();break;
                        }
                    }
                }
                if(!FParse::Param(FCommandLine::Get(),TEXT("CXFlowCapture"))) { return true; }
                if(!PendingCapture.IsEmpty()&&FPlatformTime::Seconds()>PendingCaptureAt)
                {
                    FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory,Role+TEXT("_")+PendingCapture+TEXT(".png")),true,false);
                    PendingCapture.Empty();
                }
                if(!Captured.Contains(Phase))
                {
                    Captured.Add(Phase);
                    PendingCapture=Phase;
                    PendingCaptureAt=FPlatformTime::Seconds()+0.3;
                }
                return true;
            }
            if(!FirstPawn) { return true; }
            UChaosVehicleMovementComponent* Movement=FirstPawn->GetVehicleMovementComponent();
            if((Phase==TEXT("countdown")||Phase==TEXT("finished"))&&LockStage!=Phase)
            {
                LockStage=Phase;
                LockInjectedAt=World->GetTimeSeconds();
                Movement->SetThrottleInput(1.f);
                Movement->SetSteeringInput(0.8f);
                Movement->SetHandbrakeInput(false);
                FirstPawn->ProcessEvent(FirstPawn->FindFunctionChecked(TEXT("CTSPlaceMine")),nullptr);
                FirstPawn->ProcessEvent(FirstPawn->FindFunctionChecked(TEXT("CTSResetVehicle")),nullptr);
            }
            if(LockInjectedAt>0&&World->GetTimeSeconds()-LockInjectedAt>0.3)
            {
                Result->SetBoolField(LockStage+TEXT("_server_input_locked"),FMath::IsNearlyZero(Movement->GetThrottleInput())&&FMath::IsNearlyZero(Movement->GetSteeringInput())&&Movement->GetHandbrakeInput());
                LockInjectedAt=0;
                Save();
            }
            if(Phase!=TEXT("playing")) { return true; }
            if(FParse::Param(FCommandLine::Get(),TEXT("CXFlowAimFixture")))
            {
                if(!bAimSetup&&Time<=176)
                {
                    int32 Index=0;
                    for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
                    {
                        ACXPawn* Pawn=It->IsValid()?Cast<ACXPawn>(It->Get()->GetPawn()):nullptr;
                        if(!Pawn) { continue; }
                        Place(Pawn,FVector(-6500+500*Index,-9500,50),FRotator(0,Index?180:0,0));
                        if(Index) { Pawn->GetPlayerState<ACXPlayerState>()->AddCoins(10); }
                        ++Index;
                    }
                    bAimSetup=true;
                }
                if(Time<=166)
                {
                    TArray<TSharedPtr<FJsonValue>> Scores;
                    for(APlayerState* Player:State->PlayerArray) if(ACXPlayerState* CXPlayer=Cast<ACXPlayerState>(Player))
                    {
                        TSharedRef<FJsonObject> Score=MakeShared<FJsonObject>();Score->SetStringField(TEXT("name"),CXPlayer->GetPlayerName());Score->SetNumberField(TEXT("coins"),CXPlayer->GetCoins());Scores.Add(MakeShared<FJsonValueObject>(Score));
                    }
                    Result->SetArrayField(TEXT("aim_fixture_scores"),Scores);Save();
                }
                return true;
            }
            if(Time>=178) { Result->SetBoolField(TEXT("playing_brake_released"),!Movement->GetHandbrakeInput()); }
            if(RecoveryStage==0&&Time<=155)
            {
                TestedPawn=FirstPawn;
                OriginalState=FirstPawn->GetPlayerState<ACXPlayerState>();
                OriginalCoins=OriginalState.IsValid()?OriginalState->GetCoins():-1;
                Place(FirstPawn,FVector(-6000,-9500,150),FRotator(0,0,180));
                RecoveryAt=World->GetTimeSeconds();RecoveryStage=1;
                Result->SetStringField(TEXT("recovery_original_pawn"),FirstPawn->GetPathName());
                Save();
            }
            if(RecoveryStage==1&&World->GetTimeSeconds()-RecoveryAt>4)
            {
                ACXPawn* Pawn=TestedPawn.Get();
                Result->SetBoolField(TEXT("overturned_recovered"),Pawn&&Pawn->GetActorUpVector().Z>0.8f);
                if(Pawn) { Place(Pawn,FVector(14800,-5000,150),FRotator::ZeroRotator); }
                RecoveryAt=World->GetTimeSeconds();RecoveryStage=2;Save();
            }
            if(RecoveryStage==2&&World->GetTimeSeconds()-RecoveryAt>4)
            {
                ACXPawn* Pawn=TestedPawn.Get();
                bool bRoad=false;
                if(Pawn) for(TActorIterator<ACXRoadRecoveryArea> It(World);It;++It) { bRoad|=It->ContainsRoadPoint(Pawn->GetActorLocation(),300.f); }
                Result->SetBoolField(TEXT("outside_recovered_to_road"),bRoad);
                Result->SetBoolField(TEXT("same_pawn_and_player_state"),Pawn==FirstPawn&&OriginalState.Get()==FirstPawn->GetPlayerState());
                Result->SetBoolField(TEXT("recovery_kept_coins"),OriginalState.IsValid()&&OriginalState->GetCoins()==OriginalCoins);
                if(Pawn) { Result->SetStringField(TEXT("recovered_position"),Pawn->GetActorLocation().ToString()); }
                RecoveryStage=3;Save();
            }
            return true;
        }
    };
}

FTSTicker::FDelegateHandle StartCXFlowSmoke()
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("CXFlowSmoke"))) { return {}; }
    const TSharedRef<FFlowObservation> Observation=MakeShared<FFlowObservation>();
    FParse::Value(FCommandLine::Get(),TEXT("CXFlowRole="),Observation->Role);
    if(Observation->Role.IsEmpty()) { Observation->Role=TEXT("Server"); }
    Observation->Directory=FPaths::Combine(FPaths::ProjectDir(),TEXT("tmp/unreal/flow-refresh-20261027"));
    FParse::Value(FCommandLine::Get(),TEXT("CXFlowProof="),Observation->Directory);
    IFileManager::Get().MakeDirectory(*Observation->Directory,true);
    return FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Observation](float Delta){return Observation->Tick(Delta);}),0.1f);
}
