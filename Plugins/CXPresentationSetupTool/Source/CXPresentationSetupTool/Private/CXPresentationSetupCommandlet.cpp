#include "CXPresentationSetupCommandlet.h"
#include "Vehicle/CXRoadRecoveryArea.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "ChaosVehicleMovementComponent.h"
#include "WheeledVehiclePawn.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkyLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/OverlapResult.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Factories/TextureFactory.h"
#include "FileHelpers.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "TextureResource.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogCXPresentation, Log, All);

int32 RunCXBumperProbe(const FString& Params);

namespace
{
    const FString ArenaPath(TEXT("/Game/CX/Maps/L_CXCityArena"));
    const FString MenuPath(TEXT("/Game/CX/Maps/L_CXMainMenu"));
    const FString MaterialRoot(TEXT("/Game/CX/Materials/"));
    const FString ProofRoot(TEXT("tmp/unreal/city-presentation-20261027/"));
    const FVector ArenaExtent(16000, 16000, 0);

    FString ObjectPath(const FString& Path)
    {
        return Path + TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
    }

    UObject* ReadObject(UObject* Object, FName Name)
    {
        FObjectPropertyBase* Property = Object ? FindFProperty<FObjectPropertyBase>(Object->GetClass(), Name) : nullptr;
        return Property ? Property->GetObjectPropertyValue_InContainer(Object) : nullptr;
    }

    bool SetObject(UObject* Object, FName Name, UObject* Value)
    {
        FObjectPropertyBase* Property = Object ? FindFProperty<FObjectPropertyBase>(Object->GetClass(), Name) : nullptr;
        if (!Property || !Value || !Value->IsA(Property->PropertyClass)) { return false; }
        Property->SetObjectPropertyValue_InContainer(Object, Value);
        return ReadObject(Object, Name) == Value;
    }

    bool SaveAsset(UObject* Object)
    {
        if (!Object) { return false; }
        UPackage* Package = Object->GetOutermost();
        Package->MarkPackageDirty();
        const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        return UPackage::SavePackage(Package, Object, *Filename, Args);
    }

    FLinearColor Hex(const TCHAR* Value)
    {
        return FLinearColor::FromSRGBColor(FColor::FromHex(Value));
    }

    //월드 좌표 노이즈라 건물 크기와 상관없이 표면 입자 크기가 일정함
    UMaterial* MakeMaterial(const TCHAR* Name, const FLinearColor& Color, float Roughness,
        float Metallic = 0.f, float NoiseScale = 0.f, float Glow = 0.f, bool bUnlit = false)
    {
        const FString Path = MaterialRoot + Name;
        UMaterial* Material = LoadObject<UMaterial>(nullptr, *ObjectPath(Path), nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (Material)
        {
            bool bChanged=false;
            if(bUnlit&&!Material->bIsSky)
            {
                Material->bIsSky=true;
                bChanged=true;
            }
            if(!bUnlit&&!Material->bUsedWithInstancedStaticMeshes)
            {
                bool bNeedsRecompile=false;
                if(!Material->SetMaterialUsage(bNeedsRecompile,MATUSAGE_InstancedStaticMeshes)) { return nullptr; }
                bChanged=true;
            }
            if(bChanged)
            {
                Material->PostEditChange();
                if(!SaveAsset(Material)) { return nullptr; }
            }
            return Material;
        }
        UPackage* Package = CreatePackage(*Path);
        Material = NewObject<UMaterial>(Package, FName(Name), RF_Public | RF_Standalone);
        Material->TwoSided = bUnlit;
        Material->bIsSky = bUnlit;
        Material->bUsedWithInstancedStaticMeshes = !bUnlit;
        if (bUnlit) { Material->SetShadingModel(MSM_Unlit); }
        UMaterialExpressionConstant3Vector* Base = NewObject<UMaterialExpressionConstant3Vector>(Material);
        Base->Constant = Color;
        Material->GetExpressionCollection().AddExpression(Base);
        UMaterialExpression* ColorResult = Base;
        if (NoiseScale > 0.f)
        {
            UMaterialExpressionWorldPosition* Position = NewObject<UMaterialExpressionWorldPosition>(Material);
            UMaterialExpressionNoise* Noise = NewObject<UMaterialExpressionNoise>(Material);
            UMaterialExpressionMultiply* Multiply = NewObject<UMaterialExpressionMultiply>(Material);
            Noise->Position.Connect(0, Position);
            Noise->Scale = NoiseScale;
            Noise->Levels = 1;
            Noise->Quality = 1;
            Noise->NoiseFunction = NOISEFUNCTION_GradientTex3D;
            Noise->OutputMin = .68f;
            Noise->OutputMax = 1.08f;
            Multiply->A.Connect(0, Base);
            Multiply->B.Connect(0, Noise);
            for (UMaterialExpression* Expression : TArray<UMaterialExpression*>{Position, Noise, Multiply})
            {
                Material->GetExpressionCollection().AddExpression(Expression);
            }
            ColorResult = Multiply;
        }
        if (bUnlit)
        {
            Material->GetEditorOnlyData()->EmissiveColor.Connect(0, ColorResult);
        }
        else
        {
            Material->GetEditorOnlyData()->BaseColor.Connect(0, ColorResult);
            Material->GetEditorOnlyData()->Roughness.UseConstant = true;
            Material->GetEditorOnlyData()->Roughness.Constant = Roughness;
            Material->GetEditorOnlyData()->Metallic.UseConstant = true;
            Material->GetEditorOnlyData()->Metallic.Constant = Metallic;
            if (Glow > 0.f)
            {
                UMaterialExpressionMultiply* Emissive = NewObject<UMaterialExpressionMultiply>(Material);
                Emissive->A.Connect(0, Base);
                Emissive->ConstB = Glow;
                Material->GetExpressionCollection().AddExpression(Emissive);
                Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Emissive);
            }
        }
        Material->PostEditChange();
        FAssetRegistryModule::AssetCreated(Material);
        return SaveAsset(Material) ? Material : nullptr;
    }

    struct FCityBuilder
    {
        UWorld* World = nullptr;
        UStaticMesh* Cube = nullptr;
        AActor* Geometry = nullptr;
        TMap<FString, UHierarchicalInstancedStaticMeshComponent*> Batches;
        TMap<FString, UMaterial*> Materials;
        int32 Boxes = 0;
        int32 Buildings = 0;
        int32 Windows = 0;
        int32 Signs = 0;

        bool Init(UWorld* InWorld)
        {
            World = InWorld;
            Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
            Geometry = World->SpawnActor<AActor>();
            Geometry->SetActorLabel(TEXT("CX_City_InstancedGeometry"));
            Geometry->SetFolderPath(TEXT("CX City/Architecture"));
            USceneComponent* Root = NewObject<USceneComponent>(Geometry, TEXT("CityRoot"));
            Geometry->SetRootComponent(Root);
            Geometry->AddInstanceComponent(Root);
            Root->SetMobility(EComponentMobility::Static);
            Root->RegisterComponent();
            Materials.Add(TEXT("Asphalt"), MakeMaterial(TEXT("M_CXAsphalt"), Hex(TEXT("25282E")), .93f, 0.f, .19f));
            Materials.Add(TEXT("Concrete"), MakeMaterial(TEXT("M_CXConcrete"), Hex(TEXT("A9ADB2")), .86f, 0.f, .035f));
            Materials.Add(TEXT("White"), MakeMaterial(TEXT("M_CXOffWhite"), Hex(TEXT("F5F2EC")), .73f, 0.f, .015f));
            Materials.Add(TEXT("Red"), MakeMaterial(TEXT("M_CXCrimson"), Hex(TEXT("D91C2B")), .56f, .13f));
            Materials.Add(TEXT("Black"), MakeMaterial(TEXT("M_CXCarbon"), Hex(TEXT("13151A")), .68f, .10f));
            Materials.Add(TEXT("Glass"), MakeMaterial(TEXT("M_CXSmokedGlass"), Hex(TEXT("13151A")), .21f, .66f));
            Materials.Add(TEXT("GlassLight"), MakeMaterial(TEXT("M_CXSilverGlass"), Hex(TEXT("A9ADB2")), .24f, .55f));
            Materials.Add(TEXT("Gold"), MakeMaterial(TEXT("M_CXCoinGold"), Hex(TEXT("FFD05A")), .27f, .45f, 0.f, .25f));
            Materials.Add(TEXT("Lamp"), MakeMaterial(TEXT("M_CXWarmLamp"), Hex(TEXT("F5F2EC")), .35f, 0.f, 0.f, 2.f));
            Materials.Add(TEXT("Rubber"), MakeMaterial(TEXT("M_CXTireMarks"), Hex(TEXT("13151A")), .97f));
            Materials.Add(TEXT("Sky"), MakeMaterial(TEXT("M_CXOvercastSky"), Hex(TEXT("F5F2EC")), 1.f, 0.f, 0.f, 0.f, true));
            for (const auto& Pair : Materials) { if (!Pair.Value) { return false; } }
            return Cube && Geometry;
        }

        UHierarchicalInstancedStaticMeshComponent* Batch(const FString& Key, bool bCollision)
        {
            const FString Name = Key + (bCollision ? TEXT("_Solid") : TEXT("_Detail"));
            if (auto* Existing = Batches.Find(Name)) { return *Existing; }
            UHierarchicalInstancedStaticMeshComponent* Mesh = NewObject<UHierarchicalInstancedStaticMeshComponent>(Geometry, FName(*Name));
            Geometry->AddInstanceComponent(Mesh);
            Mesh->SetupAttachment(Geometry->GetRootComponent());
            Mesh->SetMobility(EComponentMobility::Static);
            Mesh->SetStaticMesh(Cube);
            Mesh->SetMaterial(0, Materials.FindChecked(Key));
            Mesh->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
            Mesh->SetCollisionObjectType(ECC_WorldStatic);
            Mesh->SetCollisionResponseToAllChannels(bCollision ? ECR_Block : ECR_Ignore);
            Mesh->SetGenerateOverlapEvents(false);
            Mesh->SetCanEverAffectNavigation(bCollision);
            Mesh->SetCastShadow(bCollision);
            Mesh->bAutoRebuildTreeOnInstanceChanges=false;
            Mesh->RegisterComponent();
            Batches.Add(Name, Mesh);
            return Mesh;
        }

        void Box(const FVector& Center, const FVector& Size, const FString& Material, bool bCollision = false, float Yaw = 0.f)
        {
            Batch(Material, bCollision)->AddInstance(FTransform(FRotator(0, Yaw, 0), Center, Size / 100.f));
            ++Boxes;
        }

        void Text(const FVector& Position, const FRotator& Rotation, const FString& Value,
            float Size = 140.f, const FColor& Color = FColor(245,242,236))
        {
            AActor* Actor = World->SpawnActor<AActor>();
            Actor->SetActorLabel(TEXT("CX_Sign_") + FString::FromInt(++Signs));
            Actor->SetFolderPath(TEXT("CX City/Signage"));
            UTextRenderComponent* Text = NewObject<UTextRenderComponent>(Actor, TEXT("SignText"));
            Actor->AddInstanceComponent(Text);
            Actor->SetRootComponent(Text);
            Text->SetMobility(EComponentMobility::Static);
            Text->SetText(FText::FromString(Value));
            Text->SetTextRenderColor(Color);
            Text->SetWorldSize(Size);
            Text->SetHorizontalAlignment(EHTA_Center);
            Text->SetVerticalAlignment(EVRTA_TextCenter);
            Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Text->RegisterComponent();
            Actor->SetActorLocationAndRotation(Position, Rotation);
        }

        void Building(const FVector2D& Center, const FVector2D& Footprint, float Height, int32 Style, int32 Seed)
        {
            ++Buildings;
            const FString Body = Style == 0 ? TEXT("White") : Style == 1 ? TEXT("Red") : TEXT("Concrete");
            const float BaseZ = 18.f;
            Box(FVector(Center, BaseZ + Height / 2.f), FVector(Footprint, Height), Body, true);
            //바닥층과 처마를 따로 둬서 실루엣과 층 구분이 보이게 함
            Box(FVector(Center, 250.f), FVector(Footprint + FVector2D(90), 450), TEXT("Black"), true);
            Box(FVector(Center, Height + BaseZ + 55), FVector(Footprint + FVector2D(100), 110), TEXT("White"));
            Box(FVector(Center, Height + BaseZ + 170), FVector(Footprint * .78f, 240), TEXT("Black"));
            Box(FVector(Center + FVector2D(-Footprint.X*.2f,Footprint.Y*.1f), Height + BaseZ + 360), FVector(450,380,180), TEXT("Concrete"));
            Box(FVector(Center + FVector2D(Footprint.X*.18f,-Footprint.Y*.12f), Height + BaseZ + 410), FVector(250,440,270), TEXT("White"));
            Box(FVector(Center + FVector2D(Footprint.X*.29f,Footprint.Y*.28f), Height + BaseZ + 650), FVector(28,28,900), TEXT("Black"));
            const int32 Floors = FMath::Max(2, FMath::FloorToInt((Height - 600) / 340));
            for (int32 Side = 0; Side < 4; ++Side)
            {
                const bool bXFace = Side < 2;
                const float Sign = Side % 2 == 0 ? 1.f : -1.f;
                const float Span = bXFace ? Footprint.Y : Footprint.X;
                const int32 Columns = FMath::Max(2, FMath::FloorToInt(Span / 240));
                for (int32 Floor = 0; Floor < Floors; ++Floor)
                {
                    const float Z = 650 + Floor * 340.f;
                    const FVector FramePosition = bXFace ? FVector(Center.X + Sign*(Footprint.X/2+10), Center.Y, Z-125)
                        : FVector(Center.X, Center.Y+Sign*(Footprint.Y/2+10), Z-125);
                    Box(FramePosition, bXFace ? FVector(18, Span, 24) : FVector(Span,18,24), TEXT("Black"));
                    for (int32 Column = 0; Column < Columns; ++Column)
                    {
                        const float Along = -Span/2 + (Column+.5f)*Span/Columns;
                        const FVector Position = bXFace ? FVector(Center.X+Sign*(Footprint.X/2+12),Center.Y+Along,Z)
                            : FVector(Center.X+Along,Center.Y+Sign*(Footprint.Y/2+12),Z);
                        const FString Glass = ((Column*3+Floor+Seed)%7 == 0) ? TEXT("GlassLight") : TEXT("Glass");
                        Box(Position, bXFace ? FVector(20,Span/Columns-36,210) : FVector(Span/Columns-36,20,210), Glass);
                        ++Windows;
                    }
                }
                const int32 ColumnsForRibs = FMath::Max(2, FMath::FloorToInt(Span/480));
                for (int32 Rib = 0; Rib <= ColumnsForRibs; ++Rib)
                {
                    const float Along = -Span/2 + Rib*Span/ColumnsForRibs;
                    const FVector Position = bXFace ? FVector(Center.X+Sign*(Footprint.X/2+28),Center.Y+Along,Height/2)
                        : FVector(Center.X+Along,Center.Y+Sign*(Footprint.Y/2+28),Height/2);
                    Box(Position, bXFace ? FVector(45,35,Height) : FVector(35,45,Height), Body);
                }
            }
        }

        void Lamp(const FVector2D& P, float Yaw)
        {
            Box(FVector(P,350),FVector(22,22,700),TEXT("Black"));
            const FVector Arm = FRotator(0,Yaw,0).RotateVector(FVector(100,0,0));
            Box(FVector(P,680)+Arm,FVector(230,22,22),TEXT("Black"),false,Yaw);
            Box(FVector(P,665)+Arm*1.8f,FVector(115,65,15),TEXT("Lamp"),false,Yaw);
            Box(FVector(P,22),FVector(100,100,44),TEXT("Concrete"),true);
        }

        void RoadDress()
        {
            Box(FVector(0,0,-30), FVector(36000,36000,60), TEXT("Asphalt"), true);
            //사거리와 순환도로는 같은 높이를 유지해서 이음새에 바퀴가 걸리지 않게 함
            for (float SignX : {-1.f,1.f}) for (float SignY : {-1.f,1.f})
            {
                const FVector2D Center(SignX*5000,SignY*5000);
                Box(FVector(Center,7),FVector(6400,6400,14),TEXT("Concrete"),true);
                for (int32 Edge = 0; Edge < 4; ++Edge)
                {
                    const bool bAlongX = Edge < 2;
                    const float Sign = Edge%2 ? -1.f : 1.f;
                    for (int32 I = -15; I <= 15; ++I)
                    {
                        const FVector P = bAlongX ? FVector(Center.X+I*200,Center.Y+Sign*3200,14)
                            : FVector(Center.X+Sign*3200,Center.Y+I*200,14);
                        Box(P,bAlongX?FVector(195,34,28):FVector(34,195,28),I%4==0?TEXT("Red"):TEXT("White"),true);
                    }
                }
            }
            //바깥 경계는 낮은 경주용 방호벽으로 닫고 거리 폭은 최소 24m 남김
            for (int32 Side=0;Side<4;++Side)
            {
                const bool bAlongX=Side<2;
                const float Sign=Side%2? -1.f:1.f;
                for (int32 I=-24;I<=24;++I)
                {
                    const FVector P=bAlongX?FVector(I*500,Sign*12200,60):FVector(Sign*12200,I*500,60);
                    Box(P,bAlongX?FVector(485,90,120):FVector(90,485,120),I%3?TEXT("White"):TEXT("Red"),true);
                }
            }
            //흰색 차선은 교차로 앞에서 끊음
            for (int32 Axis=0;Axis<2;++Axis)
            {
                for (float LaneCenter : {-10000.f,0.f,10000.f})
                {
                    for (int32 I=-22;I<=22;++I)
                    {
                        const float Along=I*500.f;
                        if (FMath::Abs(Along)<1500 || FMath::Abs(FMath::Abs(Along)-10000)<1400) { continue; }
                        const FVector P=Axis==0?FVector(Along,LaneCenter,1.8f):FVector(LaneCenter,Along,1.8f);
                        Box(P,Axis==0?FVector(250,16,2):FVector(16,250,2),TEXT("White"));
                    }
                }
            }
            for (float X : {-10000.f,0.f,10000.f}) for(float Y : {-10000.f,0.f,10000.f})
            {
                for(int32 Side=0;Side<4;++Side)
                {
                    const bool bAlongX=Side<2;
                    const float Sign=Side%2?-1.f:1.f;
                    if ((bAlongX&&FMath::Abs(Y+Sign*1700)>11200)||(!bAlongX&&FMath::Abs(X+Sign*1700)>11200)) { continue; }
                    for(int32 Stripe=-4;Stripe<=4;++Stripe)
                    {
                        const FVector P=bAlongX?FVector(X+Stripe*180,Y+Sign*1550,2):FVector(X+Sign*1550,Y+Stripe*180,2);
                        Box(P,bAlongX?FVector(85,420,2):FVector(420,85,2),TEXT("White"));
                    }
                }
            }
            for (int32 I=-2;I<=2;++I)
            {
                Lamp(FVector2D(I*3900,-11600),90);
                Lamp(FVector2D(I*3900,11600),-90);
                Lamp(FVector2D(-11600,I*3900),0);
                Lamp(FVector2D(11600,I*3900),180);
            }
            for(float SX:{-1.f,1.f}) for(float SY:{-1.f,1.f})
            {
                Lamp(FVector2D(SX*2050,SY*2200),SX<0?0:180);
                Lamp(FVector2D(SX*7850,SY*2200),SX<0?0:180);
            }
            //사거리의 원형 타이어 흔적은 충돌 없는 얇은 조각으로 만듦
            for(int32 Track=0;Track<2;++Track) for(int32 Step=0;Step<74;++Step)
            {
                const float Angle=Step*4.5f;
                const float Radius=690+Track*120.f;
                const FVector P(FMath::Cos(FMath::DegreesToRadians(Angle))*Radius,FMath::Sin(FMath::DegreesToRadians(Angle))*Radius,2.5f);
                Box(P,FVector(80,22,1),TEXT("Rubber"),false,Angle+90);
            }
            for(int32 I=0;I<14;++I)
            {
                Box(FVector(-3700+I*180,-9650,2.5),FVector(175,24,1),TEXT("Rubber"),false,3);
                Box(FVector(-3700+I*180,-9480,2.5),FVector(175,24,1),TEXT("Rubber"),false,3);
            }
        }

        void Architecture()
        {
            int32 Seed=0;
            for(float SX:{-1.f,1.f}) for(float SY:{-1.f,1.f})
            {
                Building(FVector2D(SX*4100,SY*5200),FVector2D(2200,3400),5100+(Seed%3)*680,Seed%3,Seed++);
                Building(FVector2D(SX*6900,SY*6200),FVector2D(1700,2200),3600+(Seed%2)*1800,Seed%3,Seed++);
                Building(FVector2D(SX*6300,SY*2900),FVector2D(2600,1200),1900,0,Seed++);
                //도시 블록의 코너를 비워 도로에서 보행로와 건물 입면이 같이 보이게 함
                Box(FVector(SX*6800,SY*4300,190),FVector(1000,400,330),TEXT("Red"));
                for(int32 Parking=0;Parking<4;++Parking)
                {
                    Box(FVector(SX*(2200+Parking*250),SY*7500,16),FVector(18,550,2),TEXT("White"));
                }
                Text(FVector(SX*6800,SY*4090,220),FRotator(0,SY<0?-90:90,0),TEXT("CX / CITY"),90);
            }
            for(int32 Side=0;Side<4;++Side)
            {
                const bool bAlongX=Side<2;
                const float Sign=Side%2?-1.f:1.f;
                for(int32 I=-3;I<=3;++I)
                {
                    const FVector2D Position=bAlongX?FVector2D(I*4100,Sign*(14300+(I%2)*650))
                        :FVector2D(Sign*(14300+(I%2)*650),I*4100);
                    const float Height=4500+((I+4)*1237+Side*971)%6200;
                    Building(Position,FVector2D(2600+(I%2)*300,2400),Height,(I+Side+6)%3,Seed++);
                }
            }
            //거리 안내판은 각 진입로에서 정면으로 읽히게 배치함
            const TArray<FString> Labels{TEXT("NORTH / COIN DISTRICT"),TEXT("CX CITY ARENA"),TEXT("REDLINE / SOUTH"),TEXT("MARKET / LOOP")};
            for(int32 Side=0;Side<4;++Side)
            {
                const float Angle=Side*90.f;
                const FVector Out=FRotator(0,Angle,0).RotateVector(FVector(11280,0,0));
                Box(Out+FVector(0,0,540),FVector(70,1100,290),TEXT("Red"),false,Angle);
                Box(Out+FVector(0,0,260),FVector(55,55,520),TEXT("Black"),true);
                Text(Out-FRotator(0,Angle,0).RotateVector(FVector(42,0,0))+FVector(0,0,550),FRotator(0,Angle+180,0),Labels[Side],100);
            }
            Text(FVector(0,-12080,480),FRotator(0,90,0),TEXT("CX  /  CITY ARENA"),230);
            Box(FVector(0,-12120,480),FVector(3500,75,460),TEXT("Black"));
        }

        bool Lighting()
        {
            UTextureCube* Daylight=LoadObject<UTextureCube>(nullptr,TEXT("/Game/StarterContent/HDRI/HDRI_Epic_Courtyard_Daylight.HDRI_Epic_Courtyard_Daylight"));
            if(!Daylight) { return false; }
            ADirectionalLight* Sun=World->SpawnActor<ADirectionalLight>(FVector(0,0,15000),FRotator(-46,-28,0));
            Sun->SetActorLabel(TEXT("CX_Overcast_KeyLight"));
            Sun->SetFolderPath(TEXT("CX City/Lighting"));
            Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
            Sun->GetLightComponent()->SetIntensity(10.0f);
            Sun->GetLightComponent()->SetLightColor(FLinearColor::White);
            ASkyLight* Sky=World->SpawnActor<ASkyLight>();
            Sky->SetActorLabel(TEXT("CX_Soft_SkyFill"));
            Sky->SetFolderPath(TEXT("CX City/Lighting"));
            Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
            Sky->GetLightComponent()->SourceType=SLS_SpecifiedCubemap;
            Sky->GetLightComponent()->SetCubemap(Daylight);
            Sky->GetLightComponent()->SetIntensity(3.0f);
            Sky->GetLightComponent()->SetLightColor(FLinearColor::White);
            Sky->GetLightComponent()->bLowerHemisphereIsBlack=false;
            AStaticMeshActor* Dome=World->SpawnActor<AStaticMeshActor>();
            Dome->SetActorLabel(TEXT("CX_White_Overcast_SkyDome"));
            Dome->Tags.Add(TEXT("CXCitySkyDome"));
            Dome->SetFolderPath(TEXT("CX City/Lighting"));
            Dome->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
            Dome->GetStaticMeshComponent()->SetMaterial(0,Materials.FindChecked(TEXT("Sky")));
            Dome->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("NoCollision"));
            Dome->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Dome->GetStaticMeshComponent()->SetGenerateOverlapEvents(false);
            Dome->SetActorEnableCollision(false);
            Dome->GetStaticMeshComponent()->SetCastShadow(false);
            Dome->GetStaticMeshComponent()->SetAffectDistanceFieldLighting(false);
            Dome->GetStaticMeshComponent()->SetVisibleInRayTracing(false);
            Dome->SetActorScale3D(FVector(1800));
            AExponentialHeightFog* Fog=World->SpawnActor<AExponentialHeightFog>();
            Fog->SetActorLabel(TEXT("CX_Distant_City_Haze"));
            Fog->SetFolderPath(TEXT("CX City/Lighting"));
            Fog->GetComponent()->SetFogDensity(.008f);
            Fog->GetComponent()->SetFogHeightFalloff(.16f);
            Fog->GetComponent()->SetFogInscatteringColor(Hex(TEXT("F5F2EC")));
            Fog->GetComponent()->SetStartDistance(8500);
            Fog->GetComponent()->SetFogMaxOpacity(.42f);
            APostProcessVolume* Post=World->SpawnActor<APostProcessVolume>();
            Post->SetActorLabel(TEXT("CX_Editorial_City_Grade"));
            Post->SetFolderPath(TEXT("CX City/Lighting"));
            Post->bUnbound=true;
            Post->Settings.bOverride_AutoExposureMethod=true;
            Post->Settings.AutoExposureMethod=AEM_Histogram;
            Post->Settings.bOverride_AutoExposureMinBrightness=true;
            Post->Settings.bOverride_AutoExposureMaxBrightness=true;
            Post->Settings.AutoExposureMinBrightness=0.f;
            Post->Settings.AutoExposureMaxBrightness=0.f;
            Post->Settings.bOverride_AutoExposureBias=true;
            Post->Settings.AutoExposureBias=.15f;
            Post->Settings.bOverride_BloomIntensity=true;
            Post->Settings.BloomIntensity=.17f;
            Post->Settings.bOverride_VignetteIntensity=true;
            Post->Settings.VignetteIntensity=.16f;
            Post->Settings.bOverride_ColorSaturation=true;
            Post->Settings.ColorSaturation=FVector4(1.02f,.96f,.95f,1.f);
            World->GetWorldSettings()->bForceNoPrecomputedLighting=true;
            Sky->GetLightComponent()->RecaptureSky();
            return true;
        }
    };

    void InspectOriginalCoinSetup(UBlueprint* Coin, const TSharedRef<FJsonObject>& Report)
    {
        TSharedRef<FJsonObject> Original=MakeShared<FJsonObject>();
        UObject* CDO=Coin->GeneratedClass->GetDefaultObject();
        UStaticMeshComponent* Mesh=Cast<UStaticMeshComponent>(ReadObject(CDO,TEXT("ItemMesh")));
        USphereComponent* Sphere=Cast<USphereComponent>(ReadObject(CDO,TEXT("OverlapSphere")));
        Original->SetBoolField(TEXT("coin_mesh_assigned"),Mesh&&Mesh->GetStaticMesh());
        Original->SetStringField(TEXT("coin_mesh"),Mesh&&Mesh->GetStaticMesh()?Mesh->GetStaticMesh()->GetPathName():TEXT(""));
        Original->SetStringField(TEXT("coin_mesh_scale"),Mesh?Mesh->GetRelativeScale3D().ToString():TEXT(""));
        Original->SetBoolField(TEXT("coin_mesh_visible"),Mesh&&Mesh->IsVisible()&&!Mesh->bHiddenInGame);
        Original->SetNumberField(TEXT("coin_overlap_radius"),Sphere?Sphere->GetUnscaledSphereRadius():0);
        Original->SetBoolField(TEXT("coin_overlap_enabled"),Sphere&&Sphere->GetGenerateOverlapEvents()&&Sphere->GetCollisionEnabled()==ECollisionEnabled::QueryOnly);
        UWorld* Old=UEditorLoadingAndSavingUtils::LoadMap(FPackageName::LongPackageNameToFilename(TEXT("/Game/VehicleTemplate/Maps/VehicleAdvExampleMap"),FPackageName::GetMapPackageExtension()));
        TArray<TSharedPtr<FJsonValue>> Items;
        if(Old)
        {
            for(TActorIterator<AActor> It(Old);It;++It)
            {
                UClass* Class=Cast<UClass>(ReadObject(*It,TEXT("ItemClass")));
                if(!Class||Class!=Coin->GeneratedClass) { continue; }
                TSharedRef<FJsonObject> Item=MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("name"),It->GetActorLabel());
                Item->SetStringField(TEXT("class"),Class->GetPathName());
                Item->SetStringField(TEXT("location"),It->GetActorLocation().ToString());
                Item->SetStringField(TEXT("scale"),It->GetActorScale3D().ToString());
                FHitResult Ground;
                const FVector P=It->GetActorLocation();
                const bool bGround=Old->LineTraceSingleByObjectType(Ground,P+FVector(0,0,800),P-FVector(0,0,2000),FCollisionObjectQueryParams(ECC_WorldStatic));
                Item->SetBoolField(TEXT("ground_trace_hit"),bGround);
                if(bGround) { Item->SetNumberField(TEXT("height_above_static_ground_cm"),P.Z-Ground.ImpactPoint.Z); }
                double NearestStart=TNumericLimits<double>::Max();
                for(TActorIterator<APlayerStart> Start(Old);Start;++Start) { NearestStart=FMath::Min(NearestStart,FVector::Dist2D(P,Start->GetActorLocation())); }
                if(NearestStart<TNumericLimits<double>::Max()) { Item->SetNumberField(TEXT("nearest_player_start_distance_cm"),NearestStart); }
                Items.Add(MakeShared<FJsonValueObject>(Item));
            }
        }
        Original->SetBoolField(TEXT("template_map_loaded"),Old!=nullptr);
        Original->SetNumberField(TEXT("coin_spawner_count"),Items.Num());
        Original->SetArrayField(TEXT("coin_spawners"),Items);
        Original->SetStringField(TEXT("diagnosis_scope"),TEXT("Saved assets only; no original-map gameplay failure reproduced here."));
        Report->SetObjectField(TEXT("original_coin_setup"),Original);
    }

    bool ImportCover(const TSharedRef<FJsonObject>& Report)
    {
        const FString File=FPaths::Combine(FPaths::ProjectDir(),TEXT("output/art/CX_CityArena_Cover.png"));
        if (!FPaths::FileExists(File)) { Report->SetStringField(TEXT("cover_status"),TEXT("pending_source_png")); return true; }
        const FString Path(TEXT("/Game/CX/Art/T_CXCover"));
        UTexture2D* Texture=LoadObject<UTexture2D>(nullptr,*ObjectPath(Path),nullptr,LOAD_NoWarn|LOAD_Quiet);
        if(!Texture)
        {
            TArray<uint8> Data;
            if(!FFileHelper::LoadFileToArray(Data,*File)) { return false; }
            UTextureFactory* Factory=NewObject<UTextureFactory>();
            Factory->SuppressImportOverwriteDialog(true);
            Factory->LODGroup=TEXTUREGROUP_UI;
            UPackage* Package=CreatePackage(*Path);
            const uint8* Buffer=Data.GetData();
            Texture=Cast<UTexture2D>(Factory->FactoryCreateBinary(UTexture2D::StaticClass(),Package,TEXT("T_CXCover"),RF_Public|RF_Standalone,
                nullptr,TEXT("png"),Buffer,Buffer+Data.Num(),GWarn));
            if(!Texture) { return false; }
            Texture->LODGroup=TEXTUREGROUP_UI;
            Texture->MipGenSettings=TMGS_NoMipmaps;
            Texture->NeverStream=true;
            Texture->SRGB=true;
            Texture->PostEditChange();
            FAssetRegistryModule::AssetCreated(Texture);
        }
        const bool bSaved=SaveAsset(Texture);
        Report->SetStringField(TEXT("cover_status"),bSaved?TEXT("imported"):TEXT("save_failed"));
        Report->SetStringField(TEXT("cover_asset"),Texture->GetPathName());
        Report->SetNumberField(TEXT("cover_width"),Texture->GetSizeX());
        Report->SetNumberField(TEXT("cover_height"),Texture->GetSizeY());
        return bSaved;
    }

    bool Capture(UWorld* World,const FVector& Position,const FVector& Target,const FString& Filename)
    {
        if (!FApp::CanEverRender()) { return false; }
        FAssetCompilingManager::Get().FinishAllCompilation();
        if(GShaderCompilingManager) { GShaderCompilingManager->FinishAllCompilation(); }
        World->UpdateWorldComponents(true,false);
        USkyLightComponent::UpdateSkyCaptureContents(World);
        FlushRenderingCommands();
        ASceneCapture2D* Camera=World->SpawnActor<ASceneCapture2D>(Position,(Target-Position).Rotation());
        UTextureRenderTarget2D* TargetTexture=NewObject<UTextureRenderTarget2D>(Camera);
        TargetTexture->RenderTargetFormat=RTF_RGBA8;
        TargetTexture->InitAutoFormat(1600,900);
        TargetTexture->UpdateResourceImmediate(true);
        USceneCaptureComponent2D* Scene=Camera->GetCaptureComponent2D();
        Scene->TextureTarget=TargetTexture;
        Scene->CaptureSource=SCS_FinalColorLDR;
        Scene->FOVAngle=75;
        Scene->bCaptureEveryFrame=false;
        Scene->bCaptureOnMovement=false;
        Scene->PostProcessSettings.bOverride_AutoExposureMinBrightness=true;
        Scene->PostProcessSettings.bOverride_AutoExposureMaxBrightness=true;
        Scene->PostProcessSettings.AutoExposureMinBrightness=0;
        Scene->PostProcessSettings.AutoExposureMaxBrightness=0;
        for(int32 Frame=0;Frame<8;++Frame)
        {
            World->Tick(LEVELTICK_All,1.f/30.f);
            Scene->CaptureScene();
            FlushRenderingCommands();
        }
        TArray<FColor> Pixels;
        const bool bRead=TargetTexture->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        int32 NonBlack=0;
        uint8 Minimum=255,Maximum=0;
        for(const FColor& Pixel:Pixels)
        {
            const uint8 Brightest=FMath::Max3(Pixel.R,Pixel.G,Pixel.B);
            NonBlack+=Brightest>8?1:0;
            Minimum=FMath::Min(Minimum,Brightest);
            Maximum=FMath::Max(Maximum,Brightest);
        }
        const bool bHasScene=bRead&&Pixels.Num()>0&&NonBlack>Pixels.Num()/50&&Maximum-Minimum>15;
        UE_LOG(LogCXPresentation,Display,TEXT("CX_CAPTURE_PIXELS file=%s nonblack=%d/%d range=%d..%d valid=%d"),*Filename,NonBlack,Pixels.Num(),Minimum,Maximum,bHasScene);
        TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*Filename));
        const bool bSaved=Writer&&FImageUtils::ExportRenderTarget2DAsPNG(TargetTexture,*Writer);
        if(Writer) { Writer->Close(); }
        Camera->Destroy();
        return bSaved&&bHasScene;
    }

    bool VerifySpawnClearance(UWorld* World,const TSharedRef<FJsonObject>& Report)
    {
        const AGameModeBase* Mode=World->GetWorldSettings()->DefaultGameMode
            ?Cast<AGameModeBase>(World->GetWorldSettings()->DefaultGameMode->GetDefaultObject()):nullptr;
        if(!Mode||!Mode->DefaultPawnClass) { return false; }
        TArray<TSharedPtr<FJsonValue>> Results;
        int32 Clear=0,Tested=0;
        for(TActorIterator<APlayerStart> It(World);It;++It)
        {
            ++Tested;
            TSharedRef<FJsonObject> Entry=MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("start"),It->GetActorLabel());
            Entry->SetStringField(TEXT("location"),It->GetActorLocation().ToString());
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
            Params.ObjectFlags|=RF_Transient;
            Params.bTemporaryEditorActor=true;
            FVector Adjustment=FVector::ZeroVector;
            Entry->SetBoolField(TEXT("encroaches_blocking_geometry"),World->EncroachingBlockingGeometry(
                Mode->DefaultPawnClass->GetDefaultObject<AActor>(),It->GetActorLocation(),It->GetActorRotation(),&Adjustment));
            Entry->SetStringField(TEXT("proposed_adjustment"),Adjustment.ToString());
            APawn* Probe=World->SpawnActor<APawn>(Mode->DefaultPawnClass,It->GetActorTransform(),Params);
            const bool bSpawned=IsValid(Probe);
            Entry->SetBoolField(TEXT("default_pawn_spawned_without_collision_override"),bSpawned);
            if(!bSpawned)
            {
                TArray<FOverlapResult> Overlaps;
                World->OverlapMultiByObjectType(Overlaps,It->GetActorLocation(),It->GetActorQuat(),
                    FCollisionObjectQueryParams(ECC_WorldStatic),FCollisionShape::MakeBox(FVector(300,150,150)));
                TArray<TSharedPtr<FJsonValue>> Blockers;
                for(const FOverlapResult& Overlap:Overlaps)
                {
                    Blockers.Add(MakeShared<FJsonValueString>(GetNameSafe(Overlap.GetActor())+TEXT("/")+GetNameSafe(Overlap.GetComponent())));
                }
                Entry->SetArrayField(TEXT("nearby_static_blockers"),Blockers);
            }
            if(Probe)
            {
                Entry->SetStringField(TEXT("pawn_bounds"),Probe->GetComponentsBoundingBox().ToString());
                Probe->Destroy();
                ++Clear;
            }
            Results.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Report->SetNumberField(TEXT("spawn_clearance_tested"),Tested);
        Report->SetNumberField(TEXT("spawn_clearance_passed"),Clear);
        Report->SetBoolField(TEXT("spawn_probe_world_actors_initialized"),World->AreActorsInitialized());
        Report->SetBoolField(TEXT("spawn_clearance_is_runtime_proof"),false);
        Report->SetStringField(TEXT("spawn_probe_scope"),TEXT("Editor-world shape/asset probe only; real post-initialization collision requires live-server diagnostics."));
        Report->SetArrayField(TEXT("spawn_clearance"),Results);
        return Tested>=8&&Clear==Tested;
    }

    bool Inspect(UWorld* World,const TSharedRef<FJsonObject>& Report)
    {
        int32 Coins=0,Speeds=0,Starts=0,Instances=0,Batches=0;
        TArray<TSharedPtr<FJsonValue>> SpawnLocations;
        bool bCorrectClasses=true,bStartsClear=true,bSkyCollisionDisabled=false,bMaterialUsage=true;
        TSharedRef<FJsonObject> MaterialUsage=MakeShared<FJsonObject>();
        for(TActorIterator<AActor> It(World);It;++It)
        {
            if(It->ActorHasTag(TEXT("CXCitySkyDome")))
            {
                AStaticMeshActor* Dome=Cast<AStaticMeshActor>(*It);
                UStaticMeshComponent* Mesh=Dome?Dome->GetStaticMeshComponent():nullptr;
                bSkyCollisionDisabled=Mesh&&!Dome->GetActorEnableCollision()&&
                    Mesh->GetCollisionProfileName()==TEXT("NoCollision")&&
                    Mesh->GetCollisionEnabled()==ECollisionEnabled::NoCollision&&!Mesh->GetGenerateOverlapEvents();
                Report->SetStringField(TEXT("sky_actor"),It->GetPathName());
                Report->SetBoolField(TEXT("sky_actor_collision_enabled"),Dome&&Dome->GetActorEnableCollision());
                Report->SetStringField(TEXT("sky_collision_profile"),Mesh?Mesh->GetCollisionProfileName().ToString():TEXT("missing"));
                Report->SetNumberField(TEXT("sky_collision_enabled"),Mesh?static_cast<int32>(Mesh->GetCollisionEnabled()):-1);
                Report->SetBoolField(TEXT("sky_generates_overlap_events"),Mesh&&Mesh->GetGenerateOverlapEvents());
            }
            if(It->IsA<APlayerStart>())
            {
                ++Starts;
                bStartsClear &= It->GetActorLocation().Z>=120.f&&FMath::Abs(It->GetActorLocation().X)<11500&&FMath::Abs(It->GetActorLocation().Y)<11500;
            }
            if(It->GetActorLabel().StartsWith(TEXT("CX_CoinSpawner_"))||It->GetActorLabel().StartsWith(TEXT("CX_SpeedSpawner_")))
            {
                UClass* Class=Cast<UClass>(ReadObject(*It,TEXT("ItemClass")));
                const bool bCoin=It->GetActorLabel().StartsWith(TEXT("CX_CoinSpawner_"));
                Coins+=bCoin?1:0;
                Speeds+=bCoin?0:1;
                bCorrectClasses &= Class&&Class->GetName()==(bCoin?TEXT("BP_CXCoinItem_C"):TEXT("BP_CXSpeedBuffItem_C"));
                TSharedRef<FJsonObject> Location=MakeShared<FJsonObject>();
                Location->SetStringField(TEXT("name"),It->GetActorLabel());
                Location->SetStringField(TEXT("class"),Class?Class->GetPathName():TEXT(""));
                Location->SetNumberField(TEXT("x"),It->GetActorLocation().X);
                Location->SetNumberField(TEXT("y"),It->GetActorLocation().Y);
                Location->SetNumberField(TEXT("z"),It->GetActorLocation().Z);
                SpawnLocations.Add(MakeShared<FJsonValueObject>(Location));
            }
            TArray<UHierarchicalInstancedStaticMeshComponent*> Meshes;
            It->GetComponents(Meshes);
            for(auto* Mesh:Meshes)
            {
                Instances+=Mesh->GetInstanceCount();
                ++Batches;
                UMaterial* Material=Mesh->GetMaterial(0)?Mesh->GetMaterial(0)->GetMaterial():nullptr;
                const bool bUsage=Material&&Material->bUsedWithInstancedStaticMeshes;
                MaterialUsage->SetBoolField(Material?Material->GetPathName():TEXT("missing_material"),bUsage);
                bMaterialUsage &= bUsage;
            }
        }
        UClass* Mode=World->GetWorldSettings()->DefaultGameMode;
        const bool bMode=Mode&&Mode->GetName()==TEXT("BP_VehicleAdvGameMode_C");
        Report->SetStringField(TEXT("arena_map"),World->GetOutermost()->GetName());
        Report->SetStringField(TEXT("arena_game_mode"),Mode?Mode->GetPathName():TEXT(""));
        Report->SetNumberField(TEXT("coin_spawners"),Coins);
        Report->SetNumberField(TEXT("speed_spawners"),Speeds);
        Report->SetNumberField(TEXT("player_starts"),Starts);
        Report->SetNumberField(TEXT("instanced_parts"),Instances);
        Report->SetNumberField(TEXT("instancing_batches"),Batches);
        Report->SetArrayField(TEXT("spawners"),SpawnLocations);
        Report->SetBoolField(TEXT("spawner_classes_correct"),bCorrectClasses);
        Report->SetBoolField(TEXT("player_starts_inside_drivable_boundary"),bStartsClear);
        Report->SetBoolField(TEXT("original_game_mode_preserved"),bMode);
        Report->SetBoolField(TEXT("sky_collision_disabled_at_actor_and_component"),bSkyCollisionDisabled);
        Report->SetObjectField(TEXT("instanced_material_usage"),MaterialUsage);
        Report->SetBoolField(TEXT("all_instanced_material_usage_flags_saved"),bMaterialUsage);
        return Coins>=24&&Speeds>=4&&Starts>=8&&Instances>2000&&bCorrectClasses&&bStartsClear&&bMode&&bSkyCollisionDisabled&&bMaterialUsage;
    }
}

UCXPresentationSetupCommandlet::UCXPresentationSetupCommandlet()
{
    IsClient=false;
    IsServer=false;
    IsEditor=true;
    LogToConsole=true;
}

int32 UCXPresentationSetupCommandlet::Main(const FString& Params)
{
    if(FParse::Param(*Params,TEXT("BumperProbe"))) { return RunCXBumperProbe(Params); }
    const bool bVerify=FParse::Param(*Params,TEXT("VerifyOnly"));
    const bool bRender=FParse::Param(*Params,TEXT("CapturePreview"));
    const bool bCoverOnly=FParse::Param(*Params,TEXT("CoverOnly"));
    const bool bRefresh=FParse::Param(*Params,TEXT("RefreshPresentation"));
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("verify_only"),bVerify);
    Report->SetBoolField(TEXT("runtime_test"),false);
    const FString Proof=FPaths::Combine(FPaths::ProjectDir(),bRefresh?TEXT("tmp/unreal/flow-refresh-20261027/"):ProofRoot);
    IFileManager::Get().MakeDirectory(*Proof,true);
    auto Finish=[&](bool bSuccess,const FString& Error)
    {
        Report->SetBoolField(TEXT("success"),bSuccess);
        Report->SetStringField(TEXT("error"),Error);
        FString Json;
        TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json);
        FJsonSerializer::Serialize(Report,Writer);
        const FString File=FPaths::Combine(Proof,bVerify?TEXT("saved_assets_verification.json"):bCoverOnly?TEXT("cover_import.json"):TEXT("asset_authoring.json"));
        FFileHelper::SaveStringToFile(Json,*File,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogCXPresentation,Display,TEXT("CX_PRESENTATION_SUCCESS=%s %s"),bSuccess?TEXT("true"):TEXT("false"),*Error);
        return bSuccess?0:1;
    };
    if(!bVerify&&!FParse::Param(*Params,TEXT("AssetsBackupConfirmed")))
    {
        return Finish(false,TEXT("AssetsBackupConfirmed is required before authoring."));
    }
    if(bRefresh)
    {
        UWorld* World=UEditorLoadingAndSavingUtils::LoadMap(FPackageName::LongPackageNameToFilename(ArenaPath,FPackageName::GetMapPackageExtension()));
        if(!World||World->HasBegunPlay()) { return Finish(false,TEXT("Saved editor arena missing.")); }
        int32 Coins=0,Signs=0;
        for(TActorIterator<AActor> It(World);It;++It)
        {
            if(It->GetActorLabel().StartsWith(TEXT("CX_CoinSpawner_")))
            {
                FFloatProperty* Interval=FindFProperty<FFloatProperty>(It->GetClass(),TEXT("SpawnInterval"));
                if(!Interval) { return Finish(false,TEXT("Spawner interval missing.")); }
                if(!bVerify) { Interval->SetPropertyValue_InContainer(*It,8.f); }
                if(!FMath::IsNearlyEqual(Interval->GetPropertyValue_InContainer(*It),8.f)) { return Finish(false,TEXT("Coin respawn interval mismatch.")); }
                ++Coins;
            }
            TArray<UTextRenderComponent*> Texts;
            It->GetComponents(Texts);
            for(UTextRenderComponent* Text:Texts)
            {
                const FString Label=Text->Text.ToString();
                if(!bVerify&&(Label==TEXT("CX / CITY")||Label==TEXT("CX CITY ARENA")||Label==TEXT("CX  /  CITY ARENA")))
                {
                    Text->SetText(FText::FromString(TEXT("Crash X Coins")));
                }
                if(Text->Text.ToString()==TEXT("Crash X Coins")) { ++Signs; }
                if(bVerify&&Label.Contains(TEXT("CITY ARENA"))) { return Finish(false,TEXT("Old arena title remains.")); }
            }
        }
        if(!bVerify)
        {
            TArray<ACXRoadRecoveryArea*> OldAreas;
            for(TActorIterator<ACXRoadRecoveryArea> It(World);It;++It) { OldAreas.Add(*It); }
            for(ACXRoadRecoveryArea* Area:OldAreas) { World->DestroyActor(Area); }
            int32 Index=0;
            for(float Yaw:{0.f,90.f}) for(float Side:{-10000.f,0.f,10000.f})
            {
                const FVector Center=Yaw==0?FVector(0,Side,0):FVector(Side,0,0);
                ACXRoadRecoveryArea* Area=World->SpawnActor<ACXRoadRecoveryArea>(Center,FRotator(0,Yaw,0));
                if(!Area) { return Finish(false,TEXT("Road recovery area creation failed.")); }
                Area->ConfigureRoad(FVector(11500,1500,300));
                Area->Tags.AddUnique(TEXT("CXRoadRecovery"));
                Area->SetActorLabel(FString::Printf(TEXT("CX_RoadRecovery_%02d"),++Index));
                Area->SetFolderPath(TEXT("CX City/Road Recovery"));
            }
        }
        int32 Roads=0;
        for(TActorIterator<ACXRoadRecoveryArea> It(World);It;++It) { ++Roads; }
        Report->SetNumberField(TEXT("coin_spawners_8_seconds"),Coins);
        Report->SetNumberField(TEXT("branded_signs"),Signs);
        Report->SetNumberField(TEXT("road_recovery_areas"),Roads);
        if(Coins!=30||Signs<2||Roads!=6) { return Finish(false,TEXT("Refresh counts mismatch.")); }
        if(!bVerify&&!UEditorLoadingAndSavingUtils::SaveMap(World,ArenaPath)) { return Finish(false,TEXT("Updated arena save failed.")); }
        const TArray<FString> Vehicles{
            TEXT("/Game/VehicleTemplate/Blueprints/SportsCar/BP_SportsCar_Pawn.BP_SportsCar_Pawn")};
        TArray<TSharedPtr<FJsonValue>> VehicleData;
        for(const FString& Path:Vehicles)
        {
            UBlueprint* BP=LoadObject<UBlueprint>(nullptr,*Path);
            if(!BP) { return Finish(false,FString::Printf(TEXT("Vehicle blueprint missing: %s"),*Path)); }
            if(!bVerify)
            {
                FKismetEditorUtilities::CompileBlueprint(BP);
                if(BP->Status==BS_Error||!SaveAsset(BP)) { return Finish(false,TEXT("Vehicle blueprint compile/save failed.")); }
            }
            AWheeledVehiclePawn* Vehicle=Cast<AWheeledVehiclePawn>(BP->GeneratedClass->GetDefaultObject());
            if(!Vehicle||Vehicle->GetVehicleMovementComponent()->GetClass()->GetName()!=TEXT("CXVehicleMovementComponent")) { return Finish(false,TEXT("Vehicle blueprint still uses the previous movement class.")); }
            TSharedRef<FJsonObject> Data=MakeShared<FJsonObject>();
            Data->SetStringField(TEXT("blueprint"),Path);
            Data->SetStringField(TEXT("movement_class"),Vehicle->GetVehicleMovementComponent()->GetClass()->GetPathName());
            Data->SetStringField(TEXT("mesh_local_bounds"),Vehicle->GetMesh()->CalcBounds(FTransform::Identity).GetBox().ToString());
            TArray<UCameraComponent*> Cameras;
            Vehicle->GetComponents(Cameras);
            for(UCameraComponent* Camera:Cameras)
            {
                Data->SetStringField(Camera->GetName()+TEXT("_relative_rotation"),Camera->GetRelativeRotation().ToString());
                if(Camera->GetAttachParent()) { Data->SetStringField(Camera->GetName()+TEXT("_parent_rotation"),Camera->GetAttachParent()->GetRelativeRotation().ToString()); }
            }
            VehicleData.Add(MakeShared<FJsonValueObject>(Data));
        }
        Report->SetArrayField(TEXT("vehicle_defaults"),VehicleData);
        Report->SetBoolField(TEXT("existing_city_preserved"),true);
        return Finish(true,TEXT(""));
    }
    if(bCoverOnly) { return Finish(ImportCover(Report),TEXT("")); }
    if(bVerify)
    {
        UWorld* World=UEditorLoadingAndSavingUtils::LoadMap(FPackageName::LongPackageNameToFilename(ArenaPath,FPackageName::GetMapPackageExtension()));
        if(!World||!Inspect(World,Report)) { return Finish(false,TEXT("Saved arena readback failed.")); }
        if(!VerifySpawnClearance(World,Report)) { return Finish(false,TEXT("Saved PlayerStart collision clearance failed for the actual default pawn.")); }
        if(bRender)
        {
            const bool bStreet=Capture(World,FVector(-10600,-8700,430),FVector(-5000,-10000,1100),FPaths::Combine(Proof,TEXT("city_street_preview.png")));
            const bool bAerial=Capture(World,FVector(-21800,-24000,22000),FVector(0,0,800),FPaths::Combine(Proof,TEXT("city_aerial_preview.png")));
            Report->SetBoolField(TEXT("street_preview_rendered"),bStreet);
            Report->SetBoolField(TEXT("aerial_preview_rendered"),bAerial);
            if(!bStreet||!bAerial) { return Finish(false,TEXT("SceneCapture PNGs failed pixel-content validation; use an actual game viewport capture.")); }
        }
        UWorld* Menu=UEditorLoadingAndSavingUtils::LoadMap(FPackageName::LongPackageNameToFilename(MenuPath,FPackageName::GetMapPackageExtension()));
        UClass* MenuMode=Menu?Menu->GetWorldSettings()->DefaultGameMode:nullptr;
        const bool bMenu=MenuMode&&MenuMode->GetPathName()==TEXT("/Script/CX.CXMainMenuGameMode");
        Report->SetBoolField(TEXT("menu_native_mode_assigned"),bMenu);
        return Finish(bMenu,bMenu?TEXT(""):TEXT("Saved menu native GameMode missing."));
    }

    UBlueprint* Mode=LoadObject<UBlueprint>(nullptr,TEXT("/Game/VehicleTemplate/Blueprints/BP_VehicleAdvGameMode.BP_VehicleAdvGameMode"));
    UBlueprint* Coin=LoadObject<UBlueprint>(nullptr,TEXT("/Game/VehicleTemplate/Blueprints/Item/BP_CXCoinItem.BP_CXCoinItem"));
    UBlueprint* Speed=LoadObject<UBlueprint>(nullptr,TEXT("/Game/VehicleTemplate/Blueprints/Item/BP_CXSpeedBuffItem.BP_CXSpeedBuffItem"));
    UBlueprint* Spawner=LoadObject<UBlueprint>(nullptr,TEXT("/Game/VehicleTemplate/Blueprints/Item/BP_CXItemSpawner.BP_CXItemSpawner"));
    UClass* MenuMode=LoadClass<AGameModeBase>(nullptr,TEXT("/Script/CX.CXMainMenuGameMode"));
    if(!Mode||!Mode->GeneratedClass||!Coin||!Coin->GeneratedClass||!Speed||!Speed->GeneratedClass||!Spawner||!Spawner->GeneratedClass||!MenuMode)
    {
        return Finish(false,TEXT("Required existing item/gameplay blueprints or native menu mode missing."));
    }
    InspectOriginalCoinSetup(Coin,Report);
    UWorld* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if(!World||World->HasBegunPlay()) { return Finish(false,TEXT("Could not create a safe editor-only world.")); }
    World->GetWorldSettings()->DefaultGameMode=Mode->GeneratedClass;
    FCityBuilder City;
    if(!City.Init(World)) { return Finish(false,TEXT("City mesh/material initialization failed.")); }
    City.RoadDress();
    City.Architecture();
    if(!City.Lighting()) { return Finish(false,TEXT("Required daylight HDRI cubemap is missing.")); }
    Report->SetStringField(TEXT("skylight_fill"),TEXT("Specified HDRI_Epic_Courtyard_Daylight cubemap, intensity3; fixed exposure EV100 0; key light10."));
    const TArray<FVector> Starts{
        {-7800,-10400,500},{-7800,-9600,500},{-6100,-10400,500},{-6100,-9600,500},
        {7800,10400,500},{7800,9600,500},{6100,10400,500},{6100,9600,500}
    };
    for(int32 I=0;I<Starts.Num();++I)
    {
        APlayerStart* Start=World->SpawnActor<APlayerStart>(Starts[I],FRotator(0,I<4?0:180,0));
        Start->SetActorLabel(FString::Printf(TEXT("CX_Start_%02d"),I+1));
        Start->SetFolderPath(TEXT("CX City/Player Starts"));
    }
    TArray<FVector> CoinPoints;
    for(float Side:{-1.f,1.f}) for(float Along:{-8500.f,-4300.f,0.f,4300.f,8500.f})
    {
        CoinPoints.Add(FVector(Along,Side*10000,130));
        CoinPoints.Add(FVector(Side*10000,Along,130));
    }
    for(float Along:{-6800.f,-3400.f,3400.f,6800.f})
    {
        CoinPoints.Add(FVector(Along,0,130));
        CoinPoints.Add(FVector(0,Along,130));
    }
    CoinPoints.Add(FVector(-750,-750,130));
    CoinPoints.Add(FVector(750,750,130));
    for(int32 I=0;I<CoinPoints.Num();++I)
    {
        AActor* Actor=World->SpawnActor<AActor>(Spawner->GeneratedClass,CoinPoints[I],FRotator(0,0,90));
        Actor->SetActorScale3D(FVector(1.6f));
        Actor->SetActorLabel(FString::Printf(TEXT("CX_CoinSpawner_%02d"),I+1));
        Actor->SetFolderPath(TEXT("CX City/Pickups"));
        if(!SetObject(Actor,TEXT("ItemClass"),Coin->GeneratedClass)) { return Finish(false,TEXT("Coin ItemClass assignment failed.")); }
        City.Box(FVector(CoinPoints[I].X,CoinPoints[I].Y,2.5),FVector(200,200,2),TEXT("Gold"));
    }
    const TArray<FVector> SpeedPoints{{-10000,-6000,95},{10000,6000,95},{-6000,10000,95},{6000,-10000,95}};
    for(int32 I=0;I<SpeedPoints.Num();++I)
    {
        AActor* Actor=World->SpawnActor<AActor>(Spawner->GeneratedClass,SpeedPoints[I],FRotator::ZeroRotator);
        Actor->SetActorLabel(FString::Printf(TEXT("CX_SpeedSpawner_%02d"),I+1));
        Actor->SetFolderPath(TEXT("CX City/Pickups"));
        if(!SetObject(Actor,TEXT("ItemClass"),Speed->GeneratedClass)) { return Finish(false,TEXT("Speed ItemClass assignment failed.")); }
    }
    ACameraActor* Preview=World->SpawnActor<ACameraActor>(FVector(-10600,-8700,430),FRotator(-6,-15,0));
    Preview->SetActorLabel(TEXT("CX_City_Preview_Camera"));
    Preview->Tags.Add(TEXT("CXPreviewStreet"));
    Preview->SetActorRotation((FVector(-5000,-10000,1100)-Preview->GetActorLocation()).Rotation());
    Preview->SetFolderPath(TEXT("CX City/Cameras"));
    Preview->GetCameraComponent()->FieldOfView=75;
    ACameraActor* Aerial=World->SpawnActor<ACameraActor>(FVector(-21800,-24000,22000),
        (FVector(0,0,800)-FVector(-21800,-24000,22000)).Rotation());
    Aerial->SetActorLabel(TEXT("CX_City_Aerial_Camera"));
    Aerial->SetFolderPath(TEXT("CX City/Cameras"));
    Aerial->Tags.Add(TEXT("CXPreviewAerial"));
    Aerial->GetCameraComponent()->FieldOfView=75;
    Report->SetNumberField(TEXT("city_width_cm"),ArenaExtent.X*2);
    Report->SetNumberField(TEXT("buildings"),City.Buildings);
    Report->SetNumberField(TEXT("window_panels"),City.Windows);
    Report->SetNumberField(TEXT("road_width_cm"),2400);
    Report->SetNumberField(TEXT("signs"),City.Signs);
    for(const auto& Pair:City.Batches) { Pair.Value->BuildTreeIfOutdated(false,true); }
    if(!Inspect(World,Report)||!UEditorLoadingAndSavingUtils::SaveMap(World,ArenaPath))
    {
        return Finish(false,TEXT("City inspection or map save failed."));
    }
    if(bRender)
    {
        Report->SetBoolField(TEXT("street_preview_rendered"),Capture(World,FVector(-10600,-8700,430),FVector(-5000,-10000,1100),FPaths::Combine(Proof,TEXT("city_street_preview.png"))));
    }
    UWorld* Menu=UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if(!Menu) { return Finish(false,TEXT("Menu map creation failed.")); }
    Menu->GetWorldSettings()->DefaultGameMode=MenuMode;
    Menu->GetWorldSettings()->bForceNoPrecomputedLighting=true;
    if(!UEditorLoadingAndSavingUtils::SaveMap(Menu,MenuPath)||!ImportCover(Report))
    {
        return Finish(false,TEXT("Menu map save or cover import failed."));
    }
    return Finish(true,TEXT(""));
}
