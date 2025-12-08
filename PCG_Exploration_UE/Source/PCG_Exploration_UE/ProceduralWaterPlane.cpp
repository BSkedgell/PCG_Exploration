//-----------------------------------------------------------------------------
// ProceduralWaterPlane.cpp
//-----------------------------------------------------------------------------

#include "ProceduralWaterPlane.h"
#include "ProceduralLandmass.h"              // For AProceduralLandmass & FTerrainType
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"

AProceduralWaterPlane::AProceduralWaterPlane()
{
    PrimaryActorTick.bCanEverTick = false;

    WaterMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaterMesh"));
    RootComponent = WaterMesh;

    WaterMesh->bUseAsyncCooking = true;
    WaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WaterMesh->CastShadow = false;
}

void AProceduralWaterPlane::BeginPlay()
{
    Super::BeginPlay();
    GenerateWater();
}

#if WITH_EDITOR
void AProceduralWaterPlane::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    GenerateWater();
}

void AProceduralWaterPlane::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    GenerateWater();
}
#endif

void AProceduralWaterPlane::GenerateWater()
{
    if (!bGenerateWaterMesh)
    {
        if (WaterMesh)
        {
            WaterMesh->ClearAllMeshSections();
        }
        return;
    }

    // If linked to a landmass and auto-sync is enabled, pull in its settings
    if (bAutoSyncWithLandmass && LinkedLandmass)
    {
        SyncFromLinkedLandmass();

        // NEW: make sure this actor sits at the same world location as the landmass
        SetActorLocation(LinkedLandmass->GetActorLocation());
    }

    if (MapWidth < 2 || MapHeight < 2)
    {
        if (WaterMesh)
        {
            WaterMesh->ClearAllMeshSections();
        }
        return;
    }

    if (WaterMaterial)
    {
        WaterMesh->SetMaterial(0, WaterMaterial);
    }

    CreateWaterMesh();
}

void AProceduralWaterPlane::SyncFromLinkedLandmass()
{
    if (!LinkedLandmass)
    {
        return;
    }

    // Copy grid dimensions directly
    MapWidth = LinkedLandmass->MapWidth;
    MapHeight = LinkedLandmass->MapHeight;
    GridSize = LinkedLandmass->GridSize;

    // Try to find a "Water" biome on the landmass, otherwise fall back to first biome
    float NormalizedWaterHeight = 0.2f; // safe default

    const TArray<FTerrainType>& Types = LinkedLandmass->TerrainTypes;
    if (Types.Num() > 0)
    {
        const FTerrainType* WaterType = nullptr;

        // Prefer a biome literally named "Water" (case-insensitive)
        for (const FTerrainType& Type : Types)
        {
            if (Type.Name.ToString().Equals(TEXT("Water"), ESearchCase::IgnoreCase))
            {
                WaterType = &Type;
                break;
            }
        }

        if (WaterType)
        {
            NormalizedWaterHeight = WaterType->Height;
        }
        else
        {
            // Otherwise just use the first biome's height
            NormalizedWaterHeight = Types[0].Height;
        }
    }

    // Convert normalized height [0,1] to world Z using the landmass origin + HeightMultiplier
    const float BaseZ = LinkedLandmass->GetActorLocation().Z;
    WaterHeightWorld = BaseZ + NormalizedWaterHeight * LinkedLandmass->HeightMultiplier;
}

void AProceduralWaterPlane::CreateWaterMesh()
{
    if (!WaterMesh)
    {
        return;
    }

    const int32 NumVertsX = MapWidth;
    const int32 NumVertsY = MapHeight;

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;

    const int32 NumVerts = NumVertsX * NumVertsY;
    Vertices.Reserve(NumVerts);
    Normals.Reserve(NumVerts);
    UVs.Reserve(NumVerts);
    VertexColors.Reserve(NumVerts);

    const float HalfWidth = static_cast<float>(NumVertsX - 1) * GridSize * 0.5f;
    const float HalfHeight = static_cast<float>(NumVertsY - 1) * GridSize * 0.5f;

    const float WaterZ = WaterHeightWorld;

    // Build a flat grid of vertices at WaterZ
    for (int32 y = 0; y < NumVertsY; ++y)
    {
        for (int32 x = 0; x < NumVertsX; ++x)
        {
            const float WorldX = static_cast<float>(x) * GridSize - HalfWidth;
            const float WorldY = static_cast<float>(y) * GridSize - HalfHeight;

            Vertices.Add(FVector(WorldX, WorldY, WaterZ));

            const float U = static_cast<float>(x) / static_cast<float>(NumVertsX - 1);
            const float V = static_cast<float>(y) / static_cast<float>(NumVertsY - 1);
            UVs.Add(FVector2D(U, V));

            Normals.Add(FVector::UpVector);
            VertexColors.Add(FLinearColor::White);
        }
    }

    Triangles.Reserve((NumVertsX - 1) * (NumVertsY - 1) * 6);

    for (int32 y = 0; y < NumVertsY - 1; ++y)
    {
        for (int32 x = 0; x < NumVertsX - 1; ++x)
        {
            const int32 I0 = x + y * NumVertsX;
            const int32 I1 = x + (y + 1) * NumVertsX;
            const int32 I2 = (x + 1) + y * NumVertsX;
            const int32 I3 = (x + 1) + (y + 1) * NumVertsX;

            Triangles.Add(I0);
            Triangles.Add(I3);
            Triangles.Add(I2);

            Triangles.Add(I0);
            Triangles.Add(I1);
            Triangles.Add(I3);
        }
    }

    const bool bCreateCollision = false;

    WaterMesh->ClearAllMeshSections();
    WaterMesh->CreateMeshSection_LinearColor(
        0,
        Vertices,
        Triangles,
        Normals,
        UVs,
        VertexColors,
        Tangents,
        bCreateCollision
    );
}
