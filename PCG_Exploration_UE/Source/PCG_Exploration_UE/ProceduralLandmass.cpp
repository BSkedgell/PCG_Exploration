//-----------------------------------------------------------------------------
// ProceduralLandmass.cpp
//
// Implementation of a simple Perlin-noise-based terrain generator.
// This is roughly analogous to Seb Lague's Noise + MeshGenerator combo,
// but adapted to Unreal Engine using UProceduralMeshComponent.
//-----------------------------------------------------------------------------

#include "ProceduralLandmass.h"
#include "ProceduralMeshComponent.h"
#include "Math/UnrealMathUtility.h"

// Sets default values
AProceduralLandmass::AProceduralLandmass()
{
    // We don't need ticking for this actor (generation happens on demand)
    PrimaryActorTick.bCanEverTick = false;

    // Create the procedural mesh component and make it the root
    ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralMesh"));
    RootComponent = ProceduralMesh;

    // Recommended for runtime-generated meshes to avoid blocking the game thread
    ProceduralMesh->bUseAsyncCooking = true;
}

void AProceduralLandmass::BeginPlay()
{
    Super::BeginPlay();

    // Generate the terrain automatically when the game starts
    GenerateTerrain();
}

#if WITH_EDITOR
void AProceduralLandmass::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    // Regenerate terrain whenever a property changes or the actor is moved
    // This lets you tweak noise values live in the editor.
    GenerateTerrain();
}
#endif

// Called from BeginPlay, OnConstruction, and the CallInEditor button
void AProceduralLandmass::GenerateTerrain()
{
    if (MapWidth < 2 || MapHeight < 2)
    {
        UE_LOG(LogTemp, Warning, TEXT("MapWidth and MapHeight must be >= 2"));
        return;
    }

    // 1) Build a height map using multi-octave Perlin noise
    TArray<float> HeightMap;
    GenerateNoiseMap(HeightMap);

    // 2) Turn that height map into a mesh in the ProceduralMesh component
    CreateMeshFromHeightMap(HeightMap);
}

//-----------------------------------------------------------------------------
// Noise generation
//-----------------------------------------------------------------------------

void AProceduralLandmass::GenerateNoiseMap(TArray<float>& OutHeightMap)
{
    const int32 NumPoints = MapWidth * MapHeight;
    OutHeightMap.SetNum(NumPoints);

    // Prevent division by zero when using NoiseScale
    if (NoiseScale <= 0.0f)
    {
        NoiseScale = 0.0001f;
    }

    // Deterministic random stream for octave offsets (reproducible with same Seed)
    FRandomStream RandomStream(Seed);

    const int32 NumOctaves = FMath::Max(Octaves, 1);

    // Each octave will sample noise at a different offset
    TArray<FVector2D> OctaveOffsets;
    OctaveOffsets.SetNum(NumOctaves);

    for (int32 i = 0; i < NumOctaves; ++i)
    {
        const float OffsetX = RandomStream.FRandRange(-100000.0f, 100000.0f) + NoiseOffset.X;
        const float OffsetY = RandomStream.FRandRange(-100000.0f, 100000.0f) + NoiseOffset.Y;
        OctaveOffsets[i] = FVector2D(OffsetX, OffsetY);
    }

    // Track min/max so we can normalize heights to [0,1]
    float MaxNoiseHeight = TNumericLimits<float>::Lowest();
    float MinNoiseHeight = TNumericLimits<float>::Max();

    // Center the sampling coordinates so the terrain "radiates" around the actor
    const float HalfWidth = static_cast<float>(MapWidth) / 2.0f;
    const float HalfHeight = static_cast<float>(MapHeight) / 2.0f;

    // Loop over each point in the grid and compute its noise-based height
    for (int32 y = 0; y < MapHeight; ++y)
    {
        for (int32 x = 0; x < MapWidth; ++x)
        {
            float Amplitude = 1.0f; // controls contribution of this octave
            float Frequency = 1.0f; // controls scale of this octave
            float NoiseHeight = 0.0f;

            // Combine multiple octaves of Perlin noise
            for (int32 i = 0; i < NumOctaves; ++i)
            {
                const float SampleX =
                    ((static_cast<float>(x) - HalfWidth) / NoiseScale) * Frequency + OctaveOffsets[i].X;

                const float SampleY =
                    ((static_cast<float>(y) - HalfHeight) / NoiseScale) * Frequency + OctaveOffsets[i].Y;

                // Unreal's PerlinNoise2D returns values in the range [-1, 1]
                const float PerlinValue = FMath::PerlinNoise2D(FVector2D(SampleX, SampleY));

                // Accumulate this octave's contribution
                NoiseHeight += PerlinValue * Amplitude;

                // Update amplitude/frequency for the next octave
                Amplitude *= Persistance;
                Frequency *= Lacunarity;
            }

            const int32 Index = x + y * MapWidth;
            OutHeightMap[Index] = NoiseHeight;

            // Track min and max for later normalization
            if (NoiseHeight > MaxNoiseHeight)
            {
                MaxNoiseHeight = NoiseHeight;
            }
            if (NoiseHeight < MinNoiseHeight)
            {
                MinNoiseHeight = NoiseHeight;
            }
        }
    }

    // Normalize all heights into [0,1] so we can scale by HeightMultiplier cleanly
    const FVector2D InputRange(MinNoiseHeight, MaxNoiseHeight);
    const FVector2D OutputRange(0.0f, 1.0f);

    for (int32 i = 0; i < NumPoints; ++i)
    {
        OutHeightMap[i] = FMath::GetMappedRangeValueClamped(InputRange, OutputRange, OutHeightMap[i]);
    }
}

//-----------------------------------------------------------------------------
// Mesh generation
//-----------------------------------------------------------------------------

void AProceduralLandmass::CreateMeshFromHeightMap(const TArray<float>& HeightMap)
{
    const int32 NumVertsX = MapWidth;
    const int32 NumVertsY = MapHeight;

    // Mesh data arrays
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> VertexColors;
    TArray<FProcMeshTangent> Tangents; // not used yet, but required by the API

    const int32 NumVerts = NumVertsX * NumVertsY;

    Vertices.Reserve(NumVerts);
    Normals.Reserve(NumVerts);
    UVs.Reserve(NumVerts);
    VertexColors.Reserve(NumVerts);

    // Center the mesh around the actor's origin in X/Y
    const float HalfWidth = static_cast<float>(NumVertsX - 1) * GridSize * 0.5f;
    const float HalfHeight = static_cast<float>(NumVertsY - 1) * GridSize * 0.5f;

    // Build vertex positions, UVs, normals, and vertex colors
    for (int32 y = 0; y < NumVertsY; ++y)
    {
        for (int32 x = 0; x < NumVertsX; ++x)
        {
            const int32 Index = x + y * NumVertsX;

            // Height value [0,1] => scale by HeightMultiplier for world Z
            const float NoiseValue = HeightMap.IsValidIndex(Index) ? HeightMap[Index] : 0.0f;
            const float Height = NoiseValue * HeightMultiplier;

            // World X/Y position (grid spacing)
            const float WorldX = static_cast<float>(x) * GridSize - HalfWidth;
            const float WorldY = static_cast<float>(y) * GridSize - HalfHeight;

            Vertices.Add(FVector(WorldX, WorldY, Height));

            // UVs across the grid from (0,0) to (1,1)
            const float U = static_cast<float>(x) / static_cast<float>(NumVertsX - 1);
            const float V = static_cast<float>(y) / static_cast<float>(NumVertsY - 1);
            UVs.Add(FVector2D(U, V));

            // Simple up-facing normals for now (can be improved by sampling neighbors)
            Normals.Add(FVector::UpVector);

            // Grayscale vertex color based on height (useful for debug or simple shading)
            VertexColors.Add(FLinearColor(NoiseValue, NoiseValue, NoiseValue, 1.0f));
        }
    }

    // Each quad (cell) in the grid is made of 2 triangles (6 indices)
    Triangles.Reserve((NumVertsX - 1) * (NumVertsY - 1) * 6);

    for (int32 y = 0; y < NumVertsY - 1; ++y)
    {
        for (int32 x = 0; x < NumVertsX - 1; ++x)
        {
            // Indices of the four corners of the current quad
            const int32 I0 = x + y * NumVertsX;             // top-left
            const int32 I1 = x + (y + 1) * NumVertsX;       // bottom-left
            const int32 I2 = (x + 1) + y * NumVertsX;       // top-right
            const int32 I3 = (x + 1) + (y + 1) * NumVertsX; // bottom-right

            // Triangle 1: I0, I3, I2
            Triangles.Add(I0);
            Triangles.Add(I3);
            Triangles.Add(I2);

            // Triangle 2: I0, I1, I3
            Triangles.Add(I0);
            Triangles.Add(I1);
            Triangles.Add(I3);
        }
    }

    const bool bCreateCollision = true;

    // Clear any previous mesh data and create a new section
    ProceduralMesh->ClearAllMeshSections();

    ProceduralMesh->CreateMeshSection_LinearColor(
        0,              // Section index
        Vertices,
        Triangles,
        Normals,
        UVs,
        VertexColors,
        Tangents,
        bCreateCollision
    );
}
