//-----------------------------------------------------------------------------
// ProceduralLandmass.h
//
// Runtime-generated terrain with height- and slope-based biome coloring.
//-----------------------------------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralLandmass.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

// Represents one "biome" or terrain layer
USTRUCT(BlueprintType)
struct FTerrainType
{
    GENERATED_BODY();

    UPROPERTY(EditAnywhere, Category = "Terrain")
    FName Name = "Biome";

    // Height threshold in [0,1]. If vertexHeight <= Height, this biome is eligible.
    UPROPERTY(EditAnywhere, Category = "Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Height = 0.0f;

    // Slope range this biome applies to (0 = flat, 1 = vertical)
    UPROPERTY(EditAnywhere, Category = "Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float MinSlope = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float MaxSlope = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain")
    FLinearColor Color = FLinearColor::White;
};

UCLASS()
class PCG_EXPLORATION_UE_API AProceduralLandmass : public AActor
{
    GENERATED_BODY()

public:
    AProceduralLandmass();

protected:
    virtual void BeginPlay() override;

#if WITH_EDITOR
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    // Terrain mesh
    UPROPERTY(VisibleAnywhere, Category = "Terrain")
    UProceduralMeshComponent* ProceduralMesh;

public:
    // =========================
    //   Noise / shape settings
    // =========================

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "2"))
    int32 MapWidth = 100;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "2"))
    int32 MapHeight = 100;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "1.0"))
    float GridSize = 100.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "0.001"))
    float NoiseScale = 50.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    int32 Seed = 1337;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "1"))
    int32 Octaves = 5;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Persistance = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "1.0"))
    float Lacunarity = 2.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    FVector2D NoiseOffset = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    float HeightMultiplier = 300.0f;

    // =========================
    //   Biome / color settings
    // =========================

    UPROPERTY(EditAnywhere, Category = "Terrain|Biomes")
    UMaterialInterface* TerrainMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category = "Terrain|Biomes")
    TArray<FTerrainType> TerrainTypes;

    // How much neighboring biomes overlap in height for blending
    UPROPERTY(EditAnywhere, Category = "Terrain|Biomes", meta = (ClampMin = "0.0", ClampMax = "0.5"))
    float HeightBlendRange = 0.03f;

    // =========================
    //   Editor utility
    // =========================

    UFUNCTION(CallInEditor, Category = "Terrain")
    void GenerateTerrain();

private:
    void GenerateNoiseMap(TArray<float>& OutHeightMap);
    void CreateMeshFromHeightMap(const TArray<float>& HeightMap);
    FLinearColor GetColorForHeightAndSlope(float NormalizedHeight, float Slope) const;
};
