// ProceduralWaterPlane.cpp

#include "ProceduralWaterPlane.h"
#include "ProceduralLandmass.h"

#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

AProceduralWaterPlane::AProceduralWaterPlane()
{
    PrimaryActorTick.bCanEverTick = true;

    Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaterMesh"));
    RootComponent = Mesh;

    Mesh->bUseAsyncCooking = true;
}

void AProceduralWaterPlane::OnConstruction(const FTransform& Transform)
{
    RefreshFromLandmass();
}

void AProceduralWaterPlane::BeginPlay()
{
    Super::BeginPlay();

    EnsureMaterialInstance();
}

void AProceduralWaterPlane::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    InternalTime += DeltaSeconds;

    if (WaterMID)
    {
        WaterMID->SetScalarParameterValue(TEXT("WaveTime"), InternalTime);
        WaterMID->SetScalarParameterValue(TEXT("WaveSpeed1"), WaveSpeed1);
        WaterMID->SetScalarParameterValue(TEXT("WaveSpeed2"), WaveSpeed2);
    }
}

void AProceduralWaterPlane::RefreshFromLandmass()
{
    // If we have a landmass linked, auto-sync size and height
    if (LinkedLandmass)
    {
        // Match the plane size to the terrain extents
        PlaneSizeX = (LinkedLandmass->MapWidth - 1) * LinkedLandmass->GridSize;
        PlaneSizeY = (LinkedLandmass->MapHeight - 1) * LinkedLandmass->GridSize;

        // Convert normalized water height (0..1) to world Z
        const float NormalizedWater = LinkedLandmass->GetDefaultWaterHeight01();
        const float WorldWaterOffset = NormalizedWater * LinkedLandmass->HeightMultiplier;

        // Center over the landmass in X/Y, and set Z to the water level
        FVector Center = LinkedLandmass->GetLandmassCenter();
        Center.Z = LinkedLandmass->GetActorLocation().Z + WorldWaterOffset;

        SetActorLocation(Center);
    }

    BuildWaterPlane();
    EnsureMaterialInstance();
}

void AProceduralWaterPlane::EnsureMaterialInstance()
{
    if (!Mesh)
    {
        return;
    }

    if (!WaterMID)
    {
        UMaterialInterface* BaseMat = WaterMaterial ? WaterMaterial : Mesh->GetMaterial(0);
        if (BaseMat)
        {
            WaterMID = UMaterialInstanceDynamic::Create(BaseMat, this);
            Mesh->SetMaterial(0, WaterMID);
        }
    }
}

void AProceduralWaterPlane::BuildWaterPlane()
{
    if (!Mesh)
    {
        return;
    }

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;

    Vertices.SetNum(4);
    Normals.SetNum(4);
    UVs.SetNum(4);
    Colors.SetNum(4);
    Tangents.SetNum(4);

    const float HX = PlaneSizeX * 0.5f;
    const float HY = PlaneSizeY * 0.5f;

    // Local Z = 0, actor location controls actual water height
    Vertices[0] = FVector(-HX, -HY, 0.0f);
    Vertices[1] = FVector(HX, -HY, 0.0f);
    Vertices[2] = FVector(-HX, HY, 0.0f);
    Vertices[3] = FVector(HX, HY, 0.0f);

    UVs[0] = FVector2D(0.0f, 0.0f);
    UVs[1] = FVector2D(1.0f, 0.0f);
    UVs[2] = FVector2D(0.0f, 1.0f);
    UVs[3] = FVector2D(1.0f, 1.0f);

    for (int32 i = 0; i < 4; ++i)
    {
        Normals[i] = FVector::UpVector;
        Colors[i] = FLinearColor::White;
        Tangents[i] = FProcMeshTangent(1.0f, 0.0f, 0.0f);
    }

    Triangles = { 0, 1, 2, 2, 1, 3 };

    Mesh->CreateMeshSection_LinearColor(
        0,
        Vertices,
        Triangles,
        Normals,
        UVs,
        Colors,
        Tangents,
        false   // no collision for water
    );
}
