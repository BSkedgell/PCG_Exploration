#include "ProceduralCliffGenerator.h"

#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralLandmass.h"
#include "ProceduralMeshComponent.h"

namespace
{
    const FLinearColor DebugFrontFaceColor(1.0f, 0.04f, 0.02f, 1.0f);
    const FLinearColor DebugOverhangUndersideColor(1.0f, 0.28f, 0.02f, 1.0f);
    const FLinearColor DebugTopColor(1.0f, 0.86f, 0.05f, 1.0f);
    const FLinearColor DebugBottomColor(0.15f, 0.95f, 1.0f, 1.0f);
    const FLinearColor DebugEndColor(1.0f, 0.2f, 1.0f, 1.0f);
    const FLinearColor DebugBackColor(0.05f, 0.2f, 1.0f, 1.0f);
    const FLinearColor DebugLeftFaceColor(0.05f, 0.85f, 0.18f, 1.0f);
    const FLinearColor DebugRightFaceColor(0.55f, 0.55f, 0.55f, 1.0f);
}

AProceduralCliffGenerator::AProceduralCliffGenerator()
{
    PrimaryActorTick.bCanEverTick = false;

    CliffMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CliffMesh"));
    RootComponent = CliffMesh;
    CliffMesh->bUseAsyncCooking = true;

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> DebugMaterialFinder(TEXT("/Game/Materials/M_DebugVertexColor.M_DebugVertexColor"));
    if (DebugMaterialFinder.Succeeded())
    {
        DebugColorMaterial = DebugMaterialFinder.Object;
    }
}

void AProceduralCliffGenerator::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    GenerateCliff();
}

void AProceduralCliffGenerator::GenerateCliff()
{
    if (!CliffMesh)
    {
        return;
    }

    ClearCliff();

    if (bUsePyramidPrototype)
    {
        GeneratePyramidCliff();
        return;
    }

    TArray<FVector> FaceVertices;
    TArray<int32> FaceTriangles;
    TArray<FVector2D> FaceUVs;
    TArray<FLinearColor> FaceVertexColors;
    TArray<FProcMeshTangent> FaceTangents;
    BuildFaceGrid(FaceVertices, FaceTriangles, FaceUVs, FaceVertexColors, FaceTangents);
    MakeGeometryDoubleSided(FaceVertices, FaceTriangles, FaceUVs, FaceVertexColors, FaceTangents);

    TArray<FVector> FaceNormals;
    CalculateAveragedNormals(FaceVertices, FaceTriangles, FaceNormals);
    CliffMesh->CreateMeshSection_LinearColor(
        0,
        FaceVertices,
        FaceTriangles,
        FaceNormals,
        FaceUVs,
        FaceVertexColors,
        FaceTangents,
        bCreateCollision);

    TArray<FVector> TopVertices;
    TArray<int32> TopTriangles;
    TArray<FVector2D> TopUVs;
    TArray<FLinearColor> TopVertexColors;
    TArray<FProcMeshTangent> TopTangents;
    BuildStitchStrip(
        FaceVertices,
        GetEffectiveVerticalSegments(),
        FVector(0.0f, -GetCliffDirectionSign() * FMath::Abs(TopStitchDepth), 0.0f),
        DebugTopColor,
        false,
        TopVertices,
        TopTriangles,
        TopUVs,
        TopVertexColors,
        TopTangents);
    const TArray<FVector> TopStripVerticesForBackWall = TopVertices;
    MakeGeometryDoubleSided(TopVertices, TopTriangles, TopUVs, TopVertexColors, TopTangents);

    TArray<FVector> TopNormals;
    CalculateAveragedNormals(TopVertices, TopTriangles, TopNormals);
    CliffMesh->CreateMeshSection_LinearColor(
        1,
        TopVertices,
        TopTriangles,
        TopNormals,
        TopUVs,
        TopVertexColors,
        TopTangents,
        bCreateCollision);

    TArray<FVector> BottomVertices;
    TArray<int32> BottomTriangles;
    TArray<FVector2D> BottomUVs;
    TArray<FLinearColor> BottomVertexColors;
    TArray<FProcMeshTangent> BottomTangents;
    BuildStitchStrip(
        FaceVertices,
        0,
        FVector(0.0f, GetCliffDirectionSign() * FMath::Abs(BottomStitchDepth), 0.0f),
        DebugBottomColor,
        true,
        BottomVertices,
        BottomTriangles,
        BottomUVs,
        BottomVertexColors,
        BottomTangents);
    const TArray<FVector> BottomStripVerticesForBackWall = BottomVertices;
    MakeGeometryDoubleSided(BottomVertices, BottomTriangles, BottomUVs, BottomVertexColors, BottomTangents);

    TArray<FVector> BottomNormals;
    CalculateAveragedNormals(BottomVertices, BottomTriangles, BottomNormals);
    CliffMesh->CreateMeshSection_LinearColor(
        2,
        BottomVertices,
        BottomTriangles,
        BottomNormals,
        BottomUVs,
        BottomVertexColors,
        BottomTangents,
        bCreateCollision);

    if (bCreateEndStitchStrips)
    {
        TArray<FVector> LeftEndVertices;
        TArray<int32> LeftEndTriangles;
        TArray<FVector2D> LeftEndUVs;
        TArray<FLinearColor> LeftEndVertexColors;
        TArray<FProcMeshTangent> LeftEndTangents;
        BuildEndStitchStrip(
            FaceVertices,
            0,
            FVector(-FMath::Abs(EndStitchDepth), 0.0f, 0.0f),
            DebugEndColor,
            true,
            LeftEndVertices,
            LeftEndTriangles,
            LeftEndUVs,
            LeftEndVertexColors,
            LeftEndTangents);
        MakeGeometryDoubleSided(LeftEndVertices, LeftEndTriangles, LeftEndUVs, LeftEndVertexColors, LeftEndTangents);

        TArray<FVector> LeftEndNormals;
        CalculateAveragedNormals(LeftEndVertices, LeftEndTriangles, LeftEndNormals);
        CliffMesh->CreateMeshSection_LinearColor(
            3,
            LeftEndVertices,
            LeftEndTriangles,
            LeftEndNormals,
            LeftEndUVs,
            LeftEndVertexColors,
            LeftEndTangents,
            bCreateCollision);

        TArray<FVector> RightEndVertices;
        TArray<int32> RightEndTriangles;
        TArray<FVector2D> RightEndUVs;
        TArray<FLinearColor> RightEndVertexColors;
        TArray<FProcMeshTangent> RightEndTangents;
        BuildEndStitchStrip(
            FaceVertices,
            GetEffectiveHorizontalSegments(),
            FVector(FMath::Abs(EndStitchDepth), 0.0f, 0.0f),
            DebugEndColor,
            false,
            RightEndVertices,
            RightEndTriangles,
            RightEndUVs,
            RightEndVertexColors,
            RightEndTangents);
        MakeGeometryDoubleSided(RightEndVertices, RightEndTriangles, RightEndUVs, RightEndVertexColors, RightEndTangents);

        TArray<FVector> RightEndNormals;
        CalculateAveragedNormals(RightEndVertices, RightEndTriangles, RightEndNormals);
        CliffMesh->CreateMeshSection_LinearColor(
            4,
            RightEndVertices,
            RightEndTriangles,
            RightEndNormals,
            RightEndUVs,
            RightEndVertexColors,
            RightEndTangents,
            bCreateCollision);
    }

    if (bCreateBackStitchWall)
    {
        TArray<FVector> BackWallVertices;
        TArray<int32> BackWallTriangles;
        TArray<FVector2D> BackWallUVs;
        TArray<FLinearColor> BackWallVertexColors;
        TArray<FProcMeshTangent> BackWallTangents;
        BuildBackStitchWall(
            TopStripVerticesForBackWall,
            BottomStripVerticesForBackWall,
            DebugBackColor,
            BackWallVertices,
            BackWallTriangles,
            BackWallUVs,
            BackWallVertexColors,
            BackWallTangents);
        MakeGeometryDoubleSided(BackWallVertices, BackWallTriangles, BackWallUVs, BackWallVertexColors, BackWallTangents);

        TArray<FVector> BackWallNormals;
        CalculateAveragedNormals(BackWallVertices, BackWallTriangles, BackWallNormals);
        CliffMesh->CreateMeshSection_LinearColor(
            5,
            BackWallVertices,
            BackWallTriangles,
            BackWallNormals,
            BackWallUVs,
            BackWallVertexColors,
            BackWallTangents,
            bCreateCollision);
    }

    UMaterialInterface* MaterialToApply = CliffMaterial;
    if (bUseDebugSectionColors && DebugColorMaterial)
    {
        MaterialToApply = DebugColorMaterial;
    }

    if (MaterialToApply)
    {
        for (int32 SectionIndex = 0; SectionIndex <= 5; ++SectionIndex)
        {
            CliffMesh->SetMaterial(SectionIndex, MaterialToApply);
        }
    }
}

void AProceduralCliffGenerator::ClearCliff()
{
    if (CliffMesh)
    {
        CliffMesh->ClearAllMeshSections();
    }
}

float AProceduralCliffGenerator::GetCliffDirectionSign() const
{
    return bInvertCliffDirection ? -1.0f : 1.0f;
}

int32 AProceduralCliffGenerator::GetEffectiveHorizontalSegments() const
{
    const int32 SafeSegments = FMath::Max(HorizontalSegments, 1);
    return bUseSimplifiedDebugGeometry
        ? FMath::Clamp(DebugHorizontalSegments, 1, SafeSegments)
        : SafeSegments;
}

int32 AProceduralCliffGenerator::GetEffectiveVerticalSegments() const
{
    const int32 SafeSegments = FMath::Max(VerticalSegments, 1);
    return bUseSimplifiedDebugGeometry
        ? FMath::Clamp(DebugVerticalSegments, 1, SafeSegments)
        : SafeSegments;
}

float AProceduralCliffGenerator::ComputeEndTaper(float AlphaX) const
{
    const float SafeTaperFraction = FMath::Clamp(EndTaperFraction, 0.0f, 0.45f);
    if (SafeTaperFraction <= KINDA_SMALL_NUMBER)
    {
        return 1.0f;
    }

    // Fade all Y displacement down at both ends so the generated sheet dies back
    // into the sampled landmass instead of ending as an exposed cliff cross-section.
    const float LeftTaper = FMath::SmoothStep(0.0f, SafeTaperFraction, AlphaX);
    const float RightTaper = FMath::SmoothStep(0.0f, SafeTaperFraction, 1.0f - AlphaX);
    return FMath::Clamp(FMath::Min(LeftTaper, RightTaper), 0.0f, 1.0f);
}

float AProceduralCliffGenerator::ComputeFaceOffset(float AlphaX, float AlphaZ) const
{
    const float SafeNoiseScale = FMath::Max(NoiseScale, 0.001f);
    const float EffectiveCliffHeight = FMath::Max(FMath::Abs(CliffHeight), 1.0f);
    const float EndTaper = ComputeEndTaper(AlphaX);

    // X/Z sampling keeps the cliff deterministic while still producing uneven
    // depth. Later, this can be biased by the real terrain slope or material mask.
    const float NoiseA = FMath::PerlinNoise2D(FVector2D(AlphaX * CliffLength / SafeNoiseScale, AlphaZ * EffectiveCliffHeight / SafeNoiseScale));
    const float NoiseB = FMath::PerlinNoise2D(FVector2D(AlphaX * CliffLength / (SafeNoiseScale * 0.47f) + 17.0f, AlphaZ * 8.0f + 3.0f));
    const float NoiseOffset = (NoiseA * 0.7f + NoiseB * 0.3f) * NoiseStrength;

    // Ledge shaping creates horizontal shelf bands. Positive/negative offsets
    // make the face step in and out without needing overhang topology yet.
    const float LedgeWave = FMath::Sin(AlphaZ * FMath::Max(LedgeFrequency, 0.0f) * UE_TWO_PI);
    const float LedgeMask = FMath::Pow(FMath::Max(0.0f, LedgeWave), 4.0f);
    const float LedgeOffset = LedgeMask * LedgeStrength;

    // A signed overhang profile. Positive OverhangAmount pushes the upper cliff
    // outward in +Y, while the undercut pulls the lower/middle wall backward.
    // The extra top lip gives the silhouette the classic jutting shelf profile;
    // this is still a heightfield-like wall, not true cave/voxel geometry yet.
    const float ClampedStart = FMath::Clamp(OverhangStartAlpha, 0.0f, 0.95f);
    const float OverhangAlpha = FMath::Pow(FMath::SmoothStep(ClampedStart, 1.0f, AlphaZ), 1.85f);
    const float LipAlpha = FMath::Pow(FMath::SmoothStep(0.74f, 1.0f, AlphaZ), 2.15f);
    const float UndercutBand = 1.0f - FMath::SmoothStep(0.08f, FMath::Min(ClampedStart + 0.18f, 0.96f), AlphaZ);
    const float UndercutAlpha = FMath::Pow(FMath::Clamp(UndercutBand, 0.0f, 1.0f), 0.85f);
    const float OverhangOffset = OverhangAmount * (OverhangAlpha + LipAlpha * FMath::Clamp(TopLipStrength, 0.0f, 2.0f));
    const float UndercutOffset = -OverhangAmount * FMath::Clamp(UndercutStrength, 0.0f, 1.0f) * UndercutAlpha;

    return (NoiseOffset + LedgeOffset + OverhangOffset + UndercutOffset) * EndTaper * GetCliffDirectionSign();
}

FVector AProceduralCliffGenerator::GetLandmassAnchoredFaceVertex(float AlphaX, float AlphaZ, float FaceYOffset) const
{
    const float LocalX = AlphaX * CliffLength;
    const float EffectiveCliffHeight = FMath::Max(FMath::Abs(CliffHeight), 1.0f);
    float TopLocalZ = EffectiveCliffHeight;

    if (LandmassActor && bSnapTopEdgeToLandmass)
    {
        // Use one stable terrain sample for the top of this vertical column.
        // The rows below can push in/out in Y for overhang shape, but they should
        // still hang from the same top edge instead of resampling the terrain as
        // if every row were a separate terrain-following sheet.
        // Anchor the top height to the terrain seam, then let the generated
        // rows push outward from that stable edge. Sampling at the overhang lip
        // makes auto-spawned cliffs inherit the mountain slope instead of
        // visibly jutting away from it.
        const float TopFaceYOffset = bAnchorTopHeightAtAttachEdge
            ? 0.0f
            : ComputeFaceOffset(AlphaX, 1.0f);
        FVector WorldTopSample = GetActorTransform().TransformPosition(FVector(LocalX, TopFaceYOffset, 0.0f));
        float TerrainZ = bClampSamplesToLandmassBounds
            ? LandmassActor->GetTerrainHeightAtWorldLocationClamped(WorldTopSample)
            : LandmassActor->GetTerrainHeightAtWorldLocation(WorldTopSample);
        if (bUseActorZAsMinimumTopHeight)
        {
            TerrainZ = FMath::Max(TerrainZ, GetActorLocation().Z);
        }
        WorldTopSample.Z = TerrainZ + LandmassSurfaceZOffset;
        TopLocalZ = GetActorTransform().InverseTransformPosition(WorldTopSample).Z;
    }

    const float BottomLocalZ = TopLocalZ - EffectiveCliffHeight;
    return FVector(LocalX, FaceYOffset, FMath::Lerp(BottomLocalZ, TopLocalZ, AlphaZ));
}

FVector AProceduralCliffGenerator::GetStitchTerrainVertex(const FVector& CliffEdgeVertex, const FVector& TerrainEdgeOffset) const
{
    FVector TerrainVertex = CliffEdgeVertex + TerrainEdgeOffset;
    if (LandmassActor && bSampleStitchEdgesFromLandmass)
    {
        FVector WorldTerrainVertex = GetActorTransform().TransformPosition(TerrainVertex);
        float TerrainZ = bClampSamplesToLandmassBounds
            ? LandmassActor->GetTerrainHeightAtWorldLocationClamped(WorldTerrainVertex)
            : LandmassActor->GetTerrainHeightAtWorldLocation(WorldTerrainVertex);
        if (bUseActorZAsMinimumStitchHeight)
        {
            TerrainZ = FMath::Max(TerrainZ, GetActorLocation().Z);
        }
        WorldTerrainVertex.Z = TerrainZ + LandmassSurfaceZOffset;
        TerrainVertex = GetActorTransform().InverseTransformPosition(WorldTerrainVertex);
    }

    return TerrainVertex;
}

FLinearColor AProceduralCliffGenerator::MakeCliffVertexColor(const FVector& LocalPosition, float AlphaZ, const FLinearColor& DebugColor) const
{
    if (bUseDebugSectionColors)
    {
        return DebugColor;
    }

    float Height01 = AlphaZ;
    float WaterHeight01 = 0.22f;

    if (LandmassActor && LandmassActor->HeightMultiplier > KINDA_SMALL_NUMBER)
    {
        const FVector WorldPosition = GetActorTransform().TransformPosition(LocalPosition);
        const FVector LandmassLocalPosition = LandmassActor->GetActorTransform().InverseTransformPosition(WorldPosition);
        Height01 = FMath::Clamp(LandmassLocalPosition.Z / LandmassActor->HeightMultiplier, 0.0f, 1.0f);
        WaterHeight01 = LandmassActor->WaterHeight01;
    }

    const float BeachBand = 0.08f;
    const float BeachStart = WaterHeight01 - BeachBand * 0.35f;
    const float BeachPeak = WaterHeight01 + BeachBand;
    const float BeachEnd = BeachPeak + 0.18f;
    const float BeachMask = FMath::Clamp(
        FMath::SmoothStep(BeachStart, BeachPeak, Height01) *
            (1.0f - FMath::SmoothStep(BeachPeak, BeachEnd, Height01)),
        0.0f,
        1.0f);
    const float CliffSlopeMask = 1.0f;
    const float DryLandMask = FMath::SmoothStep(WaterHeight01, WaterHeight01 + BeachBand + 0.02f, Height01);

    // Match the landmass material contract as closely as this standalone mesh can:
    // R = beach, G = slope/cliff, B = normalized height, A = dry-land.
    return FLinearColor(BeachMask, CliffSlopeMask, Height01, DryLandMask);
}

float AProceduralCliffGenerator::ResolvePrototypeTopLocalZ() const
{
    const float EffectiveCliffHeight = FMath::Max(FMath::Abs(CliffHeight), 1.0f);
    float TopLocalZ = EffectiveCliffHeight;

    if (LandmassActor && bSnapTopEdgeToLandmass)
    {
        FVector WorldTopSample = GetActorTransform().TransformPosition(FVector(CliffLength * 0.5f, 0.0f, 0.0f));
        float TerrainZ = bClampSamplesToLandmassBounds
            ? LandmassActor->GetTerrainHeightAtWorldLocationClamped(WorldTopSample)
            : LandmassActor->GetTerrainHeightAtWorldLocation(WorldTopSample);
        if (bUseActorZAsMinimumTopHeight)
        {
            TerrainZ = FMath::Max(TerrainZ, GetActorLocation().Z);
        }

        WorldTopSample.Z = TerrainZ + LandmassSurfaceZOffset;
        TopLocalZ = GetActorTransform().InverseTransformPosition(WorldTopSample).Z;
    }

    return TopLocalZ;
}

float AProceduralCliffGenerator::SampleLandmassHeightForLocalPoint(const FVector& LocalPoint) const
{
    if (!LandmassActor)
    {
        return LocalPoint.Z;
    }

    FVector WorldPoint = GetActorTransform().TransformPosition(LocalPoint);
    const float TerrainZ = bClampSamplesToLandmassBounds
        ? LandmassActor->GetTerrainHeightAtWorldLocationClamped(WorldPoint)
        : LandmassActor->GetTerrainHeightAtWorldLocation(WorldPoint);
    WorldPoint.Z = TerrainZ + LandmassSurfaceZOffset;
    return GetActorTransform().InverseTransformPosition(WorldPoint).Z;
}

float AProceduralCliffGenerator::ResolvePrototypeBackFaceEmbedDepth(float SafeLength, float TopZ, float ApexZ) const
{
    const float MinimumDepth = FMath::Max(FMath::Abs(PrototypeBackFaceEmbedDepth), 0.0f);
    if (!LandmassActor || !bAutoFitPrototypeBackFaceToLandmass)
    {
        return MinimumDepth;
    }

    const float Direction = GetCliffDirectionSign();
    const float MaxDepth = FMath::Max(MinimumDepth, PrototypeMaxAutoEmbedDepth);
    const float StepSize = FMath::Max(CliffDepth * 0.25f, 50.0f);
    const int32 SampleCount = FMath::Clamp(PrototypeBackFaceFitSamples, 3, 11);
    const float BurialMargin = FMath::Max(PrototypeBackFaceBurialMargin, 0.0f);

    auto IsBackFaceBuriedAtDepth = [&](float CandidateDepth) -> bool
    {
        // The blue back face is treated as a full vertical strip. It is hidden
        // when the landmass height at the same XY is above representative
        // samples across that strip, so the generated cliff adapts to local
        // terrain instead of relying on a hand-tuned embed distance.
        for (int32 XIndex = 0; XIndex < SampleCount; ++XIndex)
        {
            const float AlphaX = SampleCount == 1
                ? 0.5f
                : static_cast<float>(XIndex) / static_cast<float>(SampleCount - 1);
            const float LocalX = AlphaX * SafeLength;

            for (int32 ZIndex = 0; ZIndex < SampleCount; ++ZIndex)
            {
                const float AlphaZ = static_cast<float>(ZIndex) / static_cast<float>(SampleCount - 1);
                const FVector LocalSample(LocalX, -Direction * CandidateDepth, FMath::Lerp(ApexZ, TopZ, AlphaZ));
                const float TerrainLocalZ = SampleLandmassHeightForLocalPoint(LocalSample);
                if (TerrainLocalZ < LocalSample.Z + BurialMargin)
                {
                    return false;
                }
            }
        }

        return true;
    };

    for (float CandidateDepth = MinimumDepth; CandidateDepth <= MaxDepth + KINDA_SMALL_NUMBER; CandidateDepth += StepSize)
    {
        if (IsBackFaceBuriedAtDepth(CandidateDepth))
        {
            return CandidateDepth;
        }
    }

    return MaxDepth;
}

void AProceduralCliffGenerator::CreateTriangleSection(
    int32 SectionIndex,
    const FVector& VertexA,
    const FVector& VertexB,
    const FVector& VertexC,
    const FLinearColor& DebugColor,
    bool bUseCollision)
{
    TArray<FVector> Vertices;
    Vertices.Reserve(3);
    Vertices.Add(VertexA);
    Vertices.Add(VertexB);
    Vertices.Add(VertexC);

    TArray<int32> Triangles;
    Triangles.Reserve(3);
    Triangles.Add(0);
    Triangles.Add(1);
    Triangles.Add(2);

    TArray<FVector2D> UVs;
    UVs.Reserve(3);
    UVs.Add(FVector2D(0.0f, 0.0f));
    UVs.Add(FVector2D(1.0f, 0.0f));
    UVs.Add(FVector2D(0.5f, 1.0f));

    TArray<FLinearColor> VertexColors;
    VertexColors.Init(MakeCliffVertexColor(VertexA, 1.0f, DebugColor), 3);

    TArray<FProcMeshTangent> Tangents;
    Tangents.Init(FProcMeshTangent(1.0f, 0.0f, 0.0f), 3);

    MakeGeometryDoubleSided(Vertices, Triangles, UVs, VertexColors, Tangents);

    TArray<FVector> Normals;
    CalculateAveragedNormals(Vertices, Triangles, Normals);
    CliffMesh->CreateMeshSection_LinearColor(
        SectionIndex,
        Vertices,
        Triangles,
        Normals,
        UVs,
        VertexColors,
        Tangents,
        bUseCollision);
}

void AProceduralCliffGenerator::CreateQuadSection(
    int32 SectionIndex,
    const FVector& VertexA,
    const FVector& VertexB,
    const FVector& VertexC,
    const FVector& VertexD,
    const FLinearColor& DebugColor,
    bool bUseCollision)
{
    TArray<FVector> Vertices;
    Vertices.Reserve(4);
    Vertices.Add(VertexA);
    Vertices.Add(VertexB);
    Vertices.Add(VertexC);
    Vertices.Add(VertexD);

    TArray<int32> Triangles;
    Triangles.Reserve(6);
    Triangles.Add(0);
    Triangles.Add(1);
    Triangles.Add(2);
    Triangles.Add(0);
    Triangles.Add(2);
    Triangles.Add(3);

    TArray<FVector2D> UVs;
    UVs.Reserve(4);
    UVs.Add(FVector2D(0.0f, 0.0f));
    UVs.Add(FVector2D(1.0f, 0.0f));
    UVs.Add(FVector2D(1.0f, 1.0f));
    UVs.Add(FVector2D(0.0f, 1.0f));

    TArray<FLinearColor> VertexColors;
    VertexColors.Init(MakeCliffVertexColor(VertexA, 1.0f, DebugColor), 4);

    TArray<FProcMeshTangent> Tangents;
    Tangents.Init(FProcMeshTangent(1.0f, 0.0f, 0.0f), 4);

    MakeGeometryDoubleSided(Vertices, Triangles, UVs, VertexColors, Tangents);

    TArray<FVector> Normals;
    CalculateAveragedNormals(Vertices, Triangles, Normals);
    CliffMesh->CreateMeshSection_LinearColor(
        SectionIndex,
        Vertices,
        Triangles,
        Normals,
        UVs,
        VertexColors,
        Tangents,
        bUseCollision);
}

void AProceduralCliffGenerator::CreateSection(
    int32 SectionIndex,
    const TArray<FVector>& InVertices,
    const TArray<int32>& InTriangles,
    const FLinearColor& DebugColor,
    bool bUseCollision)
{
    TArray<FVector> Vertices = InVertices;
    TArray<int32> Triangles = InTriangles;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;

    UVs.Reserve(Vertices.Num());
    VertexColors.Reserve(Vertices.Num());
    Tangents.Reserve(Vertices.Num());
    for (const FVector& Vertex : Vertices)
    {
        UVs.Add(FVector2D(Vertex.X / FMath::Max(FMath::Abs(CliffLength), 1.0f), Vertex.Y / FMath::Max(FMath::Abs(OverhangAmount), 1.0f)));
        VertexColors.Add(MakeCliffVertexColor(Vertex, 1.0f, DebugColor));
        Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
    }

    MakeGeometryDoubleSided(Vertices, Triangles, UVs, VertexColors, Tangents);

    TArray<FVector> Normals;
    CalculateAveragedNormals(Vertices, Triangles, Normals);
    CliffMesh->CreateMeshSection_LinearColor(
        SectionIndex,
        Vertices,
        Triangles,
        Normals,
        UVs,
        VertexColors,
        Tangents,
        bUseCollision);
}

void AProceduralCliffGenerator::GeneratePyramidCliff()
{
    const float SafeLength = FMath::Max(FMath::Abs(CliffLength), 1.0f);
    const float SafeHeight = FMath::Max(FMath::Abs(CliffHeight), 1.0f);
    const float OutwardDepth = FMath::Max(FMath::Abs(OverhangAmount), FMath::Abs(CliffDepth));
    const float Direction = GetCliffDirectionSign();
    const float TopZ = ResolvePrototypeTopLocalZ();
    const float ApexZ = TopZ - SafeHeight;
    const float BackEmbedDepth = ResolvePrototypeBackFaceEmbedDepth(SafeLength, TopZ, ApexZ);
    const int32 SegmentCount = FMath::Clamp(PrototypeStripSegments, 1, 8);
    const float SafeEndTaper = FMath::Clamp(PrototypeEndTaper, 0.0f, 0.49f);
    const float SafeWidthNoise = FMath::Clamp(PrototypeWidthNoise, 0.0f, 0.65f);
    const float MaxTopSlopeRadians = FMath::DegreesToRadians(FMath::Clamp(PrototypeMaxTopSlopeDegrees, 0.0f, 45.0f));
    const float MaxTopSlopePerUnit = FMath::Tan(MaxTopSlopeRadians);
    const int32 FrontCurveRows = FMath::Clamp(PrototypeFrontCurveSegments, 1, 16);
    const float FrontCurveStrength = FMath::Clamp(PrototypeFrontCurveStrength, 0.0f, 1.0f);
    const float FrontNoiseStrength = FMath::Max(PrototypeFrontNoiseStrength, 0.0f);
    const float FrontNoiseScale = FMath::Max(PrototypeFrontNoiseScale, 1.0f);
    const float CenterLocalX = SafeLength * 0.5f;
    float CenterTopZ = TopZ;
    if (LandmassActor && bSnapTopEdgeToLandmass)
    {
        CenterTopZ = SampleLandmassHeightForLocalPoint(FVector(CenterLocalX, 0.0f, TopZ));
    }

    TArray<FVector> BackTop;
    TArray<FVector> FrontTop;
    TArray<FVector> BottomRidge;
    BackTop.Reserve(SegmentCount + 1);
    FrontTop.Reserve(SegmentCount + 1);
    BottomRidge.Reserve(SegmentCount + 1);

    for (int32 Index = 0; Index <= SegmentCount; ++Index)
    {
        const float AlphaX = static_cast<float>(Index) / static_cast<float>(SegmentCount);
        const float LocalX = AlphaX * SafeLength;
        const float LeftTaper = SafeEndTaper > KINDA_SMALL_NUMBER ? FMath::SmoothStep(0.0f, SafeEndTaper, AlphaX) : 1.0f;
        const float RightTaper = SafeEndTaper > KINDA_SMALL_NUMBER ? FMath::SmoothStep(0.0f, SafeEndTaper, 1.0f - AlphaX) : 1.0f;
        const float Taper = FMath::Clamp(FMath::Min(LeftTaper, RightTaper), 0.0f, 1.0f);
        const float Noise = FMath::PerlinNoise1D(AlphaX * 3.7f + 11.0f) * SafeWidthNoise;
        const float WidthScale = FMath::Clamp(0.25f + 0.75f * Taper + Noise, 0.18f, 1.25f);

        FVector BackVertex(LocalX, -Direction * BackEmbedDepth, TopZ);
        if (LandmassActor && bSnapTopEdgeToLandmass)
        {
            const float TerrainTopZ = SampleLandmassHeightForLocalPoint(FVector(LocalX, 0.0f, TopZ));
            const float MaxTopDelta = FMath::Abs(LocalX - CenterLocalX) * MaxTopSlopePerUnit;
            BackVertex.Z = FMath::Clamp(TerrainTopZ, CenterTopZ - MaxTopDelta, CenterTopZ + MaxTopDelta);
        }

        const float LipDrop = SafeHeight * 0.04f * Taper;
        const FVector FrontVertex(LocalX, Direction * OutwardDepth * WidthScale, BackVertex.Z - LipDrop);
        const FVector BottomVertex(LocalX, -Direction * BackEmbedDepth, BackVertex.Z - SafeHeight * (0.65f + 0.25f * Taper));

        BackTop.Add(BackVertex);
        FrontTop.Add(FrontVertex);
        BottomRidge.Add(BottomVertex);
    }

    TArray<FVector> TopVertices;
    TArray<int32> TopTriangles;
    TopVertices.Reserve((SegmentCount + 1) * 2);
    for (int32 Index = 0; Index <= SegmentCount; ++Index)
    {
        TopVertices.Add(BackTop[Index]);
        TopVertices.Add(FrontTop[Index]);
    }
    for (int32 Index = 0; Index < SegmentCount; ++Index)
    {
        const int32 BackLeft = Index * 2;
        const int32 FrontLeft = BackLeft + 1;
        const int32 BackRight = BackLeft + 2;
        const int32 FrontRight = BackLeft + 3;
        TopTriangles.Add(BackLeft);
        TopTriangles.Add(FrontLeft);
        TopTriangles.Add(FrontRight);
        TopTriangles.Add(BackLeft);
        TopTriangles.Add(FrontRight);
        TopTriangles.Add(BackRight);
    }

    TArray<FVector> FrontVertices;
    TArray<int32> FrontTriangles;
    FrontVertices.Reserve((SegmentCount + 1) * (FrontCurveRows + 1));
    auto GetFrontVertexIndex = [FrontCurveRows](int32 ColumnIndex, int32 RowIndex) -> int32
    {
        return ColumnIndex * (FrontCurveRows + 1) + RowIndex;
    };

    for (int32 Index = 0; Index <= SegmentCount; ++Index)
    {
        const float AlphaX = static_cast<float>(Index) / static_cast<float>(SegmentCount);
        const float LocalX = AlphaX * SafeLength;
        for (int32 Row = 0; Row <= FrontCurveRows; ++Row)
        {
            const float AlphaZ = static_cast<float>(Row) / static_cast<float>(FrontCurveRows);
            const float VerticalAlpha = FMath::SmoothStep(0.0f, 1.0f, AlphaZ);
            const FVector BottomPoint = BottomRidge[Index];
            const FVector TopPoint = FrontTop[Index];
            FVector ControlPoint = FMath::Lerp(BottomPoint, TopPoint, 0.55f);

            // Pull the control point back toward the buried side so the exposed
            // face becomes concave: it hangs under the yellow lip, then curves
            // inward as it descends toward the landmass.
            ControlPoint.Y -= Direction * OutwardDepth * FrontCurveStrength * 0.75f;
            ControlPoint.Z += SafeHeight * FrontCurveStrength * 0.08f;

            const float OneMinusAlpha = 1.0f - VerticalAlpha;
            FVector CurvePoint =
                BottomPoint * (OneMinusAlpha * OneMinusAlpha) +
                ControlPoint * (2.0f * OneMinusAlpha * VerticalAlpha) +
                TopPoint * (VerticalAlpha * VerticalAlpha);

            // Keep procedural detail away from the shared seams. The red face
            // can be noisy in the middle, but the top lip and buried bottom
            // ridge must stay clean so the side/back sections remain sealed.
            const float SeamFade =
                FMath::SmoothStep(0.0f, 0.35f, VerticalAlpha) *
                FMath::SmoothStep(0.0f, 0.35f, 1.0f - VerticalAlpha);
            const float EndFade = FMath::Clamp(
                FMath::SmoothStep(0.0f, 0.15f, AlphaX) *
                    FMath::SmoothStep(0.0f, 0.15f, 1.0f - AlphaX),
                0.0f,
                1.0f);
            const float NoiseFade = SeamFade * FMath::Lerp(0.45f, 1.0f, EndFade);
            if (FrontNoiseStrength > KINDA_SMALL_NUMBER)
            {
                const FVector ActorLocation = GetActorLocation();
                const FVector2D NoiseUV(
                    (LocalX + ActorLocation.X * 0.173f) / FrontNoiseScale,
                    (VerticalAlpha * SafeHeight + ActorLocation.Y * 0.131f) / FrontNoiseScale);
                const float BroadNoise = FMath::PerlinNoise2D(NoiseUV);
                const float DetailNoise = FMath::PerlinNoise2D(NoiseUV * 2.35f + FVector2D(19.0f, 7.0f));
                const float CombinedNoise = BroadNoise * 0.72f + DetailNoise * 0.28f;
                CurvePoint.Y += Direction * CombinedNoise * FrontNoiseStrength * NoiseFade;
                CurvePoint.Z += DetailNoise * FrontNoiseStrength * 0.18f * NoiseFade;
            }

            // Guardrail for aggressive noise: never let an interior red-face
            // vertex cross behind the buried blue side or punch past the yellow
            // lip for its column. This preserves clean topology without adding
            // another hand-tuned editor parameter.
            const float SignedBottomY = BottomPoint.Y * Direction;
            const float SignedTopY = TopPoint.Y * Direction;
            const float SignedMinY = FMath::Min(SignedBottomY, SignedTopY);
            const float SignedMaxY = FMath::Max(SignedBottomY, SignedTopY);
            const float GuardRange = SignedMaxY - SignedMinY;
            if (GuardRange > KINDA_SMALL_NUMBER)
            {
                const float GuardMargin = FMath::Min(
                    GuardRange * 0.18f,
                    FMath::Max(20.0f, OutwardDepth * 0.04f));
                const float BoundaryGuard = SeamFade;
                const float MinAllowedY = SignedMinY + GuardMargin * BoundaryGuard;
                const float MaxAllowedY = SignedMaxY - GuardMargin * BoundaryGuard;
                if (MinAllowedY < MaxAllowedY)
                {
                    CurvePoint.Y = FMath::Clamp(CurvePoint.Y * Direction, MinAllowedY, MaxAllowedY) * Direction;
                }
            }
            FrontVertices.Add(CurvePoint);
        }
    }
    for (int32 Index = 0; Index < SegmentCount; ++Index)
    {
        for (int32 Row = 0; Row < FrontCurveRows; ++Row)
        {
            const int32 LeftBottom = GetFrontVertexIndex(Index, Row);
            const int32 LeftTop = LeftBottom + 1;
            const int32 RightBottom = GetFrontVertexIndex(Index + 1, Row);
            const int32 RightTop = RightBottom + 1;
            FrontTriangles.Add(LeftTop);
            FrontTriangles.Add(LeftBottom);
            FrontTriangles.Add(RightTop);
            FrontTriangles.Add(RightTop);
            FrontTriangles.Add(LeftBottom);
            FrontTriangles.Add(RightBottom);
        }
    }

    TArray<FVector> BackVertices;
    TArray<int32> BackTriangles;
    BackVertices.Reserve((SegmentCount + 1) * 2);
    for (int32 Index = 0; Index <= SegmentCount; ++Index)
    {
        BackVertices.Add(BottomRidge[Index]);
        BackVertices.Add(BackTop[Index]);
    }
    for (int32 Index = 0; Index < SegmentCount; ++Index)
    {
        const int32 BottomLeft = Index * 2;
        const int32 TopLeft = BottomLeft + 1;
        const int32 BottomRight = BottomLeft + 2;
        const int32 TopRight = BottomLeft + 3;
        BackTriangles.Add(BottomLeft);
        BackTriangles.Add(TopLeft);
        BackTriangles.Add(BottomRight);
        BackTriangles.Add(BottomRight);
        BackTriangles.Add(TopLeft);
        BackTriangles.Add(TopRight);
    }

    CreateSection(0, TopVertices, TopTriangles, DebugTopColor, bCreateCollision);
    CreateSection(1, FrontVertices, FrontTriangles, DebugFrontFaceColor, false);
    CreateSection(2, BackVertices, BackTriangles, DebugBackColor, false);

    auto BuildEndCapSection = [&](int32 SectionIndex, int32 ColumnIndex, const FLinearColor& DebugColor, bool bReverseWinding)
    {
        TArray<FVector> CapVertices;
        TArray<int32> CapTriangles;
        CapVertices.Reserve(FrontCurveRows + 2);
        CapVertices.Add(BackTop[ColumnIndex]);
        for (int32 Row = FrontCurveRows; Row >= 0; --Row)
        {
            CapVertices.Add(FrontVertices[GetFrontVertexIndex(ColumnIndex, Row)]);
        }

        for (int32 VertexIndex = 1; VertexIndex < CapVertices.Num() - 1; ++VertexIndex)
        {
            if (bReverseWinding)
            {
                CapTriangles.Add(0);
                CapTriangles.Add(VertexIndex + 1);
                CapTriangles.Add(VertexIndex);
            }
            else
            {
                CapTriangles.Add(0);
                CapTriangles.Add(VertexIndex);
                CapTriangles.Add(VertexIndex + 1);
            }
        }

        CreateSection(SectionIndex, CapVertices, CapTriangles, DebugColor, false);
    };

    BuildEndCapSection(3, 0, DebugLeftFaceColor, false);
    BuildEndCapSection(4, SegmentCount, DebugRightFaceColor, true);

    UMaterialInterface* MaterialToApply = CliffMaterial;
    if (bUseDebugSectionColors && DebugColorMaterial)
    {
        MaterialToApply = DebugColorMaterial;
    }

    if (MaterialToApply)
    {
        for (int32 SectionIndex = 0; SectionIndex <= 4; ++SectionIndex)
        {
            CliffMesh->SetMaterial(SectionIndex, MaterialToApply);
        }
    }
}

void AProceduralCliffGenerator::BuildFaceGrid(
    TArray<FVector>& OutVertices,
    TArray<int32>& OutTriangles,
    TArray<FVector2D>& OutUVs,
    TArray<FLinearColor>& OutVertexColors,
    TArray<FProcMeshTangent>& OutTangents) const
{
    const int32 SafeHorizontalSegments = GetEffectiveHorizontalSegments();
    const int32 SafeVerticalSegments = GetEffectiveVerticalSegments();
    const int32 NumColumns = SafeHorizontalSegments + 1;
    const int32 NumRows = SafeVerticalSegments + 1;

    OutVertices.Reserve(NumColumns * NumRows);
    OutUVs.Reserve(NumColumns * NumRows);
    OutVertexColors.Reserve(NumColumns * NumRows);
    OutTangents.Reserve(NumColumns * NumRows);

    for (int32 ZIndex = 0; ZIndex <= SafeVerticalSegments; ++ZIndex)
    {
        const float AlphaZ = static_cast<float>(ZIndex) / static_cast<float>(SafeVerticalSegments);
        for (int32 XIndex = 0; XIndex <= SafeHorizontalSegments; ++XIndex)
        {
            const float AlphaX = static_cast<float>(XIndex) / static_cast<float>(SafeHorizontalSegments);
            const float YOffset = ComputeFaceOffset(AlphaX, AlphaZ);

            const FVector Vertex = GetLandmassAnchoredFaceVertex(AlphaX, AlphaZ, YOffset);
            OutVertices.Add(Vertex);
            OutUVs.Add(FVector2D(AlphaX, 1.0f - AlphaZ));
            // In debug mode, color by how the surface reads in the viewport:
            // red/orange is the exposed cliff wall, yellow is reserved for the
            // walkable top shelf, and blue is reserved for landmass-facing backs.
            const FLinearColor DebugColor = AlphaZ >= FMath::Clamp(OverhangStartAlpha, 0.0f, 0.95f)
                ? DebugOverhangUndersideColor
                : DebugFrontFaceColor;
            OutVertexColors.Add(MakeCliffVertexColor(Vertex, AlphaZ, DebugColor));
            OutTangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
        }
    }

    for (int32 ZIndex = 0; ZIndex < SafeVerticalSegments; ++ZIndex)
    {
        for (int32 XIndex = 0; XIndex < SafeHorizontalSegments; ++XIndex)
        {
            const int32 BottomLeft = ZIndex * NumColumns + XIndex;
            const int32 BottomRight = BottomLeft + 1;
            const int32 TopLeft = BottomLeft + NumColumns;
            const int32 TopRight = TopLeft + 1;

            OutTriangles.Add(BottomLeft);
            OutTriangles.Add(TopLeft);
            OutTriangles.Add(BottomRight);

            OutTriangles.Add(BottomRight);
            OutTriangles.Add(TopLeft);
            OutTriangles.Add(TopRight);
        }
    }
}

void AProceduralCliffGenerator::BuildStitchStrip(
    const TArray<FVector>& FaceVertices,
    int32 RowIndex,
    const FVector& TerrainEdgeOffset,
    const FLinearColor& DebugColor,
    bool bFlipWinding,
    TArray<FVector>& OutVertices,
    TArray<int32>& OutTriangles,
    TArray<FVector2D>& OutUVs,
    TArray<FLinearColor>& OutVertexColors,
    TArray<FProcMeshTangent>& OutTangents) const
{
    const int32 SafeHorizontalSegments = GetEffectiveHorizontalSegments();
    const int32 SafeVerticalSegments = GetEffectiveVerticalSegments();
    const int32 NumColumns = SafeHorizontalSegments + 1;
    const int32 ClampedRowIndex = FMath::Clamp(RowIndex, 0, SafeVerticalSegments);
    const float RowAlpha = static_cast<float>(ClampedRowIndex) / static_cast<float>(SafeVerticalSegments);

    OutVertices.Reserve(NumColumns * 2);
    OutUVs.Reserve(NumColumns * 2);
    OutVertexColors.Reserve(NumColumns * 2);
    OutTangents.Reserve(NumColumns * 2);

    // First row is the cliff edge, second row is a terrain edge. With LandmassActor
    // assigned this samples the real heightfield; otherwise it uses the prototype
    // offset strip so the actor still works standalone.
    for (int32 XIndex = 0; XIndex <= SafeHorizontalSegments; ++XIndex)
    {
        const float AlphaX = static_cast<float>(XIndex) / static_cast<float>(SafeHorizontalSegments);
        const FVector CliffEdgeVertex = FaceVertices[ClampedRowIndex * NumColumns + XIndex];

        OutVertices.Add(CliffEdgeVertex);
        OutUVs.Add(FVector2D(AlphaX, 0.0f));
        OutVertexColors.Add(MakeCliffVertexColor(CliffEdgeVertex, RowAlpha, DebugColor));
        OutTangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));

        const FVector TerrainVertex = GetStitchTerrainVertex(CliffEdgeVertex, TerrainEdgeOffset);
        OutVertices.Add(TerrainVertex);
        OutUVs.Add(FVector2D(AlphaX, 1.0f));
        OutVertexColors.Add(MakeCliffVertexColor(TerrainVertex, RowAlpha, DebugColor));
        OutTangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
    }

    for (int32 XIndex = 0; XIndex < SafeHorizontalSegments; ++XIndex)
    {
        const int32 CliffLeft = XIndex * 2;
        const int32 TerrainLeft = CliffLeft + 1;
        const int32 CliffRight = CliffLeft + 2;
        const int32 TerrainRight = CliffLeft + 3;

        if (!bFlipWinding)
        {
            OutTriangles.Add(CliffLeft);
            OutTriangles.Add(TerrainLeft);
            OutTriangles.Add(CliffRight);

            OutTriangles.Add(CliffRight);
            OutTriangles.Add(TerrainLeft);
            OutTriangles.Add(TerrainRight);
        }
        else
        {
            OutTriangles.Add(CliffLeft);
            OutTriangles.Add(CliffRight);
            OutTriangles.Add(TerrainLeft);

            OutTriangles.Add(CliffRight);
            OutTriangles.Add(TerrainRight);
            OutTriangles.Add(TerrainLeft);
        }
    }
}

void AProceduralCliffGenerator::BuildEndStitchStrip(
    const TArray<FVector>& FaceVertices,
    int32 ColumnIndex,
    const FVector& TerrainEdgeOffset,
    const FLinearColor& DebugColor,
    bool bFlipWinding,
    TArray<FVector>& OutVertices,
    TArray<int32>& OutTriangles,
    TArray<FVector2D>& OutUVs,
    TArray<FLinearColor>& OutVertexColors,
    TArray<FProcMeshTangent>& OutTangents) const
{
    const int32 SafeHorizontalSegments = GetEffectiveHorizontalSegments();
    const int32 SafeVerticalSegments = GetEffectiveVerticalSegments();
    const int32 NumColumns = SafeHorizontalSegments + 1;
    const int32 ClampedColumnIndex = FMath::Clamp(ColumnIndex, 0, SafeHorizontalSegments);

    OutVertices.Reserve((SafeVerticalSegments + 1) * 2);
    OutUVs.Reserve((SafeVerticalSegments + 1) * 2);
    OutVertexColors.Reserve((SafeVerticalSegments + 1) * 2);
    OutTangents.Reserve((SafeVerticalSegments + 1) * 2);

    // Side strips close the left/right ends. With terrain sampling enabled, the
    // outside edge is pulled onto the landmass heightfield so the cliff can fade
    // back into surrounding terrain instead of showing a raw vertical boundary.
    for (int32 ZIndex = 0; ZIndex <= SafeVerticalSegments; ++ZIndex)
    {
        const float AlphaZ = static_cast<float>(ZIndex) / static_cast<float>(SafeVerticalSegments);
        const FVector CliffEdgeVertex = FaceVertices[ZIndex * NumColumns + ClampedColumnIndex];

        OutVertices.Add(CliffEdgeVertex);
        OutUVs.Add(FVector2D(0.0f, 1.0f - AlphaZ));
        OutVertexColors.Add(MakeCliffVertexColor(CliffEdgeVertex, AlphaZ, DebugColor));
        OutTangents.Add(FProcMeshTangent(0.0f, 0.0f, 1.0f));

        const FVector TerrainVertex = GetStitchTerrainVertex(CliffEdgeVertex, TerrainEdgeOffset);
        OutVertices.Add(TerrainVertex);
        OutUVs.Add(FVector2D(1.0f, 1.0f - AlphaZ));
        OutVertexColors.Add(MakeCliffVertexColor(TerrainVertex, AlphaZ, DebugColor));
        OutTangents.Add(FProcMeshTangent(0.0f, 0.0f, 1.0f));
    }

    for (int32 ZIndex = 0; ZIndex < SafeVerticalSegments; ++ZIndex)
    {
        const int32 CliffBottom = ZIndex * 2;
        const int32 TerrainBottom = CliffBottom + 1;
        const int32 CliffTop = CliffBottom + 2;
        const int32 TerrainTop = CliffBottom + 3;

        if (!bFlipWinding)
        {
            OutTriangles.Add(CliffBottom);
            OutTriangles.Add(TerrainBottom);
            OutTriangles.Add(CliffTop);

            OutTriangles.Add(CliffTop);
            OutTriangles.Add(TerrainBottom);
            OutTriangles.Add(TerrainTop);
        }
        else
        {
            OutTriangles.Add(CliffBottom);
            OutTriangles.Add(CliffTop);
            OutTriangles.Add(TerrainBottom);

            OutTriangles.Add(CliffTop);
            OutTriangles.Add(TerrainTop);
            OutTriangles.Add(TerrainBottom);
        }
    }
}

void AProceduralCliffGenerator::BuildBackStitchWall(
    const TArray<FVector>& TopStripVertices,
    const TArray<FVector>& BottomStripVertices,
    const FLinearColor& DebugColor,
    TArray<FVector>& OutVertices,
    TArray<int32>& OutTriangles,
    TArray<FVector2D>& OutUVs,
    TArray<FLinearColor>& OutVertexColors,
    TArray<FProcMeshTangent>& OutTangents) const
{
    const int32 SafeHorizontalSegments = GetEffectiveHorizontalSegments();
    const int32 NumColumns = SafeHorizontalSegments + 1;
    const int32 RequiredStripVertexCount = NumColumns * 2;
    if (TopStripVertices.Num() < RequiredStripVertexCount || BottomStripVertices.Num() < RequiredStripVertexCount)
    {
        return;
    }

    OutVertices.Reserve(NumColumns * 2);
    OutUVs.Reserve(NumColumns * 2);
    OutVertexColors.Reserve(NumColumns * 2);
    OutTangents.Reserve(NumColumns * 2);

    // The top and bottom stitch strips each store vertices as pairs:
    // cliff-edge, terrain-edge. The terrain-edge rows are the back side of the
    // shell; connecting them closes the "pita pocket" opening without changing
    // the sculpted front cliff profile.
    for (int32 XIndex = 0; XIndex <= SafeHorizontalSegments; ++XIndex)
    {
        const float AlphaX = static_cast<float>(XIndex) / static_cast<float>(SafeHorizontalSegments);
        const FVector BottomBackVertex = BottomStripVertices[XIndex * 2 + 1];
        const FVector TopBackVertex = TopStripVertices[XIndex * 2 + 1];

        OutVertices.Add(BottomBackVertex);
        OutUVs.Add(FVector2D(AlphaX, 1.0f));
        OutVertexColors.Add(MakeCliffVertexColor(BottomBackVertex, 0.0f, DebugColor));
        OutTangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));

        OutVertices.Add(TopBackVertex);
        OutUVs.Add(FVector2D(AlphaX, 0.0f));
        OutVertexColors.Add(MakeCliffVertexColor(TopBackVertex, 1.0f, DebugColor));
        OutTangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
    }

    for (int32 XIndex = 0; XIndex < SafeHorizontalSegments; ++XIndex)
    {
        const int32 BottomLeft = XIndex * 2;
        const int32 TopLeft = BottomLeft + 1;
        const int32 BottomRight = BottomLeft + 2;
        const int32 TopRight = BottomLeft + 3;

        OutTriangles.Add(BottomLeft);
        OutTriangles.Add(TopLeft);
        OutTriangles.Add(BottomRight);

        OutTriangles.Add(BottomRight);
        OutTriangles.Add(TopLeft);
        OutTriangles.Add(TopRight);
    }
}

void AProceduralCliffGenerator::CalculateAveragedNormals(
    const TArray<FVector>& Vertices,
    const TArray<int32>& Triangles,
    TArray<FVector>& OutNormals) const
{
    OutNormals.Init(FVector::ZeroVector, Vertices.Num());

    for (int32 TriangleIndex = 0; TriangleIndex + 2 < Triangles.Num(); TriangleIndex += 3)
    {
        const int32 IndexA = Triangles[TriangleIndex];
        const int32 IndexB = Triangles[TriangleIndex + 1];
        const int32 IndexC = Triangles[TriangleIndex + 2];
        if (!Vertices.IsValidIndex(IndexA) || !Vertices.IsValidIndex(IndexB) || !Vertices.IsValidIndex(IndexC))
        {
            continue;
        }

        const FVector EdgeAB = Vertices[IndexB] - Vertices[IndexA];
        const FVector EdgeAC = Vertices[IndexC] - Vertices[IndexA];
        const FVector FaceNormal = FVector::CrossProduct(EdgeAC, EdgeAB).GetSafeNormal();

        OutNormals[IndexA] += FaceNormal;
        OutNormals[IndexB] += FaceNormal;
        OutNormals[IndexC] += FaceNormal;
    }

    for (FVector& Normal : OutNormals)
    {
        Normal = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector::YAxisVector);
    }
}

void AProceduralCliffGenerator::MakeGeometryDoubleSided(
    TArray<FVector>& Vertices,
    TArray<int32>& Triangles,
    TArray<FVector2D>& UVs,
    TArray<FLinearColor>& VertexColors,
    TArray<FProcMeshTangent>& Tangents) const
{
    if (!bDoubleSidedGeometry || Vertices.IsEmpty() || Triangles.IsEmpty())
    {
        return;
    }

    const int32 OriginalVertexCount = Vertices.Num();
    const int32 OriginalTriangleIndexCount = Triangles.Num();

    Vertices.Reserve(OriginalVertexCount * 2);
    UVs.Reserve(UVs.Num() + OriginalVertexCount);
    VertexColors.Reserve(VertexColors.Num() + OriginalVertexCount);
    Tangents.Reserve(Tangents.Num() + OriginalVertexCount);
    Triangles.Reserve(OriginalTriangleIndexCount * 2);

    for (int32 VertexIndex = 0; VertexIndex < OriginalVertexCount; ++VertexIndex)
    {
        const FVector VertexCopy = Vertices[VertexIndex];
        const FVector2D UVCopy = UVs.IsValidIndex(VertexIndex) ? UVs[VertexIndex] : FVector2D::ZeroVector;
        const FLinearColor VertexColorCopy = VertexColors.IsValidIndex(VertexIndex) ? VertexColors[VertexIndex] : FLinearColor::White;
        const FProcMeshTangent TangentCopy = Tangents.IsValidIndex(VertexIndex) ? Tangents[VertexIndex] : FProcMeshTangent(1.0f, 0.0f, 0.0f);

        Vertices.Add(VertexCopy);
        UVs.Add(UVCopy);
        VertexColors.Add(VertexColorCopy);
        Tangents.Add(TangentCopy);
    }

    // Backfaces use duplicate vertices so averaged normals do not cancel out
    // against the front side. This closes the visual holes caused by one-sided
    // procedural sheets while the cliff system is still prototype geometry.
    for (int32 TriangleIndex = 0; TriangleIndex + 2 < OriginalTriangleIndexCount; TriangleIndex += 3)
    {
        const int32 IndexA = Triangles[TriangleIndex] + OriginalVertexCount;
        const int32 IndexB = Triangles[TriangleIndex + 1] + OriginalVertexCount;
        const int32 IndexC = Triangles[TriangleIndex + 2] + OriginalVertexCount;

        Triangles.Add(IndexA);
        Triangles.Add(IndexC);
        Triangles.Add(IndexB);
    }
}
