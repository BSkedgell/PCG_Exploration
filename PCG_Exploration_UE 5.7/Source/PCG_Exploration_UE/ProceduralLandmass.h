#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralLandmass.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UCLASS()
class PCG_EXPLORATION_UE_API AProceduralLandmass : public AActor
{
    GENERATED_BODY()

public:
    AProceduralLandmass();

    virtual void BeginPlay() override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    // Runtime helpers used by other gameplay/world systems.
    float   GetDefaultWaterHeight01() const;
    FVector GetLandmassCenter() const;

    UFUNCTION(BlueprintPure, Category = "Terrain|Queries")
    float GetDefaultWaterSurfaceZ() const;

    UFUNCTION(BlueprintPure, Category = "Terrain|Queries")
    float GetTerrainHeightAtWorldLocation(const FVector& WorldLocation) const;

    // Components
    // ProceduralMesh is the heightfield terrain. OverhangMesh is separate geometry
    // because true overhangs cannot be represented by a one-height-per-XY heightmap.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terrain")
    UProceduralMeshComponent* ProceduralMesh = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terrain")
    UProceduralMeshComponent* OverhangMesh = nullptr;

    // Dimensions
    // MapWidth/MapHeight control vertex count; GridSize controls spacing between
    // vertices. Increasing resolution improves slopes/material masks but costs more.
    UPROPERTY(EditAnywhere, Category = "Terrain|Dimensions")
    int32 MapWidth = 128;

    UPROPERTY(EditAnywhere, Category = "Terrain|Dimensions")
    int32 MapHeight = 128;

    UPROPERTY(EditAnywhere, Category = "Terrain|Dimensions")
    float GridSize = 100.0f;

    // Height Controls
    // Heights are stored internally as 0..1 and multiplied by HeightMultiplier
    // when mesh vertices are created.
    UPROPERTY(EditAnywhere, Category = "Terrain|Heights")
    float HeightMultiplier = 2000.0f;

    // WaterHeight01 acts as sea level for shoreline masks/materials. It does not
    // spawn or move Unreal Water actors by itself.
    UPROPERTY(EditAnywhere, Category = "Terrain|Heights", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float WaterHeight01 = 0.22f;

    // Base Noise
    // These drive the initial fBm terrain before coastline, landforms, erosion,
    // and cliff shaping are layered on top.
    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    float NoiseScale = 80.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    int32 Seed = 1337;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    int32 Octaves = 4;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    float Persistence = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    float Lacunarity = 2.0f;

    // Coast & Beaches
    // CoastMask keeps the landmass from becoming noisy all the way to the edge.
    // Beach controls flatten the sea-level band into a walkable shoreline shelf.
    UPROPERTY(EditAnywhere, Category = "Terrain|Coast & Beaches", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CoastFalloff = 0.55f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Coast & Beaches", meta = (ClampMin = "0.1"))
    float CoastEdgeHardness = 1.8f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Coast & Beaches", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CoastNoiseInfluence = 0.35f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Coast & Beaches", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float BeachFlattenStrength = 0.72f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Coast & Beaches", meta = (ClampMin = "0.5", ClampMax = "4.0"))
    float BeachWidthScale = 1.65f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Coast & Beaches", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float BeachNoiseReduction = 0.75f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Coast & Beaches", meta = (ClampMin = "0.01", ClampMax = "0.5"))
    float BeachBandWidth = 0.08f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Coast & Beaches", meta = (ClampMin = "0.01", ClampMax = "1.0"))
    float BeachFadeDistance = 0.18f;

    // Erosion
    // Beach erosion adds subtle berm/wash variation without reintroducing noisy
    // coastal hills. Thermal erosion softens harsh mountain ridges post-process.
    UPROPERTY(EditAnywhere, Category = "Terrain|Erosion")
    bool bEnableBeachErosion = true;

    UPROPERTY(EditAnywhere, Category = "Terrain|Erosion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float BeachErosionStrength = 0.28f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Erosion", meta = (ClampMin = "8.0"))
    float BeachErosionScale = 26.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Erosion")
    bool bEnableThermalErosion = true;

    UPROPERTY(EditAnywhere, Category = "Terrain|Erosion", meta = (ClampMin = "0", ClampMax = "8"))
    int32 ThermalErosionIterations = 2;

    UPROPERTY(EditAnywhere, Category = "Terrain|Erosion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ThermalErosionStrength = 0.22f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Erosion", meta = (ClampMin = "0.001", ClampMax = "0.2"))
    float ThermalTalusThreshold = 0.03f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Erosion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ThermalHeightMin = 0.42f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Erosion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float MountainWearStrength = 0.12f;

    // Mountain Shape
    // Blends normal fBm hills with ridged noise to get sharper fantasy peaks.
    UPROPERTY(EditAnywhere, Category = "Terrain|Mountains", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float MountainBlend = 0.4f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Mountains", meta = (ClampMin = "1.0"))
    float RidgeSharpness = 2.6f;

    // Small Detail
    // Adds subtle high-frequency variation after the macro terrain has been made.
    UPROPERTY(EditAnywhere, Category = "Terrain|Detail", meta = (ClampMin = "1.0"))
    float DetailNoiseScale = 18.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Detail", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DetailStrength = 0.08f;

    // Plateaus
    // Sparse flat-topped landforms with broad approach shaping. These are still
    // heightfield-safe, unlike the separate overhang mesh.
    UPROPERTY(EditAnywhere, Category = "Terrain|Landforms", meta = (ClampMin = "8.0"))
    float PlateauCellSize = 30.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Landforms", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float PlateauChance = 0.16f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Landforms", meta = (ClampMin = "0.05", ClampMax = "0.45"))
    float PlateauRadiusMin = 0.12f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Landforms", meta = (ClampMin = "0.05", ClampMax = "0.55"))
    float PlateauRadiusMax = 0.2f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Landforms", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float PlateauFlattenStrength = 0.75f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Landforms", meta = (ClampMin = "0.01", ClampMax = "0.5"))
    float PlateauRampWidth = 0.12f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Landforms", meta = (ClampMin = "0.2", ClampMax = "2.0"))
    float PlateauRampReach = 0.75f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Landforms", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float PlateauRampStrength = 0.85f;

    // Heightfield Cliff Bands
    // Creates stepped/terraced rocky faces inside the terrain mesh. This is not
    // true overhang geometry; it is a heightmap-safe visual cliff treatment.
    UPROPERTY(EditAnywhere, Category = "Terrain|Cliffs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CliffShelfStrength = 0.28f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Cliffs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CliffShelfHeightMin = 0.62f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Cliffs", meta = (ClampMin = "2.0", ClampMax = "12.0"))
    float CliffShelfFrequency = 6.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Cliffs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CliffWallStrength = 0.45f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Cliffs", meta = (ClampMin = "0.05", ClampMax = "0.8"))
    float CliffShelfFlatness = 0.22f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Cliffs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CliffNoiseThreshold = 0.52f;

    // Procedural Overhang Mesh
    // Generated as separate closed geometry attached to steep terrain regions.
    // This is experimental/prototype-quality and intentionally kept isolated
    // from the main heightmap so it can evolve independently.
    UPROPERTY(EditAnywhere, Category = "Terrain|Overhangs")
    bool bEnableOverhangs = true;

    UPROPERTY(EditAnywhere, Category = "Terrain|Overhangs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float OverhangHeightMin = 0.58f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Overhangs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float OverhangSlopeMin = 0.4f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Overhangs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float OverhangChance = 0.45f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Overhangs", meta = (ClampMin = "0.0", ClampMax = "0.3"))
    float OverhangCliffDeltaMin = 0.035f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Overhangs", meta = (ClampMin = "10.0"))
    float OverhangDepth = 180.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Overhangs", meta = (ClampMin = "10.0"))
    float OverhangThickness = 70.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Overhangs", meta = (ClampMin = "0.0"))
    float OverhangLipDrop = 90.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Overhangs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float OverhangNoiseAmount = 0.28f;

    // Material Masks
    // Vertex colors generated for M_ProceduralTerrain:
    // R = beach, G = slope, B = height, A = dry-land mask.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    FVector2D TileOffset = FVector2D(0, 0);

    // Material Assets
    // BaseTerrainMaterial is wrapped in a dynamic MID so WaterHeight can be synced.
    UPROPERTY(EditAnywhere, Category = "Terrain|Material")
    UMaterialInterface* BaseTerrainMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category = "Terrain|Material")
    UMaterialInterface* OverhangMaterial = nullptr;

private:
    // Internal generation pipeline.
    void GenerateTerrain();
    void CreateMesh();
    void BuildHeightMap(TArray<float>& OutHeights) const;
    void BuildOverhangMesh(const TArray<float>& Heights, const TArray<FVector>& Normals);
    void EnsureTerrainMaterialInstance();
    bool IsGenerationProperty(FName PropertyName) const;
    bool IsMaterialProperty(FName PropertyName) const;
    bool SampleHeight01AtLocalXY(float LocalX, float LocalY, float& OutHeight01) const;

    // Dynamic material instance for the terrain. Never expose directly; assign
    // BaseTerrainMaterial instead.
    UPROPERTY(Transient)
    UMaterialInstanceDynamic* TerrainMID = nullptr;

    // Cached normalized heightfield used by water/shoreline biome queries.
    UPROPERTY(Transient)
    TArray<float> CachedHeightMap;
};
