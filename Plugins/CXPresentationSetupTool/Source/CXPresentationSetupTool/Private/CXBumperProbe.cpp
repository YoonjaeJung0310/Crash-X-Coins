#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"

int32 RunCXBumperProbe(const FString& Params)
{
    FString Proof=FPaths::Combine(FPaths::ProjectDir(),TEXT("tmp/unreal/reverse-brake-bumper-20261027/bumper_before.json"));
    FParse::Value(*Params,TEXT("BumperProof="),Proof);
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>();
    UWorld* World=nullptr;
    bool bAssetsSaved=false;
    auto Vec=[](const FVector& V) { return MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)}); };
    auto Finish=[&](const FString& Error)
    {
        Report->SetBoolField(TEXT("success"),Error.IsEmpty()); Report->SetStringField(TEXT("error"),Error);
        Report->SetBoolField(TEXT("assets_saved"),bAssetsSaved);
        FString Json; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Proof),true);
        const bool bSaved=FFileHelper::SaveStringToFile(Json,*Proof,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        if(World) { World->DestroyWorld(false); }
        UE_LOG(LogTemp,Display,TEXT("CX_BUMPER_PROBE %s %s"),*Proof,*Error);
        return Error.IsEmpty()&&bSaved?0:1;
    };
    const bool bAddQuery=FParse::Param(*Params,TEXT("AddLowerBumperQuery"));
    if(bAddQuery&&!FParse::Param(*Params,TEXT("AssetsBackupConfirmed"))) { return Finish(TEXT("AssetsBackupConfirmed is required before authoring.")); }
    UBlueprint* BP=LoadObject<UBlueprint>(nullptr,TEXT("/Game/VehicleTemplate/Blueprints/SportsCar/BP_SportsCar_Pawn.BP_SportsCar_Pawn"));
    USkeletalMesh* Mesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Vehicles/SportsCar/SKM_SportsCar.SKM_SportsCar"));
    UPhysicsAsset* PA=Mesh?Mesh->GetPhysicsAsset():nullptr;
    FSkeletalMeshModel* Model=Mesh?Mesh->GetImportedModel():nullptr;
    if(!BP||!BP->GeneratedClass||!PA||!Model||Model->LODModels.IsEmpty()) { return Finish(TEXT("Sports BP, physics asset or imported LOD0 missing.")); }
    const int32 RootIndex=Mesh->GetRefSkeleton().FindBoneIndex(TEXT("Root"));
    if(RootIndex==INDEX_NONE) { return Finish(TEXT("Root bone missing.")); }
    auto CaptureBodies=[&]()
    {
    TArray<TSharedPtr<FJsonValue>> Bodies;
    for(USkeletalBodySetup* Body:PA->SkeletalBodySetups)
    {
        TSharedRef<FJsonObject> Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("bone"),Body->BoneName.ToString());
        TArray<TSharedPtr<FJsonValue>> Shapes;
        for(int32 I=0;I<Body->AggGeom.GetElementCount();++I)
        {
            const FKShapeElem* Shape=Body->AggGeom.GetElement(I); TSharedRef<FJsonObject> S=MakeShared<FJsonObject>();
            S->SetStringField(TEXT("name"),Shape->GetName().ToString()); S->SetNumberField(TEXT("collision_enabled"),Shape->GetCollisionEnabled()); S->SetBoolField(TEXT("contribute_to_mass"),Shape->GetContributeToMass());
            S->SetNumberField(TEXT("type"),Shape->GetShapeType()); Shapes.Add(MakeShared<FJsonValueObject>(S));
        }
        Row->SetArrayField(TEXT("shapes"),Shapes);
        if(Body->BoneName==TEXT("Root"))
        {
            TArray<TSharedPtr<FJsonValue>> Vertices;
            for(const FKConvexElem& C:Body->AggGeom.ConvexElems) { for(const FVector& V:C.VertexData) { Vertices.Add(Vec(C.GetTransform().TransformPosition(V))); } }
            Row->SetArrayField(TEXT("convex_vertices"),Vertices);
        }
        Bodies.Add(MakeShared<FJsonValueObject>(Row));
    }
    Report->SetArrayField(TEXT("bodies"),Bodies);
    };
    const FSkeletalMeshLODModel& LOD=Model->LODModels[0]; TArray<FSoftSkinVertex> Vertices; LOD.GetVertices(Vertices);
    TArray<bool> RootVertices; RootVertices.Init(false,Vertices.Num()); FBox RootBounds(ForceInit); FBox LowerBounds(ForceInit);
    for(const FSkelMeshSection& Section:LOD.Sections)
    {
        for(int32 I=0;I<Section.SoftVertices.Num();++I)
        {
            const FSoftSkinVertex& V=Section.SoftVertices[I]; int32 Strongest=0;
            for(int32 J=1;J<MAX_TOTAL_INFLUENCES;++J) { if(V.InfluenceWeights[J]>V.InfluenceWeights[Strongest]) { Strongest=J; } }
            if(Section.BoneMap.IsValidIndex(V.InfluenceBones[Strongest])&&Section.BoneMap[V.InfluenceBones[Strongest]]==RootIndex)
            {
                RootVertices[Section.BaseVertexIndex+I]=true; const FVector P(V.Position); RootBounds+=P; if(P.Z<=30) { LowerBounds+=P; }
            }
        }
    }
    if(!RootBounds.IsValid||!LowerBounds.IsValid) { return Finish(TEXT("Root-weighted body vertices missing.")); }
    Report->SetField(TEXT("root_vertex_min"),Vec(RootBounds.Min)); Report->SetField(TEXT("root_vertex_max"),Vec(RootBounds.Max));
    Report->SetField(TEXT("lower_body_vertex_min"),Vec(LowerBounds.Min)); Report->SetField(TEXT("lower_body_vertex_max"),Vec(LowerBounds.Max));
    if(bAddQuery)
    {
        USkeletalBodySetup* RootBody=nullptr;
        for(USkeletalBodySetup* Body:PA->SkeletalBodySetups) { if(Body->BoneName==TEXT("Root")) { RootBody=Body; } }
        if(!RootBody) { return Finish(TEXT("Root physics body missing.")); }
        const FName QueryName(TEXT("CX_LowerBumperQuery")); FKConvexElem* Existing=nullptr;
        for(FKConvexElem& C:RootBody->AggGeom.ConvexElems) { if(C.GetName()==QueryName) { if(Existing) { return Finish(TEXT("Duplicate lower query hulls.")); } Existing=&C; } }
        if(Existing)
        {
            if(Existing->GetCollisionEnabled()!=ECollisionEnabled::QueryOnly||Existing->GetContributeToMass()) { return Finish(TEXT("Existing lower query hull flags mismatch.")); }
            Report->SetBoolField(TEXT("query_already_present"),true);
            Report->SetNumberField(TEXT("authored_vertex_count"),Existing->VertexData.Num());
            Report->SetField(TEXT("authored_local_min"),Vec(Existing->ElemBox.Min)); Report->SetField(TEXT("authored_local_max"),Vec(Existing->ElemBox.Max));
        }
        else
        {
            FTransform RootToMesh=Mesh->GetRefSkeleton().GetRefBonePose()[RootIndex];
            for(int32 Parent=Mesh->GetRefSkeleton().GetParentIndex(RootIndex);Parent!=INDEX_NONE;Parent=Mesh->GetRefSkeleton().GetParentIndex(Parent)) { RootToMesh*=Mesh->GetRefSkeleton().GetRefBonePose()[Parent]; }
            FBox HullBounds(ForceInit);
            for(const FKConvexElem& C:RootBody->AggGeom.ConvexElems) { for(const FVector& V:C.VertexData) { HullBounds+=RootToMesh.TransformPosition(C.GetTransform().TransformPosition(V)); } }
            if(!HullBounds.IsValid) { return Finish(TEXT("Original Root convex missing.")); }
            FKConvexElem Query; const float Ceiling=HullBounds.Min.Z+1.f;
            for(int32 I=0;I<Vertices.Num();++I) { if(RootVertices[I]&&Vertices[I].Position.Z<=Ceiling) { Query.VertexData.AddUnique(RootToMesh.InverseTransformPosition(FVector(Vertices[I].Position))); } }
            if(Query.VertexData.Num()<4) { return Finish(TEXT("Too few lower-body vertices for a convex hull.")); }
            const FVector A=Query.VertexData[0]; FVector B=A,Normal=FVector::ZeroVector;
            for(const FVector& V:Query.VertexData) { if(FVector::DistSquared(A,V)>FVector::DistSquared(A,B)) { B=V; } }
            for(const FVector& V:Query.VertexData) { const FVector N=FVector::CrossProduct(B-A,V-A); if(N.SizeSquared()>Normal.SizeSquared()) { Normal=N; } }
            Normal.Normalize(); double Depth=0;
            for(const FVector& V:Query.VertexData) { Depth=FMath::Max(Depth,FMath::Abs(FVector::DotProduct(Normal,V-A))); }
            if(Normal.IsNearlyZero()||Depth<.01f) { return Finish(TEXT("Lower-body vertices are coplanar.")); }
            //보이는 차체 하부만 판정에 더하고 접지용 형상과 질량은 유지함
            Query.SetName(QueryName); Query.SetCollisionEnabled(ECollisionEnabled::QueryOnly); Query.SetContributeToMass(false); Query.UpdateElemBox();
            Report->SetNumberField(TEXT("authored_vertex_count"),Query.VertexData.Num()); Report->SetNumberField(TEXT("vertex_ceiling_mesh_z"),Ceiling);
            Report->SetField(TEXT("authored_local_min"),Vec(Query.ElemBox.Min)); Report->SetField(TEXT("authored_local_max"),Vec(Query.ElemBox.Max));
            FKConvexElem& Stored=RootBody->AggGeom.ConvexElems.Add_GetRef(Query);
            Stored.SetCollisionEnabled(ECollisionEnabled::QueryOnly); Stored.SetContributeToMass(false);
            RootBody->InvalidatePhysicsData(); RootBody->CreatePhysicsMeshes();
            if(RootBody->bFailedToCreatePhysicsMeshes) { return Finish(TEXT("Lower-body convex physics cooking failed.")); }
            UPackage* Package=PA->GetOutermost(); Package->MarkPackageDirty(); FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
            const FString Filename=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
            bAssetsSaved=UPackage::SavePackage(Package,PA,*Filename,Args);
            if(!bAssetsSaved) { return Finish(TEXT("Physics asset save failed.")); }
        }
    }
    CaptureBodies();
    //빈 월드에 차만 놓아 도로·벽에 가려지는 경우를 제외함
    UWorld::InitializationValues IV; IV.CreatePhysicsScene(true).ShouldSimulatePhysics(false).EnableTraceCollision(true).CreateNavigation(false).CreateAISystem(false);
    World=UWorld::CreateWorld(EWorldType::Editor,false,TEXT("CXBumperProbeWorld"),nullptr,true,ERHIFeatureLevel::Num,&IV);
    if(!World||!World->GetPhysicsScene()) { return Finish(TEXT("Temporary physics world unavailable.")); }
    FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* Car=World->SpawnActor<AActor>(BP->GeneratedClass,FTransform::Identity,Spawn);
    USkeletalMeshComponent* Component=Car?Car->FindComponentByClass<USkeletalMeshComponent>():nullptr;
    if(!Component) { return Finish(TEXT("Sports skeletal component missing.")); }
    Component->SetSimulatePhysics(false); Component->RecreatePhysicsState(); World->SendAllEndOfFrameUpdates();
    if(!Component->IsPhysicsStateCreated()||!Component->GetBodyInstance(TEXT("Root"))) { return Finish(TEXT("Sports query physics state unavailable.")); }
    TArray<TSharedPtr<FJsonValue>> Rays;
    auto Trace=[&](const FString& Label,const FVector& LocalStart,const FVector& LocalEnd)
    {
        TSharedRef<FJsonObject> Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("label"),Label); Row->SetField(TEXT("local_start"),Vec(LocalStart)); Row->SetField(TEXT("local_end"),Vec(LocalEnd));
        const FTransform TM=Component->GetComponentTransform(); FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(CXBumperProbe),false);
        const bool bHit=World->LineTraceSingleByChannel(Hit,TM.TransformPosition(LocalStart),TM.TransformPosition(LocalEnd),ECC_Visibility,Query);
        Row->SetBoolField(TEXT("physics_hit"),bHit); Row->SetStringField(TEXT("bone"),Hit.BoneName.ToString()); Row->SetStringField(TEXT("actor"),GetNameSafe(Hit.GetActor()));
        if(bHit) { Row->SetField(TEXT("local_impact"),Vec(TM.InverseTransformPosition(Hit.ImpactPoint))); }
        double Closest=TNumericLimits<double>::Max(); FVector VisiblePoint=FVector::ZeroVector;
        for(const FSkelMeshSection& Section:LOD.Sections) { for(uint32 T=0;T<Section.NumTriangles;++T)
        {
            const uint32 K=Section.BaseIndex+T*3; const uint32 A=LOD.IndexBuffer[K],B=LOD.IndexBuffer[K+1],C=LOD.IndexBuffer[K+2];
            if(!RootVertices[A]||!RootVertices[B]||!RootVertices[C]) { continue; }
            FVector Point,Normal;
            if(FMath::SegmentTriangleIntersection(LocalStart,LocalEnd,FVector(Vertices[A].Position),FVector(Vertices[B].Position),FVector(Vertices[C].Position),Point,Normal)&&FVector::DistSquared(LocalStart,Point)<Closest)
            { Closest=FVector::DistSquared(LocalStart,Point); VisiblePoint=Point; }
        } }
        Row->SetBoolField(TEXT("root_render_hit"),Closest<TNumericLimits<double>::Max());
        if(Closest<TNumericLimits<double>::Max()) { Row->SetField(TEXT("root_render_impact"),Vec(VisiblePoint)); }
        Rays.Add(MakeShared<FJsonValueObject>(Row));
    };
    for(float Z:{-8.f,-4.f,-2.f,0.f,2.f,5.f,15.f,30.f}) { for(float Y:{0.f,-50.f,50.f}) { for(float Side:{-1.f,1.f})
    { Trace(FString::Printf(TEXT("body_x%+.0f_y%+.0f_z%+.0f"),Side,Y,Z),FVector(Side*400,Y,Z),FVector(-Side*400,Y,Z)); } } }
    for(USkeletalBodySetup* Body:PA->SkeletalBodySetups) { if(Body->BoneName.ToString().Contains(TEXT("Wheel")))
    {
        const FVector Center=Component->GetBoneLocation(Body->BoneName,EBoneSpaces::ComponentSpace);
        Trace(TEXT("wheel_")+Body->BoneName.ToString(),Center+FVector(0,FMath::Sign(Center.Y)*200,0),Center);
    } }
    Report->SetArrayField(TEXT("rays"),Rays);
    return Finish(TEXT(""));
}
