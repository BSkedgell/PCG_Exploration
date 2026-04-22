// ProceduralLandmass.cpp
//
// Generation overview:
// 1. BuildHeightMap creates a 0..1 terrain heightfield from layered noise,
//    coastline shaping, beaches, plateaus, cliffs, and erosion.
// 2. CreateMesh converts that heightfield into the main procedural terrain mesh
//    and writes vertex color masks for the terrain material.
// 3. BuildOverhangMesh creates separate mesh geometry for true overhangs, which
//    cannot be represented by the heightfield alone.

#include "ProceduralLandmass.h"

#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
// Deterministic 0..1 hash used for procedural feature placement. This avoids
// relying on mutable random stream state inside nested generation loops.
float Hash01(int32 X, int32 Y, int32 Seed)
{
    uint32 Hash = static_cast<uint32>(X) * 374761393u;
    Hash += static_cast<uint32>(Y) * 668265263u;
    Hash += static_cast<uint32>(Seed) * 2246822519u;
    Hash = (Hash ^ (Hash >> 13)) * 1274126177u;
    Hash ^= (Hash >> 16);
    return static_cast<float>(Hash & 0x00ffffff) / static_cast<float>(0x01000000);
}

// Smooth trapezoid-like mask: fades in, holds, then fades out.
float SmoothPulse(float Start, float PeakStart, float PeakEnd, float End, float Value)
{
    const float Rise = FMath::SmoothStep(Start, PeakStart, Value);
    const float Fall = 1.0f - FMath::SmoothStep(PeakEnd, End, Value);
    return FMath::Clamp(Rise * Fall, 0.0f, 1.0f);
}

FLinearColor MakeTerrainVertexColor(
    float Height01,
    float SlopeMask,
    float WaterHeight01,
    float BeachBandWidth,
    float BeachFadeDistance)
{
    const float BeachStart = WaterHeight01 - BeachBandWidth * 0.35f;
    const float BeachPeak = WaterHeight01 + BeachBandWidth;
    const float BeachEnd = BeachPeak + BeachFadeDistance;

    const float BeachRise = FMath::SmoothStep(BeachStart, BeachPeak, Height01);
    const float BeachFall = 1.0f - FMath::SmoothStep(BeachPeak, BeachEnd, Height01);
    const float BeachMask = FMath::Clamp(BeachRise * BeachFall, 0.0f, 1.0f);
    const float DryLandMask = FMath::SmoothStep(
        WaterHeight01,
        WaterHeight01 + BeachBandWidth + 0.02f,
        Height01);

    // Vertex color contract used by M_ProceduralTerrain:
    // R = beach mask, G = slope mask, B = height, A = dry-land mask.
    return FLinearColor(BeachMask, SlopeMask, Height01, DryLandMask);
}

// Standard fractal Brownian motion from Perlin noise. Used for broad terrain.
float ComputeFractalNoise(
    float SampleX,
    float SampleY,
    int32 Octaves,
    float Persistence,
    float Lacunarity)
{
    float NoiseHeight = 0.0f;
    float Amplitude = 1.0f;
    float Frequency = 1.0f;
    float MaxPossible = 0.0f;

    for (int32 Oct = 0; Oct < Octaves; ++Oct)
    {
        const float Perlin = FMath::PerlinNoise2D(FVector2D(SampleX * Frequency, SampleY * Frequency));
        NoiseHeight += Perlin * Amplitude;

        MaxPossible += Amplitude;
        Amplitude *= Persistence;
        Frequency *= Lacunarity;
    }

    if (MaxPossible <= 0.0f)
    {
        return 0.0f;
    }

    return FMath::Clamp((NoiseHeight / MaxPossible) * 0.5f + 0.5f, 0.0f, 1.0f);
}

// Ridged noise for sharper fantasy mountain silhouettes and cliff bands.
float ComputeRidgedNoise(
    float SampleX,
    float SampleY,
    int32 Octaves,
    float Persistence,
    float Lacunarity,
    float RidgeSharpness)
{
    float NoiseHeight = 0.0f;
    float Amplitude = 1.0f;
    float Frequency = 1.0f;
    float MaxPossible = 0.0f;

    for (int32 Oct = 0; Oct < Octaves; ++Oct)
    {
        const float Perlin = FMath::PerlinNoise2D(FVector2D(SampleX * Frequency, SampleY * Frequency));
        const float Ridged = 1.0f - FMath::Abs(Perlin);
        NoiseHeight += FMath::Pow(FMath::Clamp(Ridged, 0.0f, 1.0f), RidgeSharpness) * Amplitude;

        MaxPossible += Amplitude;
        Amplitude *= Persistence;
        Frequency *= Lacunarity;
    }

    if (MaxPossible <= 0.0f)
    {
        return 0.0f;
    }

    return FMath::Clamp(NoiseHeight / MaxPossible, 0.0f, 1.0f);
}

// Adds a quad with winding corrected toward PreferredNormal. The helper also
// duplicates normals/tangents/UVs because overhangs use flat-shaded faces.
void AddQuad(
    TArray<FVector>& Vertices,
    TArray<int32>& Triangles,
    TArray<FVector>& Normals,
    TArray<FVector2D>& UVs,
    TArray<FLinearColor>& Colors,
    TArray<FProcMeshTangent>& Tangents,
    const FVector& A,
    const FVector& B,
    const FVector& C,
    const FVector& D,
    const FVector& PreferredNormal)
{
    const int32 BaseIndex = Vertices.Num();
    FVector P0 = A;
    FVector P1 = B;
    FVector P2 = C;
    FVector P3 = D;

    const FVector DesiredNormal = PreferredNormal.GetSafeNormal();
    FVector FaceNormal = FVector::CrossProduct(P1 - P0, P2 - P0).GetSafeNormal();
    if (!DesiredNormal.IsNearlyZero() && FVector::DotProduct(FaceNormal, DesiredNormal) < 0.0f)
    {
        Swap(P1, P3);
        FaceNormal = FVector::CrossProduct(P1 - P0, P2 - P0).GetSafeNormal();
    }

    if (FaceNormal.IsNearlyZero())
    {
        FaceNormal = FVector::UpVector;
    }

    FVector FaceTangent = (P1 - P0).GetSafeNormal();
    if (FaceTangent.IsNearlyZero())
    {
        FaceTangent = FVector::CrossProduct(FaceNormal, FVector::UpVector).GetSafeNormal();
        if (FaceTangent.IsNearlyZero())
        {
            FaceTangent = FVector::RightVector;
        }
    }

    Vertices.Add(P0);
    Vertices.Add(P1);
    Vertices.Add(P2);
    Vertices.Add(P3);

    Normals.Add(FaceNormal);
    Normals.Add(FaceNormal);
    Normals.Add(FaceNormal);
    Normals.Add(FaceNormal);

    UVs.Add(FVector2D(0.0f, 0.0f));
    UVs.Add(FVector2D(1.0f, 0.0f));
    UVs.Add(FVector2D(1.0f, 1.0f));
    UVs.Add(FVector2D(0.0f, 1.0f));

    Colors.Add(FLinearColor::White);
    Colors.Add(FLinearColor::White);
    Colors.Add(FLinearColor::White);
    Colors.Add(FLinearColor::White);

    Tangents.Add(FProcMeshTangent(FaceTangent, false));
    Tangents.Add(FProcMeshTangent(FaceTangent, false));
    Tangents.Add(FProcMeshTangent(FaceTangent, false));
    Tangents.Add(FProcMeshTangent(FaceTangent, false));

    Triangles.Add(BaseIndex + 0);
    Triangles.Add(BaseIndex + 2);
    Triangles.Add(BaseIndex + 1);
    Triangles.Add(BaseIndex + 0);
    Triangles.Add(BaseIndex + 3);
    Triangles.Add(BaseIndex + 2);
}

// Adds a triangle with winding corrected toward PreferredNormal.
void AddTriangle(
    TArray<FVector>& Vertices,
    TArray<int32>& Triangles,
    TArray<FVector>& Normals,
    TArray<FVector2D>& UVs,
    TArray<FLinearColor>& Colors,
    TArray<FProcMeshTangent>& Tangents,
    const FVector& A,
    const FVector& B,
    const FVector& C,
    const FVector& PreferredNormal)
{
    const int32 BaseIndex = Vertices.Num();
    FVector P0 = A;
    FVector P1 = B;
    FVector P2 = C;

    const FVector DesiredNormal = PreferredNormal.GetSafeNormal();
    FVector FaceNormal = FVector::CrossProduct(P1 - P0, P2 - P0).GetSafeNormal();
    if (!DesiredNormal.IsNearlyZero() && FVector::DotProduct(FaceNormal, DesiredNormal) < 0.0f)
    {
        Swap(P1, P2);
        FaceNormal = FVector::CrossProduct(P1 - P0, P2 - P0).GetSafeNormal();
    }

    if (FaceNormal.IsNearlyZero())
    {
        FaceNormal = FVector::UpVector;
    }

    FVector FaceTangent = (P1 - P0).GetSafeNormal();
    if (FaceTangent.IsNearlyZero())
    {
        FaceTangent = FVector::RightVector;
    }

    Vertices.Add(P0);
    Vertices.Add(P1);
    Vertices.Add(P2);

    Normals.Add(FaceNormal);
    Normals.Add(FaceNormal);
    Normals.Add(FaceNormal);

    UVs.Add(FVector2D(0.0f, 0.0f));
    UVs.Add(FVector2D(1.0f, 0.0f));
    UVs.Add(FVector2D(0.5f, 1.0f));

    Colors.Add(FLinearColor::White);
    Colors.Add(FLinearColor::White);
    Colors.Add(FLinearColor::White);

    Tangents.Add(FProcMeshTangent(FaceTangent, false));
    Tangents.Add(FProcMeshTangent(FaceTangent, false));
    Tangents.Add(FProcMeshTangent(FaceTangent, false));

    Triangles.Add(BaseIndex + 0);
    Triangles.Add(BaseIndex + 2);
    Triangles.Add(BaseIndex + 1);
}

}

// ---------------------------------------------------------------------------
// Actor Lifecycle / Editor Integration
// ---------------------------------------------------------------------------

AProceduralLandmass::AProceduralLandmass()
{
    PrimaryActorTick.bCanEverTick = false;

    ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralMesh"));
    RootComponent = ProceduralMesh;

    OverhangMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("OverhangMesh"));
    OverhangMesh->SetupAttachment(RootComponent);

    ProceduralMesh->bUseAsyncCooking = true;
    OverhangMesh->bUseAsyncCooking = true;
    OverhangMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    OverhangMesh->SetCollisionResponseToAllChannels(ECR_Block);
}

void AProceduralLandmass::BeginPlay()
{
    Super::BeginPlay();

    // Make sure our MID exists and is synced at runtime
    EnsureTerrainMaterialInstance();
}

#if WITH_EDITOR
void AProceduralLandmass::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    const FName PropName = PropertyChangedEvent.Property
        ? PropertyChangedEvent.Property->GetFName()
        : NAME_None;

    if (IsGenerationProperty(PropName))
    {
        GenerateTerrain();
        return;
    }

    if (IsMaterialProperty(PropName))
    {
        if (PropName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, BaseTerrainMaterial))
        {
            TerrainMID = nullptr;
        }

        EnsureTerrainMaterialInstance();
    }
}
#endif // WITH_EDITOR

bool AProceduralLandmass::IsGenerationProperty(FName PropertyName) const
{
    return
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, MapWidth) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, MapHeight) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, GridSize) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, HeightMultiplier) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, WaterHeight01) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, NoiseScale) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, Seed) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, Octaves) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, Persistence) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, Lacunarity) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, CoastFalloff) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, CoastEdgeHardness) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, CoastNoiseInfluence) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, BeachFlattenStrength) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, BeachWidthScale) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, BeachNoiseReduction) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, BeachBandWidth) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, BeachFadeDistance) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, bEnableBeachErosion) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, BeachErosionStrength) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, BeachErosionScale) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, bEnableThermalErosion) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, ThermalErosionIterations) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, ThermalErosionStrength) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, ThermalTalusThreshold) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, ThermalHeightMin) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, MountainWearStrength) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, MountainBlend) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, RidgeSharpness) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, DetailNoiseScale) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, DetailStrength) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, PlateauCellSize) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, PlateauChance) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, PlateauRadiusMin) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, PlateauRadiusMax) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, PlateauFlattenStrength) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, PlateauRampWidth) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, PlateauRampReach) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, PlateauRampStrength) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, CliffShelfStrength) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, CliffShelfHeightMin) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, CliffShelfFrequency) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, CliffWallStrength) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, CliffShelfFlatness) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, CliffNoiseThreshold) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, bEnableOverhangs) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, OverhangHeightMin) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, OverhangSlopeMin) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, OverhangChance) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, OverhangCliffDeltaMin) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, OverhangDepth) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, OverhangThickness) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, OverhangLipDrop) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, OverhangNoiseAmount);
}

bool AProceduralLandmass::IsMaterialProperty(FName PropertyName) const
{
    return
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, BaseTerrainMaterial) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralLandmass, OverhangMaterial);
}

// ---------------------------------------------------------------------------
// Public Queries
// ---------------------------------------------------------------------------

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

float AProceduralLandmass::GetDefaultWaterSurfaceZ() const
{
    const FVector LocalWaterPoint(0.0f, 0.0f, WaterHeight01 * HeightMultiplier);
    return GetActorTransform().TransformPosition(LocalWaterPoint).Z;
}

float AProceduralLandmass::GetTerrainHeightAtWorldLocation(const FVector& WorldLocation) const
{
    const FVector LocalLocation = GetActorTransform().InverseTransformPosition(WorldLocation);

    float Height01 = 0.0f;
    if (!SampleHeight01AtLocalXY(LocalLocation.X, LocalLocation.Y, Height01))
    {
        return GetActorLocation().Z;
    }

    const FVector LocalTerrainPoint(LocalLocation.X, LocalLocation.Y, Height01 * HeightMultiplier);
    return GetActorTransform().TransformPosition(LocalTerrainPoint).Z;
}

bool AProceduralLandmass::SampleHeight01AtLocalXY(float LocalX, float LocalY, float& OutHeight01) const
{
    OutHeight01 = 0.0f;

    if (GridSize <= KINDA_SMALL_NUMBER || MapWidth < 2 || MapHeight < 2)
    {
        return false;
    }

    if (CachedHeightMap.Num() != MapWidth * MapHeight)
    {
        return false;
    }

    const float GridX = LocalX / GridSize;
    const float GridY = LocalY / GridSize;

    if (GridX < 0.0f || GridY < 0.0f || GridX > static_cast<float>(MapWidth - 1) || GridY > static_cast<float>(MapHeight - 1))
    {
        return false;
    }

    const int32 X0 = FMath::Clamp(FMath::FloorToInt(GridX), 0, MapWidth - 1);
    const int32 Y0 = FMath::Clamp(FMath::FloorToInt(GridY), 0, MapHeight - 1);
    const int32 X1 = FMath::Min(X0 + 1, MapWidth - 1);
    const int32 Y1 = FMath::Min(Y0 + 1, MapHeight - 1);

    const float Tx = GridX - static_cast<float>(X0);
    const float Ty = GridY - static_cast<float>(Y0);

    const float H00 = CachedHeightMap[Y0 * MapWidth + X0];
    const float H10 = CachedHeightMap[Y0 * MapWidth + X1];
    const float H01 = CachedHeightMap[Y1 * MapWidth + X0];
    const float H11 = CachedHeightMap[Y1 * MapWidth + X1];

    const float Bottom = FMath::Lerp(H00, H10, Tx);
    const float Top = FMath::Lerp(H01, H11, Tx);
    OutHeight01 = FMath::Clamp(FMath::Lerp(Bottom, Top, Ty), 0.0f, 1.0f);
    return true;
}

// ---------------------------------------------------------------------------
// Generation Pipeline
// ---------------------------------------------------------------------------

void AProceduralLandmass::GenerateTerrain()
{
    CreateMesh();
    EnsureTerrainMaterialInstance();
}

void AProceduralLandmass::EnsureTerrainMaterialInstance()
{
    if (!ProceduralMesh)
    {
        return;
    }

    // Create the terrain MID once. This lets code keep water/material params in
    // sync while still letting artists assign BaseTerrainMaterial in the editor.
    if (!TerrainMID)
    {
        // Prefer the explicitly assigned base material; otherwise use whatever
        // is already assigned to Element 0.
        UMaterialInterface* BaseMat = BaseTerrainMaterial ? BaseTerrainMaterial
            : ProceduralMesh->GetMaterial(0);

        if (!BaseMat)
        {
            return;
        }

        TerrainMID = UMaterialInstanceDynamic::Create(BaseMat, this);
        ProceduralMesh->SetMaterial(0, TerrainMID);
    }

    // Keep scalar parameters in sync with generator controls.
    if (TerrainMID)
    {
        TerrainMID->SetScalarParameterValue(TEXT("WaterHeight"), WaterHeight01);
    }

    // Overhangs can use their own debug/rock material. If none is assigned, use
    // the same terrain MID so color masks behave consistently.
    if (OverhangMesh)
    {
        if (OverhangMaterial)
        {
            OverhangMesh->SetMaterial(0, OverhangMaterial);
        }
        else if (TerrainMID)
        {
            OverhangMesh->SetMaterial(0, TerrainMID);
        }
        else if (UMaterialInterface* TerrainMat = ProceduralMesh->GetMaterial(0))
        {
            OverhangMesh->SetMaterial(0, TerrainMat);
        }
    }
}

// ---------------------------------------------------------------------------
// Main Terrain Mesh
// ---------------------------------------------------------------------------

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

    // Build the normalized 0..1 heightfield first. Geometry and material masks
    // are both derived from this same source of truth.
    TArray<float> Heights;
    BuildHeightMap(Heights);
    CachedHeightMap = Heights;

    // Build vertices, UVs, vertex colors, and tangents.
    for (int32 y = 0; y < NumVertsY; ++y)
    {
        for (int32 x = 0; x < NumVertsX; ++x)
        {
            const int32 Index = y * NumVertsX + x;

            const float Height01 = Heights.IsValidIndex(Index) ? Heights[Index] : 0.0f;
            const float Z = Height01 * HeightMultiplier;

            Vertices[Index] = FVector(x * GridSize, y * GridSize, Z);

            const float U = (NumVertsX > 1) ? (static_cast<float>(x) / (NumVertsX - 1)) : 0.0f;
            const float V = (NumVertsY > 1) ? (static_cast<float>(y) / (NumVertsY - 1)) : 0.0f;
            UVs[Index] = FVector2D(U, V);

            // Slope mask is filled after normals are computed below.
            VertexColors[Index] = MakeTerrainVertexColor(
                Height01,
                0.0f,
                WaterHeight01,
                BeachBandWidth,
                BeachFadeDistance);

            Tangents[Index] = FProcMeshTangent(1.0f, 0.0f, 0.0f);

            // Normals are accumulated from face normals after triangles exist.
            Normals[Index] = FVector::ZeroVector;
        }
    }

    // Build triangle indices for the heightfield grid.
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

    // Compute smooth-ish vertex normals from triangle normals. These are also
    // used to create the slope mask for rock material blending.
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

        const float SlopeMask = 1.0f - FMath::Clamp(N.Z, 0.0f, 1.0f);
        VertexColors[i].G = SlopeMask;
    }

    // Push mesh to the component and create collision for gameplay.
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

    BuildOverhangMesh(Heights, Normals);
}

// ---------------------------------------------------------------------------
// Procedural Overhang Mesh
// ---------------------------------------------------------------------------

void AProceduralLandmass::BuildOverhangMesh(const TArray<float>& Heights, const TArray<FVector>& Normals)
{
    if (!OverhangMesh)
    {
        return;
    }

    OverhangMesh->ClearAllMeshSections();

    if (!bEnableOverhangs || MapWidth < 2 || MapHeight < 2)
    {
        return;
    }

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> OutNormals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    TArray<FBox> OccupiedOverhangBounds;

    // Local sampling helpers keep the overhang builder decoupled from the
    // heightmap implementation details.
    auto GetVertexLocal = [this, &Heights](int32 X, int32 Y) -> FVector
    {
        const int32 Index = Y * MapWidth + X;
        const float Height01 = Heights.IsValidIndex(Index) ? Heights[Index] : 0.0f;
        return FVector(X * GridSize, Y * GridSize, Height01 * HeightMultiplier);
    };

    auto GetSlopeMask = [&Normals, this](int32 X, int32 Y) -> float
    {
        const int32 Index = Y * MapWidth + X;
        if (!Normals.IsValidIndex(Index))
        {
            return 0.0f;
        }

        return 1.0f - FMath::Clamp(Normals[Index].Z, 0.0f, 1.0f);
    };

    auto GetTerrainNormalAtLocalPoint = [this, &Normals](const FVector& LocalPoint) -> FVector
    {
        const int32 X = FMath::Clamp(FMath::RoundToInt(LocalPoint.X / FMath::Max(GridSize, 1.0f)), 0, MapWidth - 1);
        const int32 Y = FMath::Clamp(FMath::RoundToInt(LocalPoint.Y / FMath::Max(GridSize, 1.0f)), 0, MapHeight - 1);
        const int32 Index = Y * MapWidth + X;
        if (!Normals.IsValidIndex(Index))
        {
            return FVector::UpVector;
        }

        const FVector TerrainNormal = Normals[Index].GetSafeNormal();
        return TerrainNormal.IsNearlyZero() ? FVector::UpVector : TerrainNormal;
    };

    auto SampleTerrainPointAtLocalXY = [this, &Heights](float LocalX, float LocalY) -> FVector
    {
        const float SafeGrid = FMath::Max(GridSize, 1.0f);
        const float GridX = FMath::Clamp(LocalX / SafeGrid, 0.0f, static_cast<float>(MapWidth - 1));
        const float GridY = FMath::Clamp(LocalY / SafeGrid, 0.0f, static_cast<float>(MapHeight - 1));

        const int32 X0 = FMath::Clamp(FMath::FloorToInt(GridX), 0, MapWidth - 1);
        const int32 Y0 = FMath::Clamp(FMath::FloorToInt(GridY), 0, MapHeight - 1);
        const int32 X1 = FMath::Clamp(X0 + 1, 0, MapWidth - 1);
        const int32 Y1 = FMath::Clamp(Y0 + 1, 0, MapHeight - 1);

        const float FracX = GridX - static_cast<float>(X0);
        const float FracY = GridY - static_cast<float>(Y0);

        const float H00 = Heights.IsValidIndex(Y0 * MapWidth + X0) ? Heights[Y0 * MapWidth + X0] : 0.0f;
        const float H10 = Heights.IsValidIndex(Y0 * MapWidth + X1) ? Heights[Y0 * MapWidth + X1] : H00;
        const float H01 = Heights.IsValidIndex(Y1 * MapWidth + X0) ? Heights[Y1 * MapWidth + X0] : H00;
        const float H11 = Heights.IsValidIndex(Y1 * MapWidth + X1) ? Heights[Y1 * MapWidth + X1] : H00;

        const float HX0 = FMath::Lerp(H00, H10, FracX);
        const float HX1 = FMath::Lerp(H01, H11, FracX);
        const float Height01 = FMath::Lerp(HX0, HX1, FracY);

        return FVector(LocalX, LocalY, Height01 * HeightMultiplier);
    };

    auto IsEligibleEdge = [&](int32 AX, int32 AY, int32 BX, int32 BY, float EdgeDrop) -> bool
    {
        const int32 IndexA = AY * MapWidth + AX;
        const int32 IndexB = BY * MapWidth + BX;
        if (!Heights.IsValidIndex(IndexA) || !Heights.IsValidIndex(IndexB))
        {
            return false;
        }

        const float HeightA = Heights[IndexA];
        const float HeightB = Heights[IndexB];
        const float AvgHeight = (HeightA + HeightB) * 0.5f;
        const float AvgSlope = (GetSlopeMask(AX, AY) + GetSlopeMask(BX, BY)) * 0.5f;

        if (AvgHeight < OverhangHeightMin || AvgSlope < OverhangSlopeMin || EdgeDrop < OverhangCliffDeltaMin)
        {
            return false;
        }

        return true;
    };

    // Builds one continuous ledge from a run of cliff-edge points. The generated
    // points form a shelf-like closed mesh:
    // - AttachPoints: inner top edge slightly inset into terrain
    // - LipPoints: exposed outer top edge
    // - ChinPoints: lower outer break that gives the underside a rock shape
    // - BottomPoints: inner lower edge
    // - TerrainSealPoints: rear seam used to close against the terrain surface
    auto BuildOverhangStrip = [&](const TArray<FVector>& TopInnerPoints, const FVector& OutwardDir, int32 HashSalt)
    {
        if (TopInnerPoints.Num() < 4)
        {
            return;
        }

        // Average nearby terrain normals so ledges point generally away from the
        // mountain face, not merely along the raw grid edge.
        FVector HorizontalOut = FVector::ZeroVector;
        for (const FVector& Point : TopInnerPoints)
        {
            const FVector TerrainNormal = GetTerrainNormalAtLocalPoint(Point);
            HorizontalOut += FVector(TerrainNormal.X, TerrainNormal.Y, 0.0f).GetSafeNormal();
        }

        HorizontalOut = HorizontalOut.GetSafeNormal();
        if (HorizontalOut.IsNearlyZero())
        {
            HorizontalOut = FVector(OutwardDir.X, OutwardDir.Y, 0.0f).GetSafeNormal();
        }
        else if (FVector::DotProduct(HorizontalOut, FVector(OutwardDir.X, OutwardDir.Y, 0.0f).GetSafeNormal()) < 0.0f)
        {
            HorizontalOut *= -1.0f;
        }

        if (HorizontalOut.IsNearlyZero())
        {
            return;
        }

        // Lightly smooth the strip anchor line so overhangs follow the terrain
        // without every single grid-step kink becoming visible geometry.
        TArray<FVector> SmoothedPoints = TopInnerPoints;
        for (int32 i = 1; i < SmoothedPoints.Num() - 1; ++i)
        {
            SmoothedPoints[i] = (TopInnerPoints[i - 1] + TopInnerPoints[i] * 2.0f + TopInnerPoints[i + 1]) * 0.25f;
        }

        float ApproxLength = 0.0f;
        for (int32 i = 1; i < SmoothedPoints.Num(); ++i)
        {
            ApproxLength += FVector::Dist2D(SmoothedPoints[i - 1], SmoothedPoints[i]);
        }

        if (ApproxLength < GridSize * 2.5f)
        {
            return;
        }

        // Overhang tops are biased toward a shared shelf height so players can
        // stand on them instead of sliding off steep inherited mountain slopes.
        const int32 MidIndex = SmoothedPoints.Num() / 2;
        const FVector StartSurface = SmoothedPoints[0];
        const FVector MidSurface = SmoothedPoints[MidIndex];
        const FVector EndSurface = SmoothedPoints.Last();
        const float ShelfBaseZ = FMath::Max3(StartSurface.Z, MidSurface.Z, EndSurface.Z);
        const float ShelfTargetZ = ShelfBaseZ - OverhangThickness * 0.04f;

        const float InsetDepth = OverhangDepth * 0.08f;
        const float TopVariance = OverhangThickness * 0.025f;
        const float InnerDrop = OverhangThickness * 0.8f;
        const float MinCrossDepth = FMath::Max(GridSize * 0.28f, OverhangDepth * 0.18f);
        const float MinVerticalDrop = FMath::Max(GridSize * 0.18f, OverhangThickness * 0.55f);
        const float MinFaceSpan = GridSize * 0.22f;
        const float MinFaceArea = GridSize * GridSize * 0.025f;

        TArray<FVector> AttachPoints;
        TArray<FVector> LipPoints;
        TArray<FVector> ChinPoints;
        TArray<FVector> BottomPoints;
        TArray<FVector> OutwardVectors;
        TArray<FVector> TerrainSealPoints;
        TArray<FVector> TerrainBackTopPoints;
        AttachPoints.Reserve(SmoothedPoints.Num());
        LipPoints.Reserve(SmoothedPoints.Num());
        ChinPoints.Reserve(SmoothedPoints.Num());
        BottomPoints.Reserve(SmoothedPoints.Num());
        OutwardVectors.Reserve(SmoothedPoints.Num());
        TerrainSealPoints.Reserve(SmoothedPoints.Num());
        TerrainBackTopPoints.Reserve(SmoothedPoints.Num());

        FBox CandidateBounds(EForceInit::ForceInit);
        float PreviousDepth = OverhangDepth;
        FVector PreviousOut = HorizontalOut;

        for (int32 i = 0; i < SmoothedPoints.Num(); ++i)
        {
            const float T = static_cast<float>(i) / FMath::Max(SmoothedPoints.Num() - 1, 1);
            const float EdgeFade = SmoothPulse(0.0f, 0.12f, 0.88f, 1.0f, T);
            const float EndBlend = FMath::Clamp(EdgeFade, 0.0f, 1.0f);
            const float ShapeNoise = (Hash01(HashSalt + 17, i + 3, Seed + 952) - 0.5f) * 2.0f;
            const float DepthNoise = (Hash01(HashSalt + 29, i + 7, Seed + 953) - 0.5f) * 2.0f;
            const float DropNoise = (Hash01(HashSalt + 43, i + 11, Seed + 954) - 0.5f) * 2.0f;

            // Per-point outward vectors preserve some terrain-following shape,
            // then get smoothed to prevent folded or self-crossing strips.
            FVector LocalOut = FVector::ZeroVector;
            const FVector TerrainNormal = GetTerrainNormalAtLocalPoint(SmoothedPoints[i]);
            LocalOut = FVector(TerrainNormal.X, TerrainNormal.Y, 0.0f).GetSafeNormal();
            if (LocalOut.IsNearlyZero())
            {
                LocalOut = HorizontalOut;
            }
            else if (FVector::DotProduct(LocalOut, HorizontalOut) < 0.0f)
            {
                LocalOut *= -1.0f;
            }

            LocalOut = (HorizontalOut * 0.6f + LocalOut * 0.4f).GetSafeNormal();
            if (i > 0)
            {
                if (FVector::DotProduct(LocalOut, PreviousOut) < 0.55f)
                {
                    LocalOut = (PreviousOut * 0.82f + LocalOut * 0.18f).GetSafeNormal();
                }
                else
                {
                    LocalOut = (PreviousOut * 0.45f + LocalOut * 0.55f).GetSafeNormal();
                }
            }

            // Taper the first/last points so the ledge dies into the landmass
            // instead of ending as a blunt rectangular cut.
            const float EndDepthScale = FMath::Lerp(0.5f, 1.0f, EndBlend);
            const float EndDropScale = FMath::Lerp(0.7f, 1.0f, EndBlend);
            const float EndInsetScale = FMath::Lerp(1.45f, 1.0f, EndBlend);
            const float EndThicknessScale = FMath::Lerp(0.72f, 1.0f, EndBlend);

            float Depth = OverhangDepth * EndDepthScale * (1.0f + DepthNoise * OverhangNoiseAmount * 0.35f);
            if (i > 0)
            {
                Depth = FMath::Lerp(PreviousDepth, Depth, 0.35f);
            }
            const float Drop = OverhangLipDrop * EndDropScale * (0.9f + DropNoise * OverhangNoiseAmount * 0.2f);
            const float LocalShelfZ = FMath::Lerp(
                SmoothedPoints[i].Z,
                ShelfTargetZ + ShapeNoise * TopVariance,
                0.93f);
            const float LipZ = LocalShelfZ + GridSize * 0.003f + ShapeNoise * TopVariance * 0.35f;

            const FVector Attach =
                FVector(SmoothedPoints[i].X, SmoothedPoints[i].Y, LocalShelfZ) -
                LocalOut * (InsetDepth * EndInsetScale);
            const FVector Lip =
                FVector(SmoothedPoints[i].X, SmoothedPoints[i].Y, LipZ) +
                LocalOut * Depth;
            const FVector Bottom =
                Attach - FVector(0.0f, 0.0f, InnerDrop * EndThicknessScale + Drop);
            const FVector Chin =
                FMath::Lerp(Bottom, Lip, 0.42f) +
                LocalOut * (Depth * 0.06f + ShapeNoise * GridSize * 0.03f);
            const FVector TerrainBackSample = SampleTerrainPointAtLocalXY(
                SmoothedPoints[i].X - LocalOut.X * (InsetDepth + GridSize * 0.45f),
                SmoothedPoints[i].Y - LocalOut.Y * (InsetDepth + GridSize * 0.45f));
            const float SeamTopZ = Attach.Z;
            const FVector TerrainSeal =
                FVector(TerrainBackSample.X, TerrainBackSample.Y, SeamTopZ);
            const FVector TerrainBackTop =
                Attach - LocalOut * GridSize * 0.12f;

            // Reject bad geometry early. These checks prevent tiny faces,
            // folded strips, and paper-thin underside sections.
            const float CrossDepth = FVector::Dist2D(Attach, Lip);
            const float VerticalDrop = Attach.Z - Bottom.Z;
            if (CrossDepth < MinCrossDepth || VerticalDrop < MinVerticalDrop)
            {
                return;
            }

            AttachPoints.Add(Attach);
            LipPoints.Add(Lip);
            BottomPoints.Add(Bottom);
            ChinPoints.Add(Chin);
            OutwardVectors.Add(LocalOut);
            TerrainSealPoints.Add(TerrainSeal);
            TerrainBackTopPoints.Add(TerrainBackTop);

            CandidateBounds += Attach;
            CandidateBounds += Lip;
            CandidateBounds += Bottom;
            CandidateBounds += Chin;
            CandidateBounds += TerrainSeal;
            CandidateBounds += TerrainBackTop;

            PreviousDepth = Depth;
            PreviousOut = LocalOut;
        }

        CandidateBounds = CandidateBounds.ExpandBy(FVector(GridSize * 0.15f, GridSize * 0.15f, OverhangThickness * 0.2f));

        for (const FBox& ExistingBounds : OccupiedOverhangBounds)
        {
            if (CandidateBounds.Intersect(ExistingBounds))
            {
                return;
            }
        }

        // Whole-strip sanity pass. If any section is too folded, too short, or
        // points inward, skip the entire overhang rather than emit broken mesh.
        FVector PreviousSegmentDir = FVector::ZeroVector;
        // Emit the final closed strip: rear seam, top shelf, front lip,
        // underside, back wall, and end caps.
        for (int32 i = 0; i < AttachPoints.Num() - 1; ++i)
        {
            const FVector AttachDelta2D = FVector(AttachPoints[i + 1].X - AttachPoints[i].X, AttachPoints[i + 1].Y - AttachPoints[i].Y, 0.0f);
            const FVector LipDelta2D = FVector(LipPoints[i + 1].X - LipPoints[i].X, LipPoints[i + 1].Y - LipPoints[i].Y, 0.0f);
            const FVector SegmentDir = AttachDelta2D.GetSafeNormal();
            const FVector AttachToLip3DA = LipPoints[i] - AttachPoints[i];
            const FVector AttachToLip3DB = LipPoints[i + 1] - AttachPoints[i + 1];
            const FVector ChinToBottom3DA = BottomPoints[i] - ChinPoints[i];
            const FVector ChinToBottom3DB = BottomPoints[i + 1] - ChinPoints[i + 1];

            if (AttachDelta2D.SizeSquared() < FMath::Square(GridSize * 0.22f) ||
                LipDelta2D.SizeSquared() < FMath::Square(GridSize * 0.22f))
            {
                return;
            }

            if (i > 0 && FVector::DotProduct(SegmentDir, PreviousSegmentDir) < 0.2f)
            {
                return;
            }

            if (FVector::DotProduct(OutwardVectors[i], OutwardVectors[i + 1]) < 0.35f)
            {
                return;
            }

            const FVector AttachToLipA = FVector(LipPoints[i].X - AttachPoints[i].X, LipPoints[i].Y - AttachPoints[i].Y, 0.0f).GetSafeNormal();
            const FVector AttachToLipB = FVector(LipPoints[i + 1].X - AttachPoints[i + 1].X, LipPoints[i + 1].Y - AttachPoints[i + 1].Y, 0.0f).GetSafeNormal();
            if (FVector::DotProduct(AttachToLipA, AttachToLipB) < 0.4f ||
                FVector::DotProduct(AttachToLipA, HorizontalOut) < 0.45f ||
                FVector::DotProduct(AttachToLipB, HorizontalOut) < 0.45f)
            {
                return;
            }

            if (AttachToLip3DA.Size2D() < MinCrossDepth ||
                AttachToLip3DB.Size2D() < MinCrossDepth ||
                ChinToBottom3DA.Size() < MinVerticalDrop * 0.35f ||
                ChinToBottom3DB.Size() < MinVerticalDrop * 0.35f)
            {
                return;
            }

            const float TopFaceArea = FVector::CrossProduct(
                AttachPoints[i + 1] - AttachPoints[i],
                LipPoints[i] - AttachPoints[i]).Size();
            const float FrontFaceArea = FVector::CrossProduct(
                LipPoints[i + 1] - LipPoints[i],
                ChinPoints[i] - LipPoints[i]).Size();
            const float UnderFaceArea = FVector::CrossProduct(
                ChinPoints[i + 1] - ChinPoints[i],
                BottomPoints[i] - ChinPoints[i]).Size();
            if (TopFaceArea < MinFaceArea || FrontFaceArea < MinFaceArea || UnderFaceArea < MinFaceArea)
            {
                return;
            }

            const float SegmentSpan = FMath::Min(
                FVector::Dist(AttachPoints[i], AttachPoints[i + 1]),
                FVector::Dist(LipPoints[i], LipPoints[i + 1]));
            if (SegmentSpan < MinFaceSpan)
            {
                return;
            }

            PreviousSegmentDir = SegmentDir;
        }

        for (int32 i = 0; i < AttachPoints.Num() - 1; ++i)
        {
            const FVector SegmentDir = (AttachPoints[i + 1] - AttachPoints[i]).GetSafeNormal();
            const FVector SegmentOut = FVector(LipPoints[i] - AttachPoints[i]).GetSafeNormal();

            AddQuad(
                Vertices, Triangles, OutNormals, UVs, Colors, Tangents,
                TerrainSealPoints[i], TerrainSealPoints[i + 1], AttachPoints[i + 1], AttachPoints[i],
                FVector::UpVector * 0.25f - SegmentOut * 0.75f);

            AddQuad(
                Vertices, Triangles, OutNormals, UVs, Colors, Tangents,
                AttachPoints[i], AttachPoints[i + 1], LipPoints[i + 1], LipPoints[i],
                FVector::UpVector);

            AddQuad(
                Vertices, Triangles, OutNormals, UVs, Colors, Tangents,
                LipPoints[i], LipPoints[i + 1], ChinPoints[i + 1], ChinPoints[i],
                SegmentOut);

            AddQuad(
                Vertices, Triangles, OutNormals, UVs, Colors, Tangents,
                ChinPoints[i], ChinPoints[i + 1], BottomPoints[i + 1], BottomPoints[i],
                FVector::DownVector * 0.85f - SegmentOut * 0.15f);

            AddQuad(
                Vertices, Triangles, OutNormals, UVs, Colors, Tangents,
                TerrainSealPoints[i + 1], TerrainSealPoints[i], BottomPoints[i], BottomPoints[i + 1],
                -SegmentOut);

            if (i == 0)
            {
                const FVector CapBendBottom =
                    FMath::Lerp(BottomPoints[i], ChinPoints[i], 0.46f);

                AddTriangle(
                    Vertices, Triangles, OutNormals, UVs, Colors, Tangents,
                    AttachPoints[i], LipPoints[i], ChinPoints[i],
                    -SegmentDir * 0.8f);

                AddTriangle(
                    Vertices, Triangles, OutNormals, UVs, Colors, Tangents,
                    AttachPoints[i], ChinPoints[i], CapBendBottom,
                    -SegmentDir * 0.75f + FVector::DownVector * 0.25f);

                AddTriangle(
                    Vertices, Triangles, OutNormals, UVs, Colors, Tangents,
                    AttachPoints[i], CapBendBottom, BottomPoints[i],
                    -SegmentDir);

                AddTriangle(
                    Vertices, Triangles, OutNormals, UVs, Colors, Tangents,
                    TerrainSealPoints[i], BottomPoints[i], AttachPoints[i],
                    -SegmentDir * 0.75f - SegmentOut * 0.25f);
            }

            if (i == AttachPoints.Num() - 2)
            {
                const FVector CapBendBottom =
                    FMath::Lerp(BottomPoints[i + 1], ChinPoints[i + 1], 0.46f);

                AddTriangle(
                    Vertices, Triangles, OutNormals, UVs, Colors, Tangents,
                    AttachPoints[i + 1], ChinPoints[i + 1], LipPoints[i + 1],
                    SegmentDir * 0.8f);

                AddTriangle(
                    Vertices, Triangles, OutNormals, UVs, Colors, Tangents,
                    AttachPoints[i + 1], CapBendBottom, ChinPoints[i + 1],
                    SegmentDir * 0.75f + FVector::DownVector * 0.25f);

                AddTriangle(
                    Vertices, Triangles, OutNormals, UVs, Colors, Tangents,
                    AttachPoints[i + 1], BottomPoints[i + 1], CapBendBottom,
                    SegmentDir);

                AddTriangle(
                    Vertices, Triangles, OutNormals, UVs, Colors, Tangents,
                    TerrainSealPoints[i + 1], AttachPoints[i + 1], BottomPoints[i + 1],
                    SegmentDir * 0.75f - SegmentOut * 0.25f);
            }
        }

        OccupiedOverhangBounds.Add(CandidateBounds);
    };

    const float HeightDeltaThreshold = OverhangCliffDeltaMin;

    // Pass 1: scan vertical grid edges for long cliff runs.
    for (int32 X = 0; X < MapWidth - 1; ++X)
    {
        int32 Y = 0;
        while (Y < MapHeight - 1)
        {
            const int32 Index = Y * MapWidth + X;
            const float LeftHeight = Heights.IsValidIndex(Index) ? Heights[Index] : 0.0f;
            const float RightHeight = Heights.IsValidIndex(Index + 1) ? Heights[Index + 1] : LeftHeight;
            const bool bLeftHigh = LeftHeight - RightHeight > HeightDeltaThreshold;
            const bool bRightHigh = RightHeight - LeftHeight > HeightDeltaThreshold;

            if (!bLeftHigh && !bRightHigh)
            {
                ++Y;
                continue;
            }

            const bool bHighSideIsLeft = bLeftHigh;
            const FVector OutwardDir = bHighSideIsLeft ? FVector(1.0f, 0.0f, 0.0f) : FVector(-1.0f, 0.0f, 0.0f);
            const int32 RunStartY = Y;
            TArray<FVector> StripPoints;

            while (Y < MapHeight - 1)
            {
                const int32 RunIndex = Y * MapWidth + X;
                const float RunLeft = Heights.IsValidIndex(RunIndex) ? Heights[RunIndex] : 0.0f;
                const float RunRight = Heights.IsValidIndex(RunIndex + 1) ? Heights[RunIndex + 1] : RunLeft;
                const float RunDrop = bHighSideIsLeft ? (RunLeft - RunRight) : (RunRight - RunLeft);

                const bool bStillMatches = bHighSideIsLeft
                    ? (RunLeft - RunRight > HeightDeltaThreshold)
                    : (RunRight - RunLeft > HeightDeltaThreshold);

                const int32 AX = bHighSideIsLeft ? X : X + 1;
                const int32 BX = AX;
                const int32 AY = Y;
                const int32 BY = Y + 1;

                if (!bStillMatches || !IsEligibleEdge(AX, AY, BX, BY, RunDrop))
                {
                    break;
                }

                if (StripPoints.Num() == 0)
                {
                    StripPoints.Add(GetVertexLocal(AX, AY));
                }
                StripPoints.Add(GetVertexLocal(BX, BY));
                ++Y;
            }

            if (StripPoints.Num() >= 4)
            {
                const float RunLength01 = FMath::Clamp((static_cast<float>(StripPoints.Num()) - 4.0f) / 8.0f, 0.0f, 1.0f);
                const float SpawnChance = OverhangChance * FMath::Lerp(0.3f, 1.0f, RunLength01);
                const float SpawnRoll = Hash01(X + 11, RunStartY + 11, Seed + 911);
                if (SpawnRoll <= SpawnChance)
                {
                    BuildOverhangStrip(StripPoints, OutwardDir, 11 + X + RunStartY);
                }
            }
            else
            {
                ++Y;
            }
        }
    }

    // Pass 2: scan horizontal grid edges for long cliff runs.
    for (int32 Y = 0; Y < MapHeight - 1; ++Y)
    {
        int32 X = 0;
        while (X < MapWidth - 1)
        {
            const int32 Index = Y * MapWidth + X;
            const float BottomHeight = Heights.IsValidIndex(Index) ? Heights[Index] : 0.0f;
            const float TopHeight = Heights.IsValidIndex(Index + MapWidth) ? Heights[Index + MapWidth] : BottomHeight;
            const bool bBottomHigh = BottomHeight - TopHeight > HeightDeltaThreshold;
            const bool bTopHigh = TopHeight - BottomHeight > HeightDeltaThreshold;

            if (!bBottomHigh && !bTopHigh)
            {
                ++X;
                continue;
            }

            const bool bHighSideIsBottom = bBottomHigh;
            const FVector OutwardDir = bHighSideIsBottom ? FVector(0.0f, 1.0f, 0.0f) : FVector(0.0f, -1.0f, 0.0f);
            const int32 RunStartX = X;
            TArray<FVector> StripPoints;

            while (X < MapWidth - 1)
            {
                const int32 RunIndex = Y * MapWidth + X;
                const float RunBottom = Heights.IsValidIndex(RunIndex) ? Heights[RunIndex] : 0.0f;
                const float RunTop = Heights.IsValidIndex(RunIndex + MapWidth) ? Heights[RunIndex + MapWidth] : RunBottom;
                const float RunDrop = bHighSideIsBottom ? (RunBottom - RunTop) : (RunTop - RunBottom);

                const bool bStillMatches = bHighSideIsBottom
                    ? (RunBottom - RunTop > HeightDeltaThreshold)
                    : (RunTop - RunBottom > HeightDeltaThreshold);

                const int32 AY = bHighSideIsBottom ? Y : Y + 1;
                const int32 BY = AY;
                const int32 AX = X + 1;
                const int32 BX = X;

                if (!bStillMatches || !IsEligibleEdge(AX, AY, BX, BY, RunDrop))
                {
                    break;
                }

                if (StripPoints.Num() == 0)
                {
                    StripPoints.Add(GetVertexLocal(BX, BY));
                }
                StripPoints.Add(GetVertexLocal(AX, AY));
                ++X;
            }

            if (StripPoints.Num() >= 4)
            {
                const float RunLength01 = FMath::Clamp((static_cast<float>(StripPoints.Num()) - 4.0f) / 8.0f, 0.0f, 1.0f);
                const float SpawnChance = OverhangChance * FMath::Lerp(0.3f, 1.0f, RunLength01);
                const float SpawnRoll = Hash01(RunStartX + 23, Y + 23, Seed + 923);
                if (SpawnRoll <= SpawnChance)
                {
                    BuildOverhangStrip(StripPoints, OutwardDir, 23 + RunStartX + Y);
                }
            }
            else
            {
                ++X;
            }
        }
    }

    if (Vertices.Num() == 0)
    {
        return;
    }

    for (int32 i = 0; i < Vertices.Num(); ++i)
    {
        const float Height01 = (HeightMultiplier > KINDA_SMALL_NUMBER)
            ? FMath::Clamp(Vertices[i].Z / HeightMultiplier, 0.0f, 1.0f)
            : 0.0f;

        const FVector FaceNormal = OutNormals.IsValidIndex(i) ? OutNormals[i].GetSafeNormal() : FVector::UpVector;
        const float SlopeMask = 1.0f - FMath::Clamp(FaceNormal.Z, 0.0f, 1.0f);

        Colors[i] = MakeTerrainVertexColor(
            Height01,
            SlopeMask,
            WaterHeight01,
            BeachBandWidth,
            BeachFadeDistance);
    }

    OverhangMesh->CreateMeshSection_LinearColor(
        0,
        Vertices,
        Triangles,
        OutNormals,
        UVs,
        Colors,
        Tangents,
        true);

    OverhangMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    OverhangMesh->SetCollisionResponseToAllChannels(ECR_Block);

    if (OverhangMaterial)
    {
        OverhangMesh->SetMaterial(0, OverhangMaterial);
    }
    else if (TerrainMID)
    {
        OverhangMesh->SetMaterial(0, TerrainMID);
    }
    else if (BaseTerrainMaterial)
    {
        OverhangMesh->SetMaterial(0, BaseTerrainMaterial);
    }
}

// ---------------------------------------------------------------------------
// Heightmap Generation
// ---------------------------------------------------------------------------
// This function owns the main landmass look. It intentionally keeps the terrain
// as a heightfield and layers systems in this order:
// 1. world-aligned fBm/ridged noise
// 2. island/coast shaping
// 3. sparse plateaus and access ramps
// 4. heightfield-safe cliff shelves
// 5. beach flattening / tidal erosion
// 6. mountain wear
// 7. thermal erosion post-process

void AProceduralLandmass::BuildHeightMap(TArray<float>& OutHeights) const
{
    const int32 NumVerts = MapWidth * MapHeight;
    OutHeights.SetNum(NumVerts);

    if (NoiseScale <= KINDA_SMALL_NUMBER || DetailNoiseScale <= KINDA_SMALL_NUMBER)
    {
        for (int32 i = 0; i < NumVerts; ++i)
        {
            OutHeights[i] = 0.0f;
        }
        return;
    }

    // Convert actor location into grid-space so noise remains world-aligned if
    // the landmass is moved or eventually tiled.
    const float InvGridSize = (GridSize > 0.0f) ? (1.0f / GridSize) : 0.0f;
    const FVector ActorLoc = GetActorLocation();

    const float BaseWorldX = ActorLoc.X * InvGridSize;
    const float BaseWorldY = ActorLoc.Y * InvGridSize;
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

            const float WorldGridX = BaseWorldX + static_cast<float>(x);
            const float WorldGridY = BaseWorldY + static_cast<float>(y);

            // CoastMask is high inland and low near the island edge.
            const float CenteredX = (MapWidth > 1) ? (static_cast<float>(x) / (MapWidth - 1)) * 2.0f - 1.0f : 0.0f;
            const float CenteredY = (MapHeight > 1) ? (static_cast<float>(y) / (MapHeight - 1)) * 2.0f - 1.0f : 0.0f;
            const float RadialDistance = FMath::Sqrt(CenteredX * CenteredX + CenteredY * CenteredY);
            const float Falloff = FMath::Clamp((RadialDistance - CoastFalloff) / FMath::Max(1.0f - CoastFalloff, 0.001f), 0.0f, 1.0f);
            const float CoastMask = 1.0f - FMath::Pow(Falloff, CoastEdgeHardness);

            // Domain warping keeps the Perlin layers from looking too grid-like.
            const float WarpNoiseX = FMath::PerlinNoise2D(FVector2D((WorldGridX + Offset.X) / 140.0f, (WorldGridY + Offset.Y) / 140.0f));
            const float WarpNoiseY = FMath::PerlinNoise2D(FVector2D((WorldGridX - Offset.X) / 140.0f, (WorldGridY - Offset.Y) / 140.0f));
            const float WarpedGridX = WorldGridX + WarpNoiseX * 18.0f;
            const float WarpedGridY = WorldGridY + WarpNoiseY * 18.0f;

            const float SampleX = (WarpedGridX + Offset.X) / NoiseScale;
            const float SampleY = (WarpedGridY + Offset.Y) / NoiseScale;
            const float DetailSampleX = (WarpedGridX - Offset.X * 0.35f) / DetailNoiseScale;
            const float DetailSampleY = (WarpedGridY + Offset.Y * 0.35f) / DetailNoiseScale;

            // Macro terrain: soft fBm hills blended with ridged mountain noise.
            const float BaseNoise = ComputeFractalNoise(SampleX, SampleY, Octaves, Persistence, Lacunarity);
            const float RidgeNoise = ComputeRidgedNoise(SampleX * 0.85f, SampleY * 0.85f, Octaves, Persistence, Lacunarity, RidgeSharpness);
            const float DetailNoise = FMath::PerlinNoise2D(FVector2D(DetailSampleX, DetailSampleY)) * 0.5f + 0.5f;

            const float MountainMask = FMath::Pow(FMath::Clamp(BaseNoise, 0.0f, 1.0f), 1.35f);
            const float BlendedMountains = FMath::Lerp(BaseNoise, RidgeNoise, MountainBlend * MountainMask);
            const float DetailedHeight = BlendedMountains + (DetailNoise - 0.5f) * DetailStrength;
            const float CoastShapedHeight = DetailedHeight * FMath::Lerp(1.0f, CoastMask, 1.0f - CoastNoiseInfluence);
            const float CoastWithVariation = CoastShapedHeight - (1.0f - CoastMask) * CoastNoiseInfluence * 0.25f;

            const float ValleyFloor = FMath::Pow(FMath::Clamp(CoastWithVariation, 0.0f, 1.0f), 1.15f);
            const float PeakBoost = FMath::Pow(ValleyFloor, 1.35f);

            float FinalHeight = FMath::Clamp(
                FMath::Lerp(ValleyFloor, PeakBoost, MountainMask * 0.65f),
                0.0f,
                1.0f);

            // Sparse large-shape landforms: flat plateaus with broad access
            // approaches. These remain heightfield-safe.
            if (PlateauCellSize > 0.0f && PlateauChance > 0.0f)
            {
                const int32 CellX = FMath::FloorToInt(WorldGridX / PlateauCellSize);
                const int32 CellY = FMath::FloorToInt(WorldGridY / PlateauCellSize);

                float BestCandidateScore = 0.0f;
                float BestFeatureMask = 0.0f;
                float BestCapMask = 0.0f;
                float BestTargetHeight = FinalHeight;
                float BestAccessHeight = FinalHeight;
                float BestAccessMask = 0.0f;

                for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                {
                    for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                    {
                        const int32 CandidateCellX = CellX + OffsetX;
                        const int32 CandidateCellY = CellY + OffsetY;

                        const float SpawnRoll = Hash01(CandidateCellX, CandidateCellY, Seed + 701);
                        if (SpawnRoll > PlateauChance)
                        {
                            continue;
                        }

                        const FVector2D CellJitter(
                            (Hash01(CandidateCellX, CandidateCellY, Seed + 702) - 0.5f) * PlateauCellSize * 0.7f,
                            (Hash01(CandidateCellX, CandidateCellY, Seed + 703) - 0.5f) * PlateauCellSize * 0.7f);
                        const FVector2D CellCenter(
                            (static_cast<float>(CandidateCellX) + 0.5f) * PlateauCellSize + CellJitter.X,
                            (static_cast<float>(CandidateCellY) + 0.5f) * PlateauCellSize + CellJitter.Y);

                        const FVector2D ToCenter(WorldGridX - CellCenter.X, WorldGridY - CellCenter.Y);
                        const float DistToCenter = ToCenter.Size();

                        const float Radius01 = FMath::Lerp(
                            PlateauRadiusMin,
                            PlateauRadiusMax,
                            Hash01(CandidateCellX, CandidateCellY, Seed + 705));
                        const float FeatureRadius = Radius01 * FMath::Min(MapWidth, MapHeight);
                        const float RampReach = FMath::Max(PlateauRampReach, 0.2f);
                        const float RampOuterRadius = FeatureRadius * (1.0f + RampReach);
                        if (FeatureRadius <= KINDA_SMALL_NUMBER || DistToCenter >= RampOuterRadius)
                        {
                            continue;
                        }

                        const float InnerRadius = FeatureRadius * 0.55f;
                        const float FeatureMask = (DistToCenter < FeatureRadius)
                            ? (1.0f - FMath::SmoothStep(InnerRadius, FeatureRadius, DistToCenter))
                            : 0.0f;
                        const float CandidateInfluence = 1.0f - FMath::SmoothStep(FeatureRadius * 0.92f, RampOuterRadius, DistToCenter);
                        if (CandidateInfluence <= BestCandidateScore)
                        {
                            continue;
                        }

                        const float TargetHeight = FMath::Lerp(
                            0.52f,
                            0.74f,
                            Hash01(CandidateCellX, CandidateCellY, Seed + 706));

                        BestCandidateScore = CandidateInfluence;
                        BestFeatureMask = FeatureMask;
                        BestCapMask = FeatureMask;
                        BestTargetHeight = FMath::Max(
                            FinalHeight,
                            TargetHeight);
                        BestAccessMask = 0.0f;
                        BestAccessHeight = BestTargetHeight;

                        const float AccessAngle = Hash01(CandidateCellX, CandidateCellY, Seed + 707) * UE_TWO_PI;
                        const FVector2D AccessDir(FMath::Cos(AccessAngle), FMath::Sin(AccessAngle));
                        const FVector2D AccessPerp(-AccessDir.Y, AccessDir.X);

                        // Positive forward means "toward the chosen accessible side of the plateau".
                        const float Forward = -FVector2D::DotProduct(ToCenter, AccessDir);
                        const float Lateral = FMath::Abs(FVector2D::DotProduct(ToCenter, AccessPerp));

                        const float AccessOuterRadius = FeatureRadius * (1.0f + RampReach);
                        const float PlateauTopRadius = FeatureRadius * 0.58f;
                        const float AccessHalfWidth = FeatureRadius * (0.22f + PlateauRampWidth * 1.6f);

                        const float DirectionMask = FMath::SmoothStep(-FeatureRadius * 0.08f, FeatureRadius * 0.22f, Forward);
                        const float WidthMask = 1.0f - FMath::SmoothStep(
                            AccessHalfWidth * 0.65f,
                            FMath::Max(AccessHalfWidth, 0.001f),
                            Lateral);
                        const float RadialMask = 1.0f - FMath::SmoothStep(
                            PlateauTopRadius,
                            AccessOuterRadius,
                            DistToCenter);

                        BestAccessMask = FMath::Clamp(
                            DirectionMask * WidthMask * RadialMask * PlateauRampStrength,
                            0.0f,
                            1.0f);

                        const float AccessProgress = 1.0f - FMath::Clamp(
                            (DistToCenter - PlateauTopRadius) / FMath::Max((AccessOuterRadius - PlateauTopRadius), 0.001f),
                            0.0f,
                            1.0f);

                        // Build a broad natural approach that fully meets the plateau top at the breach.
                        const float AccessProfile = FMath::SmoothStep(0.0f, 1.0f, AccessProgress);
                        BestAccessHeight = FMath::Lerp(FinalHeight, BestTargetHeight, AccessProfile);

                        // Soften the steep rim on the access side, while preserving most of the plateau cap.
                        const float RimBreachMask = FMath::Clamp(
                            BestAccessMask * SmoothPulse(
                                PlateauTopRadius * 0.9f,
                                PlateauTopRadius,
                                FeatureRadius * 0.92f,
                                FeatureRadius * 1.04f,
                                DistToCenter),
                            0.0f,
                            1.0f);
                        BestFeatureMask = FMath::Max(BestFeatureMask * (1.0f - RimBreachMask), FeatureMask * (1.0f - RimBreachMask));
                        BestAccessHeight = FMath::Min(BestAccessHeight, BestTargetHeight);
                    }
                }

                if (BestFeatureMask > 0.0f || BestAccessMask > 0.0f)
                {
                    const float FlattenStrength = PlateauFlattenStrength;
                    if (BestFeatureMask > 0.0f)
                    {
                        FinalHeight = FMath::Lerp(
                            FinalHeight,
                            BestTargetHeight,
                            BestFeatureMask * FlattenStrength);
                    }

                    if (BestAccessMask > 0.0f)
                    {
                        FinalHeight = FMath::Lerp(FinalHeight, BestAccessHeight, BestAccessMask);
                    }

                    if (BestCapMask > 0.0f)
                    {
                        // Preserve a flatter plateau top even after the access breach shapes the rim.
                        const float CapPreserveMask = FMath::SmoothStep(0.35f, 0.78f, BestCapMask);
                        FinalHeight = FMath::Lerp(
                            FinalHeight,
                            BestTargetHeight,
                            CapPreserveMask * FlattenStrength);
                    }
                }
            }

            // Heightfield-safe cliff shelf shaping for high rocky regions.
            if (CliffShelfStrength > 0.0f && CliffShelfFrequency > 0.0f)
            {
                const float ShelfNoise = ComputeRidgedNoise(
                    SampleX * 1.9f,
                    SampleY * 1.9f,
                    3,
                    0.6f,
                    2.15f,
                    1.8f);
                const float CliffMicroNoise = ComputeRidgedNoise(
                    SampleX * 3.1f,
                    SampleY * 3.1f,
                    2,
                    0.55f,
                    2.0f,
                    1.5f);
                const float HighCliffMask =
                    FMath::SmoothStep(CliffShelfHeightMin, 1.0f, FinalHeight) *
                    FMath::SmoothStep(0.5f, 0.82f, RidgeNoise) *
                    FMath::SmoothStep(CliffNoiseThreshold, 0.82f, ShelfNoise) *
                    FMath::SmoothStep(0.35f, 0.7f, CliffMicroNoise);

                const float ShelfStep = 1.0f / CliffShelfFrequency;
                const float ShelfIndex = FMath::FloorToFloat(FinalHeight / ShelfStep);
                const float ShelfBase = ShelfIndex * ShelfStep;
                const float ShelfFraction = (ShelfStep > KINDA_SMALL_NUMBER)
                    ? (FinalHeight - ShelfBase) / ShelfStep
                    : 0.0f;

                const float Flatness = FMath::Clamp(CliffShelfFlatness, 0.05f, 0.8f);
                const float ShelfTopMask = FMath::SmoothStep(0.0f, Flatness, ShelfFraction);
                const float ShelfTopHeight = ShelfBase + ShelfStep * Flatness * 0.55f;
                const float ShelfLedgeHeight = ShelfBase + ShelfStep * 0.18f;

                // Lower part of each band becomes a flatter ledge; upper part becomes a steeper wall.
                const float ShelfTarget = FMath::Lerp(ShelfLedgeHeight, FinalHeight, ShelfTopMask);
                const float WallTarget = FMath::Lerp(FinalHeight, ShelfBase + ShelfStep * 0.96f, FMath::Pow(FMath::Clamp(ShelfFraction, 0.0f, 1.0f), 0.55f));

                FinalHeight = FMath::Lerp(FinalHeight, ShelfTarget, HighCliffMask * CliffShelfStrength);
                FinalHeight = FMath::Lerp(FinalHeight, WallTarget, HighCliffMask * CliffWallStrength);
                FinalHeight = FMath::Clamp(FinalHeight, 0.0f, 1.0f);
            }

            // Shoreline pass. This acts like a lightweight tidal erosion layer:
            // flatten the sea-level band, suppress noisy beach hills, then add
            // subtle berm/wash variation so the beach is not perfectly uniform.
            if (BeachFlattenStrength > 0.0f && BeachWidthScale > 0.0f)
            {
                const float BeachOuter = WaterHeight01 + BeachBandWidth * BeachWidthScale + BeachFadeDistance * 0.45f;
                const float BeachInner = WaterHeight01 - BeachBandWidth * 0.28f;
                const float CoastProximity = 1.0f - CoastMask;
                const float CoastalBeachMask =
                    FMath::SmoothStep(0.06f, 0.55f, CoastProximity) *
                    FMath::SmoothStep(BeachInner, WaterHeight01 + 0.025f, FinalHeight) *
                    (1.0f - FMath::SmoothStep(
                        WaterHeight01 + BeachBandWidth * BeachWidthScale,
                        FMath::Max(BeachOuter, WaterHeight01 + BeachBandWidth * BeachWidthScale + 0.001f),
                        FinalHeight));

                if (CoastalBeachMask > 0.0f)
                {
                    const float BeachT = FMath::Clamp(
                        (FinalHeight - WaterHeight01) / FMath::Max(BeachOuter - WaterHeight01, 0.001f),
                        0.0f,
                        1.0f);

                    const float FlatBeachProfile =
                        WaterHeight01 +
                        FMath::Pow(BeachT, 1.85f) * (BeachOuter - WaterHeight01) * 0.82f;

                    FinalHeight = FMath::Lerp(
                        FinalHeight,
                        FlatBeachProfile,
                        CoastalBeachMask * BeachFlattenStrength);

                    FinalHeight = FMath::Lerp(
                        FinalHeight,
                        FlatBeachProfile,
                        CoastalBeachMask * BeachNoiseReduction * 0.55f);

                    if (bEnableBeachErosion && BeachErosionStrength > 0.0f && BeachErosionScale > 0.0f)
                    {
                        const float ErosionSampleX = (WarpedGridX + Offset.X * 0.17f) / BeachErosionScale;
                        const float ErosionSampleY = (WarpedGridY - Offset.Y * 0.17f) / BeachErosionScale;
                        const float ErosionNoise = FMath::PerlinNoise2D(FVector2D(ErosionSampleX, ErosionSampleY)) * 0.5f + 0.5f;
                        const float ErosionRidge = ComputeRidgedNoise(
                            ErosionSampleX * 0.65f,
                            ErosionSampleY * 0.65f,
                            3,
                            0.58f,
                            2.0f,
                            1.7f);

                        const float BermMask =
                            FMath::SmoothStep(0.35f, 0.72f, BeachT) *
                            (1.0f - FMath::SmoothStep(0.82f, 1.0f, BeachT));
                        const float WashMask =
                            (1.0f - FMath::SmoothStep(0.22f, 0.58f, BeachT)) *
                            FMath::SmoothStep(0.0f, 0.35f, BeachT);

                        const float BermOffset = (ErosionRidge - 0.5f) * 0.05f * BeachErosionStrength * BermMask;
                        const float WashOffset = (ErosionNoise - 0.5f) * 0.03f * BeachErosionStrength * WashMask;

                        FinalHeight += (BermOffset + WashOffset) * CoastalBeachMask;
                        FinalHeight = FMath::Lerp(
                            FinalHeight,
                            FlatBeachProfile,
                            CoastalBeachMask * BeachNoiseReduction * 0.2f);
                        FinalHeight = FMath::Clamp(FinalHeight, 0.0f, 1.0f);
                    }
                }
            }

            // Pre-thermal mountain wear: nudges sharp ridged peaks slightly back
            // toward the smoother valley profile before the neighbor-transfer pass.
            if (MountainWearStrength > 0.0f)
            {
                const float MountainWearMask =
                    FMath::SmoothStep(ThermalHeightMin, 1.0f, FinalHeight) *
                    FMath::SmoothStep(0.48f, 0.86f, RidgeNoise) *
                    CoastMask;
                const float WearTarget = FMath::Lerp(FinalHeight, ValleyFloor, 0.22f);
                FinalHeight = FMath::Lerp(
                    FinalHeight,
                    WearTarget,
                    MountainWearMask * MountainWearStrength);
                FinalHeight = FMath::Clamp(FinalHeight, 0.0f, 1.0f);
            }

            OutHeights[Index] = FinalHeight;
        }
    }

    // Thermal erosion post-process. This approximates loose material sliding
    // from steep high vertices to lower neighbors once the local height
    // difference exceeds a talus threshold.
    if (bEnableThermalErosion && ThermalErosionIterations > 0 && ThermalErosionStrength > 0.0f)
    {
        TArray<float> ErodedHeights = OutHeights;
        TArray<float> WorkingHeights = OutHeights;
        WorkingHeights.SetNumUninitialized(OutHeights.Num());

        // Keep the newly flattened beach shelf from being re-eroded by the
        // mountain-focused pass.
        const float BeachProtectionHeight =
            WaterHeight01 + BeachBandWidth * BeachWidthScale + BeachFadeDistance * 0.45f;

        for (int32 Iteration = 0; Iteration < ThermalErosionIterations; ++Iteration)
        {
            WorkingHeights = ErodedHeights;

            for (int32 Y = 1; Y < MapHeight - 1; ++Y)
            {
                for (int32 X = 1; X < MapWidth - 1; ++X)
                {
                    const int32 CenterIndex = Y * MapWidth + X;
                    const float CenterHeight = ErodedHeights[CenterIndex];
                    if (CenterHeight < FMath::Max(ThermalHeightMin, BeachProtectionHeight))
                    {
                        continue;
                    }

                    // Gather lower neighbors that exceed the talus angle.
                    float TotalExcess = 0.0f;
                    float NeighborExcess[8] = {};
                    int32 NeighborIndices[8] = {};
                    int32 NeighborCount = 0;

                    for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                    {
                        for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                        {
                            if (OffsetX == 0 && OffsetY == 0)
                            {
                                continue;
                            }

                            const int32 NeighborX = X + OffsetX;
                            const int32 NeighborY = Y + OffsetY;
                            const int32 NeighborIndex = NeighborY * MapWidth + NeighborX;
                            const float NeighborHeight = ErodedHeights[NeighborIndex];
                            const float Delta = CenterHeight - NeighborHeight;
                            const float Excess = Delta - ThermalTalusThreshold;
                            if (Excess <= 0.0f)
                            {
                                continue;
                            }

                            NeighborIndices[NeighborCount] = NeighborIndex;
                            NeighborExcess[NeighborCount] = Excess;
                            TotalExcess += Excess;
                            ++NeighborCount;
                        }
                    }

                    if (NeighborCount == 0 || TotalExcess <= KINDA_SMALL_NUMBER)
                    {
                        continue;
                    }

                    // Move a small amount of material away from the steep point
                    // and distribute it proportionally among eligible neighbors.
                    const float TransferAmount = FMath::Min(
                        TotalExcess * 0.5f,
                        (CenterHeight - FMath::Max(ThermalHeightMin, BeachProtectionHeight)) * ThermalErosionStrength);

                    if (TransferAmount <= KINDA_SMALL_NUMBER)
                    {
                        continue;
                    }

                    WorkingHeights[CenterIndex] -= TransferAmount;

                    for (int32 NeighborIdx = 0; NeighborIdx < NeighborCount; ++NeighborIdx)
                    {
                        const float Share = TransferAmount * (NeighborExcess[NeighborIdx] / TotalExcess);
                        WorkingHeights[NeighborIndices[NeighborIdx]] += Share;
                    }
                }
            }

            for (int32 Index = 0; Index < WorkingHeights.Num(); ++Index)
            {
                WorkingHeights[Index] = FMath::Clamp(WorkingHeights[Index], 0.0f, 1.0f);
            }

            ErodedHeights = MoveTemp(WorkingHeights);
        }

        OutHeights = MoveTemp(ErodedHeights);
    }
}
