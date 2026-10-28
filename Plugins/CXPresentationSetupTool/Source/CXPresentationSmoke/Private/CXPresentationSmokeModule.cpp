#include "Modules/ModuleManager.h"
#include "CXFlowSmoke.h"
#include "CXAudioSmoke.h"
#include "CXCombatSmoke.h"
#include "Containers/Ticker.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"

//검사용 실행 인자가 있을 때만 켜며 게임 소스에는 들어가지 않는 에디터 도구임
class FCXPresentationSmokeModule final : public IModuleInterface
{
    FTSTicker::FDelegateHandle FlowHandle;
    FTSTicker::FDelegateHandle AudioHandle;
    FTSTicker::FDelegateHandle CombatHandle;
    FTSTicker::FDelegateHandle TickHandle;
    FTSTicker::FDelegateHandle ScreenshotHandle;
    FDelegateHandle ScreenshotCapturedHandle;
    FString ScreenshotPath;
    FString ScreenshotCamera;
    bool bScreenshotCameraSet=false;
    bool bDiagnosedSpawns=false;
    double ScreenshotWorldReadyAt=0;
    double StartedAt=0;
    double PickupAt=0;
    int32 Stage=0;
    int32 CoinsBefore=0;
    TWeakObjectPtr<AActor> PickedCoin;
    TWeakObjectPtr<APawn> Vehicle;
    FVector CoinPosition=FVector::ZeroVector;
    FTransform OriginalVehicleTransform;
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>();

    int32 Coins(APlayerState* State) const
    {
        FIntProperty* Property=State?FindFProperty<FIntProperty>(State->GetClass(),TEXT("Coins")):nullptr;
        return Property?Property->GetPropertyValue_InContainer(State):MIN_int32;
    }

    void DiagnoseRuntimeStarts(UWorld* World)
    {
        AGameModeBase* Mode=World->GetAuthGameMode();
        if(!Mode||!Mode->DefaultPawnClass) { return; }
        TSharedRef<FJsonObject> Diagnostics=MakeShared<FJsonObject>();
        Diagnostics->SetBoolField(TEXT("actors_initialized"),World->AreActorsInitialized());
        Diagnostics->SetStringField(TEXT("pawn_class"),Mode->DefaultPawnClass->GetPathName());
        Diagnostics->SetStringField(TEXT("scope"),TEXT("Transient AlwaysSpawn probes in initialized dedicated-server World; never counted as playable pawns or smoke success."));
        for(TActorIterator<AActor> Sky(World);Sky;++Sky)
        {
            if(!Sky->ActorHasTag(TEXT("CXCitySkyDome"))) { continue; }
            UStaticMeshComponent* Mesh=Sky->FindComponentByClass<UStaticMeshComponent>();
            TSharedRef<FJsonObject> SkyState=MakeShared<FJsonObject>();
            SkyState->SetStringField(TEXT("actor"),Sky->GetPathName());
            SkyState->SetBoolField(TEXT("actor_collision_enabled"),Sky->GetActorEnableCollision());
            SkyState->SetStringField(TEXT("profile"),Mesh?Mesh->GetCollisionProfileName().ToString():TEXT(""));
            SkyState->SetNumberField(TEXT("component_collision_enabled"),Mesh?static_cast<int32>(Mesh->GetCollisionEnabled()):-1);
            SkyState->SetBoolField(TEXT("overlap_events"),Mesh&&Mesh->GetGenerateOverlapEvents());
            SkyState->SetStringField(TEXT("mesh"),Mesh?GetPathNameSafe(Mesh->GetStaticMesh()):TEXT(""));
            Diagnostics->SetObjectField(TEXT("runtime_sky_collision"),SkyState);
            break;
        }
        TArray<TSharedPtr<FJsonValue>> Starts;
        for(TActorIterator<APlayerStart> It(World);It;++It)
        {
            TSharedRef<FJsonObject> Start=MakeShared<FJsonObject>();
            Start->SetStringField(TEXT("start"),It->GetName());
            Start->SetStringField(TEXT("transform"),It->GetActorTransform().ToString());
            FVector CDOAdjustment=FVector::ZeroVector;
            Start->SetBoolField(TEXT("cdo_encroaches"),World->EncroachingBlockingGeometry(
                Mode->DefaultPawnClass->GetDefaultObject<AActor>(),It->GetActorLocation(),It->GetActorRotation(),&CDOAdjustment));
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Params.ObjectFlags|=RF_Transient;
            APawn* Probe=World->SpawnActor<APawn>(Mode->DefaultPawnClass,It->GetActorTransform(),Params);
            Start->SetBoolField(TEXT("diagnostic_probe_created"),Probe!=nullptr);
            if(Probe)
            {
                Probe->SetReplicates(false);
                Probe->SetActorTickEnabled(false);
                Start->SetStringField(TEXT("actual_root"),GetNameSafe(Probe->GetRootComponent()));
                Start->SetStringField(TEXT("actual_root_transform"),Probe->GetRootComponent()?Probe->GetRootComponent()->GetComponentTransform().ToString():TEXT(""));
                Start->SetStringField(TEXT("actual_pawn_bounds"),Probe->GetComponentsBoundingBox().ToString());
                UMovementComponent* Movement=Probe->GetMovementComponent();
                FObjectPropertyBase* UpdatedProperty=Movement?FindFProperty<FObjectPropertyBase>(Movement->GetClass(),TEXT("UpdatedComponent")):nullptr;
                UObject* Updated=UpdatedProperty?UpdatedProperty->GetObjectPropertyValue_InContainer(Movement):nullptr;
                Start->SetStringField(TEXT("movement_class"),Movement?Movement->GetClass()->GetPathName():TEXT(""));
                Start->SetStringField(TEXT("movement_updated_component"),GetNameSafe(Updated));
                FVector ActualAdjustment=FVector::ZeroVector;
                Start->SetBoolField(TEXT("actual_probe_encroaches"),World->EncroachingBlockingGeometry(Probe,It->GetActorLocation(),It->GetActorRotation(),&ActualAdjustment));
                Start->SetStringField(TEXT("actual_proposed_adjustment"),ActualAdjustment.ToString());
                TArray<UPrimitiveComponent*> Primitives;
                Probe->GetComponents(Primitives);
                TArray<TSharedPtr<FJsonValue>> Components;
                FComponentQueryParams Query;
                Query.AddIgnoredActor(Probe);
                for(UPrimitiveComponent* Primitive:Primitives)
                {
                    if(!Primitive->IsRegistered()||!Primitive->IsQueryCollisionEnabled()) { continue; }
                    TSharedRef<FJsonObject> Component=MakeShared<FJsonObject>();
                    Component->SetStringField(TEXT("name"),Primitive->GetName());
                    Component->SetStringField(TEXT("class"),Primitive->GetClass()->GetPathName());
                    Component->SetStringField(TEXT("bounds"),Primitive->Bounds.GetBox().ToString());
                    Component->SetStringField(TEXT("transform"),Primitive->GetComponentTransform().ToString());
                    Component->SetNumberField(TEXT("object_channel"),Primitive->GetCollisionObjectType());
                    Component->SetNumberField(TEXT("collision_enabled"),Primitive->GetCollisionEnabled());
                    if(USkeletalMeshComponent* Skeletal=Cast<USkeletalMeshComponent>(Primitive))
                    {
                        Component->SetBoolField(TEXT("enable_physics_on_dedicated_server"),Skeletal->bEnablePhysicsOnDedicatedServer);
                        Component->SetBoolField(TEXT("simulating_physics"),Skeletal->IsSimulatingPhysics());
                    }
                    TArray<FOverlapResult> Overlaps;
                    const bool bBlocking=World->ComponentOverlapMultiByChannel(Overlaps,Primitive,
                        Primitive->GetComponentLocation(),Primitive->GetComponentQuat(),Primitive->GetCollisionObjectType(),Query);
                    Component->SetBoolField(TEXT("blocking_overlap"),bBlocking);
                    TArray<TSharedPtr<FJsonValue>> Hits;
                    for(const FOverlapResult& Hit:Overlaps)
                    {
                        UPrimitiveComponent* Other=Hit.GetComponent();
                        TSharedRef<FJsonObject> Record=MakeShared<FJsonObject>();
                        Record->SetBoolField(TEXT("blocking"),Hit.bBlockingHit);
                        Record->SetStringField(TEXT("actor"),GetPathNameSafe(Hit.GetActor()));
                        Record->SetStringField(TEXT("actor_label"),Hit.GetActor()?Hit.GetActor()->GetActorLabel():TEXT(""));
                        Record->SetBoolField(TEXT("actor_is_pawn"),Hit.GetActor()&&Hit.GetActor()->IsA<APawn>());
                        Record->SetStringField(TEXT("component"),GetPathNameSafe(Other));
                        Record->SetNumberField(TEXT("item_index"),Hit.ItemIndex);
                        Record->SetNumberField(TEXT("collision_enabled"),Other?static_cast<int32>(Other->GetCollisionEnabled()):-1);
                        Record->SetNumberField(TEXT("vehicle_response"),Other?static_cast<int32>(Other->GetCollisionResponseToChannel(ECC_Vehicle)):-1);
                        if(UStaticMeshComponent* Static=Cast<UStaticMeshComponent>(Other))
                        {
                            Record->SetStringField(TEXT("static_mesh"),GetPathNameSafe(Static->GetStaticMesh()));
                            Record->SetStringField(TEXT("collision_profile"),Static->GetCollisionProfileName().ToString());
                        }
                        if(UInstancedStaticMeshComponent* Instanced=Cast<UInstancedStaticMeshComponent>(Other))
                        {
                            FTransform Instance;
                            if(Instanced->GetInstanceTransform(Hit.ItemIndex,Instance,true)) { Record->SetStringField(TEXT("instance_world_transform"),Instance.ToString()); }
                        }
                        Hits.Add(MakeShared<FJsonValueObject>(Record));
                    }
                    Component->SetArrayField(TEXT("overlaps"),Hits);
                    Components.Add(MakeShared<FJsonValueObject>(Component));
                }
                Start->SetArrayField(TEXT("registered_primitives"),Components);
                Probe->Destroy();
            }
            Starts.Add(MakeShared<FJsonValueObject>(Start));
        }
        Diagnostics->SetArrayField(TEXT("starts"),Starts);
        Report->SetObjectField(TEXT("live_spawn_diagnostics"),Diagnostics);
        FString Json;
        const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json);
        FJsonSerializer::Serialize(Diagnostics,Writer);
        const FString Directory=FPaths::Combine(FPaths::ProjectDir(),TEXT("tmp/unreal/flow-refresh-20261027"));
        IFileManager::Get().MakeDirectory(*Directory,true);
        FFileHelper::SaveStringToFile(Json,*FPaths::Combine(Directory,TEXT("runtime_spawn_diagnostics.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp,Display,TEXT("CX_RUNTIME_SPAWN_DIAGNOSTICS=%d"),Starts.Num());
        bDiagnosedSpawns=true;
    }

    bool Playing(AGameStateBase* State) const
    {
        FEnumProperty* Property=State?FindFProperty<FEnumProperty>(State->GetClass(),TEXT("CurrentGameState")):nullptr;
        return Property&&Property->GetUnderlyingProperty()->GetSignedIntPropertyValue(Property->ContainerPtrToValuePtr<void>(State))==1;
    }

    void MoveVehicle(APawn* Pawn,const FTransform& Transform)
    {
        if(UPrimitiveComponent* Mesh=Cast<UPrimitiveComponent>(Pawn->GetRootComponent()))
        {
            Mesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
            Mesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
        }
        Pawn->SetActorTransform(Transform,false,nullptr,ETeleportType::TeleportPhysics);
        if(APlayerController* PC=Cast<APlayerController>(Pawn->GetController()))
        {
            PC->ClientSetLocation(Transform.GetLocation(),Transform.Rotator());
        }
        Pawn->ForceNetUpdate();
        if(Pawn->GetRootComponent()) { Pawn->GetRootComponent()->UpdateOverlaps(); }
    }

    bool Finish(bool bSuccess,const FString& Error)
    {
        Report->SetBoolField(TEXT("success"),bSuccess);
        Report->SetStringField(TEXT("error"),Error);
        Report->SetNumberField(TEXT("elapsed_seconds"),FPlatformTime::Seconds()-StartedAt);
        const FString Directory=FPaths::Combine(FPaths::ProjectDir(),TEXT("tmp/unreal/flow-refresh-20261027"));
        IFileManager::Get().MakeDirectory(*Directory,true);
        FString Json;
        const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json);
        FJsonSerializer::Serialize(Report,Writer);
        FFileHelper::SaveStringToFile(Json,*FPaths::Combine(Directory,TEXT("runtime_items.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp,Display,TEXT("CX_PRESENTATION_ITEM_SMOKE=%s %s"),bSuccess?TEXT("true"):TEXT("false"),*Error);
        return false;
    }

    bool Tick(float)
    {
        if(FPlatformTime::Seconds()-StartedAt>300) { return Finish(false,TEXT("Timed out waiting for dedicated server, two players, Playing state or item overlap.")); }
        UWorld* World=nullptr;
        if(GEngine) for(const FWorldContext& Context:GEngine->GetWorldContexts())
        {
            if(Context.World()&&Context.World()->GetNetMode()==NM_DedicatedServer&&Context.World()->HasBegunPlay()) { World=Context.World(); break; }
        }
        if(!World) { return true; }
        AGameStateBase* State=World->GetGameState();
        if(!State) { return true; }
        Report->SetBoolField(TEXT("dedicated_server"),World->GetNetMode()==NM_DedicatedServer);
        Report->SetNumberField(TEXT("connected_players"),State->PlayerArray.Num());
        int32 CoinCount=0,SpawnerCount=0,PawnCount=0;
        AActor* CandidateCoin=nullptr;
        for(TActorIterator<AActor> It(World);It;++It)
        {
            if(It->GetClass()->GetName()==TEXT("BP_CXCoinItem_C"))
            {
                ++CoinCount;
                if(!CandidateCoin||It->GetActorLocation().X<CandidateCoin->GetActorLocation().X) { CandidateCoin=*It; }
            }
            if(It->GetClass()->GetName()==TEXT("BP_CXItemSpawner_C")) { ++SpawnerCount; }
            if(It->IsA<APawn>()) { ++PawnCount; }
        }
        Report->SetNumberField(TEXT("server_coin_actors"),CoinCount);
        Report->SetNumberField(TEXT("server_spawner_actors"),SpawnerCount);
        Report->SetNumberField(TEXT("server_pawn_actors"),PawnCount);
        Report->SetBoolField(TEXT("server_playing"),Playing(State));
        if(!bDiagnosedSpawns&&State->PlayerArray.Num()>=1) { DiagnoseRuntimeStarts(World); }
        if(Stage==0)
        {
            if(State->PlayerArray.Num()<2||!Playing(State)) { return true; }
            APawn* Pawn=nullptr;
            for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
            {
                if(It->IsValid()&&It->Get()->GetPawn()&&It->Get()->PlayerState) { Pawn=It->Get()->GetPawn(); break; }
            }
            if(!Pawn) { return true; }
            AActor* Coin=CandidateCoin;
            if(!Coin) { return true; }
            Vehicle=Pawn;
            PickedCoin=Coin;
            CoinPosition=Coin->GetActorLocation();
            OriginalVehicleTransform=Pawn->GetActorTransform();
            CoinsBefore=Coins(Pawn->GetPlayerState());
            Report->SetNumberField(TEXT("coin_count_initial"),CoinCount);
            Report->SetNumberField(TEXT("coins_before"),CoinsBefore);
            Report->SetStringField(TEXT("player_name"),Pawn->GetPlayerState()->GetPlayerName());
            Report->SetStringField(TEXT("pickup_position"),CoinPosition.ToString());
            FTransform Destination=OriginalVehicleTransform;
            Destination.SetLocation(FVector(CoinPosition.X,CoinPosition.Y,110));
            MoveVehicle(Pawn,Destination);
            TArray<USphereComponent*> Spheres;
            Coin->GetComponents(Spheres);
            for(USphereComponent* Sphere:Spheres) { Sphere->UpdateOverlaps(); }
            PickupAt=FPlatformTime::Seconds();
            Stage=1;
            return true;
        }
        APawn* Pawn=Vehicle.Get();
        if(!Pawn||!Pawn->GetPlayerState()) { return Finish(false,TEXT("Test vehicle or PlayerState disappeared.")); }
        if(Stage==1)
        {
            const int32 Current=Coins(Pawn->GetPlayerState());
            if(Current==CoinsBefore+10&&!PickedCoin.IsValid())
            {
                Report->SetNumberField(TEXT("coins_after"),Current);
                Report->SetNumberField(TEXT("pickup_delta"),Current-CoinsBefore);
                Report->SetBoolField(TEXT("original_coin_destroyed"),true);
                MoveVehicle(Pawn,OriginalVehicleTransform);
                PickupAt=FPlatformTime::Seconds();
                Stage=2;
            }
            else if(FPlatformTime::Seconds()-PickupAt>8) { return Finish(false,TEXT("Vehicle teleport did not produce exactly one +10 pickup and item destruction.")); }
            return true;
        }
        if(Stage==2)
        {
            for(TActorIterator<AActor> It(World);It;++It)
            {
                if(It->GetClass()->GetName()==TEXT("BP_CXCoinItem_C")&&It->GetActorLocation().Equals(CoinPosition,1.f))
                {
                    Report->SetBoolField(TEXT("coin_respawned_same_location"),true);
                    Report->SetNumberField(TEXT("respawn_delay_seconds"),FPlatformTime::Seconds()-PickupAt);
                    Report->SetNumberField(TEXT("coins_after_respawn"),Coins(Pawn->GetPlayerState()));
                    return Finish(Coins(Pawn->GetPlayerState())==CoinsBefore+10,TEXT(""));
                }
            }
            if(FPlatformTime::Seconds()-PickupAt>12) { return Finish(false,TEXT("Destroyed coin did not respawn at its spawner.")); }
        }
        return true;
    }

public:
    virtual void StartupModule() override
    {
        FlowHandle=StartCXFlowSmoke();
        AudioHandle=StartCXAudioSmoke();
        CombatHandle=StartCXCombatSmoke();
        if(FParse::Param(FCommandLine::Get(),TEXT("CXPresentationSmoke")))
        {
            StartedAt=FPlatformTime::Seconds();
            TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this,&FCXPresentationSmokeModule::Tick),.1f);
        }
        if(FParse::Value(FCommandLine::Get(),TEXT("CXPresentationScreenshot="),ScreenshotPath))
        {
            FParse::Value(FCommandLine::Get(),TEXT("CXPresentationCamera="),ScreenshotCamera);
            ScreenshotHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this,&FCXPresentationSmokeModule::ScreenshotTick),.2f);
        }
    }

    virtual void ShutdownModule() override
    {
        if(FlowHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(FlowHandle); }
        if(AudioHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(AudioHandle); }
        if(CombatHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(CombatHandle); }
        if(TickHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); }
        if(ScreenshotHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(ScreenshotHandle); }
        if(ScreenshotCapturedHandle.IsValid()) { UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotCapturedHandle); }
    }

    bool ScreenshotTick(float)
    {
        if(!GEngine||!GEngine->GameViewport) { return true; }
        UWorld* World=GEngine->GameViewport->GetWorld();
        if(!World||!World->HasBegunPlay()||World->GetNetMode()==NM_DedicatedServer) { return true; }
        if(ScreenshotWorldReadyAt==0) { ScreenshotWorldReadyAt=FPlatformTime::Seconds(); }
        if(!ScreenshotCamera.IsEmpty()&&!bScreenshotCameraSet&&FPlatformTime::Seconds()-ScreenshotWorldReadyAt>2)
        {
            APlayerController* PC=World->GetFirstPlayerController();
            const FName Tag=ScreenshotCamera.Equals(TEXT("Aerial"),ESearchCase::IgnoreCase)?TEXT("CXPreviewAerial"):TEXT("CXPreviewStreet");
            if(PC) for(TActorIterator<ACameraActor> It(World);It;++It)
            {
                if(It->ActorHasTag(Tag))
                {
                    PC->SetViewTarget(*It);
                    bScreenshotCameraSet=true;
                    break;
                }
            }
            if(!bScreenshotCameraSet) { return true; }
        }
        if(FPlatformTime::Seconds()-ScreenshotWorldReadyAt<14) { return true; }
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(ScreenshotPath),true);
        ScreenshotCapturedHandle=UGameViewportClient::OnScreenshotCaptured().AddRaw(this,&FCXPresentationSmokeModule::ScreenshotCaptured);
        FScreenshotRequest::RequestScreenshot(ScreenshotPath,true,false);
        UE_LOG(LogTemp,Display,TEXT("CX_PRESENTATION_SCREENSHOT_REQUESTED=%s"),*ScreenshotPath);
        return false;
    }

    void ScreenshotCaptured(int32 Width,int32 Height,const TArray<FColor>& Pixels)
    {
        UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotCapturedHandle);
        ScreenshotCapturedHandle.Reset();
        int32 NonBlack=0;
        uint8 Minimum=255,Maximum=0;
        for(const FColor& Pixel:Pixels)
        {
            const uint8 Brightness=FMath::Max3(Pixel.R,Pixel.G,Pixel.B);
            NonBlack+=Brightness>8?1:0;
            Minimum=FMath::Min(Minimum,Brightness);
            Maximum=FMath::Max(Maximum,Brightness);
        }
        TArray64<uint8> PNG;
        FImageUtils::PNGCompressImageArray(Width,Height,TArrayView64<const FColor>(Pixels.GetData(),Pixels.Num()),PNG);
        const bool bSaved=FFileHelper::SaveArrayToFile(PNG,*ScreenshotPath);
        const bool bScene=Pixels.Num()>0&&NonBlack>Pixels.Num()/50&&Maximum-Minimum>15;
        TSharedRef<FJsonObject> QA=MakeShared<FJsonObject>();
        QA->SetBoolField(TEXT("success"),bSaved&&bScene);
        QA->SetBoolField(TEXT("png_saved"),bSaved);
        QA->SetStringField(TEXT("source"),TEXT("actual_game_viewport_with_ui"));
        QA->SetStringField(TEXT("camera"),ScreenshotCamera);
        QA->SetBoolField(TEXT("fixed_camera_selected"),bScreenshotCameraSet);
        QA->SetNumberField(TEXT("width"),Width);
        QA->SetNumberField(TEXT("height"),Height);
        QA->SetNumberField(TEXT("nonblack_pixels"),NonBlack);
        QA->SetNumberField(TEXT("total_pixels"),Pixels.Num());
        QA->SetNumberField(TEXT("brightness_min"),Minimum);
        QA->SetNumberField(TEXT("brightness_max"),Maximum);
        FString Json;
        const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json);
        FJsonSerializer::Serialize(QA,Writer);
        FFileHelper::SaveStringToFile(Json,*(ScreenshotPath+TEXT(".qa.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp,Display,TEXT("CX_VIEWPORT_CAPTURE=%s file=%s nonblack=%d/%d range=%d..%d"),bSaved&&bScene?TEXT("true"):TEXT("false"),*ScreenshotPath,NonBlack,Pixels.Num(),Minimum,Maximum);
    }
};

IMPLEMENT_MODULE(FCXPresentationSmokeModule, CXPresentationSmoke)
