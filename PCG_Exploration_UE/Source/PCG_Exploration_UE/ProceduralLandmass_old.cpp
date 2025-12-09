//-----------------------------------------------------------------------------
// ProceduralLandmass.cpp
//
// Perlin-noise-based terrain with height- and slope-based biome coloring.
//-----------------------------------------------------------------------------

#include "ProceduralLandmass.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Math/UnrealMathUtility.h"

// Sets default values
AProceduralLandmass::AProceduralLandmass()
{
    PrimaryActorTick.bCanEverTick = false;

    // Create the procedural mesh component and make it the root
    ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralMesh"));
    RootComponent = ProceduralMesh;

    ProceduralMesh->bUseAsyncCooking = true;

    // -------------------------------------------------------------
    // Default biome setup:
    // If the array is empty, seed Water/Sand/Dirt/Grass/Rock/Snow.
    // This works for the CDO, new actors, and reset actors,
    // but won't overwrite existing custom setups.
    // -------------------------------------------------------------
    if (TerrainTypes.Num() == 0)
    {
        TerrainTypes.Empty();
        TerrainTypes.Reserve(6);

        // Water: very low height, fairly flat
        {
            FTerrainType Water;
            Water.Name = "Water";
            Water.Height = 0.20f; // up to 20% normalized height
            Water.MinSlope = 0.0f;
            Water.MaxSlope = 0.4f;
            Water.Color = FLinearColor(0.05f, 0.15f, 0.35f, 1.0f); // deep blue
            TerrainTypes.Add(Water);
        }

        // Sand: shoreline / beaches
        {
            FTerrainType Sand;
            Sand.Name = "Sand";
            Sand.Height = 0.35f; // up to 35% height
            Sand.MinSlope = 0.0f;
            Sand.MaxSlope = 0.6f;
            Sand.Color = FLinearColor(0.86f, 0.80f, 0.61f, 1.0f); // light tan
            TerrainTypes.Add(Sand);
        }

        // Dirt: transition between sand and grass
        {
            FTerrainType Dirt;
            Dirt.Name = "Dirt";
            Dirt.Height = 0.55f; // between sand and grass
            Dirt.MinSlope = 0.0f;
            Dirt.MaxSlope = 0.6f;
            Dirt.Color = FLinearColor(0.40f, 0.25f, 0.15f, 1.0f); // brown
            TerrainTypes.Add(Dirt);
        }

        // Grass: mid height, gentle slopes
        {
            FTerrainType Grass;
            Grass.Name = "Grass";
            Grass.Height = 0.75f; // up to 75% height
            Grass.MinSlope = 0.0f;
            Grass.MaxSlope = 0.5f;
            Grass.Color = FLinearColor(0.10f, 0.50f, 0.10f, 1.0f); // green
            TerrainTypes.Add(Grass);
        }

        // Rock: higher, steeper areas
        {
            FTerrainType Rock;
            Rock.Name = "Rock";
            Rock.Height = 0.95f; // up to 95% height
            Rock.MinSlope = 0.4f;  // only on steeper slopes
            Rock.MaxSlope = 1.0f;
            Rock.Color = FLinearColor(0.35f, 0.35f, 0.35f, 1.0f); // dark gray
            TerrainTypes.Add(Rock);
        }

        // Snow: very high, any slope
        {
            FTerrainType Snow;
            Snow.Name = "Snow";
            Snow.Height = 1.0f;  // top of the world
            Snow.MinSlope = 0.0f;
            Snow.MaxSlope = 1.0f;
            Snow.Color = FLinearColor::White;
            TerrainTypes.Add(Snow);
        }
    }
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

    // Generate terrain whenever properties change in the editor
    GenerateTerrain();
}

void AProceduralLandmass::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    // Any time a property changes in the Details panel, rebuild the terrain
    GenerateTerrain();
}
#endif

void AProceduralLandmass::GenerateTerrain()
{
    if (MapWidth < 2 || MapHeight < 2)
    {
        UE_LOG(LogTemp, Warning, TEXT("MapWidth and MapHeight must be >= 2"));
        return;
    }

    // If a terrain material is assigned, make sure the mesh uses it
    if (TerrainMaterial)
    {
        ProceduralMesh->SetMaterial(0, TerrainMaterial);
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

    if (NoiseScale <= 0.0f)
    {
        NoiseScale = 0.0001f;
    }

    FRandomStream RandomStream(Seed);
    const int32 NumOctaves = FMath::Max(Octaves, 1);

    TArray<FVector2D> OctaveOffsets;
    OctaveOffsets.SetNum(NumOctaves);

    // Random offsets for each octave (Seb-style)
    for (int32 i = 0; i < NumOctaves; ++i)
    {
        const float OffsetX = RandomStream.FRandRange(-100000.0f, 100000.0f) + NoiseOffset.X;
        const float OffsetY = RandomStream.FRandRange(-100000.0f, 100000.0f) + NoiseOffset.Y;
        OctaveOffsets[i] = FVector2D(OffsetX, OffsetY);
    }

    float MaxNoiseHeight = TNumericLimits<float>::Lowest();
    float MinNoiseHeight = TNumericLimits<float>::Max();

    const float HalfWidth = static_cast<float>(MapWidth) / 2.0f;
    const float HalfHeight = static_cast<float>(MapHeight) / 2.0f;

    // Compute raw noise heights
    for (int32 y = 0; y < MapHeight; ++y)
    {
        for (int32 x = 0; x < MapWidth; ++x)
        {
            float Amplitude = 1.0f;
            float Frequency = 1.0f;
            float NoiseHeight = 0.0f;

            for (int32 i = 0; i < NumOctaves; ++i)
            {
                const float SampleX =
                    ((static_cast<float>(x) - HalfWidth) / NoiseScale) * Frequency + OctaveOffsets[i].X;

                const float SampleY =
                    ((static_cast<float>(y) - HalfHeight) / NoiseScale) * Frequency + OctaveOffsets[i].Y;

                const float PerlinValue = FMath::PerlinNoise2D(FVector2D(SampleX, SampleY)); // [-1,1]

                NoiseHeight += PerlinValue * Amplitude;

                Amplitude *= Persistance;
                Frequency *= Lacunarity;
            }

            const int32 Index = x + y * MapWidth;
            OutHeightMap[Index] = NoiseHeight;

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

    // Normalize heights to [0,1]
    const FVector2D InputRange(MinNoiseHeight, MaxNoiseHeight);
    const FVector2D OutputRange(0.0f, 1.0f);

    for (int32 i = 0; i < NumPoints; ++i)
    {
        OutHeightMap[i] = FMath::GetMappedRangeValueClamped(InputRange, OutputRange, OutHeightMap[i]);
    }
}

//-----------------------------------------------------------------------------
// Biome helper
//-----------------------------------------------------------------------------

FLinearColor AProceduralLandmass::GetColorForHeightAndSlope(float NormalizedHeight, float Slope) const
{
    // Fallback if we don't have any biome definitions
    if (TerrainTypes.Num() == 0)
    {
        return FLinearColor(NormalizedHeight, NormalizedHeight, NormalizedHeight, 1.0f);
    }

    const int32 NumTypes = TerrainTypes.Num();

    const FTerrainType* UpperMatch = nullptr;
    const FTerrainType* LowerMatch = nullptr;

    // Track the last biome that matched slope (and was below or at current height)
    const FTerrainType* LastSlopeOK = nullptr;

    // First pass: find the "upper" biome for this height and slope,
    // and the "lower" biome just below it (also slope-compatible).
    for (int32 i = 0; i < NumTypes; ++i)
    {
        const FTerrainType& Type = TerrainTypes[i];

        const bool bSlopeOK = (Slope >= Type.MinSlope && Slope <= Type.MaxSlope);
        if (!bSlopeOK)
        {
            continue;
        }

        if (NormalizedHeight <= Type.Height)
        {
            UpperMatch = &Type;
            LowerMatch = LastSlopeOK; // might be null if nothing below matched
            break;
        }

        // Height is above this biome's threshold, but slope is OK,
        // so for now this is our "last valid lower" biome.
        LastSlopeOK = &Type;
    }

    // If we didn't find an UpperMatch, use the highest slope-compatible biome
    if (!UpperMatch)
    {
        for (int32 i = NumTypes - 1; i >= 0; --i)
        {
            const FTerrainType& Type = TerrainTypes[i];
            const bool bSlopeOK = (Slope >= Type.MinSlope && Slope <= Type.MaxSlope);
            if (bSlopeOK)
            {
                return Type.Color;
            }
        }

        // As a last resort, fall back to the last biome’s color
        return TerrainTypes.Last().Color;
    }

    // If we have no lower biome (e.g. water at the very bottom),
    // or blending is disabled, just use the upper biome color.
    if (!LowerMatch || HeightBlendRange <= 0.0f)
    {
        return UpperMatch->Color;
    }

    // Now we have a lower+upper biome pair that both match slope.
    // Blend in a band just below the upper biome's height.
    const float BlendTop = UpperMatch->Height;                    // center of the boundary
    const float BlendBottom = BlendTop - HeightBlendRange;           // start of blending
    const float h = NormalizedHeight;

    if (h <= BlendBottom)
    {
        // Below the blend band: pure lower biome
        return LowerMatch->Color;
    }
    else if (h >= BlendTop)
    {
        // Above the threshold: pure upper biome
        return UpperMatch->Color;
    }
    else
    {
        // Inside the blend band: smoothly interpolate between lower and upper
        const float Alpha = FMath::Clamp((h - BlendBottom) / HeightBlendRange, 0.0f, 1.0f);

        // Smoothstep to remove harsh interpolation
        const float SmoothAlpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);

        return FMath::Lerp(LowerMatch->Color, UpperMatch->Color, SmoothAlpha);
    }
}

//-----------------------------------------------------------------------------
// Mesh generation
//-----------------------------------------------------------------------------

void AProceduralLandmass::CreateMeshFromHeightMap(const TArray<float>& HeightMap)
{
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

    // Build vertex data
    for (int32 y = 0; y < NumVertsY; ++y)
    {
        for (int32 x = 0; x < NumVertsX; ++x)
        {
            const int32 Index = x + y * NumVertsX;

            // Normalized noise [0,1] at this vertex
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

            // --- Compute normal from neighbor heights (slope-aware) ---

            // Clamp neighbor indices so edges still get valid data
            const int32 xL = FMath::Clamp(x - 1, 0, NumVertsX - 1);
            const int32 xR = FMath::Clamp(x + 1, 0, NumVertsX - 1);
            const int32 yD = FMath::Clamp(y - 1, 0, NumVertsY - 1);
            const int32 yU = FMath::Clamp(y + 1, 0, NumVertsY - 1);

            const float hL = HeightMap[xL + y * NumVertsX] * HeightMultiplier;
            const float hR = HeightMap[xR + y * NumVertsX] * HeightMultiplier;
            const float hD = HeightMap[x + yD * NumVertsX] * HeightMultiplier;
            const float hU = HeightMap[x + yU * NumVertsX] * HeightMultiplier;

            // Approximate surface normal using central differences
            FVector Normal(
                hL - hR,           // change in X
                hD - hU,           // change in Y
                2.0f * GridSize);  // vertical scale factor

            Normal = Normal.GetSafeNormal();
            Normals.Add(Normal);

            // Slope metric: 0 = flat, 1 = vertical
            const float Slope = 1.0f - FVector::DotProduct(Normal, FVector::UpVector);

            // Pick biome color based on height + slope
            VertexColors.Add(GetColorForHeightAndSlope(NoiseValue, Slope));
        }
    }

    // Build triangles (two per quad in the grid)
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

    const bool bCreateCollision = true;

    ProceduralMesh->ClearAllMeshSections();
    ProceduralMesh->CreateMeshSection_LinearColor(
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
