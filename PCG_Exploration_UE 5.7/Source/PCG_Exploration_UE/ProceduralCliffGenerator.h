#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "ProceduralCliffGenerator.generated.h"

class UMaterialInterface;
class AProceduralLandmass;

UCLASS()
class PCG_EXPLORATION_UE_API AProceduralCliffGenerator : public AActor
{
    GENERATED_BODY()

public:
    AProceduralCliffGenerator();

    virtual void OnConstruction(const FTransform& Transform) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cliff")
    UProceduralMeshComponent* CliffMesh = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|References")
    AProceduralLandmass* LandmassActor = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Terrain Stitching")
    bool bSnapTopEdgeToLandmass = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Terrain Stitching")
    bool bSampleStitchEdgesFromLandmass = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Terrain Stitching", meta = (ClampMin = "-10000.0", ClampMax = "10000.0", UIMin = "-500.0", UIMax = "500.0"))
    float LandmassSurfaceZOffset = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Terrain Stitching")
    bool bClampSamplesToLandmassBounds = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Terrain Stitching")
    bool bUseActorZAsMinimumTopHeight = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Terrain Stitching")
    bool bUseActorZAsMinimumStitchHeight = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Terrain Stitching")
    bool bAnchorTopHeightAtAttachEdge = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Terrain Stitching", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "600.0"))
    float TopStitchDepth = 180.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Terrain Stitching", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "600.0"))
    float BottomStitchDepth = 120.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Terrain Stitching", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "600.0"))
    float EndStitchDepth = 120.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Shape")
    float CliffLength = 2500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Shape", meta = (ClampMin = "1.0"))
    float CliffHeight = 900.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Shape")
    float CliffDepth = 220.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Shape")
    bool bInvertCliffDirection = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Rendering")
    bool bDoubleSidedGeometry = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Prototype")
    bool bUsePyramidPrototype = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Prototype", meta = (ClampMin = "1", UIMin = "2", UIMax = "8"))
    int32 PrototypeStripSegments = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Prototype", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "0.5"))
    float PrototypeEndTaper = 0.22f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Prototype", meta = (ClampMin = "0.0", ClampMax = "0.65", UIMin = "0.0", UIMax = "0.35"))
    float PrototypeWidthNoise = 0.12f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Prototype", meta = (ClampMin = "0.0", ClampMax = "45.0", UIMin = "0.0", UIMax = "25.0"))
    float PrototypeMaxTopSlopeDegrees = 15.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Prototype", meta = (ClampMin = "1", UIMin = "2", UIMax = "16"))
    int32 PrototypeFrontCurveSegments = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Prototype", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
    float PrototypeFrontCurveStrength = 0.55f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Prototype", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "600.0"))
    float PrototypeFrontNoiseStrength = 55.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Prototype", meta = (ClampMin = "1.0", UIMin = "50.0", UIMax = "1500.0"))
    float PrototypeFrontNoiseScale = 450.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Prototype", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1200.0"))
    float PrototypeBackFaceEmbedDepth = 350.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Prototype")
    bool bAutoFitPrototypeBackFaceToLandmass = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Prototype", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "250.0"))
    float PrototypeBackFaceBurialMargin = 35.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Prototype", meta = (ClampMin = "1", UIMin = "3", UIMax = "11"))
    int32 PrototypeBackFaceFitSamples = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Prototype", meta = (ClampMin = "0.0", UIMin = "300.0", UIMax = "3000.0"))
    float PrototypeMaxAutoEmbedDepth = 1800.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Debug")
    bool bUseDebugSectionColors = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Debug")
    bool bUseSimplifiedDebugGeometry = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Debug", meta = (ClampMin = "1", UIMin = "1", UIMax = "24"))
    int32 DebugHorizontalSegments = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Debug", meta = (ClampMin = "1", UIMin = "1", UIMax = "16"))
    int32 DebugVerticalSegments = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Shape", meta = (ClampMin = "1"))
    int32 HorizontalSegments = 32;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Shape", meta = (ClampMin = "1"))
    int32 VerticalSegments = 12;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Noise", meta = (ClampMin = "0.001"))
    float NoiseScale = 220.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Noise")
    float NoiseStrength = 85.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Ledges", meta = (ClampMin = "0.0"))
    float LedgeFrequency = 4.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Ledges")
    float LedgeStrength = 70.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Overhang")
    float OverhangAmount = 180.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Overhang", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float OverhangStartAlpha = 0.55f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Overhang", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float UndercutStrength = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Overhang", meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float TopLipStrength = 0.45f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Overhang", meta = (ClampMin = "0.0", ClampMax = "0.45"))
    float EndTaperFraction = 0.14f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Terrain Stitching")
    bool bCreateEndStitchStrips = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Terrain Stitching")
    bool bCreateBackStitchWall = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Collision")
    bool bCreateCollision = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Material")
    UMaterialInterface* CliffMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cliff|Material")
    UMaterialInterface* DebugColorMaterial = nullptr;

    UFUNCTION(BlueprintCallable, Category = "Cliff")
    void GenerateCliff();

    UFUNCTION(BlueprintCallable, Category = "Cliff")
    void ClearCliff();

private:
    float GetCliffDirectionSign() const;
    int32 GetEffectiveHorizontalSegments() const;
    int32 GetEffectiveVerticalSegments() const;
    float ComputeEndTaper(float AlphaX) const;
    float ComputeFaceOffset(float AlphaX, float AlphaZ) const;
    FVector GetLandmassAnchoredFaceVertex(float AlphaX, float AlphaZ, float FaceYOffset) const;
    FVector GetStitchTerrainVertex(const FVector& CliffEdgeVertex, const FVector& TerrainEdgeOffset) const;
    FLinearColor MakeCliffVertexColor(const FVector& LocalPosition, float AlphaZ, const FLinearColor& DebugColor) const;
    float ResolvePrototypeTopLocalZ() const;
    float ResolvePrototypeBackFaceEmbedDepth(float SafeLength, float TopZ, float ApexZ) const;
    float SampleLandmassHeightForLocalPoint(const FVector& LocalPoint) const;
    void CreateTriangleSection(
        int32 SectionIndex,
        const FVector& VertexA,
        const FVector& VertexB,
        const FVector& VertexC,
        const FLinearColor& DebugColor,
        bool bUseCollision);
    void CreateQuadSection(
        int32 SectionIndex,
        const FVector& VertexA,
        const FVector& VertexB,
        const FVector& VertexC,
        const FVector& VertexD,
        const FLinearColor& DebugColor,
        bool bUseCollision);
    void CreateSection(
        int32 SectionIndex,
        const TArray<FVector>& InVertices,
        const TArray<int32>& InTriangles,
        const FLinearColor& DebugColor,
        bool bUseCollision);
    void GeneratePyramidCliff();
    void BuildFaceGrid(
        TArray<FVector>& OutVertices,
        TArray<int32>& OutTriangles,
        TArray<FVector2D>& OutUVs,
        TArray<FLinearColor>& OutVertexColors,
        TArray<FProcMeshTangent>& OutTangents) const;
    void BuildStitchStrip(
        const TArray<FVector>& FaceVertices,
        int32 RowIndex,
        const FVector& TerrainEdgeOffset,
        const FLinearColor& DebugColor,
        bool bFlipWinding,
        TArray<FVector>& OutVertices,
        TArray<int32>& OutTriangles,
        TArray<FVector2D>& OutUVs,
        TArray<FLinearColor>& OutVertexColors,
        TArray<FProcMeshTangent>& OutTangents) const;
    void BuildEndStitchStrip(
        const TArray<FVector>& FaceVertices,
        int32 ColumnIndex,
        const FVector& TerrainEdgeOffset,
        const FLinearColor& DebugColor,
        bool bFlipWinding,
        TArray<FVector>& OutVertices,
        TArray<int32>& OutTriangles,
        TArray<FVector2D>& OutUVs,
        TArray<FLinearColor>& OutVertexColors,
        TArray<FProcMeshTangent>& OutTangents) const;
    void BuildBackStitchWall(
        const TArray<FVector>& TopStripVertices,
        const TArray<FVector>& BottomStripVertices,
        const FLinearColor& DebugColor,
        TArray<FVector>& OutVertices,
        TArray<int32>& OutTriangles,
        TArray<FVector2D>& OutUVs,
        TArray<FLinearColor>& OutVertexColors,
        TArray<FProcMeshTangent>& OutTangents) const;
    void CalculateAveragedNormals(
        const TArray<FVector>& Vertices,
        const TArray<int32>& Triangles,
        TArray<FVector>& OutNormals) const;
    void MakeGeometryDoubleSided(
        TArray<FVector>& Vertices,
        TArray<int32>& Triangles,
        TArray<FVector2D>& UVs,
        TArray<FLinearColor>& VertexColors,
        TArray<FProcMeshTangent>& Tangents) const;
};
