#include "ProceduralLandmass.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"

AProceduralLandmass::AProceduralLandmass()
{
    PrimaryActorTick.bCanEverTick = true;

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
        PropName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, Lacunarity))
    {
        GenerateTerrain();
    }
}
#endif

void AProceduralLandmass::GenerateTerrain()
{
    CreateMesh();
}

float AProceduralLandmass::GetDefaultWaterHeight01() const
{
    // For now, assume “sea level” is around 0.2 of normalized height
    return 0.2f;
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

    TArray<FVector> Vertices;
    TArray<int32>   Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FColor> Colors;
    TArray<FProcMeshTangent> Tangents;

    Vertices.SetNum(NumVertsX * NumVertsY);
    Normals.SetNum(NumVertsX * NumVertsY);
    UVs.SetNum(NumVertsX * NumVertsY);
    Colors.SetNum(NumVertsX * NumVertsY);
    Tangents.SetNum(NumVertsX * NumVertsY);

    TArray<float> Heights;
    BuildHeightMap(Heights);

    // Build vertices
    for (int32 y = 0; y < NumVertsY; ++y)
    {
        for (int32 x = 0; x < NumVertsX; ++x)
        {
            const int32 Index = y * NumVertsX + x;

            const float Height01 = Heights[Index];
            const float Z = Height01 * HeightMultiplier;

            Vertices[Index] = FVector(x * GridSize, y * GridSize, Z);
            Normals[Index] = FVector::UpVector;
            UVs[Index] = FVector2D(
                static_cast<float>(x) / (NumVertsX - 1),
                static_cast<float>(y) / (NumVertsY - 1)
            );

            // Simple vertex color based only on height for now
            FLinearColor LinearColor = FLinearColor::LerpUsingHSV(
                FLinearColor::Blue,
                FLinearColor::Green,
                Height01
            );
            Colors[Index] = LinearColor.ToFColor(true);
        }
    }

    // Build triangle indices
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

            // First triangle
            Triangles.Add(TopLeft);
            Triangles.Add(BottomRight);
            Triangles.Add(BottomLeft);

            // Second triangle
            Triangles.Add(TopLeft);
            Triangles.Add(TopRight);
            Triangles.Add(BottomRight);
        }
    }

    ProceduralMesh->CreateMeshSection_LinearColor(
        0,
        Vertices,
        Triangles,
        Normals,
        UVs,
        Colors,
        Tangents,
        true
    );

    ProceduralMesh->SetMaterial(0, ProceduralMesh->GetMaterial(0));
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
    const FVector2D Offset(Rng.FRandRange(-10000.f, 10000.f), Rng.FRandRange(-10000.f, 10000.f));

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

            // Normalize to [0..1]
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
