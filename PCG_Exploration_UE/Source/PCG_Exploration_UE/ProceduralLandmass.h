#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "ProceduralLandmass.generated.h"

// Simple biome / terrain type definition
USTRUCT(BlueprintType)
struct FTerrainType
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    FName Name = NAME_None;

    // Normalized height [0..1] at which this biome starts
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Height = 0.0f;

    // Slope limits in degrees
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.0", ClampMax = "90.0"))
    float MinSlope = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.0", ClampMax = "90.0"))
    float MaxSlope = 90.0f;

    // Debug color for this biome (vertex color in terrain material)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    FLinearColor Color = FLinearColor::White;
};

UCLASS()
class PCG_EXPLORATION_UE_API AProceduralLandmass : public AActor
{
    GENERATED_BODY()

public:
    AProceduralLandmass();

    // Called when properties change in editor (eg. you tweak sliders)
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
    virtual void BeginPlay() override;

public:
    // Called every frame (we don’t really need this right now, but it’s fine)
    virtual void Tick(float DeltaTime) override;

    // ---- Public settings exposed to Details panel ----

    UPROPERTY(VisibleAnywhere, Category = "Terrain")
    UProceduralMeshComponent* ProceduralMesh;

    // Map dimensions in number of grid cells
    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "2", UIMin = "2"))
    int32 MapWidth = 100;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "2", UIMin = "2"))
    int32 MapHeight = 100;

    // World size of each grid cell
    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "1.0", UIMin = "1.0"))
    float GridSize = 100.0f;

    // Height multiplier (world units)
    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    float HeightMultiplier = 300.0f;

    // Perlin noise parameters
    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "0.001", UIMin = "0.001"))
    float NoiseScale = 50.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    int32 Seed = 1337;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "1", UIMin = "1"))
    int32 Octaves = 4;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Persistence = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "0.01"))
    float Lacunarity = 2.0f;

    // Biome list used by the terrain material (vertex colors)
    UPROPERTY(EditAnywhere, Category = "Terrain|Biomes")
    TArray<FTerrainType> TerrainTypes;

    // How soft the vertical blending is between biomes (normalized height range)
    UPROPERTY(EditAnywhere, Category = "Terrain|Biomes", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float HeightBlendRange = 0.02f;

    // Exposed button in Details panel
    UFUNCTION(CallInEditor, Category = "Terrain")
    void GenerateTerrain();

    // Gives external systems (like water) a normalized “sea level”
    UFUNCTION(BlueprintCallable, Category = "Terrain")
    float GetDefaultWaterHeight01() const;

    // Convenience for water: returns center location of the landmass
    UFUNCTION(BlueprintCallable, Category = "Terrain")
    FVector GetLandmassCenter() const;

private:
    void CreateMesh();
    void BuildHeightMap(TArray<float>& OutHeights) const;
    float SampleNoise2D(float X, float Y) const;
};
