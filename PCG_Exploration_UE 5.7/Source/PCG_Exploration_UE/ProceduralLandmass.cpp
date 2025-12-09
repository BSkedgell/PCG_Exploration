// ProceduralLandmass.cpp

#include "ProceduralLandmass.h"

#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"   // <-- needed for UMaterialInstanceDynamic
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "ProceduralWaterPlane.h"
#include "EngineUtils.h" // for TActorIterator

AProceduralLandmass::AProceduralLandmass()
{
    PrimaryActorTick.bCanEverTick = false;

    ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralMesh"));
    RootComponent = ProceduralMesh;

    ProceduralMesh->bUseAsyncCooking = true;
}

void AProceduralLandmass::BeginPlay()
{
    Super::BeginPlay();
}

void AProceduralLandmass::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
}

#if WITH_EDITOR
void AProceduralLandmass::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    const FName PropName = PropertyChangedEvent.Property
        ? PropertyChangedEvent.Property->GetFName()
        : NAME_None;

    if (PropName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, MapWidth) ||
        PropName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, MapHeight) ||
        PropName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, GridSize) ||
        PropName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, HeightMultiplier) ||
        PropName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, NoiseScale) ||
        PropName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, Seed) ||
        PropName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, Octaves) ||
        PropName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, Persistence) ||
        PropName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, Lacunarity) ||
        PropName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, WaterHeight01))  // <-- add this
    {
        GenerateTerrain();
    }
    // If the water height changed, also tell any linked water planes to realign.
    if (PropName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, WaterHeight01))
    {
        if (UWorld* World = GetWorld())
        {
            for (TActorIterator<AProceduralWaterPlane> It(World); It; ++It)
            {
                AProceduralWaterPlane* Water = *It;
                if (Water && Water->LinkedLandmass == this)
                {
                    // Re-run its construction logic so it snaps to the new height
                    Water->RefreshFromLandmass();
                }
            }
        }
    }
}
#endif // WITH_EDITOR

void AProceduralLandmass::GenerateTerrain()
{
    CreateMesh();
}

float AProceduralLandmass::GetDefaultWaterHeight01() const
{
    return WaterHeight01;
}

FVector AProceduralLandmass::GetLandmassCenter() const
{
    const float WidthWorld = (MapWidth - 1) * GridSize;
    const float HeightWorld = (MapHeight - 1) * GridSize;

    return GetActorLocation() + FVector(WidthWorld * 0.5f, HeightWorld * 0.5f, 0.0f);
}

void AProceduralLandmass::CreateMesh()
{
    if (!ProceduralMesh || MapWidth < 2 || MapHeight < 2)
    {
        return;
    }

    const int32 NumVertsX = MapWidth;
    const int32 NumVertsY = MapHeight;
    const int32 NumVerts = NumVertsX * NumVertsY;

    TArray<FVector>          Vertices;
    TArray<int32>            Triangles;
    TArray<FVector>          Normals;
    TArray<FVector2D>        UVs;
    TArray<FLinearColor>     VertexColors;
    TArray<FProcMeshTangent> Tangents;

    Vertices.SetNum(NumVerts);
    Normals.SetNum(NumVerts);
    UVs.SetNum(NumVerts);
    VertexColors.SetNum(NumVerts);
    Tangents.SetNum(NumVerts);

    // Build height map
    TArray<float> Heights;
    BuildHeightMap(Heights);

    // --- Build vertices, UVs, vertex colors, tangents ---
    for (int32 y = 0; y < NumVertsY; ++y)
    {
        for (int32 x = 0; x < NumVertsX; ++x)
        {
            const int32 Index = y * NumVertsX + x;

            const float Height01 = Heights.IsValidIndex(Index) ? Heights[Index] : 0.0f;
            const float Z = Height01 * HeightMultiplier;

            // Position
            Vertices[Index] = FVector(x * GridSize, y * GridSize, Z);

            // UVs in [0,1]
            const float U = (NumVertsX > 1) ? (static_cast<float>(x) / (NumVertsX - 1)) : 0.0f;
            const float V = (NumVertsY > 1) ? (static_cast<float>(y) / (NumVertsY - 1)) : 0.0f;
            UVs[Index] = FVector2D(U, V);

            // Height-only in B channel (0..1), R/G free for future use
            VertexColors[Index] = FLinearColor(
                0.0f,       // R - reserved (e.g., biome)
                0.0f,       // G - reserved (e.g., slope)
                Height01,   // B - normalized height
                1.0f        // A - wetness/whatever later
            );

            // Simple tangent along +X
            Tangents[Index] = FProcMeshTangent(1.0f, 0.0f, 0.0f);

            // Initialize normals to zero; we’ll accumulate face normals then normalize
            Normals[Index] = FVector::ZeroVector;
        }
    }

    // --- Build triangle indices ---
    const int32 NumQuadsX = NumVertsX - 1;
    const int32 NumQuadsY = NumVertsY - 1;
    Triangles.Reserve(NumQuadsX * NumQuadsY * 6);

    for (int32 y = 0; y < NumQuadsY; ++y)
    {
        for (int32 x = 0; x < NumQuadsX; ++x)
        {
            const int32 BottomLeft = y * NumVertsX + x;
            const int32 BottomRight = BottomLeft + 1;
            const int32 TopLeft = BottomLeft + NumVertsX;
            const int32 TopRight = TopLeft + 1;

            // First tri: TopLeft, BottomRight, BottomLeft
            Triangles.Add(TopLeft);
            Triangles.Add(BottomRight);
            Triangles.Add(BottomLeft);

            // Second tri: TopLeft, TopRight, BottomRight
            Triangles.Add(TopLeft);
            Triangles.Add(TopRight);
            Triangles.Add(BottomRight);
        }
    }

    // --- Compute normals from triangles ---
    const int32 NumTris = Triangles.Num() / 3;
    for (int32 i = 0; i < NumTris; ++i)
    {
        const int32 I0 = Triangles[i * 3 + 0];
        const int32 I1 = Triangles[i * 3 + 1];
        const int32 I2 = Triangles[i * 3 + 2];

        const FVector& V0 = Vertices[I0];
        const FVector& V1 = Vertices[I1];
        const FVector& V2 = Vertices[I2];

        const FVector Edge1 = V1 - V0;
        const FVector Edge2 = V2 - V0;
        const FVector Normal = FVector::CrossProduct(Edge2, Edge1).GetSafeNormal();

        Normals[I0] += Normal;
        Normals[I1] += Normal;
        Normals[I2] += Normal;
    }

    for (int32 i = 0; i < NumVerts; ++i)
    {
        FVector& N = Normals[i];

        if (!N.IsNearlyZero())
        {
            N.Normalize();
        }
        else
        {
            N = FVector::UpVector;
        }

        // Extra safety against NaNs/Infs
        if (!FMath::IsFinite(N.X) || !FMath::IsFinite(N.Y) || !FMath::IsFinite(N.Z))
        {
            N = FVector::UpVector;
        }
    }

    // --- Push mesh to the component ---
    ProceduralMesh->CreateMeshSection_LinearColor(
        0,
        Vertices,
        Triangles,
        Normals,
        UVs,
        VertexColors,
        Tangents,
        true  // bCreateCollision
    );

    // --- Set up / update dynamic material instance so the material
    //     WaterHeight parameter matches WaterHeight01 on this actor ---
    if (!TerrainMID)
    {
        // Use whatever material is assigned in the editor as the base
        UMaterialInterface* BaseMat = ProceduralMesh->GetMaterial(0);
        if (BaseMat)
        {
            TerrainMID = UMaterialInstanceDynamic::Create(BaseMat, this);
        }
    }

    if (TerrainMID)
    {
        TerrainMID->SetScalarParameterValue(TEXT("WaterHeight"), WaterHeight01);

        // (Optional) drive other scalar params (SandHeight, GrassHeight, etc.) here

        ProceduralMesh->SetMaterial(0, TerrainMID);
    }
}

void AProceduralLandmass::BuildHeightMap(TArray<float>& OutHeights) const
{
    const int32 NumVerts = MapWidth * MapHeight;
    OutHeights.SetNum(NumVerts);

    if (NoiseScale <= KINDA_SMALL_NUMBER)
    {
        for (int32 i = 0; i < NumVerts; ++i)
        {
            OutHeights[i] = 0.0f;
        }
        return;
    }

    FRandomStream Rng(Seed);
    const FVector2D Offset(
        Rng.FRandRange(-10000.f, 10000.f),
        Rng.FRandRange(-10000.f, 10000.f)
    );

    for (int32 y = 0; y < MapHeight; ++y)
    {
        for (int32 x = 0; x < MapWidth; ++x)
        {
            const int32 Index = y * MapWidth + x;

            const float SampleX = (static_cast<float>(x) + Offset.X) / NoiseScale;
            const float SampleY = (static_cast<float>(y) + Offset.Y) / NoiseScale;

            float NoiseHeight = 0.0f;
            float Amplitude = 1.0f;
            float Frequency = 1.0f;
            float MaxPossible = 0.0f;

            for (int32 Oct = 0; Oct < Octaves; ++Oct)
            {
                const float Px = SampleX * Frequency;
                const float Py = SampleY * Frequency;

                const float Perlin = FMath::PerlinNoise2D(FVector2D(Px, Py));
                NoiseHeight += Perlin * Amplitude;

                MaxPossible += Amplitude;
                Amplitude *= Persistence;
                Frequency *= Lacunarity;
            }

            if (MaxPossible > 0.0f)
            {
                NoiseHeight = (NoiseHeight / MaxPossible) * 0.5f + 0.5f;
            }
            else
            {
                NoiseHeight = 0.0f;
            }

            OutHeights[Index] = FMath::Clamp(NoiseHeight, 0.0f, 1.0f);
        }
    }
}
